/*
 * Tests der Anwendungslogik gegen einen simulierten Bus.
 * Deckt "REST-Endpunkte antworten gegen einen simulierten Bus" (Backlog T8) ab:
 * die REST-Schicht in src/ ruft genau diese masterapp-Funktionen auf.
 *
 * Seit #21 reiht masterapp die Anzeige nur ein; gesendet wird in
 * busmaster_tick (flush()).
 */
#include <ctype.h>
#include <string.h>

#include <unity.h>

#include "busmaster.h"
#include "charmap.h"
#include "masterapp.h"
#include "protocol.h"

static uint8_t   g_tx[8192];
static size_t    g_txlen;
static busmaster_t bm;
static masterapp_t app;

static void fake_tx(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    if (g_txlen + n <= sizeof(g_tx)) {
        memcpy(&g_tx[g_txlen], d, n);
        g_txlen += n;
    }
}

void setUp(void)
{
    g_txlen = 0;
    busmaster_init(&bm, fake_tx, NULL);
    masterapp_init(&app, &bm, 5);   /* 5 Module */
}
void tearDown(void) {}

static void flush(uint32_t t)
{
    busmaster_tick(&bm, t);
}

static bool last_set_all(uint8_t *out, uint8_t *len)
{
    proto_parser_t p;
    proto_parser_reset(&p);
    bool found = false;
    for (size_t i = 0; i < g_txlen; ++i) {
        if (proto_parser_feed(&p, g_tx[i]) == PARSE_FRAME_OK &&
            p.frame.cmd == CMD_SET_ALL) {
            *len = p.frame.payload_len;
            memcpy(out, p.frame.payload, p.frame.payload_len);
            found = true;
        }
    }
    return found;
}

static size_t count_cmd(uint8_t cmd)
{
    proto_parser_t p;
    proto_parser_reset(&p);
    size_t c = 0;
    for (size_t i = 0; i < g_txlen; ++i) {
        if (proto_parser_feed(&p, g_tx[i]) == PARSE_FRAME_OK && p.frame.cmd == cmd) {
            c++;
        }
    }
    return c;
}

static bool saw_cmd(uint8_t cmd)
{
    return count_cmd(cmd) > 0;
}

/* --- Minimaler JSON-Syntaxpruefer (RFC 8259) ---------------------------- */

static const char *js_value(const char *p);

static const char *js_ws(const char *p)
{
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        ++p;
    }
    return p;
}

static const char *js_string(const char *p)
{
    if (p == NULL || *p != '"') {
        return NULL;
    }
    ++p;
    while (*p != '\0' && *p != '"') {
        const unsigned char c = (unsigned char)*p;
        if (c < 0x20u) {
            return NULL;
        }
        if (c == '\\') {
            ++p;
            if (*p == 'u') {
                for (int i = 1; i <= 4; ++i) {
                    if (!isxdigit((unsigned char)p[i])) {
                        return NULL;
                    }
                }
                p += 5;
                continue;
            }
            if (*p == '\0' || strchr("\"\\/bfnrt", *p) == NULL) {
                return NULL;
            }
        }
        ++p;
    }
    return (*p == '"') ? p + 1 : NULL;
}

static const char *js_number(const char *p)
{
    const char *s = p;
    if (*p == '-') {
        ++p;
    }
    while (isdigit((unsigned char)*p)) {
        ++p;
    }
    return (p > s) ? p : NULL;
}

static const char *js_container(const char *p, char close, bool object)
{
    p = js_ws(p + 1);
    if (*p == close) {
        return p + 1;
    }
    for (;;) {
        if (object) {
            p = js_string(js_ws(p));
            if (p == NULL) {
                return NULL;
            }
            p = js_ws(p);
            if (*p != ':') {
                return NULL;
            }
            ++p;
        }
        p = js_value(p);
        if (p == NULL) {
            return NULL;
        }
        p = js_ws(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        return (*p == close) ? p + 1 : NULL;
    }
}

static const char *js_value(const char *p)
{
    p = js_ws(p);
    if (*p == '{') {
        return js_container(p, '}', true);
    }
    if (*p == '[') {
        return js_container(p, ']', false);
    }
    if (*p == '"') {
        return js_string(p);
    }
    if (strncmp(p, "true", 4) == 0) {
        return p + 4;
    }
    if (strncmp(p, "false", 5) == 0) {
        return p + 5;
    }
    if (strncmp(p, "null", 4) == 0) {
        return p + 4;
    }
    return js_number(p);
}

static bool json_valid(const char *s)
{
    const char *p = js_value(s);
    return p != NULL && *js_ws(p) == '\0';
}

/* --- Text ------------------------------------------------------- */

static void test_set_text_pushes_blaetter_and_go(void)
{
    masterapp_set_text(&app, "HALLO", 0);
    masterapp_tick(&app, 0);
    flush(0);

    uint8_t pl[32], len;
    TEST_ASSERT_TRUE(last_set_all(pl, &len));
    TEST_ASSERT_EQUAL_UINT8(5, len);
    const uint8_t exp[5] = { charmap_blatt('H'), charmap_blatt('A'),
                             charmap_blatt('L'), charmap_blatt('L'),
                             charmap_blatt('O') };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, pl, 5);
    TEST_ASSERT_TRUE(saw_cmd(CMD_GO));
}

