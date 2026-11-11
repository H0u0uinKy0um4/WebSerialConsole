/*
 * WebSerialConsole library example
 *
 * Turns an ESP32 into a browser-accessible serial console:
 *   http://<esp32-ip>/
 * WebSocket on port 81, hardware serial <-> browser terminal bridge.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "WebSerialConsole.h"
#include "WebServerHandler.h"

void setup() {
    Serial.begin(115200);   // goes through the WebSerial console when remapped

    // Connect the ESP32 to your WiFi network here, e.g.
    // WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    setupWebServer();       // serves the page, starts the WS server, attaches the console
}

void loop() {
    handleWebServer();      // server.handleClient() + WebSerial.loop()
}
