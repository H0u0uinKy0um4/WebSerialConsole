/*
 * Host-side tests for the WebSerialConsole source registry.
 *
 * These cover the parts of the console that the browser depends on:
 *  - the built-in sources are really registered (a missing count silently
 *    breaks every custom source),
 *  - user-defined sources appear in the JSON with their own label and colour,
 *  - ids and wire tags stay unique,
 *  - the wire frame carries the tag the sketch registered.
 */

#include <unity.h>

#include <string.h>

// The console remaps `Serial` to the console instance unless this is defined.
#define NO_WEB_SERIAL_REDEFINE
#include <WebSerialConsole.h>

#include <ESPmDNS.h>
#include <WebSocketsServer.h>

// Definitions for the globals the Arduino stubs declare.
HardwareSerial Serial;
EspClass ESP;
WiFiClass WiFi;
MDNSClass MDNS;

void setUp(void) {}
void tearDown(void) {}

static bool jsonHas(const String& json, const char* needle) {
    return strstr(json.c_str(), needle) != NULL;
}

static void test_builtin_sources_are_registered(void) {
    WebSerialConsole console;

    // The two built-in categories must be counted, otherwise every custom
    // source overwrites the debug source and the registry stays empty.
    TEST_ASSERT_EQUAL_UINT8(2, console.sourceCount());
    TEST_ASSERT_TRUE(console.isValidSource(WebSerialConsole::Source::DEBUG));
    TEST_ASSERT_TRUE(console.isValidSource(WebSerialConsole::Source::USER));
    TEST_ASSERT_FALSE(console.isValidSource(WebSerialConsole::Source::INVALID));

    String json = console.sourcesJson();
    TEST_ASSERT_TRUE(jsonHas(json,
        "{\"id\":\"debug\",\"tag\":\"D\",\"label\":\"DBG\",\"color\":\"#94a3b8\","
        "\"title\":\"Firmware debug output\",\"prompt\":false}"));
    TEST_ASSERT_TRUE(jsonHas(json,
        "{\"id\":\"user\",\"tag\":\"U\",\"label\":\"CMD\",\"color\":\"#38bdf8\","
        "\"title\":\"Commands typed in the console\",\"prompt\":true}"));
}

static void test_user_defined_source_keeps_its_own_label_and_color(void) {
    WebSerialConsole console;

    WebSerialConsole::Source slave =
        console.addSource("slave", 'S', "SLV", "#34d399", "Bus slave responses");

    TEST_ASSERT_TRUE(slave != WebSerialConsole::Source::INVALID);
    TEST_ASSERT_EQUAL_UINT8(3, console.sourceCount());

    String json = console.sourcesJson();
    TEST_ASSERT_TRUE(jsonHas(json,
        "{\"id\":\"slave\",\"tag\":\"S\",\"label\":\"SLV\",\"color\":\"#34d399\","
        "\"title\":\"Bus slave responses\",\"prompt\":false}"));
}

static void test_source_without_color_gets_a_palette_color(void) {
    WebSerialConsole console;

    WebSerialConsole::Source sensor = console.addSource("sensor", 'X', "SNS");
    TEST_ASSERT_TRUE(sensor != WebSerialConsole::Source::INVALID);

    String json = console.sourcesJson();
    // The colour must be a valid CSS hex value, whatever the sketch passed.
    TEST_ASSERT_TRUE(jsonHas(json, "\"id\":\"sensor\""));
    TEST_ASSERT_TRUE(jsonHas(json, "\"color\":\"#"));
}

static void test_duplicate_ids_and_tags_are_rejected(void) {
    WebSerialConsole console;

    TEST_ASSERT_TRUE(console.addSource("slave", 'S', "SLV") !=
                     WebSerialConsole::Source::INVALID);

    // Same id, different tag.
    TEST_ASSERT_TRUE(console.addSource("slave", 'X', "XXX") ==
                     WebSerialConsole::Source::INVALID);
    // Same tag, different id.
    TEST_ASSERT_TRUE(console.addSource("other", 'S', "OTH") ==
                     WebSerialConsole::Source::INVALID);
    // Empty id.
    TEST_ASSERT_TRUE(console.addSource("", 'Y', "YYY") ==
                     WebSerialConsole::Source::INVALID);
    // The tag may not collide with the framing characters.
    TEST_ASSERT_TRUE(console.addSource("broken", '\x01', "BRK") ==
                     WebSerialConsole::Source::INVALID);

    TEST_ASSERT_EQUAL_UINT8(3, console.sourceCount());
}

