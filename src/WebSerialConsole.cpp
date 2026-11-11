#define NO_WEB_SERIAL_REDEFINE
#include "WebSerialConsole.h"

#include <ctype.h>
#include <cstring>

// The single global instance every sketch uses.
WebSerialConsole WebSerial;

WebSerialConsole::WebSerialConsole() {
    // Sources the console itself needs. Everything else is up to the sketch;
    // only these two are predefined.
    defineSource(0, "debug", 'D', "DBG", "#94a3b8",
                 "Firmware debug output", false);
    defineSource(1, "user", 'U', "CMD", "#38bdf8",
                 "Commands typed in the console", true);
    _sourceCount = BUILTIN_SOURCE_COUNT;
}

// ---------------------------------------------------------------- lifecycle

void WebSerialConsole::begin(unsigned long baud) {
    // The board's default port is the console's port unless another one was
    // attached, in which case that port is already up and owned by the sketch.
    if (_hw) return;
    WebSerialDetail::getHWSerial().begin(baud);
}

void WebSerialConsole::begin(HardwareSerial& serial, unsigned long baud) {
    serial.begin(baud);
    attachSerial(serial);
}

void WebSerialConsole::attachSerial(Stream& serial) {
    _hw = &serial;
}

bool WebSerialConsole::begin(const char* ssid, const char* password,
                              unsigned long baud, unsigned long wifiTimeoutMs) {
    begin(baud);
    if (!WebSerialDetail::connectWiFi(ssid, password, wifiTimeoutMs)) {
        return false;
    }
    WebSerialDetail::startWeb();
    return true;
}

bool WebSerialConsole::beginAP(const char* ssid, const char* password,
                                unsigned long baud) {
    begin(baud);
    if (!WebSerialDetail::startAccessPoint(ssid, password)) {
        return false;
    }
    WebSerialDetail::startWeb();
    return true;
}

void WebSerialConsole::beginWeb() {
    WebSerialDetail::startWeb();
}

void WebSerialConsole::attachWebSocket(WebSocketsServer* ws) {
    _ws = ws;
    if (_ws) {
        _ws->onEvent([this](uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
            this->handleWebSocketEvent(num, type, payload, length);
        });
    }
}

void WebSerialConsole::onCommand(CommandHandler handler) {
    _customCmdHandler = handler;
}

void WebSerialConsole::onClientConnect(ClientEventHandler handler) {
    _onClientConnect = handler;
}

void WebSerialConsole::onClientDisconnect(ClientEventHandler handler) {
    _onClientDisconnect = handler;
}

// -------------------------------------------------------------- pause/resume

void WebSerialConsole::pause() {
    if (_paused) return;
    _paused = true;
    for (uint8_t i = 0; i < _sourceCount; i++) {
        _lineLen[i] = 0;
    }
}

void WebSerialConsole::resume() {
    _paused = false;
}

// ------------------------------------------------------------------ sources

void WebSerialConsole::defineSource(uint8_t idx, const char* id, char tag,
                                    const char* label, const char* color,
                                    const char* title, bool prompt) {
    if (idx >= MAX_SOURCES) return;

    SourceDef& def = _sources[idx];
    def.id = id;
    def.tag = tag;
    def.label = label;
    def.color = isValidColor(color) ? color : defaultColor(idx);
    def.title = title ? title : "";
    def.prompt = prompt;
    def.autoFlush = !prompt;
    def.used = true;
}

WebSerialConsole::Source WebSerialConsole::addSource(const char* id, char tag,
                                                      const char* label,
                                                      const char* color,
                                                      const char* title,
                                                      bool prompt) {
    String cleanId = sanitizeId(id);
    if (cleanId.length() == 0 || tag == '\0' || tag == '\n' ||
        tag == '\r' || tag == '\x01') {
        return Source::INVALID;
    }
    for (uint8_t i = 0; i < _sourceCount; i++) {
        if (!_sources[i].used) continue;
        if (_sources[i].id == cleanId || _sources[i].tag == tag) {
            return Source::INVALID;
        }
    }
    if (_sourceCount >= MAX_SOURCES) return Source::INVALID;

    uint8_t idx = _sourceCount++;
    defineSource(idx, cleanId.c_str(), tag, label ? label : cleanId.c_str(),
                 color, title, prompt);
    publishSources();
    return (Source)idx;
}

