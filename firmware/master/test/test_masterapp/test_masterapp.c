/*
 * Tests der Anwendungslogik gegen einen simulierten Bus.
 * Deckt "REST-Endpunkte antworten gegen einen simulierten Bus" (Backlog T8) ab:
 * die REST-Schicht in src/ ruft genau diese masterapp-Funktionen auf.
 */
#include <string.h>

#include <unity.h>

#include "busmaster.h"
#include "charmap.h"
#include "masterapp.h"
#include "protocol.h"

static uint8_t   g_tx[1024];
static size_t    g_txlen;
static busmaster_t bm;
static masterapp_t app;

static void fake_tx(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    memcpy(&g_tx[g_txlen], d, n);
    g_txlen += n;
}

void setUp(void)
{
    g_txlen = 0;
    busmaster_init(&bm, fake_tx, NULL);
    masterapp_init(&app, &bm, 5);   /* 5 Module */
}
void tearDown(void) {}

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

static bool saw_cmd(uint8_t cmd)
{
    proto_parser_t p;
    proto_parser_reset(&p);
    for (size_t i = 0; i < g_txlen; ++i) {
        if (proto_parser_feed(&p, g_tx[i]) == PARSE_FRAME_OK && p.frame.cmd == cmd) {
            return true;
        }
    }
    return false;
}

/* --- Text ------------------------------------------------------- */

static void test_set_text_pushes_blaetter_and_go(void)
{
    masterapp_set_text(&app, "HALLO", 0);
    masterapp_tick(&app, 0);

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
    const size_t after_first = g_txlen;
    masterapp_tick(&app, 10);
    masterapp_tick(&app, 20);
    TEST_ASSERT_EQUAL_size_t(after_first, g_txlen);   /* nichts Neues gesendet */
}

/* --- Uhr ------------------------------------------------------- */

static void test_clock_hm_renders_time(void)
{
    masterapp_set_mode(&app, APP_MODE_CLOCK_HM, '.', CHARMAP_ALIGN_CENTER, 0);
    masterapp_set_time(&app, 9, 5, 0);
    masterapp_tick(&app, 0);

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
    TEST_ASSERT_EQUAL_size_t(0, g_txlen);
}

/* --- Status-JSON gegen simulierten Bus ---------------------- */

static void test_status_json_reflects_module_state(void)
{
    /* simulierte GET_STATUS-Antwort von Modul 2 einspeisen */
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

    char json[512];
    const size_t len = masterapp_status_json(&app, json, sizeof(json));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"mode\":\"text\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"text\":\"X\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"addr\":2,\"online\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"ist\":15"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"error\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"corr\":2"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_set_text_pushes_blaetter_and_go);
    RUN_TEST(test_no_resend_when_unchanged);
    RUN_TEST(test_clock_hm_renders_time);
    RUN_TEST(test_clock_without_time_is_blank);
    RUN_TEST(test_hms_auto_falls_back_to_hm);
    RUN_TEST(test_off_mode_sends_nothing);
    RUN_TEST(test_status_json_reflects_module_state);
    return UNITY_END();
}
