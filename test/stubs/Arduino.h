// Minimal Arduino stub for the host-side unit tests of WebSerialConsole.
//
// It implements just enough of the Arduino API for the console to compile and
// run on the host: String, Print/Stream, HardwareSerial, ESP, WiFi, IPAddress
// and the usual free functions.
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#define PROGMEM
#define SERIAL_8N1 0x800001c

class String {
public:
    String() {}
    String(const char* s) : _s(s ? s : "") {}
    String(char c) : _s(1, c) {}
    String(int v) { char b[24]; snprintf(b, sizeof(b), "%d", v); _s = b; }
    String(unsigned int v) { char b[24]; snprintf(b, sizeof(b), "%u", v); _s = b; }
    String(long v) { char b[32]; snprintf(b, sizeof(b), "%ld", v); _s = b; }
    String(unsigned long v) { char b[32]; snprintf(b, sizeof(b), "%lu", v); _s = b; }
    String(const String& other) = default;
    String& operator=(const String& other) = default;

    String& operator+=(char c) { _s += c; return *this; }
    String& operator+=(const char* s) { if (s) _s += s; return *this; }
    String& operator+=(const String& other) { _s += other._s; return *this; }

    String operator+(const String& other) const { String r(*this); r += other; return r; }
    String operator+(const char* s) const { String r(*this); r += s; return r; }
    String operator+(char c) const { String r(*this); r += c; return r; }

    size_t length() const { return _s.size(); }
    const char* c_str() const { return _s.c_str(); }
    char operator[](size_t i) const { return _s[i]; }
    void reserve(size_t) {}

    bool operator==(const String& other) const { return _s == other._s; }
    bool operator!=(const String& other) const { return !(*this == other); }

    String trim() const {
        size_t b = _s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return String();
        size_t e = _s.find_last_not_of(" \t\r\n");
        return String(_s.substr(b, e - b + 1).c_str());
    }

    bool equalsIgnoreCase(const String& other) const {
        return strcasecmp(_s.c_str(), other._s.c_str()) == 0;
    }

private:
    std::string _s;
};

inline String operator+(const char* s, const String& other) {
    String r(s);
    r += other;
    return r;
}

class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t) { return 1; }
    virtual size_t write(const uint8_t* buffer, size_t size) { return size; }
    template <typename T> size_t print(T) { return 0; }
    template <typename T> size_t println(T) { return 0; }
    size_t println() { return 0; }
    int printf(const char*, ...) { return 0; }
};

class Stream : public Print {
public:
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }
    virtual void flush() {}
    void setTimeout(unsigned long) {}
    size_t readBytes(char*, size_t) { return 0; }
};

class IPAddress {
public:
    IPAddress() {}
    IPAddress(uint8_t, uint8_t, uint8_t, uint8_t) {}
    String toString() const { return String("0.0.0.0"); }
};

class HardwareSerial : public Stream {
public:
    void begin(unsigned long, uint32_t = 0, int8_t = -1, int8_t = -1) {}
    void end() {}
    void flush() override {}
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    operator bool() const { return true; }
};

class EspClass {
public:
    uint32_t getFreeHeap() { return 200000; }
    uint32_t getMinFreeHeap() { return 190000; }
    uint32_t getMaxAllocHeap() { return 100000; }
    void restart() {}
};

extern HardwareSerial Serial;
extern EspClass ESP;

inline unsigned long millis() { return 0; }
inline unsigned long micros() { return 0; }
inline void delay(unsigned long) {}
inline void yield() {}
