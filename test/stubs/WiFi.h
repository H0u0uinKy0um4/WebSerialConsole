// Host-side stub of the Arduino WiFi library.
#pragma once

#include <Arduino.h>

#define WL_CONNECTED 3
#define WIFI_STA 1
#define WIFI_AP 2

typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA = 1,
    WIFI_MODE_AP = 2,
    WIFI_MODE_APSTA = 3
} wifi_mode_t;

class WiFiClass {
public:
    IPAddress localIP() { return IPAddress(); }
    IPAddress softAPIP() { return IPAddress(); }
    IPAddress subnetMask() { return IPAddress(); }
    IPAddress gatewayIP() { return IPAddress(); }
    IPAddress dnsIP() { return IPAddress(); }
    int RSSI() { return -55; }
    String macAddress() { return String("00:00:00:00:00:00"); }
    int status() { return WL_CONNECTED; }
    bool softAPactive() { return false; }
    wifi_mode_t getMode() { return WIFI_MODE_STA; }
    bool mode(int) { return true; }
    bool begin(const char*, const char*) { return true; }
    bool softAP(const char*) { return true; }
    bool softAP(const char*, const char*) { return true; }
    void setSleep(bool) {}
};

extern WiFiClass WiFi;
