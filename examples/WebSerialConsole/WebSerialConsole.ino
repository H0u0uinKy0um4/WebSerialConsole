/*
 * WebSerialConsole example: sources, bus traffic and a custom UART
 *
 * Opens the console in a browser at http://webserial.local/ and shows how to
 * tag output by source, colour it, and bridge a different serial port.
 */

#include <WebSerialConsole.h>

const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// Application-specific sources are defined by this sketch: id (also the CSS
// class), wire tag, badge label, colour and tooltip. Only DEBUG and USER are
// predefined by the library.
WebSerialConsole::Source ha;
WebSerialConsole::Source master;
WebSerialConsole::Source slave;

void setup() {
    // Joins WiFi, starts the web servers and bridges the USB serial port.
    if (!WebSerial.begin(WIFI_SSID, WIFI_PASSWORD)) {
        WebSerial.beginAP("ESP32-Setup", "setuppass");
    }

    ha     = WebSerial.addSource("ha",     'H', "HA",  "#be82fa", "Home Assistant requests");
    master = WebSerial.addSource("master", 'M', "MST", "#fbbf24", "Outgoing bus commands");
    slave  = WebSerial.addSource("slave",  'S', "SLV", "#34d399", "Bus slave responses");

    // Even the built-in sources can be restyled:
    // WebSerial.configureSource(WebSerialConsole::Source::USER, "CMD", "#38bdf8");

    Serial.println("Console ready");
}

void loop() {
    WebSerial.loop();

    // Tag everything printed inside a scope with one source.
    static unsigned long last = 0;
    if (millis() - last > 3000) {
        last = millis();
        WebSerialConsole::Scope scope(WebSerial, slave);
        Serial.print("slave says: ");
        Serial.println(millis());
    }

    // One complete line under a specific source:
    // WebSerial.logLine(ha, "turn=1 state=open");

    // Bridge another UART instead of the USB one (see the README):
    //   Serial1.begin(115200, SERIAL_8N1, 16, 17);
    //   WebSerial.attachSerial(Serial1);
}
