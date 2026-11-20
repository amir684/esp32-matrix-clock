#include "ble.h"

#include <NimBLEDevice.h>

#include "api.h"
#include "config.h"
#include "settings.h"

namespace ble {

static NimBLEServer *server = nullptr;
static NimBLECharacteristic *rspChar = nullptr;
static SemaphoreHandle_t lock = nullptr; // the inbox is filled by the NimBLE task, read by the main loop
static String inbox;
static uint8_t inId = 0;
static bool inReady = false;
static volatile bool isConnected = false;
static volatile uint16_t mtu = 23;
static volatile bool quietMode = false;

static void advertise() {
  if (!quietMode && server && server->getConnectedCount() < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) NimBLEDevice::startAdvertising();
}

bool connected() { return isConnected; }

// Advertising stops when a phone connects and NimBLE 2.x does not restart it by itself. It is restarted on
// every connect and disconnect, so the clock can always be found: also when a closed app left a dead
// connection behind that the phone has not given up yet.
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, NimBLEConnInfo &) override {
    isConnected = true;
    mtu = 23;
    advertise();
  }
  void onDisconnect(NimBLEServer *s, NimBLEConnInfo &, int) override {
    isConnected = s->getConnectedCount() > 0;
    advertise();
  }
  void onMTUChange(uint16_t m, NimBLEConnInfo &) override { mtu = m; }
};

class RequestCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    NimBLEAttValue v = c->getValue();
    if (v.size() < 2) return;
    const uint8_t *d = v.data();
    xSemaphoreTake(lock, portMAX_DELAY);
    if (d[1] & 1) { // first packet: a new request replaces anything unfinished
      inbox = "";
      inId = d[0];
      inReady = false;
    }
    if (d[0] == inId && !inReady && inbox.length() < 8192) {
      inbox.concat((const char *)d + 2, v.size() - 2);
      if (d[1] & 2) inReady = true;
    }
    xSemaphoreGive(lock);
  }
};

static void send(uint8_t id, const String &data) {
  size_t chunk = constrain((int)mtu - 3 - 2, 18, 500);
  size_t len = data.length(), pos = 0;
  static uint8_t buf[502];
  do {
    size_t n = min(chunk, len - pos);
    buf[0] = id;
    buf[1] = (pos == 0 ? 1 : 0) | (pos + n >= len ? 2 : 0);
    memcpy(buf + 2, data.c_str() + pos, n);
    // notify() fails while NimBLE is out of buffers; give it a moment to send the previous packets
    for (int tries = 0; tries < 40 && isConnected; tries++) {
      if (rspChar->notify(buf, n + 2)) break;
      delay(5);
    }
    pos += n;
  } while (pos < len && isConnected);
}

void begin() {
  lock = xSemaphoreCreateMutex();
  NimBLEDevice::init(settings.name);
  NimBLEDevice::setMTU(517);
  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  NimBLEService *svc = server->createService(BLE_SERVICE_UUID);
  NimBLECharacteristic *req = svc->createCharacteristic(BLE_REQ_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  req->setCallbacks(new RequestCallbacks());
  rspChar = svc->createCharacteristic(BLE_RSP_UUID, NIMBLE_PROPERTY::NOTIFY);
  server->start();

  // The 128-bit service id fills most of the 31-byte advertisement, so the name goes in the scan response
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData ad, sr;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.addServiceUUID(NimBLEUUID(BLE_SERVICE_UUID));
  sr.setName(settings.name);
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sr);
  // WiFi and Bluetooth share one radio: every advertisement delays WiFi traffic. 300-500 ms instead of
  // the default 20-150 ms still finds the clock within a second.
  adv->setMinInterval(480); // x 0.625 ms
  adv->setMaxInterval(800);
  adv->start();
  Serial.printf("bluetooth advertising as %s\n", settings.name);
}

void setName(const char *name) {
  if (!server) return;
  NimBLEDevice::setDeviceName(name);
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData sr;
  sr.setName(name);
  adv->stop();
  adv->setScanResponseData(sr);
  advertise();
}

void quiet(bool on) {
  if (on == quietMode || !server) return;
  quietMode = on;
  if (on) NimBLEDevice::stopAdvertising();
  else advertise();
}

void loop() {
  if (!lock || !inReady) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  String req = inbox;
  uint8_t id = inId;
  inbox = "";
  inReady = false;
  xSemaphoreGive(lock);

  int nl = req.indexOf('\n');
  String head = nl < 0 ? req : req.substring(0, nl);
  String body = nl < 0 ? String() : req.substring(nl + 1);
  int sp = head.indexOf(' ');
  String method = sp < 0 ? String("GET") : head.substring(0, sp);
  String path = sp < 0 ? head : head.substring(sp + 1);
  api::Response r = api::handle(method, path, body);
  send(id, String(r.code) + "\n" + r.body);
}

} // namespace ble
