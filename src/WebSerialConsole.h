#pragma once
#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <functional>

// The console is header-only for the sketch-facing API, but the heavy lifting
// lives in WebSerialConsole.cpp so that a project that includes this header
// does not pay for the implementation in every translation unit.

namespace WebSerialDetail {
    // The board's real hardware serial port. It is deliberately reached
    // through a helper: the header may remap `Serial` to the console, and the
    // console must always be able to talk to the real port underneath.
    inline decltype(::Serial)& getHWSerial() {
        return ::Serial;
    }

    // Implemented in WebServerHandler.cpp. Keeping them out of the class
    // header means the console itself carries no HTTP server state.
    void startWeb();
    void handleWeb();
    bool connectWiFi(const char* ssid, const char* password, unsigned long timeoutMs);
    bool startAccessPoint(const char* ssid, const char* password);
}

class WebSerialConsole : public Stream {
public:
    typedef std::function<bool(const String& cmd)> CommandHandler;
    typedef std::function<void(uint8_t clientId, IPAddress ip)> ClientEventHandler;

    // Handle to an output category ("source"). The numeric value is also the
    // index of the per-source line buffer, so the built-in values must stay
    // stable.
    //
    // Only DEBUG and USER are built in, because the console itself needs them.
    // Every application-specific category (Home Assistant, a bus master,
    // slaves, sensors, ...) is registered by the sketch with addSource(), so
    // no project-specific naming is hardcoded in the library.
    enum class Source : uint8_t {
        DEBUG   = 0,
        USER    = 1,
        INVALID = 0xFF
    };

    // Built-in sources plus the custom ones a sketch may register.
    static constexpr uint8_t MAX_SOURCES = 12;

    // Number of sources the library itself registers (DEBUG and USER).
    static constexpr uint8_t BUILTIN_SOURCE_COUNT = 2;

    // Temporarily tag everything written through Serial with a source.
    class Scope {
    public:
        Scope(WebSerialConsole& console, Source source)
            : _console(console), _previous(console.source()) {
            _console.setSource(source);
        }
        ~Scope() { _console.setSource(_previous); }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        WebSerialConsole& _console;
        Source _previous;
    };

    // Drop console output for a short window, e.g. while sending a command that
    // a half-duplex bus echoes back. Output produced while paused is discarded
    // (not replayed), and partial lines buffered at that moment are dropped too.
    class Pause {
    public:
        explicit Pause(WebSerialConsole& console)
            : _console(console), _previous(console.paused()) {
            _console.pause();
        }
        ~Pause() {
            if (!_previous) _console.resume();
        }

        Pause(const Pause&) = delete;
        Pause& operator=(const Pause&) = delete;

    private:
        WebSerialConsole& _console;
        bool _previous;
    };

private:
    WebSocketsServer* _ws = nullptr;
    CommandHandler _customCmdHandler = nullptr;
    ClientEventHandler _onClientConnect = nullptr;
    ClientEventHandler _onClientDisconnect = nullptr;

    // Hardware serial the console is bridged to. Defaults to the board's
    // standard `Serial`; attachSerial()/begin(HardwareSerial&) can bind any
    // other port (Serial1, Serial2, ...).
    Stream* _hw = nullptr;

    Stream& hwSerial() const {
        return _hw ? *_hw : WebSerialDetail::getHWSerial();
    }

    static const size_t LINE_BUF_SIZE = 256;
    static const size_t SCROLLBACK_SIZE = 4096;

    Source _source = Source::DEBUG;
    bool _paused = false;

    // Definition of one output category. Everything the browser needs to
    // render, colour and filter a source lives here and is published through
    // sourcesJson().
    struct SourceDef {
        String id;              // stable identifier, also the CSS class suffix
        String label;           // short badge text shown in the terminal
        String color;           // CSS colour for lines, badges and chips
        String title;           // tooltip text for the filter chip
        char tag = 'D';         // single character used on the wire
        bool prompt = false;    // render lines with a leading "> "
        bool autoFlush = true;  // flush partial lines after an idle window
        bool used = false;
    };

    SourceDef _sources[MAX_SOURCES];
    uint8_t _sourceCount = 0;

    // One partially received line per source, so output from different
    // sources can never be mixed into a single terminal line.
    char _lineBuffer[MAX_SOURCES][LINE_BUF_SIZE];
    size_t _lineLen[MAX_SOURCES] = {};
    unsigned long _lineLastMs[MAX_SOURCES] = {};

    // Incoming RX ring buffer (fed by WebSocket or USB CDC)
    static const size_t RX_BUF_SIZE = 512;
    uint8_t _rxBuffer[RX_BUF_SIZE];
    volatile size_t _rxHead = 0;
    volatile size_t _rxTail = 0;

    // Recent output scrollback. Stored as already-tagged lines, so it can be
    // replayed verbatim to a newly connected client.
    char _scrollback[SCROLLBACK_SIZE];
    size_t _scrollbackLen = 0;

    // Last known address of every connected browser client, so the sketch can
    // be told who connected and from where.
    static const uint8_t MAX_CLIENTS = 8;
    struct ClientInfo {
        bool used = false;
        uint8_t id = 0;
        IPAddress ip;
    };
    ClientInfo _clients[MAX_CLIENTS];