static void test_no_resend_when_unchanged(void)
{
    masterapp_set_text(&app, "AB", 0);
    masterapp_tick(&app, 0);
    flush(0);
    const size_t after_first = g_txlen;
    masterapp_tick(&app, 10);
    flush(10);
    masterapp_tick(&app, 20);
    flush(20);
    TEST_ASSERT_EQUAL_size_t(after_first, g_txlen);   /* nichts Neues gesendet */
}

static void test_same_text_again_resends(void)
{
    /* Nach "Homing alle" muss derselbe Text erneut wirken (#26). */
    masterapp_set_text(&app, "AB", 0);
    masterapp_tick(&app, 0);
    flush(0);
    masterapp_set_text(&app, "AB", 10);
    masterapp_tick(&app, 10);
    flush(10);
    TEST_ASSERT_EQUAL_size_t(2, count_cmd(CMD_SET_ALL));
}

static void test_set_text_trims_at_utf8_boundary(void)
{
    char in[64];
    memset(in, 'A', 47);
    in[47] = (char)0xC3;   /* "Ä" = C3 84 ueber die Grenze */
    in[48] = (char)0x84;
    in[49] = '\0';
    masterapp_set_text(&app, in, 0);
    TEST_ASSERT_EQUAL_size_t(47, strlen(app.text));

    memset(in, 'B', 46);
    in[46] = (char)0xC3;   /* passt genau */
    in[47] = (char)0x84;
    in[48] = 'C';
    in[49] = '\0';
    masterapp_set_text(&app, in, 0);
    TEST_ASSERT_EQUAL_size_t(48, strlen(app.text));
}

/* --- Uhr ------------------------------------------------------- */

static void test_clock_hm_renders_time(void)
{
    masterapp_set_mode(&app, APP_MODE_CLOCK_HM, '.', CHARMAP_ALIGN_CENTER, 0);
    masterapp_set_time(&app, 9, 5, 0);
    masterapp_tick(&app, 0);
    flush(0);

    uint8_t pl[32], len;
    TEST_ASSERT_TRUE(last_set_all(pl, &len));
    /* "09.05" in 5 Modulen, zentriert -> genau passend */
    const char *s = "09.05";
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_UINT8(charmap_blatt(s[i]), pl[i]);
    }
}

static void test_clock_without_time_is_blank(void)
{
    masterapp_set_mode(&app, APP_MODE_CLOCK_HM, '.', CHARMAP_ALIGN_CENTER, 0);
    masterapp_tick(&app, 0);
    flush(0);
    uint8_t pl[32], len;
    TEST_ASSERT_TRUE(last_set_all(pl, &len));
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_UINT8(CHARMAP_LEERBILD, pl[i]);
    }
}

static void test_hms_auto_falls_back_to_hm(void)
{
    app.hms_timeout_ms = 1000;
    masterapp_set_mode(&app, APP_MODE_CLOCK_HMS, '.', CHARMAP_ALIGN_CENTER, 0);
    masterapp_set_time(&app, 12, 0, 0);
    masterapp_tick(&app, 0);
    TEST_ASSERT_EQUAL(APP_MODE_CLOCK_HMS, app.mode);

    masterapp_tick(&app, 1000);
    TEST_ASSERT_EQUAL(APP_MODE_CLOCK_HM, app.mode);
}

/* --- Off ----------------------------------------------------- */

static void test_off_mode_sends_nothing(void)
{
    masterapp_set_mode(&app, APP_MODE_OFF, '.', CHARMAP_ALIGN_LEFT, 0);
    masterapp_tick(&app, 0);
    flush(0);
    TEST_ASSERT_EQUAL_size_t(0, g_txlen);
}