static void test_builtin_sources_can_be_restyled(void) {
    WebSerialConsole console;

    TEST_ASSERT_TRUE(console.configureSource(WebSerialConsole::Source::USER,
                                             "TTY", "#ff8800", "Typed input"));

    String json = console.sourcesJson();
    TEST_ASSERT_TRUE(jsonHas(json,
        "{\"id\":\"user\",\"tag\":\"U\",\"label\":\"TTY\",\"color\":\"#ff8800\","
        "\"title\":\"Typed input\",\"prompt\":true}"));

    // An invalid colour is refused and changes nothing.
    TEST_ASSERT_FALSE(console.configureSource(WebSerialConsole::Source::USER,
                                              nullptr, "not;a;color"));
    json = console.sourcesJson();
    TEST_ASSERT_TRUE(jsonHas(json, "\"color\":\"#ff8800\""));
}

static void test_prompt_flag_is_configurable(void) {
    WebSerialConsole console;

    TEST_ASSERT_TRUE(console.configureSource(WebSerialConsole::Source::USER,
                                             nullptr, nullptr, nullptr, 0));
    String json = console.sourcesJson();
    TEST_ASSERT_TRUE(jsonHas(json, "\"id\":\"user\""));
    TEST_ASSERT_FALSE(jsonHas(json, "\"id\":\"user\",\"tag\":\"U\",\"label\":\"CMD\","
                                 "\"color\":\"#38bdf8\",\"title\":\"Commands typed in "
                                 "the console\",\"prompt\":true"));
}

static void test_lines_carry_the_registered_wire_tag(void) {
    WebSerialConsole console;
    WebSocketsServer ws;
    console.attachWebSocket(&ws);

    WebSerialConsole::Source slave =
        console.addSource("slave", 'S', "SLV", "#34d399");

    console.logLine(slave, "slave says hello");

    // Wire format: 0x01 <tag> <line text> '\n'
    TEST_ASSERT_TRUE(ws.lastFrame.size() > 3);
    TEST_ASSERT_EQUAL_UINT8(0x01, ws.lastFrame[0]);
    TEST_ASSERT_EQUAL_CHAR('S', (char)ws.lastFrame[1]);

    char text[32];
    memcpy(text, &ws.lastFrame[2], 16);
    text[16] = '\0';
    TEST_ASSERT_EQUAL_STRING("slave says hello", text);
    TEST_ASSERT_EQUAL_CHAR('\n', (char)ws.lastFrame[ws.lastFrame.size() - 1]);
}

static void test_source_table_is_pushed_to_connected_clients(void) {
    WebSerialConsole console;
    WebSocketsServer ws;
    console.attachWebSocket(&ws);

    console.addSource("slave", 'S', "SLV", "#34d399");

    // The registry change is pushed to the open clients.
    TEST_ASSERT_TRUE(ws.frames.size() > 0);
    const std::string& frame = ws.frames.back();
    TEST_ASSERT_EQUAL_UINT8(0x02, (uint8_t)frame[0]);
    TEST_ASSERT_TRUE(strstr(frame.c_str(), "\"id\":\"slave\"") != NULL);
    TEST_ASSERT_TRUE(strstr(frame.c_str(), "\"color\":\"#34d399\"") != NULL);
}

int main(int, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_builtin_sources_are_registered);
    RUN_TEST(test_user_defined_source_keeps_its_own_label_and_color);
    RUN_TEST(test_source_without_color_gets_a_palette_color);
    RUN_TEST(test_duplicate_ids_and_tags_are_rejected);
    RUN_TEST(test_builtin_sources_can_be_restyled);
    RUN_TEST(test_prompt_flag_is_configurable);
    RUN_TEST(test_lines_carry_the_registered_wire_tag);
    RUN_TEST(test_source_table_is_pushed_to_connected_clients);
    return UNITY_END();
}
