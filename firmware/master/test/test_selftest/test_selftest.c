/* Tests fuer den Selbsttest-Ablauf, siehe selftest.c / Spezifikation 7.3. */
#include <string.h>

#include <unity.h>

#include "busmaster.h"
#include "selftest.h"

static busmaster_t bm;
static selftest_t  st;
static uint8_t     g_last_cmd, g_last_addr;
static unsigned    g_frames;

static void fake_tx(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    proto_parser_t p;
    proto_parser_reset(&p);
    for (size_t i = 0; i < n; ++i) {
        if (proto_parser_feed(&p, d[i]) == PARSE_FRAME_OK) {
            g_last_cmd = p.frame.cmd;
            g_last_addr = p.frame.addr;
            g_frames++;
        }
    }
}

static void inject(uint8_t cmd, uint8_t addr, const uint8_t *pl, uint8_t len, uint32_t now)
{
    proto_frame_t f;
    f.cmd = cmd;
    f.addr = addr;
    f.payload_len = len;
    memcpy(f.payload, pl, len);
    uint8_t buf[PROTO_MAX_FRAME];
    const size_t n = proto_encode(&f, buf, sizeof(buf));
    for (size_t i = 0; i < n; ++i) {
        busmaster_on_rx_byte(&bm, buf[i], now);
    }
}

static void answer_test(uint8_t addr, uint8_t state, uint8_t blatt, uint16_t rot,
                        uint8_t mn, uint8_t mx, uint8_t err, uint32_t now)
{
    const uint8_t pl[7] = { state, blatt, (uint8_t)(rot & 0xFFu), (uint8_t)(rot >> 8),
                            mn, mx, err };
    inject(CMD_GET_TEST, addr, pl, 7, now);
}

void setUp(void)
{
    g_frames = 0;
    g_last_cmd = 0;
    busmaster_init(&bm, fake_tx, NULL);
    selftest_init(&st);
}
void tearDown(void) {}

/* Ein Schritt: Selbsttest, dann Bus (sendet ggf. GET_TEST). */
static void step(uint32_t now)
{
    selftest_tick(&st, &bm, now);
    busmaster_tick(&bm, now);
}

static void test_start_broadcasts_selftest(void)
{
    TEST_ASSERT_TRUE(selftest_start(&st, &bm, 2, 1000));
    TEST_ASSERT_FALSE(selftest_start(&st, &bm, 2, 1001));   /* laeuft schon */
    busmaster_tick(&bm, 1000);
    TEST_ASSERT_EQUAL_HEX8(CMD_SELFTEST, g_last_cmd);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ADDR_BROADCAST, g_last_addr);
    step(1000 + SELFTEST_SETTLE_MS - 1u);
    TEST_ASSERT_EQUAL_UINT(1, g_frames);                     /* noch keine Abfrage */
}

static void test_collects_and_evaluates(void)
{
    selftest_start(&st, &bm, 2, 0);
    busmaster_tick(&bm, 0);
    uint32_t t = SELFTEST_SETTLE_MS;
    step(t);
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_TEST, g_last_cmd);
    TEST_ASSERT_EQUAL_HEX8(1, g_last_addr);
    answer_test(1, 2, 40, 2400, 58, 63, 0, t + 1);
    t += 10;
    step(t);
    TEST_ASSERT_EQUAL_HEX8(2, g_last_addr);
    answer_test(2, 2, 40, 3200, 60, 90, 0, t + 1);           /* 33 % zu langsam */
    t += 10;
    TEST_ASSERT_TRUE(selftest_tick(&st, &bm, t));
    TEST_ASSERT_FALSE(st.running);
    TEST_ASSERT_TRUE(st.done);
    TEST_ASSERT_EQUAL_UINT8(ST_RES_OK, st.result[0]);
    TEST_ASSERT_EQUAL_UINT8(ST_RES_TIMING, st.result[1]);
    TEST_ASSERT_FALSE(selftest_tick(&st, &bm, t + 1));      /* nur einmal true */
}

/* Ein Modul meldet "laeuft": weiter abfragen, bis es fertig ist. */
static void test_running_module_is_polled_again(void)
{
    selftest_start(&st, &bm, 1, 0);
    busmaster_tick(&bm, 0);
    uint32_t t = SELFTEST_SETTLE_MS;
    step(t);
    answer_test(1, 1, 0, 0, 0, 0, 0, t + 1);
    t += 100;
    step(t);
    const unsigned before = g_frames;
    t += SELFTEST_POLL_MS;
    step(t);
    TEST_ASSERT_EQUAL_UINT(before + 1u, g_frames);
    answer_test(1, 2, 40, 2350, 59, 61, 0, t + 1);
    TEST_ASSERT_TRUE(selftest_tick(&st, &bm, t + 2));
    TEST_ASSERT_EQUAL_UINT8(ST_RES_OK, st.result[0]);
}

