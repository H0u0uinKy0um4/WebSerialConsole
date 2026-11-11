/*
 * WebSerialConsole quick start
 *
 * A browser console for any ESP32 project in three lines of setup:
 *   1. put your WiFi credentials below
 *   2. flash and open http://webserial.local/ (or the IP printed on the
 *      USB console)
 *
 * Every Serial.print()/println() in the sketch then also appears in the
 * browser, and anything typed in the browser can be read with Serial.read().
 */

#include <WebSerialConsole.h>

const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

void setup() {
    // Joins WiFi, starts the web servers and bridges the USB serial port.
    if (!WebSerial.begin(WIFI_SSID, WIFI_PASSWORD)) {
        // No network? Serve the console from the board's own access point.
        WebSerial.beginAP("ESP32-Setup", "setuppass");
    }

    Serial.println("Hello from the ESP32!");

    // Optional: register a custom source for this project.
    // WebSerial.addSource("sensor", 'S', "SNS", "#34d399", "Sensor readings");
}

void loop() {
    WebSerial.loop();

    static unsigned long last = 0;
    if (millis() - last > 5000) {
        last = millis();
        Serial.print("alive, free heap: ");
        Serial.print(ESP.getFreeHeap());
        Serial.println(" B");
    }
}