bool WebSerialConsole::configureSource(Source source, const char* label,
                                       const char* color, const char* title,
                                       int prompt) {
    if (!isValidSource(source)) return false;
    if (color && !isValidColor(color)) return false;

    SourceDef& def = _sources[(uint8_t)source];
    if (label) def.label = label;
    if (color) def.color = color;
    if (title) def.title = title;
    if (prompt >= 0) {
        def.prompt = prompt != 0;
        def.autoFlush = !def.prompt;
    }
    publishSources();
    return true;
}

bool WebSerialConsole::isValidSource(Source source) const {
    uint8_t idx = (uint8_t)source;
    return idx < _sourceCount && _sources[idx].used;
}

String WebSerialConsole::sourcesJson() const {
    String out;
    out.reserve(96 * (size_t)_sourceCount + 2);
    out += '[';
    bool first = true;
    for (uint8_t i = 0; i < _sourceCount; i++) {
        if (!_sources[i].used) continue;
        if (!first) out += ',';
        first = false;

        out += "{\"id\":";       appendJsonString(out, _sources[i].id);
        out += ",\"tag\":";      appendJsonString(out, String(_sources[i].tag));
        out += ",\"label\":";    appendJsonString(out, _sources[i].label);
        out += ",\"color\":";    appendJsonString(out, _sources[i].color);
        out += ",\"title\":";    appendJsonString(out, _sources[i].title);
        out += ",\"prompt\":";   out += _sources[i].prompt ? "true" : "false";
        out += '}';
    }
    out += ']';
    return out;
}

// -------------------------------------------------------------- output path

void WebSerialConsole::logLine(Source source, const String& text) {
    Scope scope(*this, source);
    write((const uint8_t*)text.c_str(), text.length());
    write('\n');
}

size_t WebSerialConsole::write(uint8_t c) {
    // Dropped while paused, so a bus echo can be kept off the console.
    if (_paused) return 1;

    // 1. Output to the attached hardware serial port immediately
    hwSerial().write(c);

    // 2. Buffer the character for the WebSocket line currently being built
    emitChar((char)c);
    return 1;
}

size_t WebSerialConsole::write(const uint8_t* buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    for (size_t i = 0; i < size; i++) {
        write(buffer[i]);
    }
    return size;
}

int WebSerialConsole::available() {
    size_t rxCount = (_rxHead >= _rxTail) ? (_rxHead - _rxTail) : (RX_BUF_SIZE + _rxHead - _rxTail);
    return (int)(rxCount + hwSerial().available());
}

int WebSerialConsole::read() {
    if (_rxHead != _rxTail) {
        uint8_t b = _rxBuffer[_rxTail];
        _rxTail = (_rxTail + 1) % RX_BUF_SIZE;
        return b;
    }
    return hwSerial().read();
}

int WebSerialConsole::peek() {
    if (_rxHead != _rxTail) {
        return _rxBuffer[_rxTail];
    }
    return hwSerial().peek();
}

void WebSerialConsole::flush() {
    hwSerial().flush();
}

WebSerialConsole::operator bool() const {
    // A bound Stream has no bool conversion of its own, so report ready.
    if (_hw) return true;
    return (bool)WebSerialDetail::getHWSerial();
}

int WebSerialConsole::clientCount() const {
    return _ws ? _ws->connectedClients() : 0;
}

// --------------------------------------------------------------------- loop

