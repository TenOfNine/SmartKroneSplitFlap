/*
 * Tests fuer die Master-Protokollseite gegen einen simulierten Bus.
 * Siehe busmaster.c / Spezifikation 4.5, 5.
 */
#include <string.h>

#include <unity.h>

#include "busmaster.h"
#include "protocol.h"

static uint8_t  g_tx[1024];
static size_t   g_txlen;

static void fake_tx(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    memcpy(&g_tx[g_txlen], d, n);
    g_txlen += n;
}

static busmaster_t bm;

void setUp(void)
{
    g_txlen = 0;
    busmaster_init(&bm, fake_tx, NULL);
}
void tearDown(void) {}

/* n-ten gesendeten Rahmen aus dem TX-Log holen. */
static bool nth_frame(size_t idx, proto_frame_t *out)
{
    proto_parser_t p;
    proto_parser_reset(&p);
    size_t seen = 0;
    for (size_t i = 0; i < g_txlen; ++i) {
        if (proto_parser_feed(&p, g_tx[i]) == PARSE_FRAME_OK) {
            if (seen == idx) {
                *out = p.frame;
                return true;
            }
            seen++;
        }
    }
    return false;
}

static size_t frame_count(void)
{
    proto_parser_t p;
    proto_parser_reset(&p);
    size_t c = 0;
    for (size_t i = 0; i < g_txlen; ++i) {
        if (proto_parser_feed(&p, g_tx[i]) == PARSE_FRAME_OK) {
            c++;
        }
    }
    return c;
}

static void inject(busmaster_t *b, uint8_t cmd, uint8_t addr,
                   const uint8_t *pl, uint8_t len, uint32_t now)
{
    proto_frame_t f;
    f.cmd = cmd;
    f.addr = addr;
    f.payload_len = len;
    for (uint8_t i = 0; i < len; ++i) {
        f.payload[i] = pl[i];
    }
    uint8_t buf[PROTO_MAX_FRAME];
    const size_t n = proto_encode(&f, buf, sizeof(buf));
    for (size_t i = 0; i < n; ++i) {
        busmaster_on_rx_byte(b, buf[i], now);
    }
}

/* --- Anzeige ------------------------------------------------------ */

static void test_show_emits_set_all_and_go(void)
{
    const uint8_t blaetter[3] = { 13, 3, 40 };
    busmaster_show(&bm, blaetter, 3);

    TEST_ASSERT_EQUAL_size_t(2, frame_count());
    proto_frame_t f;
    TEST_ASSERT_TRUE(nth_frame(0, &f));
    TEST_ASSERT_EQUAL_HEX8(CMD_SET_ALL, f.cmd);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ADDR_BROADCAST, f.addr);
    TEST_ASSERT_EQUAL_UINT8(3, f.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(blaetter, f.payload, 3);
    TEST_ASSERT_TRUE(nth_frame(1, &f));
    TEST_ASSERT_EQUAL_HEX8(CMD_GO, f.cmd);
}

/* --- Statusabfrage --------------------------------------------- */

static void test_poll_status_updates_table(void)
{
    busmaster_poll_status(&bm, 2, 100);
    proto_frame_t f;
    TEST_ASSERT_TRUE(nth_frame(0, &f));
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_STATUS, f.cmd);
    TEST_ASSERT_EQUAL_HEX8(2, f.addr);

    const uint8_t st[8] = { 17, 20, 2, 0, 40, 0x05, 0x00, 1 };
    inject(&bm, CMD_GET_STATUS, 2, st, 8, 101);

    TEST_ASSERT_TRUE(bm.mod[1].online);
    TEST_ASSERT_EQUAL_UINT8(17, bm.mod[1].ist_blatt);
    TEST_ASSERT_EQUAL_UINT8(20, bm.mod[1].ziel_blatt);
    TEST_ASSERT_EQUAL_UINT8(2, bm.mod[1].zustand);
    TEST_ASSERT_EQUAL_UINT8(40, bm.mod[1].blattzahl);
    TEST_ASSERT_EQUAL_UINT16(5, bm.mod[1].korrektur);
    TEST_ASSERT_FALSE(bm.awaiting);
}

