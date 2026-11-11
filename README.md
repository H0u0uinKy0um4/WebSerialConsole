# WebSerialConsole

A [PlatformIO](https://platformio.org) library that turns an ESP32's hardware
serial console into a browser-accessible **web serial console**.

It is the reusable "web console" building block extracted from the
`SplFlp_Master` split-flap bridge. The library owns the WebSocket bridge, the
source-tagged line protocol, the replay scrollback and the built-in console
commands, so any ESP32 sketch can serve a browser terminal without shipping its
own copy of that logic.

## Features

- **WebSocket bridge** – streams the hardware serial console to connected
  browser clients and forwards browser keystrokes back into the serial RX ring
  buffer (and therefore into `Serial.read()`).
- **Source tagging** – every output line is tagged with its origin
  (`H` = Home Assistant request, `U` = user command, `S` = slave response,
  `M` = master transmission, `D` = debug). Lines are buffered per source so
  HA traffic and manual typing never mix into one terminal line.
- **Replay scrollback** – recent output is kept and replayed to newly connected
  WebSocket clients.
- **Built-in commands** – `help`, `ip`, `heap`, `uptime`, `restart` (+ custom
  command handler via `WebSerial.onCommand()`).
- **Flush policy** – partial lines without a trailing newline are flushed after
  a short idle window; `USER` input is never auto-flushed so typing stays in one
  line until Enter.

## Installation

```ini
lib_deps =
    H0u0uinKy0um4/WebSerialConsole @ ^1.0.0
```

The library pulls in its own WebSocket dependency
(`links2004/WebSockets @ ^2.4.1`).

## Usage

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "WebSerialConsole.h"
#include "WebServerHandler.h"

void setup() {
    Serial.begin(115200);   // or WebSerial.begin(115200)

    // WiFi connection handled by the sketch

    setupWebServer();       // serves the page, starts WS, attaches console
}

void loop() {
    handleWebServer();      // server.handleClient() + WebSerial.loop()
}
```

The web UI is served from the library's embedded page (`http://<ip>/`) and
connects to the WebSocket on port 81.

### Source tagging

```cpp
WebSerialConsole::Scope scope(WebSerial, WebSerialConsole::Source::HA);
WebSerial.logLine(WebSerialConsole::Source::HA, "hello");
```

### Custom commands

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

`H` = Home Assistant request, `U` = user command, `S` = slave response,
`M` = master transmission (outgoing RS485 command), `D` = debug output.

## Relationship to the Split-Flap projects

This library is the web console of the Split-Flap firmware family:

- `SplFlp_Master` – the ESP32 master that owns the RS485 bus and MQTT bridge.
- `SplFlp_Slave_Attiny` – the ATtiny slave that answers on the bus.

See the master repository for the bus/MQTT contracts this console is used with.

## License

MIT