void WebSerialConsole::loop() {
    WebSerialDetail::handleWeb();
    if (_ws) {
        _ws->loop();
    }

    // Flush stale partial lines (a print without a trailing newline).
    // Prompt sources (USER by default) are excluded: slow typing on the
    // hardware console must stay in one line until Enter is pressed.
    unsigned long now = millis();
    for (uint8_t i = 0; i < _sourceCount; i++) {
        if (!_sources[i].used || !_sources[i].autoFlush) continue;
        if (_lineLen[i] > 0 && now - _lineLastMs[i] >= 25) {
            flushLine((Source)i);
        }
    }

    // Everything typed on the hardware serial console belongs to the USER
    // category and must also be readable through Serial.read().
    if (hwSerial().available()) {
        Scope scope(*this, Source::USER);
        while (hwSerial().available()) {
            int c = hwSerial().read();
            if (c >= 0) {
                if (!_paused) emitChar((char)c);
                pushRx((uint8_t)c);
            }
        }
    }
}

// ------------------------------------------------------------------ helpers

const char* WebSerialConsole::defaultColor(uint8_t idx) {
    static const char* const palette[] = {
        "#38bdf8", "#c084fc", "#34d399", "#fbbf24", "#f472b6",
        "#22d3ee", "#a3e635", "#fb923c", "#a78bfa", "#f87171"
    };
    return palette[idx % (sizeof(palette) / sizeof(palette[0]))];
}

// Source ids end up as CSS class names, so keep them to a safe subset.
String WebSerialConsole::sanitizeId(const char* id) {
    String out;
    if (!id) return out;
    for (const char* p = id; *p; ++p) {
        char c = *p;
        out += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    }
    return out;
}

bool WebSerialConsole::isValidColor(const char* color) {
    if (!color || !*color) return false;
    if (color[0] == '#') {
        size_t len = strlen(color + 1);
        if (len != 3 && len != 6 && len != 8) return false;
        for (const char* p = color + 1; *p; ++p) {
            if (!isxdigit((unsigned char)*p)) return false;
        }
        return true;
    }
    for (const char* p = color; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (!isalnum(c) && c != ' ' && c != '-') return false;
    }
    return true;
}

void WebSerialConsole::appendJsonString(String& out, const String& value) {
    out += '"';
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if ((unsigned char)c < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    out += '"';
}

char WebSerialConsole::tagFor(Source source) const {
    uint8_t idx = (uint8_t)source;
    if (idx >= _sourceCount || !_sources[idx].used) idx = 0;
    return _sources[idx].tag;
}

void WebSerialConsole::emitChar(char c) {
    uint8_t idx = (uint8_t)_source;
    if (idx >= _sourceCount || !_sources[idx].used) idx = 0;

    if (c == '\r' || c == '\n') {
        if (_lineLen[idx] > 0) flushLine((Source)idx);
        return;
    }

    if (_lineLen[idx] >= LINE_BUF_SIZE - 1) {
        flushLine((Source)idx);
    }

    _lineBuffer[idx][_lineLen[idx]++] = c;
    _lineLastMs[idx] = millis();
}

void WebSerialConsole::flushLine(Source source) {
    uint8_t idx = (uint8_t)source;
    size_t len = _lineLen[idx];
    if (len == 0) return;

    _lineBuffer[idx][len] = '\0';
    broadcastTagged(source, _lineBuffer[idx], len);
    _lineLen[idx] = 0;
}

// Wire format: 0x01 <tag> <line text> '\n'
// The tag character is whatever the sketch registered with addSource();
// the browser maps it back to the source's id, label and colour.
void WebSerialConsole::broadcastTagged(Source source, const char* text, size_t len) {
    char out[LINE_BUF_SIZE + 3];
    out[0] = '\x01';
    out[1] = tagFor(source);
    memcpy(out + 2, text, len);
    out[len + 2] = '\n';
    size_t total = len + 3;

    appendToScrollback(out, total);

    if (_ws && _ws->connectedClients() > 0) {
        _ws->broadcastTXT((uint8_t*)out, total);
    }
}

void WebSerialConsole::appendToScrollback(const char* data, size_t len) {
    if (len == 0) return;

    if (len >= SCROLLBACK_SIZE) {
        data += len - (SCROLLBACK_SIZE - 1);
        len = SCROLLBACK_SIZE - 1;
    }

    if (_scrollbackLen + len > SCROLLBACK_SIZE - 1) {
        size_t overflow = _scrollbackLen + len - (SCROLLBACK_SIZE - 1);
        size_t drop = overflow;
        // Do not start the replay in the middle of a tagged line.
        while (drop < _scrollbackLen && _scrollback[drop - 1] != '\n') drop++;
        if (drop > _scrollbackLen) drop = _scrollbackLen;

        memmove(_scrollback, _scrollback + drop, _scrollbackLen - drop);
        _scrollbackLen -= drop;
    }

    memcpy(_scrollback + _scrollbackLen, data, len);
    _scrollbackLen += len;
}

String WebSerialConsole::taggedLine(Source source, const String& text) {
    String out;
    out.reserve(text.length() + 3);
    out += (char)0x01;
    out += tagFor(source);
    out += text;
    out += '\n';
    return out;
}

// The source table as it goes over the WebSocket: 0x02 <json> '\n'
// The browser needs it to map the wire tags back to labels and colours, and
// it is pushed on connect so a page that loads late still gets the table.
String WebSerialConsole::sourceTableMessage() const {
    String json = sourcesJson();
    String out;
    out.reserve(json.length() + 2);
    out += (char)0x02;
    out += json;
    out += '\n';
    return out;
}

// Push the current source definitions to every connected browser, so a client
// that is already open picks up sources that were added or restyled after it
// connected.
void WebSerialConsole::publishSources() {
    if (!_ws || _ws->connectedClients() == 0) return;
    String msg = sourceTableMessage();
    _ws->broadcastTXT((uint8_t*)msg.c_str(), msg.length());
}

void WebSerialConsole::pushRx(uint8_t b) {
    size_t next = (_rxHead + 1) % RX_BUF_SIZE;
    if (next != _rxTail) {
        _rxBuffer[_rxHead] = b;
        _rxHead = next;
    }
}

void WebSerialConsole::rememberClient(uint8_t id, IPAddress ip) {
    // Update in place when the slot is already known.
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (_clients[i].used && _clients[i].id == id) {
            _clients[i].ip = ip;
            return;
        }
    }
    // Otherwise take a free slot, reusing the oldest one when full.
    uint8_t slot = MAX_CLIENTS;
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (!_clients[i].used) { slot = i; break; }
    }
    if (slot == MAX_CLIENTS) slot = 0;

    _clients[slot].used = true;
    _clients[slot].id = id;
    _clients[slot].ip = ip;
}

