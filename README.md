# WebSerialConsole

A [PlatformIO](https://platformio.org) library that turns an ESP32's serial
console into a browser-accessible **web serial console** — a drop-in debugging
console for any ESP32 project.

It is the reusable "web console" building block extracted from the
`SplFlp_Master` split-flap bridge. The library owns the WiFi/mDNS setup, the
WebSocket bridge, the source-tagged line protocol, the replay scrollback and the
built-in console commands, so any sketch can serve a browser terminal without
shipping its own copy of that logic.

## Quick start

```cpp
#include <WebSerialConsole.h>

void setup() {
    WebSerial.begin("YOUR_SSID", "YOUR_PASSWORD");   // WiFi + console + USB serial
    Serial.println("Hello from the ESP32!");
}

void loop() {
    WebSerial.loop();
}
```

Open **http://webserial.local/** (mDNS) or the IP address printed on the USB
console. Everything the sketch prints with `Serial.print()` shows up in the
browser, and anything typed in the browser can be read with `Serial.read()`.

No network around? Serve the console from the board's own access point instead:

```cpp
WebSerial.beginAP("ESP32-Setup", "setuppass");   // open http://192.168.4.1/
```

## Features

- **One-call setup** – `begin(ssid, password)` joins WiFi, starts the HTTP and
  WebSocket servers and bridges the serial port. `loop()` does the rest.
- **Easy to find** – the console answers on `http://webserial.local/` through
  mDNS, and the address is printed to the USB console at boot. `beginAP()`
  raises a setup access point when there is no known network.
- **WebSocket bridge** – streams the hardware serial console to connected
  browser clients and forwards browser keystrokes back into the serial RX ring
  buffer (and therefore into `Serial.read()`).
- **User-defined sources** – only `DEBUG` and `USER` are built in. Register
  any number of application sources (Home Assistant, a bus master, slaves, …)
  with your own tag character, short label and colour. Lines are buffered per
  source so traffic from different sources never mixes into one terminal line.
  The web UI builds its badges, filter chips and colours from the registered
  definitions, so nothing project-specific is hardcoded in the library.
- **Replay scrollback** – recent output is kept and replayed to newly connected
  WebSocket clients.
- **Built-in commands** – `help`, `ip`, `heap`, `uptime`, `restart` (+ custom
  command handler via `WebSerial.onCommand()`).
- **Pause for noisy buses** – `pause()`/`resume()` drop console output for a
  short window, e.g. while a half-duplex bus echoes a command back.
- **Flush policy** – partial lines without a trailing newline are flushed after
  a short idle window; sources registered as `prompt` (such as `USER` input) are
  never auto-flushed, so typing stays in one line until Enter.

## Installation

```ini
lib_deps =
    H0u0uinKy0um4/WebSerialConsole @ ^1.1.0
```

The library pulls in its own WebSocket dependency
(`links2004/WebSockets @ ^2.4.1`). mDNS, WiFi and the web server come from the
Arduino-ESP32 core.

## Setup

### Automatic (recommended)

```cpp
WebSerial.begin(WIFI_SSID, WIFI_PASSWORD);   // returns false on WiFi timeout
WebSerial.beginAP(ssid, password);           // ...or raise an access point
```

Both overloads also start the default serial port at `115200` baud; pass a baud
rate as the last argument to change it:

```cpp
WebSerial.begin(WIFI_SSID, WIFI_PASSWORD, 921600);
```

`begin(ssid, password, baud, wifiTimeoutMs)` waits up to 15 s for the network by
default. On failure it prints the reason and returns `false`, and no server is
started — so sketches can fall back:

```cpp
if (!WebSerial.begin(WIFI_SSID, WIFI_PASSWORD)) {
    WebSerial.beginAP("ESP32-Setup", "setuppass");
}
```

The mDNS name is `webserial`. Override it per project when several boards share
a network:

```ini
build_flags = -DWEBSERIAL_MDNS_HOSTNAME=\"console-1\"
```

### Manual (sketch owns WiFi and the port)

```cpp
void setup() {
    Serial1.begin(115200, SERIAL_8N1, 16, 17);   // your port, your pins
    WebSerial.attachSerial(Serial1);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);        // your network logic
    while (WiFi.status() != WL_CONNECTED) delay(200);

    WebSerial.beginWeb();                        // start HTTP + WebSocket
}

void loop() {
    WebSerial.loop();
}
```

`setupWebServer()` / `handleWebServer()` still exist as thin wrappers around
`beginWeb()` / `loop()` for older sketches.

### Where does console output go?

By default the console bridges the board's standard `Serial`, and the header
remaps `Serial` to the console so existing `Serial.print()` debugging appears in
the browser too. Include `WebSerialConsole.h` and use `Serial` as usual.

The remap is a macro; define `NO_WEB_SERIAL_REDEFINE` (e.g. in `build_flags`) to
disable it and call `WebSerial.print()` explicitly instead. Note that with the
remap active, `Serial` in *every* file that includes the header means the
console.

### Another hardware serial port

```cpp
WebSerial.begin(Serial1, 115200);            // console starts Serial1 itself

// or, if the sketch configures the port (custom pins, RS485, ...):
Serial1.begin(115200, SERIAL_8N1, 16, 17);
WebSerial.attachSerial(Serial1);
```

Everything received on the bound port is treated as console input and shown as
a `USER` line, so bind a port used as a console rather than a data link. One
port can be bound per console; traffic from other ports can still be shown by
writing their bytes through the console under a `Scope`.

### Pausing output

`pause()` discards console output until `resume()`, including any partial line
that was in progress. Incoming data is still delivered to `Serial.read()`, so
pausing only silences what is written to the console.

This is handy on a half-duplex bus that echoes what was sent, to keep the
command from showing up twice:

```cpp
{
    WebSerialConsole::Pause pause(WebSerial);   // mute the console
    bus.write(command);
    bus.flush();
    delay(2);                                   // let the local echo drain
}                                           // output resumes automatically

// Or control it explicitly:
WebSerial.pause();
// ...
WebSerial.resume();
```

While paused, output is dropped rather than buffered, so the paused window
should stay short.

## Sources and colours

Sources are defined by the sketch: an id (used as the CSS class), the single
character sent on the wire, the short badge label, a colour (`#rgb`, `#rrggbb`,
`#rrggbbaa` or a colour name) and an optional tooltip. The colour can be omitted
to take one from the built-in palette.

```cpp
// In setup() or at file scope:
WebSerialConsole::Source ha =
    WebSerial.addSource("ha", 'H', "HA", "#c084fc", "Home Assistant requests");
WebSerial.addSource("master", 'M', "MST", "#fbbf24", "Bus master output");
WebSerial.addSource("slave",  'S', "SLV", "#34d399", "Bus slave responses");

// Then tag output with the returned handle:
{
    WebSerialConsole::Scope scope(WebSerial, ha);   // tags everything printed
    Serial.println("hello");
}
WebSerial.logLine(ha, "hello");                    // one complete line
```

`addSource()` returns `WebSerialConsole::Source::INVALID` if the id/tag is already
taken or the registry is full (`WebSerialConsole::MAX_SOURCES`, 12 sources
including the two built-ins).

Even the built-in sources can be restyled:

```cpp
WebSerial.configureSource(WebSerialConsole::Source::USER, "CMD", "#38bdf8");
WebSerial.configureSource(WebSerialConsole::Source::DEBUG, "DBG", "#94a3b8");

// The last argument is the "prompt" flag (tri-state: -1 keeps it unchanged):
WebSerial.configureSource(WebSerialConsole::Source::USER, nullptr, nullptr,
                          nullptr, 0);   // no "> " prefix, partial lines auto-flush
```

The browser learns the definitions in two ways: `GET /sources` returns them as
JSON, and the firmware pushes the same table over the WebSocket whenever the
registry changes. So labelling, colouring and filtering adapt to whatever the
sketch registered — also for a browser that is already open when a source is
added at runtime.

## Custom commands

```cpp
WebSerial.onCommand([](const String& cmd) -> bool {
    // return true when the command was handled
    return false;
});
```

## Wire protocol

Output lines are broadcast to WebSocket clients as:

```
0x01 <tag> <line text> '\n'
```

`<tag>` is the character passed to `addSource()`; the built-in sources use
`U` (console input) and `D` (debug output).

The source definitions are sent as:

```
0x02 <json array> '\n'
```

The JSON array holds one object per registered source (`id`, `tag`, `label`,
`color`, `title`, `prompt`). It is pushed when a client connects and whenever
`addSource()` / `configureSource()` changes the registry.

## Tests

The source registry, the wire format and the JSON the browser consumes are
covered by host-side unit tests (no hardware needed, Arduino and WebSockets are
stubbed in `test/stubs`):

```bash
pio test -e native
```

`test/test_console` holds the on-device suite, which needs a board:

```bash
pio test -e esp32c3 --without-uploading
```

## Relationship to the Split-Flap projects

This library is the web console of the Split-Flap firmware family:

- `SplFlp_Master` – the ESP32 master that owns the RS485 bus and MQTT bridge.
- `SplFlp_Slave_Attiny` – the ATtiny slave that answers on the bus.

See the master repository for the bus/MQTT contracts this console is used with.

## License

MIT