static void test_off_clears_targets_and_blank_sends_again(void)
{
    masterapp_set_text(&app, "HALLO", 0);
    masterapp_tick(&app, 0);
    flush(0);
    TEST_ASSERT_TRUE(bm.mod[0].soll_valid);

    masterapp_set_mode(&app, APP_MODE_OFF, '.', CHARMAP_ALIGN_LEFT, 10);
    masterapp_tick(&app, 10);
    flush(10);
    TEST_ASSERT_FALSE(bm.mod[0].soll_valid);       /* kein Soll/Ist-Abgleich */
    TEST_ASSERT_EQUAL_size_t(1, count_cmd(CMD_SET_ALL));

    /* frueher: OFF -> BLANK sendete nichts, der Text blieb stehen (#26) */
    masterapp_set_mode(&app, APP_MODE_BLANK, '.', CHARMAP_ALIGN_LEFT, 20);
    masterapp_tick(&app, 20);
    flush(20);
    uint8_t pl[32], len;
    TEST_ASSERT_TRUE(last_set_all(pl, &len));
    TEST_ASSERT_EQUAL_size_t(2, count_cmd(CMD_SET_ALL));
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_UINT8(CHARMAP_LEERBILD, pl[i]);
    }
}

static void test_zero_field_width_sends_nothing(void)
{
    app.module_count = 0;
    masterapp_set_text(&app, "HALLO", 0);
    masterapp_tick(&app, 0);
    flush(0);
    TEST_ASSERT_EQUAL_size_t(0, g_txlen);
}

static void test_field_width_over_32_is_clamped(void)
{
    /* main.cpp setzt module_count direkt; > 32 darf want[] nicht sprengen */
    app.module_count = 40;
    masterapp_set_text(&app, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 0);
    masterapp_tick(&app, 0);
    flush(0);
    uint8_t pl[32], len;
    TEST_ASSERT_TRUE(last_set_all(pl, &len));
    TEST_ASSERT_EQUAL_UINT8(BUSMASTER_MAX_MODULES, len);
    char json[MASTERAPP_STATUS_JSON_MAX(BUSMASTER_MAX_MODULES)];
    TEST_ASSERT_TRUE(masterapp_status_json(&app, json, sizeof(json)) > 0);
    TEST_ASSERT_NULL(strstr(json, "\"addr\":33"));
}

/* --- Enumeration ---------------------------------------------- */

static void test_show_held_back_during_enumeration(void)
{
    busmaster_start_enumeration(&bm, 0);
    masterapp_set_text(&app, "HALLO", 0);
    for (uint32_t t = 0; t < 200 && busmaster_enum_busy(&bm); ++t) {
        masterapp_tick(&app, t);
        busmaster_tick(&bm, t);
        TEST_ASSERT_FALSE(saw_cmd(CMD_SET_ALL));   /* nicht in die Enumeration */
    }
    TEST_ASSERT_FALSE(busmaster_enum_busy(&bm));
    masterapp_tick(&app, 300);
    busmaster_tick(&bm, 300);
    TEST_ASSERT_TRUE(saw_cmd(CMD_SET_ALL));
}

/* --- Status-JSON gegen simulierten Bus ---------------------- */

static void test_status_json_reflects_module_state(void)
{
    busmaster_poll_status(&bm, 2, 0);
    proto_frame_t f;
    f.cmd = CMD_GET_STATUS;
    f.addr = 2;
    f.payload_len = 8;
    const uint8_t st[8] = { 15, 20, 3, 0x01, 40, 2, 0, 1 };
    memcpy(f.payload, st, 8);
    uint8_t buf[PROTO_MAX_FRAME];
    const size_t n = proto_encode(&f, buf, sizeof(buf));
    for (size_t i = 0; i < n; ++i) {
        busmaster_on_rx_byte(&bm, buf[i], 1);
    }

    masterapp_set_text(&app, "X", 0);

    char json[1536];
    const size_t len = masterapp_status_json(&app, json, sizeof(json));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(json_valid(json));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"mode\":\"text\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"text\":\"X\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"addr\":2,\"online\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"ist\":15"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"error\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"corr\":2"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"blatt\":40"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"fw\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"align\":"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"enum_busy\":false"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"warn\":0"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"soll\":0"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"coll\":0,\"uidw\":0"));
}

static void test_status_json_escapes_text_and_sep(void)
{
    masterapp_set_text(&app, "Sag \"Hi\" \\o/\n\x01\t\xC3\x84", 0);
    app.sep = '"';   /* wie apply_config_doc ohne Filter */
    char json[2048];
    const size_t len = masterapp_status_json(&app, json, sizeof(json));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(json), len);
    TEST_ASSERT_TRUE_MESSAGE(json_valid(json), json);
    TEST_ASSERT_NOT_NULL(strstr(json,
        "\"text\":\"Sag \\\"Hi\\\" \\\\o/\\n\\u0001\\t\xC3\x84\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sep\":\"\\\"\""));

    app.sep = '\x02';
    TEST_ASSERT_TRUE(masterapp_status_json(&app, json, sizeof(json)) > 0);
    TEST_ASSERT_TRUE(json_valid(json));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"sep\":\"\\u0002\""));
}