void WebSerialConsole::forgetClient(uint8_t id) {
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (_clients[i].used && _clients[i].id == id) {
            _clients[i].used = false;
        }
    }
}

IPAddress WebSerialConsole::clientIp(uint8_t id) const {
    for (uint8_t i = 0; i < MAX_CLIENTS; i++) {
        if (_clients[i].used && _clients[i].id == id) {
            return _clients[i].ip;
        }
    }
    return IPAddress();
}

// ----------------------------------------------------------- web socket side

void WebSerialConsole::handleWebSocketEvent(uint8_t num, WStype_t type,
                                             uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED: {
            IPAddress ip = clientIp(num);
            forgetClient(num);
            logLine(Source::DEBUG, "[WebSerial] Client #" + String(num) + " disconnected");
            if (_onClientDisconnect) _onClientDisconnect(num, ip);
            break;
        }
        case WStype_CONNECTED: {
            IPAddress ip = _ws ? _ws->remoteIP(num) : IPAddress();
            rememberClient(num, ip);

            // Keep this off the web stream: the client receives the same
            // information in its welcome banner below.
            hwSerial().printf(
                "[WebSerial] Client #%u connected from %s\n", num, ip.toString().c_str());

            if (_ws) {
                // The source table goes first, so every tagged line that
                // follows can be rendered with the right label and colour.
                String sourceTable = sourceTableMessage();
                _ws->sendTXT(num, sourceTable);

                // Welcome message, tagged as debug/system output
                String banner;
                banner += taggedLine(Source::DEBUG, "=== Web Serial Console connected ===");
                banner += taggedLine(Source::DEBUG,
                                     "IP: " + WiFi.localIP().toString() +
                                     " | Free Heap: " + String(ESP.getFreeHeap()) + " B");
                banner += taggedLine(Source::DEBUG, "Type 'help' for available commands.");
                banner += taggedLine(Source::DEBUG, "=================================================");
                _ws->sendTXT(num, banner);

                // Send recent scrollback if available
                if (_scrollbackLen > 0) {
                    _ws->sendTXT(num, (const uint8_t*)_scrollback, _scrollbackLen);
                }
            }

            if (_onClientConnect) _onClientConnect(num, ip);
            break;
        }
        case WStype_TEXT: {
            if (length == 0 || !payload) break;

            String text = "";
            text.reserve(length);
            for (size_t i = 0; i < length; i++) {
                text += (char)payload[i];
            }

            String cmd = text;
            cmd.trim();

            // Echo the typed command to every client (including this one)
            // as a USER line, so it is filtered/exported like any other.
            if (cmd.length() > 0) {
                logLine(Source::USER, cmd);
            }

            if (cmd.length() > 0 && runBuiltInCommand(cmd)) {
                return;
            }

            // Custom command handler if registered
            if (_customCmdHandler && _customCmdHandler(cmd)) {
                return;
            }

            // Push incoming bytes into RX buffer so standard Serial.read() can consume them
            for (size_t i = 0; i < length; i++) {
                pushRx(payload[i]);
            }
            break;
        }
        default:
            break;
    }
}

