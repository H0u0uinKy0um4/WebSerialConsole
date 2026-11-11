// Host-side stub of the links2004/WebSockets server.
//
// It records every frame the console sends, so the unit tests can assert on
// the wire format.
#pragma once

#include <Arduino.h>
#include <functional>
#include <string>
#include <vector>

enum WStype_t {
    WStype_ERROR = 0,
    WStype_DISCONNECTED,
    WStype_CONNECTED,
    WStype_TEXT,
    WStype_BIN
};

class WebSocketsServer {
public:
    typedef std::function<void(uint8_t, WStype_t, uint8_t*, size_t)> EventHandler;

    WebSocketsServer() {}
    explicit WebSocketsServer(uint16_t port) { _port = port; }

    void onEvent(EventHandler handler) { _handler = handler; }
    void begin() {}
    void loop() {}
    int connectedClients(bool = false) { return _clients; }
    IPAddress remoteIP(uint8_t) { return IPAddress(); }

    bool broadcastTXT(uint8_t* payload, size_t length = 0) {
        record(payload, length);
        return true;
    }
    bool broadcastTXT(const uint8_t* payload, size_t length = 0) {
        record(payload, length);
        return true;
    }
    bool broadcastTXT(char* payload, size_t length = 0) {
        record((const uint8_t*)payload, length);
        return true;
    }
    bool broadcastTXT(const char* payload, size_t length = 0) {
        record((const uint8_t*)payload, length);
        return true;
    }

    bool sendTXT(uint8_t, uint8_t* payload, size_t length = 0) {
        record(payload, length);
        return true;
    }
    bool sendTXT(uint8_t, const uint8_t* payload, size_t length = 0) {
        record(payload, length);
        return true;
    }
    bool sendTXT(uint8_t, char* payload, size_t length = 0) {
        record((const uint8_t*)payload, length);
        return true;
    }
    bool sendTXT(uint8_t, const char* payload, size_t length = 0) {
        record((const uint8_t*)payload, length);
        return true;
    }
    bool sendTXT(uint8_t, String& payload) {
        record((const uint8_t*)payload.c_str(), payload.length());
        return true;
    }

    // Test hooks
    std::vector<uint8_t> lastFrame;
    std::vector<std::string> frames;
    int _clients = 1;

private:
    void record(const uint8_t* payload, size_t length) {
        lastFrame.assign(payload, payload + length);
        frames.push_back(std::string((const char*)payload, length));
    }

    EventHandler _handler;
    uint16_t _port = 81;
};