/* Karte ohne GET_TEST (vor 1.19) oder offline: nach SELFTEST_MAX_POLLS
 * Abfragen "keine Antwort", ohne auf den Gesamttimeout zu warten. */
static void test_silent_module_gives_no_answer(void)
{
    selftest_start(&st, &bm, 1, 0);
    busmaster_tick(&bm, 0);
    uint32_t t = SELFTEST_SETTLE_MS;
    bool fin = false;
    for (int k = 0; k < 400 && !fin; ++k) {
        fin = selftest_tick(&st, &bm, t);
        busmaster_tick(&bm, t);
        t += 10;
    }
    TEST_ASSERT_TRUE(fin);
    TEST_ASSERT_TRUE(t < SELFTEST_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT8(ST_RES_NO_ANSWER, st.result[0]);
}

/* Modul bleibt "laeuft": Abschluss nach dem Gesamttimeout, unvollstaendig. */
static void test_timeout_marks_incomplete(void)
{
    selftest_start(&st, &bm, 1, 0);
    busmaster_tick(&bm, 0);
    step(SELFTEST_SETTLE_MS);
    answer_test(1, 1, 0, 0, 0, 0, 0, SELFTEST_SETTLE_MS + 1u);
    TEST_ASSERT_FALSE(selftest_tick(&st, &bm, SELFTEST_TIMEOUT_MS - 1u));
    busmaster_tick(&bm, SELFTEST_TIMEOUT_MS - 1u);
    TEST_ASSERT_TRUE(selftest_tick(&st, &bm, SELFTEST_TIMEOUT_MS + 10u));
    TEST_ASSERT_EQUAL_UINT8(ST_RES_INCOMPLETE, st.result[0]);
}

static void test_evaluate_rules(void)
{
    bm_module_t m;
    memset(&m, 0, sizeof(m));
    TEST_ASSERT_EQUAL(ST_RES_NO_ANSWER, selftest_evaluate(&m));
    m.test_known = true;
    m.test_state = 2;
    TEST_ASSERT_EQUAL(ST_RES_INCOMPLETE, selftest_evaluate(&m));   /* Umlauf 0 */
    m.test_rot_ms = 2400;
    m.test_blattzahl = 39;
    TEST_ASSERT_EQUAL(ST_RES_BLATTZAHL, selftest_evaluate(&m));
    m.test_blattzahl = 64;
    m.test_error = 0x03;
    TEST_ASSERT_EQUAL(ST_RES_BLATTZAHL, selftest_evaluate(&m));    /* != Konfiguration */
    m.test_error = 0;
    m.test_blattzahl = 40;
    m.test_max_ms = 60;
    TEST_ASSERT_EQUAL(ST_RES_OK, selftest_evaluate(&m));
    m.test_rot_ms = 2400 - 480;                                     /* genau -20 % */
    TEST_ASSERT_EQUAL(ST_RES_OK, selftest_evaluate(&m));
    m.test_rot_ms = 2400 - 481;
    TEST_ASSERT_EQUAL(ST_RES_TIMING, selftest_evaluate(&m));
    m.test_rot_ms = 2400;
    m.test_max_ms = SELFTEST_STEP_MAX_MS + 1u;                      /* Hakler */
    TEST_ASSERT_EQUAL(ST_RES_TIMING, selftest_evaluate(&m));
    m.test_max_ms = 60;
    m.test_blattzahl = 80;
    m.test_rot_ms = 4800;
    TEST_ASSERT_EQUAL(ST_RES_OK, selftest_evaluate(&m));
}

static void test_json_lists_modules(void)
{
    selftest_start(&st, &bm, 2, 0);
    bm.mod[0].test_known = true;
    bm.mod[0].test_state = 2;
    bm.mod[0].test_blattzahl = 40;
    bm.mod[0].test_rot_ms = 2400;
    bm.mod[0].test_max_ms = 61;
    st.running = false;
    st.done = true;
    st.result[0] = ST_RES_OK;
    st.result[1] = ST_RES_NO_ANSWER;
    char json[512];
    TEST_ASSERT_TRUE(selftest_json(&st, &bm, json, sizeof(json)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"done\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "{\"addr\":1,\"res\":\"ok\",\"blatt\":40,\"rot\":2400"));
    TEST_ASSERT_NOT_NULL(strstr(json, "{\"addr\":2,\"res\":\"keine_antwort\""));
    char small[16];
    TEST_ASSERT_EQUAL_size_t(0, selftest_json(&st, &bm, small, sizeof(small)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_broadcasts_selftest);
    RUN_TEST(test_collects_and_evaluates);
    RUN_TEST(test_running_module_is_polled_again);
    RUN_TEST(test_silent_module_gives_no_answer);
    RUN_TEST(test_timeout_marks_incomplete);
    RUN_TEST(test_evaluate_rules);
    RUN_TEST(test_json_lists_modules);
    return UNITY_END();
}