static void test_status_json_overflow_is_terminated(void)
{
    masterapp_set_text(&app, "HALLO", 0);
    const size_t need = masterapp_status_json_len(&app);
    char json[2048];
    TEST_ASSERT_TRUE(need + 1u < sizeof(json));
    memset(json, '#', sizeof(json));

    TEST_ASSERT_EQUAL_size_t(0, masterapp_status_json(&app, json, need));  /* 1 zu klein */
    TEST_ASSERT_EQUAL_CHAR('\0', json[need - 1u]);
    TEST_ASSERT_EQUAL_CHAR('#', json[need]);          /* nichts dahinter beschrieben */

    memset(json, '#', sizeof(json));
    TEST_ASSERT_EQUAL_size_t(need, masterapp_status_json(&app, json, need + 1u));
    TEST_ASSERT_EQUAL_CHAR('\0', json[need]);
    TEST_ASSERT_TRUE(json_valid(json));

    TEST_ASSERT_EQUAL_size_t(0, masterapp_status_json(&app, json, 0));
}

static void test_status_json_max_covers_worst_case(void)
{
    /* 32 Module mit Hoechstwerten, Text nur aus Steuerzeichen (je \u00XX) */
    app.module_count = BUSMASTER_MAX_MODULES;
    bm.module_count = 255;
    bm.warn = 0xFF;
    for (uint8_t i = 0; i < BUSMASTER_MAX_MODULES; ++i) {
        bm_module_t *m = &bm.mod[i];
        m->online = false;
        m->ist_blatt = m->ziel_blatt = m->soll = 255;
        m->soll_valid = true;
        m->zustand = m->fehler = m->blattzahl = m->fw_version = m->miss_count = 255;
        m->korrektur = 0xFFFF;
        m->collisions = 0xFFFF;
        m->uid_dup = m->uid_changed = true;
    }
    char text[APP_TEXT_MAX + 1];
    memset(text, 0x01, APP_TEXT_MAX);
    text[APP_TEXT_MAX] = '\0';
    masterapp_set_text(&app, text, 0);
    masterapp_set_mode(&app, APP_MODE_CLOCK_HMS, '.', CHARMAP_ALIGN_RIGHT, 0);
    app.sep = '\x1F';
    app.align = (charmap_align_t)255;

    const size_t need = masterapp_status_json_len(&app);
    TEST_ASSERT_TRUE(need + 1u <= masterapp_status_json_max(BUSMASTER_MAX_MODULES));
    TEST_ASSERT_EQUAL_size_t(MASTERAPP_STATUS_JSON_MAX(BUSMASTER_MAX_MODULES),
                             masterapp_status_json_max(200));   /* geklemmt */

    static char json[MASTERAPP_STATUS_JSON_MAX(BUSMASTER_MAX_MODULES)];
    TEST_ASSERT_EQUAL_size_t(need, masterapp_status_json(&app, json, sizeof(json)));
    TEST_ASSERT_TRUE(json_valid(json));

    /* Kopf und Moduleintrag einzeln gegen ihre Grenzen */
    app.module_count = 1;
    const size_t one = masterapp_status_json_len(&app);
    app.module_count = 2;
    const size_t two = masterapp_status_json_len(&app);
    TEST_ASSERT_TRUE(two - one <= MASTERAPP_JSON_MODULE_MAX);
    TEST_ASSERT_TRUE(one - (two - one) <= MASTERAPP_JSON_HEAD_MAX);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_set_text_pushes_blaetter_and_go);
    RUN_TEST(test_no_resend_when_unchanged);
    RUN_TEST(test_same_text_again_resends);
    RUN_TEST(test_set_text_trims_at_utf8_boundary);
    RUN_TEST(test_clock_hm_renders_time);
    RUN_TEST(test_clock_without_time_is_blank);
    RUN_TEST(test_hms_auto_falls_back_to_hm);
    RUN_TEST(test_off_mode_sends_nothing);
    RUN_TEST(test_off_clears_targets_and_blank_sends_again);
    RUN_TEST(test_zero_field_width_sends_nothing);
    RUN_TEST(test_field_width_over_32_is_clamped);
    RUN_TEST(test_show_held_back_during_enumeration);
    RUN_TEST(test_status_json_reflects_module_state);
    RUN_TEST(test_status_json_escapes_text_and_sep);
    RUN_TEST(test_status_json_overflow_is_terminated);
    RUN_TEST(test_status_json_max_covers_worst_case);
    return UNITY_END();
}
