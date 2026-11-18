#pragma once
// WiFi (the clock's own access point, or the home network with NTP), time keeping with the
// DS3231, the web server and its JSON API.
#include <Arduino.h>

namespace net {

void begin();
void loop();

bool timeValid();   // the clock knows the time (from the DS3231, the network or the web page)
bool ntpSynced();
String address();   // the IP to browse to
bool apActive();

} // namespace net

// Implemented in main.cpp: push the matrix settings to the hardware after a change
void applyHardware();
