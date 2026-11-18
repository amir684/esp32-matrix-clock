#pragma once
// The clock's JSON API, independent of how the request arrived: the web server (net.cpp)
// and Bluetooth (ble.cpp) both end up here. Must be called from the main loop task.
#include <Arduino.h>

namespace api {

struct Response {
  int code;
  String body; // JSON
};

// method: "GET" or "POST". path may carry a query string.
Response handle(const String &method, const String &path, const String &body);

bool restartRequested(); // /api/action reboot or factory was called
bool netChanged();       // WiFi settings changed; true once
bool clockChanged();     // time zone or time server changed; true once

} // namespace api
