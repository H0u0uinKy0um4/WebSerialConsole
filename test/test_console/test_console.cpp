// Unit tests for the parts of the console that do not need a network: source
// registration, tagging and the JSON source table.
//
// Run them with:
//   pio test -e esp32c3 --without-uploading
#include <Arduino.h>
#include <unity.h>

#include "WebSerialConsole.h"

void setUp() {}
void tearDown() {}

// The built-in sources are always there: the console needs a debug output
// channel and one for the user's own input.
void test_builtin_sources() {
    TEST_ASSERT_EQUAL(2, WebSerial.sourceCount());
    TEST_ASSERT_TRUE(WebSerial.isValidSource(WebSerialConsole::Source::DEBUG));
    TEST_ASSERT_TRUE(WebSerial.isValidSource(WebSerialConsole::Source::USER));
}

// A sketch registers its own categories; the library must hand out a usable
// handle for them.
void test_add_source() {
    WebSerialConsole::Source s = WebSerial.addSource("ha", 'H', "HA", "#c084fc", "HA requests");
    TEST_ASSERT_NOT_EQUAL(WebSerialConsole::Source::INVALID, s);
    TEST_ASSERT_EQUAL(3, WebSerial.sourceCount());
    TEST_ASSERT_TRUE(WebSerial.isValidSource(s));

    // A duplicate id or a duplicate wire tag must be rejected, so the tag
    // stays a unique key on the wire.
    TEST_ASSERT_EQUAL(WebSerialConsole::Source::INVALID,
                      WebSerial.addSource("ha", 'X', "OTHER"));
    TEST_ASSERT_EQUAL(WebSerialConsole::Source::INVALID,
                      WebSerial.addSource("other", 'H', "OTHER"));
}

// Even the built-in sources can be restyled by the sketch.
void test_configure_source() {
    TEST_ASSERT_TRUE(WebSerial.configureSource(WebSerialConsole::Source::USER, "CMD", "#38bdf8"));

    // An invalid colour is rejected and leaves the source untouched.
    TEST_ASSERT_FALSE(WebSerial.configureSource(WebSerialConsole::Source::USER, "CMD", "not-a-color"));

    // An invalid handle is rejected too.
    TEST_ASSERT_FALSE(WebSerial.configureSource(WebSerialConsole::Source::INVALID, "X"));
}

// The browser reads the source definitions from /sources and from the table
// pushed over the WebSocket, so the JSON must be well formed.
void test_sources_json() {
    String json = WebSerial.sourcesJson();
    TEST_ASSERT_TRUE(json.startsWith("["));
    TEST_ASSERT_TRUE(json.endsWith("]"));
    TEST_ASSERT_TRUE(json.indexOf("\"id\":\"debug\"") > 0);
    TEST_ASSERT_TRUE(json.indexOf("\"tag\":\"H\"") > 0);
    TEST_ASSERT_TRUE(json.indexOf("\"prompt\":true") > 0);
}

// Scoping a source must not leak: the previous source is restored afterwards.
void test_scope_restores_source() {
    WebSerialConsole::Source s = WebSerial.addSource("scope", 'P', "SCP");
    WebSerialConsole::Source before = WebSerial.source();
    {
        WebSerialConsole::Scope scope(WebSerial, s);
        TEST_ASSERT_EQUAL(s, WebSerial.source());
    }
    TEST_ASSERT_EQUAL(before, WebSerial.source());
}

// Pausing drops the output, resuming brings it back.
void test_pause_resume() {
    TEST_ASSERT_FALSE(WebSerial.paused());
    {
        WebSerialConsole::Pause pause(WebSerial);
        TEST_ASSERT_TRUE(WebSerial.paused());
    }
    TEST_ASSERT_FALSE(WebSerial.paused());
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_builtin_sources);
    RUN_TEST(test_add_source);
    RUN_TEST(test_configure_source);
    RUN_TEST(test_sources_json);
    RUN_TEST(test_scope_restores_source);
    RUN_TEST(test_pause_resume);
    UNITY_END();
}

void loop() {}
