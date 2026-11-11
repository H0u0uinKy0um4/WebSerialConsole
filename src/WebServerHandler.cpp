#include "WebServerHandler.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <cstring>

#ifndef WEBSERIAL_DISABLE_MDNS
#include <ESPmDNS.h>
#endif

// NO_WEB_SERIAL_REDEFINE keeps `Serial` pointing at the real hardware port in
// this translation unit. Library internals talk to the console explicitly.
#define NO_WEB_SERIAL_REDEFINE
#include "WebSerialConsole.h"
#include "index_html.h"

// Hostname used for mDNS discovery. Override with a build flag when several
// boards are on the same network.
#ifndef WEBSERIAL_MDNS_HOSTNAME
#define WEBSERIAL_MDNS_HOSTNAME "webserial"
#endif

// Port the WebSocket server listens on. The browser learns the real value
// from GET /config, so it can be changed without touching the web page.
#ifndef WEBSERIAL_WS_PORT
#define WEBSERIAL_WS_PORT 81
#endif

// Port the HTTP server listens on.
#ifndef WEBSERIAL_HTTP_PORT
#define WEBSERIAL_HTTP_PORT 80
#endif

static WebServer server(WEBSERIAL_HTTP_PORT);
static WebSocketsServer webSocket(WEBSERIAL_WS_PORT);
static bool webStarted = false;
static bool mdnsStarted = false;
static unsigned long lastMdnsTry = 0;

// How often the mDNS advertisement is retried while it has not succeeded yet.
#ifndef WEBSERIAL_MDNS_RETRY_MS
#define WEBSERIAL_MDNS_RETRY_MS 5000
#endif

static IPAddress consoleIP() {
    if (WiFi.status() == WL_CONNECTED) return WiFi.localIP();
    return WiFi.softAPIP();
}

static void reportReady() {
    IPAddress ip = consoleIP();
    String url = "http://" + ip.toString();

#ifndef WEBSERIAL_DISABLE_MDNS
    if (!mdnsStarted && MDNS.begin(WEBSERIAL_MDNS_HOSTNAME)) {
        mdnsStarted = true;
        MDNS.addService("http", "tcp", WEBSERIAL_HTTP_PORT);
        url = "http://" + String(WEBSERIAL_MDNS_HOSTNAME) + ".local";
    }
#endif

    WebSerial.logLine(WebSerialConsole::Source::DEBUG,
                      "[WebSerial] Console ready at " + url + " (" + ip.toString() + ")");
}

// The sketch may bring the network up after the console was started, e.g. a
// non-blocking WiFi.begin(). The mDNS advertisement is retried until it
// succeeds, so the hostname works no matter when the network appears.
static void retryMdns() {
#ifndef WEBSERIAL_DISABLE_MDNS
    if (mdnsStarted) return;

    // The console is reachable as soon as the board has a network: either an
    // associated station or its own access point.
    wifi_mode_t mode = WiFi.getMode();
    bool netUp = (WiFi.status() == WL_CONNECTED) ||
                 (mode == WIFI_MODE_AP) || (mode == WIFI_MODE_APSTA);
    if (!netUp) return;

    unsigned long now = millis();
    if (now - lastMdnsTry < WEBSERIAL_MDNS_RETRY_MS) return;
    lastMdnsTry = now;

    if (!MDNS.begin(WEBSERIAL_MDNS_HOSTNAME)) return;

    mdnsStarted = true;
    MDNS.addService("http", "tcp", WEBSERIAL_HTTP_PORT);
    WebSerial.logLine(WebSerialConsole::Source::DEBUG,
                      "[WebSerial] Console ready at http://" +
                      String(WEBSERIAL_MDNS_HOSTNAME) + ".local");
#endif
}

void WebSerialDetail::startWeb() {
    if (webStarted) return;
    webStarted = true;

    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });

    // Source definitions (ids, tags, labels, colours) for the embedded UI.
    server.on("/sources", HTTP_GET, []() {
        server.send(200, "application/json", WebSerial.sourcesJson());
    });

    // Runtime configuration the web page needs: the WebSocket port and the
    // path it is served on. Fetched before the socket is opened so the port
    // never has to be hardcoded in the page.
    server.on("/config", HTTP_GET, []() {
        String cfg = "{\"wsPort\":";
        cfg += WEBSERIAL_WS_PORT;
        cfg += ",\"wsPath\":\"/\"";
        cfg += ",\"mdns\":\"";
#ifndef WEBSERIAL_DISABLE_MDNS
        cfg += WEBSERIAL_MDNS_HOSTNAME;
#endif
        cfg += "\"}";
        server.send(200, "application/json", cfg);
    });

    server.onNotFound([]() {
        WebSerialDetail::getHWSerial().print("Unknown HTTP request: ");
        WebSerialDetail::getHWSerial().println(server.uri());
        server.send(404, "text/plain", "Not found: " + server.uri());
    });

    server.begin();
    webSocket.begin();
    WebSerial.attachWebSocket(&webSocket);

    reportReady();
}

void WebSerialDetail::handleWeb() {
    if (!webStarted) return;
    server.handleClient();
    retryMdns();
}

bool WebSerialDetail::connectWiFi(const char* ssid, const char* password,
                                   unsigned long timeoutMs) {
    if (!ssid || !*ssid) return false;

    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(50);
    }

    if (WiFi.status() != WL_CONNECTED) {
        WebSerialDetail::getHWSerial().print("[WebSerial] Could not join \"");
        WebSerialDetail::getHWSerial().print(ssid);
        WebSerialDetail::getHWSerial().print("\" (status ");
        WebSerialDetail::getHWSerial().print((int)WiFi.status());
        WebSerialDetail::getHWSerial().println(")");
        return false;
    }

    WebSerialDetail::getHWSerial().print("[WebSerial] Joined \"");
    WebSerialDetail::getHWSerial().print(ssid);
    WebSerialDetail::getHWSerial().print("\", IP ");
    WebSerialDetail::getHWSerial().println(WiFi.localIP());
    return true;
}

bool WebSerialDetail::startAccessPoint(const char* ssid, const char* password) {
    if (!ssid || !*ssid) return false;

    size_t passLen = password ? strlen(password) : 0;
    if (passLen > 0 && passLen < 8) {
        WebSerialDetail::getHWSerial().println("[WebSerial] AP password must be at least 8 characters");
        return false;
    }

    WiFi.mode(WIFI_AP);
    bool ok = passLen > 0 ? WiFi.softAP(ssid, password) : WiFi.softAP(ssid);
    if (!ok) {
        WebSerialDetail::getHWSerial().println("[WebSerial] Could not start the access point");
        return false;
    }

    WebSerialDetail::getHWSerial().print("[WebSerial] Access point \"");
    WebSerialDetail::getHWSerial().print(ssid);
    WebSerialDetail::getHWSerial().print("\" up, IP ");
    WebSerialDetail::getHWSerial().println(WiFi.softAPIP());
    return true;
}

// Legacy entry points, kept so existing sketches keep working.
void setupWebServer() {
    WebSerial.beginWeb();
}

void handleWebServer() {
    // loop() already serves HTTP, handles the WebSocket and pumps the serial.
    WebSerial.loop();
}