// The built-in commands. Returns true when the command was handled.
bool WebSerialConsole::runBuiltInCommand(const String& cmd) {
    if (cmd.equalsIgnoreCase("help")) {
        println("\r\n--- Built-in Console Commands ---");
        println("  help    : Show this help list");
        println("  ip      : Display WiFi connection details");
        println("  heap    : Display free RAM / heap stats");
        println("  uptime  : Display ESP32 uptime");
        println("  sources : List the registered output sources");
        println("  restart : Reboot the ESP32");
        println("---------------------------------");
        return true;
    }

    if (cmd.equalsIgnoreCase("ip")) {
        println("\r\n--- WiFi Info ---");
        print("  IP Address : "); println(WiFi.localIP());
        print("  Subnet Mask: "); println(WiFi.subnetMask());
        print("  Gateway    : "); println(WiFi.gatewayIP());
        print("  DNS        : "); println(WiFi.dnsIP());
        print("  RSSI       : "); print(WiFi.RSSI()); println(" dBm");
        print("  MAC        : "); println(WiFi.macAddress());
        println("-----------------");
        return true;
    }

    if (cmd.equalsIgnoreCase("heap")) {
        println("\r\n--- Memory Info ---");
        print("  Free Heap     : "); print(ESP.getFreeHeap()); println(" bytes");
        print("  Min Free Heap : "); print(ESP.getMinFreeHeap()); println(" bytes");
        print("  Max Alloc Heap: "); print(ESP.getMaxAllocHeap()); println(" bytes");
        println("-------------------");
        return true;
    }

    if (cmd.equalsIgnoreCase("uptime")) {
        unsigned long sec = millis() / 1000;
        unsigned long m = sec / 60;
        unsigned long h = m / 60;
        unsigned long d = h / 24;
        println("\r\n--- System Uptime ---");
        printf("  %lu days, %02lu:%02lu:%02lu\n", d, h % 24, m % 60, sec % 60);
        println("---------------------");
        return true;
    }

    if (cmd.equalsIgnoreCase("sources")) {
        println("\r\n--- Registered Sources ---");
        for (uint8_t i = 0; i < _sourceCount; i++) {
            if (!_sources[i].used) continue;
            print("  ");
            print(_sources[i].label);
            print(" [");
            print(_sources[i].id);
            print("] tag=");
            print(_sources[i].tag);
            print(" color=");
            println(_sources[i].color);
        }
        println("---------------------------");
        return true;
    }

    if (cmd.equalsIgnoreCase("restart") || cmd.equalsIgnoreCase("reboot")) {
        println("\r\n[WebSerial] Rebooting ESP32 in 500ms...");
        flush();
        delay(500);
        ESP.restart();
        return true;
    }

    return false;
}
