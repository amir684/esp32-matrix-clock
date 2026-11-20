#pragma once
// Bluetooth LE access to the same JSON API the web server offers, for the installed web app
// (Web Bluetooth in Chrome on Android).
//
// One GATT service with two characteristics:
//   REQ (write)  : the request, in packets of [id][flags][data...]
//   RSP (notify) : the answer,  in packets of [id][flags][data...]
// flags: bit 0 = first packet, bit 1 = last packet. The id ties the answer to its request.
// Request data = "METHOD /path\n" + body.   Answer data = "<HTTP code>\n" + JSON.
#include <Arduino.h>

#define BLE_SERVICE_UUID "c1a70001-5e2d-4b8e-9f3a-6d0b8e1c7a10"
#define BLE_REQ_UUID "c1a70002-5e2d-4b8e-9f3a-6d0b8e1c7a10"
#define BLE_RSP_UUID "c1a70003-5e2d-4b8e-9f3a-6d0b8e1c7a10"

namespace ble {

void begin();
void loop(); // answers a waiting request; call from the main loop
bool connected();
void setName(const char *name); // new name in scans, without dropping a connection
// Stops advertising (a game played over WiFi: the shared radio answers WiFi much faster), and back
void quiet(bool on);

} // namespace ble
