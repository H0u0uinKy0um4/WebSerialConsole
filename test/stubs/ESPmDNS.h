// Host-side stub of the Arduino-ESP32 mDNS library.
#pragma once

#include <Arduino.h>

class MDNSClass {
public:
    bool begin(const char*) { return true; }
    void addService(const char*, const char*, uint16_t) {}
};

extern MDNSClass MDNS;
