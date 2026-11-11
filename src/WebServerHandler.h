#pragma once

// Legacy entry points kept so sketches written against the original
// SplFlp_Master helper keep compiling. The library itself is driven through
// the WebSerial object (see WebSerialConsole.h).
void setupWebServer();
void handleWebServer();