/* Eine Abfrage bis zum endgueltigen Timeout durchlaufen lassen. */
static void poll_and_time_out(uint8_t addr, uint32_t start)
{
    busmaster_poll_status(&bm, addr, start);
    uint32_t now = start;
    for (int i = 0; i < 5 && bm.awaiting; ++i) {
        now += BUSMASTER_TIMEOUT_MS;
        busmaster_tick(&bm, now);
    }
}

static void test_status_timeout_retries_then_offline(void)
{
    bm.mod[0].online = true;

    poll_and_time_out(1, 0);
    TEST_ASSERT_EQUAL_size_t(3, frame_count());   /* Original + 2 Wiederholungen */
    TEST_ASSERT_FALSE(bm.awaiting);
    TEST_ASSERT_EQUAL_UINT8(1, bm.mod[0].miss_count);
    TEST_ASSERT_TRUE(bm.mod[0].online);           /* erst ab 3 Fehlversuchen offline */

    poll_and_time_out(1, 100);
    poll_and_time_out(1, 200);
    TEST_ASSERT_EQUAL_UINT8(3, bm.mod[0].miss_count);
    TEST_ASSERT_FALSE(bm.mod[0].online);
}

/* --- Enumeration --------------------------------------------- */

static void test_enumeration_two_modules(void)
{
    busmaster_start_enumeration(&bm, 0);
    TEST_ASSERT_TRUE(bm.chain_active);
    proto_frame_t f;
    TEST_ASSERT_TRUE(nth_frame(0, &f));
    TEST_ASSERT_EQUAL_HEX8(CMD_ENUM_RESET, f.cmd);

    busmaster_tick(&bm, 3);   /* nach der Pause: ENUM_ASSIGN(1) */
    TEST_ASSERT_TRUE(nth_frame(1, &f));
    TEST_ASSERT_EQUAL_HEX8(CMD_ENUM_ASSIGN, f.cmd);
    TEST_ASSERT_EQUAL_UINT8(1, f.payload[0]);

    /* Karte 1 bestaetigt */
    inject(&bm, CMD_ENUM_ASSIGN, 1, NULL, 0, 4);
    TEST_ASSERT_TRUE(nth_frame(2, &f));
    TEST_ASSERT_EQUAL_UINT8(2, f.payload[0]);   /* ENUM_ASSIGN(2) */

    /* Karte 2 bestaetigt */
    inject(&bm, CMD_ENUM_ASSIGN, 2, NULL, 0, 6);
    TEST_ASSERT_TRUE(nth_frame(3, &f));
    TEST_ASSERT_EQUAL_UINT8(3, f.payload[0]);   /* ENUM_ASSIGN(3) */

    /* keine dritte Karte -> Timeout -> ENUM_DONE */
    busmaster_tick(&bm, 6 + BUSMASTER_TIMEOUT_MS + 1);
    TEST_ASSERT_TRUE(nth_frame(4, &f));
    TEST_ASSERT_EQUAL_HEX8(CMD_ENUM_DONE, f.cmd);

    TEST_ASSERT_EQUAL_UINT8(2, bm.module_count);
    TEST_ASSERT_FALSE(bm.chain_active);
    TEST_ASSERT_FALSE(busmaster_enum_busy(&bm));
    TEST_ASSERT_TRUE(bm.mod[0].online);
    TEST_ASSERT_TRUE(bm.mod[1].online);
    TEST_ASSERT_FALSE(bm.mod[2].online);
}

static void test_set_config_payload(void)
{
    busmaster_set_config(&bm, 4, 40, 5, 12, 0x03);
    proto_frame_t f;
    TEST_ASSERT_TRUE(nth_frame(0, &f));
    TEST_ASSERT_EQUAL_HEX8(CMD_SET_CONFIG, f.cmd);
    TEST_ASSERT_EQUAL_HEX8(4, f.addr);
    const uint8_t exp[4] = { 40, 5, 12, 0x03 };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, f.payload, 4);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_show_emits_set_all_and_go);
    RUN_TEST(test_poll_status_updates_table);
    RUN_TEST(test_status_timeout_retries_then_offline);
    RUN_TEST(test_enumeration_two_modules);
    RUN_TEST(test_set_config_payload);
    return UNITY_END();
}