    void defineSource(uint8_t idx, const char* id, char tag, const char* label,
                      const char* color, const char* title, bool prompt);
    void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
    bool runBuiltInCommand(const String& cmd);
    void rememberClient(uint8_t id, IPAddress ip);
    void forgetClient(uint8_t id);
    IPAddress clientIp(uint8_t id) const;

    static const char* defaultColor(uint8_t idx);
    static String sanitizeId(const char* id);
    static bool isValidColor(const char* color);
    static void appendJsonString(String& out, const String& value);

    char tagFor(Source source) const;
    void emitChar(char c);
    void flushLine(Source source);
    void broadcastTagged(Source source, const char* text, size_t len);
    void appendToScrollback(const char* data, size_t len);
    String taggedLine(Source source, const String& text);
    String sourceTableMessage() const;
    void pushRx(uint8_t b);

public:
    WebSerialConsole();

    // Start and bridge the board's default hardware serial (the one the
    // console uses unless another port is attached).
    void begin(unsigned long baud = 115200);

    // Start and bridge a different hardware serial port (Serial1, Serial2, ...)
    // with the given baud rate. Call this instead of begin(baud).
    void begin(HardwareSerial& serial, unsigned long baud = 115200);

    // Bridge an already configured serial port. Use this when the sketch owns
    // the port setup, e.g. custom RX/TX pins or RS485/line settings.
    void attachSerial(Stream& serial);

    // One-call setup for a debug console: joins WiFi, starts the web servers
    // and bridges the default hardware serial. The console is then reachable at
    // http://webserial.local (mDNS) and at the printed IP address.
    //
    // Returns false when the network association timed out; nothing is served
    // in that case. Nothing else needs to be called in loop() besides loop().
    bool begin(const char* ssid, const char* password,
               unsigned long baud = 115200,
               unsigned long wifiTimeoutMs = 15000);

    // Same, but the board raises its own access point instead of joining a
    // network. `password` may be nullptr for an open network; otherwise it
    // needs at least 8 characters. Connect to it and open
    // http://192.168.4.1/ (or http://webserial.local/).
    bool beginAP(const char* ssid, const char* password = nullptr,
                 unsigned long baud = 115200);

    // Start the HTTP + WebSocket servers without touching WiFi. Use this when
    // the sketch owns the connection, e.g. after WebSerial.begin(Serial1) and
    // its own WiFi.begin().
    void beginWeb();

    // Attach an externally managed WebSocket server. startWeb() does this
    // automatically; only needed when the sketch owns the WebSocket server.
    void attachWebSocket(WebSocketsServer* ws);

    // Register a handler for commands typed in the browser. Return true when
    // the command was handled; false lets the console fall back to the
    // built-in commands and to the serial RX ring buffer.
    void onCommand(CommandHandler handler);

    // Called when a browser client connects or disconnects.
    void onClientConnect(ClientEventHandler handler);
    void onClientDisconnect(ClientEventHandler handler);

    Source source() const { return _source; }
    void setSource(Source source) {
        if (isValidSource(source)) _source = source;
    }

    // Stop emitting output (to the browser and the hardware port) until
    // resume(). Everything written while paused is discarded, including any
    // partial line that was in progress, so nothing appears late. Incoming
    // data is still delivered to Serial.read().
    void pause();
    void resume();
    bool paused() const { return _paused; }

    // Register a custom output category. `tag` is the single character used on
    // the wire and by the browser, `label` the short badge text and `color`
    // any CSS colour ("#rrggbb", "#rgb" or a colour name). A colour is
    // optional; sources without one get a colour from the built-in palette.
    //
    // Set `prompt` for sources that receive typed input: their lines are
    // displayed with a "> " prefix and partial lines are never auto-flushed.
    //
    // Returns Source::INVALID when the registry is full or when the id or tag
    // is already in use.
    Source addSource(const char* id, char tag, const char* label,
                     const char* color = nullptr, const char* title = nullptr,
                     bool prompt = false);

    // Change the appearance of an existing source. Also works for the built-in
    // DEBUG and USER sources, so even their colours are up to the sketch.
    // `prompt` is tri-state: -1 keeps the current value, 0 and 1 set it.
    bool configureSource(Source source, const char* label = nullptr,
                         const char* color = nullptr,
                         const char* title = nullptr,
                         int prompt = -1);

    bool isValidSource(Source source) const;

    // Number of registered sources (built-ins included).
    uint8_t sourceCount() const { return _sourceCount; }

    // JSON description of every registered source. The embedded web UI fetches
    // this to build its badges, filter chips and colours.
    String sourcesJson() const;

    // Publish the source definitions to every connected browser client. The
    // browser needs them to map the wire tags back to ids, labels and
    // colours, so this runs whenever the registry changes and when a client
    // connects.
    void publishSources();

    // Emit one complete line under the given source. Also goes to the
    // hardware UART so the USB console stays in sync.
    void logLine(Source source, const String& text);

    // Number of browsers currently connected.
    int clientCount() const;

    // Stream & Print interface implementation
    virtual size_t write(uint8_t c) override;
    virtual size_t write(const uint8_t* buffer, size_t size) override;
    virtual int available() override;
    virtual int read() override;
    virtual int peek() override;
    virtual void flush() override;

    operator bool() const;

    // Main service loop: serve HTTP, handle websockets and forward USB CDC
    // input. Calling this is all a sketch needs to do in loop().
    void loop();
};

extern WebSerialConsole WebSerial;

#ifndef NO_WEB_SERIAL_REDEFINE
#define Serial WebSerial
#endif
