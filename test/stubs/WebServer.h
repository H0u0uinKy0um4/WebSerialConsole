// Host-side stub of the Arduino WebServer library.
#pragma once

#include <Arduino.h>
#include <functional>

enum HTTPMethod {
    HTTP_ANY,
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_PATCH,
    HTTP_DELETE,
    HTTP_OPTIONS
};

class WebServer {
public:
    typedef std::function<void()> THandlerFunction;

    WebServer() {}
    explicit WebServer(uint16_t port) { _port = port; }

    void on(const char*, HTTPMethod, THandlerFunction) {}
    void onNotFound(THandlerFunction) {}
    void send_P(int, const char*, const char*) {}
    void send(int, const char*, const String&) {}
    void begin() {}
    void handleClient() {}
    String uri() { return String("/"); }

private:
    uint16_t _port = 80;
};
