#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "api.h"
#include "info.h"
#include "rtc.h"
#include "settings.h"
#include "ui.h"
#include "web_page.h"

namespace net {

static WebServer server(80);
static DNSServer dns;
static bool ap = false;
static bool wasConnected = false;
static bool wantRestart = false;
static uint32_t offlineSince = 0;
static volatile bool ntpJustSynced = false;
static bool synced = false;
static uint32_t lastRtcRead = 0;

static const uint32_t OFFLINE_GRACE_MS = 45000; // home network missing this long: open our own access point
static const uint32_t RETRY_MS = 120000;

bool timeValid() { return time(nullptr) > 1700000000; }
bool ntpSynced() { return synced; }
bool apActive() { return ap; }
String address() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return WiFi.softAPIP().toString();
}

// ---------------- Time ----------------

static void onNtpSync(timeval *) { ntpJustSynced = true; } // lwIP task: no I2C from here

static void applyClock() {
  if (settings.wifiMode == WIFI_HOME) {
    configTzTime(settings.tz, settings.ntpServer, "time.google.com", "pool.ntp.org");
  } else {
    if (esp_sntp_enabled()) esp_sntp_stop();
    setenv("TZ", settings.tz, 1);
    tzset();
  }
}

static void readRtc() {
  time_t t;
  if (rtc::read(t)) {
    timeval tv = {t, 0};
    settimeofday(&tv, nullptr);
  }
  lastRtcRead = millis();
}

// ---------------- WiFi ----------------

static void startAccessPoint() {
  if (ap) return;
  WiFi.mode(settings.wifiMode == WIFI_HOME ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_NAME, settings.apPass[0] ? settings.apPass : nullptr);
  dns.start(53, "*", WiFi.softAPIP()); // every name leads here, so phones open the page by themselves
  ap = true;
  Serial.printf("access point %s at %s\n", AP_NAME, WiFi.softAPIP().toString().c_str());
}

static void stopAccessPoint() {
  if (!ap) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap = false;
}

static void connect() {
  WiFi.setHostname(settings.hostname);
  if (settings.wifiMode == WIFI_STANDALONE || !settings.wifiSsid[0]) {
    WiFi.disconnect(true);
    ap = false;
    startAccessPoint();
    return;
  }
  WiFi.mode(ap ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(settings.wifiSsid, settings.wifiPass);
  offlineSince = millis() | 1;
  Serial.printf("connecting to %s\n", settings.wifiSsid);
}

static void reconnect() {
  wasConnected = false;
  if (ap) {
    dns.stop();
    WiFi.softAPdisconnect(true);
    ap = false;
  }
  WiFi.disconnect();
  connect();
}

// ---------------- Web handlers ----------------

static void handlePage() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", (const char *)WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
}

static void handleApi() {
  api::Response r = api::handle(server.method() == HTTP_POST ? "POST" : "GET", server.uri(), server.arg("plain"));
  server.sendHeader("Cache-Control", "no-store");
  if (server.uri() == "/api/backup")
    server.sendHeader("Content-Disposition", "attachment; filename=\"matrix-clock-settings.json\"");
  server.send(r.code, "application/json", r.body);
}

static bool updateOk = false;

static void handleUpdateDone() {
  server.send(updateOk ? 200 : 500, "text/plain", updateOk ? "OK" : Update.errorString());
  if (updateOk) wantRestart = true;
}

static void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    ui::notice("מעדכן...", CRGB(0, 160, 255), 120000);
    updateOk = Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (updateOk && Update.write(up.buf, up.currentSize) != up.currentSize) updateOk = false;
  } else if (up.status == UPLOAD_FILE_END) {
    updateOk = updateOk && Update.end(true);
    ui::notice(updateOk ? "עודכן" : "העדכון נכשל", updateOk ? CRGB::Green : CRGB::Red, 4000);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    updateOk = false;
  }
}

static void handleNotFound() {
  if (server.uri().startsWith("/api/")) {
    handleApi();
  } else if (ap && WiFi.status() != WL_CONNECTED) { // captive portal: whatever the phone asked for, show our page
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "not found");
  }
}

// ---------------- Life cycle ----------------

void begin() {
  if (rtc::begin()) readRtc();
  else Serial.println("DS3231 not found");
  sntp_set_time_sync_notification_cb(onNtpSync);
  applyClock();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  connect();

  server.on("/", HTTP_GET, handlePage);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  if (ap) dns.processNextRequest();
  server.handleClient();

  if (wantRestart || api::restartRequested()) {
    delay(500); // let the answer leave first
    ESP.restart();
  }
  if (api::clockChanged()) applyClock();
  if (api::netChanged()) reconnect();

  if (ntpJustSynced) {
    ntpJustSynced = false;
    synced = true;
    rtc::write(time(nullptr));
    info::invalidate();
    Serial.println("time from the network, written to the DS3231");
  }
  // The ESP32's own clock drifts; the DS3231 is the reference when there is no network time
  if (!synced && rtc::present() && millis() - lastRtcRead > 3600000UL) readRtc();

  if (settings.wifiMode != WIFI_HOME) return;

  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.printf("connected, address %s\n", WiFi.localIP().toString().c_str());
    MDNS.end();
    if (MDNS.begin(settings.hostname)) MDNS.addService("http", "tcp", 80);
    stopAccessPoint();
    ui::notice(WiFi.localIP().toString().c_str(), CRGB(0, 200, 120), 8000);
  }
  wasConnected = connected;

  if (connected) offlineSince = 0;
  else if (!offlineSince) offlineSince = millis() | 1;
  if (!connected && !ap && settings.wifiSsid[0] && millis() - offlineSince > OFFLINE_GRACE_MS) startAccessPoint();

  // Keep trying the home network, but not while someone uses the setup page: scanning cuts them off
  static uint32_t lastRetry = 0;
  if (!connected && ap && settings.wifiSsid[0] && millis() - lastRetry > RETRY_MS && !WiFi.softAPgetStationNum()) {
    lastRetry = millis();
    WiFi.begin(settings.wifiSsid, settings.wifiPass);
  }
}

} // namespace net
