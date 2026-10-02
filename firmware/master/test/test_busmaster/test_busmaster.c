/*
 * Tests fuer die Master-Protokollseite gegen einen simulierten Bus.
 * Siehe busmaster.c / Spezifikation 4.5, 5.
 *
 * Zwei Ebenen:
 *  - direkte Tests: Antworten werden von Hand eingespeist, Zeit explizit;
 *  - Ko-Simulation: Karten mit Antwortverzug, Sendedauer 12 Bit/Byte
 *    (RS485-Modus des ESP32-C3), Zustellverzug des UART-Treibers und
 *    Kollisionserkennung auf dem Draht (#20, #21, #22, #28).
 */
#include <string.h>

#include <unity.h>

#include "busmaster.h"
#include "masterapp.h"
#include "protocol.h"

/* --- Mitschnitt der Master-Rahmen ----------------------------------- */

typedef struct {
    uint8_t  cmd;
    uint8_t  addr;
    uint8_t  len;
    uint8_t  pl[PROTO_MAX_PAYLOAD];
    uint64_t t_us;   /* Sendebeginn (Ko-Simulation) */
} txf_t;

#define MAX_TXF 16384u
static txf_t  g_txf[MAX_TXF];
static size_t g_ntxf;

static void log_frame(const uint8_t *d, size_t n, uint64_t t_us)
{
    proto_parser_t p;
    proto_parser_reset(&p);
    for (size_t i = 0; i < n; ++i) {
        if (proto_parser_feed(&p, d[i]) == PARSE_FRAME_OK && g_ntxf < MAX_TXF) {
            txf_t *f = &g_txf[g_ntxf++];
            f->cmd = p.frame.cmd;
            f->addr = p.frame.addr;
            f->len = p.frame.payload_len;
            memcpy(f->pl, p.frame.payload, p.frame.payload_len);
            f->t_us = t_us;
        }
    }
}

static size_t count_cmd(uint8_t cmd, int addr)
{
    size_t c = 0;
    for (size_t i = 0; i < g_ntxf; ++i) {
        if (g_txf[i].cmd == cmd && (addr < 0 || g_txf[i].addr == (uint8_t)addr)) {
            c++;
        }
    }
    return c;
}

/* Index des ersten Rahmens cmd ab from, sonst -1. */
static int find_cmd(uint8_t cmd, size_t from)
{
    for (size_t i = from; i < g_ntxf; ++i) {
        if (g_txf[i].cmd == cmd) {
            return (int)i;
        }
    }
    return -1;
}

/* --- Direkte Tests: Sende-Attrappe und Uhr ---------------------------- */

static busmaster_t bm;
static uint32_t    g_clk;           /* Uhr der direkten Tests (ms) */
static uint32_t    g_tx_cost_ms;    /* je Rahmen vorgerueckte Zeit */

static void fake_tx(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    log_frame(d, n, 0);
    g_clk += g_tx_cost_ms;
}

static uint32_t fake_clock(void *ctx)
{
    (void)ctx;
    return g_clk;
}

static void inject(uint8_t cmd, uint8_t addr, const uint8_t *pl, uint8_t len, uint32_t now)
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
        busmaster_on_rx_byte(&bm, buf[i], now);
    }
}

static void inject_status(uint8_t addr, uint8_t ist, uint8_t ziel, uint8_t zustand,
                          uint8_t fehler, uint32_t now)
{
    const uint8_t st[8] = { ist, ziel, zustand, fehler, 40, 0, 0, 1 };
    inject(CMD_GET_STATUS, addr, st, 8, now);
}

/* Jede ms ticken, bis t_end (einschliesslich). */
static void tick_range(uint32_t from, uint32_t t_end)
{
    for (uint32_t t = from; t <= t_end; ++t) {
        busmaster_tick(&bm, t);
    }
}

/* Ko-Simulation (weiter unten) */
static void sim_clear(void);

void setUp(void)
{
    g_ntxf = 0;
    g_clk = 0;
    g_tx_cost_ms = 0;
    sim_clear();
    busmaster_init(&bm, fake_tx, NULL);
}
void tearDown(void) {}

/* --- Anzeige und Warteschlange ---------------------------------------- */

static void test_show_emits_set_all_and_go(void)
{
    const uint8_t blaetter[3] = { 13, 3, 40 };
    busmaster_show(&bm, blaetter, 3);
    TEST_ASSERT_EQUAL_size_t(0, g_ntxf);          /* erst im Tick */
    busmaster_tick(&bm, 0);

    TEST_ASSERT_EQUAL_size_t(2, g_ntxf);
    TEST_ASSERT_EQUAL_HEX8(CMD_SET_ALL, g_txf[0].cmd);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ADDR_BROADCAST, g_txf[0].addr);
    TEST_ASSERT_EQUAL_UINT8(3, g_txf[0].len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(blaetter, g_txf[0].pl, 3);
    TEST_ASSERT_EQUAL_HEX8(CMD_GO, g_txf[1].cmd);
    /* Soll getrennt vom gemeldeten Ziel */
    TEST_ASSERT_TRUE(bm.mod[0].soll_valid);
    TEST_ASSERT_EQUAL_UINT8(13, bm.mod[0].soll);
    TEST_ASSERT_EQUAL_UINT8(0, bm.mod[0].ziel_blatt);
    TEST_ASSERT_FALSE(bm.mod[3].soll_valid);
}

static void test_show_coalesces_unsent_set_all(void)
{
    busmaster_poll_status(&bm, 2, 0);             /* Bus belegt */
    const uint8_t a[2] = { 1, 2 };
    const uint8_t b[2] = { 7, 8 };
    busmaster_show(&bm, a, 2);
    busmaster_show(&bm, b, 2);
    inject_status(2, 1, 1, 0, 0, 1);
    tick_range(2, 4);

    TEST_ASSERT_EQUAL_size_t(1, count_cmd(CMD_SET_ALL, -1));
    TEST_ASSERT_EQUAL_size_t(1, count_cmd(CMD_GO, -1));
    const int i = find_cmd(CMD_SET_ALL, 0);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, g_txf[i].pl, 2);
}

static void test_no_send_while_awaiting_then_in_order(void)
{
    busmaster_poll_status(&bm, 2, 0);
    TEST_ASSERT_TRUE(bm.awaiting);
    const uint8_t bl[2] = { 5, 6 };
    busmaster_show(&bm, bl, 2);
    busmaster_home(&bm, 3);
    busmaster_identify(&bm, 3, 5);
    TEST_ASSERT_TRUE(busmaster_led_sync(&bm, 0));
    busmaster_tick(&bm, 1);
    TEST_ASSERT_EQUAL_size_t(1, g_ntxf);          /* nur GET_STATUS */

    inject_status(2, 1, 1, 0, 0, 2);
    TEST_ASSERT_FALSE(bm.awaiting);
    busmaster_tick(&bm, 3);                       /* < 2 ms Busruhe */
    TEST_ASSERT_EQUAL_size_t(1, g_ntxf);
    busmaster_tick(&bm, 4);                       /* SET_ALL, GO, HOME(3) -> ACK abwarten */
    TEST_ASSERT_EQUAL_size_t(4, g_ntxf);
    TEST_ASSERT_EQUAL_HEX8(CMD_SET_ALL, g_txf[1].cmd);
    TEST_ASSERT_EQUAL_HEX8(CMD_GO, g_txf[2].cmd);
    TEST_ASSERT_EQUAL_HEX8(CMD_HOME, g_txf[3].cmd);
    TEST_ASSERT_TRUE(bm.awaiting);
    TEST_ASSERT_EQUAL_UINT8(BM_TX_ACK, bm.pending_kind);

    inject(CMD_HOME, 3, NULL, 0, 5);              /* ACK */
    TEST_ASSERT_FALSE(bm.awaiting);
    busmaster_tick(&bm, 7);                       /* IDENTIFY(3) -> ACK abwarten */
    TEST_ASSERT_EQUAL_size_t(5, g_ntxf);
    TEST_ASSERT_EQUAL_HEX8(CMD_IDENTIFY, g_txf[4].cmd);
    inject(CMD_IDENTIFY, 3, NULL, 0, 8);
    busmaster_tick(&bm, 10);                      /* LED_SYNC */
    TEST_ASSERT_EQUAL_size_t(6, g_ntxf);
    TEST_ASSERT_EQUAL_HEX8(CMD_LED_SYNC, g_txf[5].cmd);
    TEST_ASSERT_EQUAL_UINT32(10, bm.led_sync_ms);
}

static void test_led_sync_coalesced(void)
{
    busmaster_poll_status(&bm, 2, 0);
    TEST_ASSERT_TRUE(busmaster_led_sync(&bm, 1));
    TEST_ASSERT_TRUE(busmaster_led_sync(&bm, 2));
    TEST_ASSERT_EQUAL_UINT8(1, bm.queue_count);
    TEST_ASSERT_EQUAL_UINT32(0, bm.led_sync_ms);  /* erst beim Senden */
}

static void test_unicast_ack_retries_with_payload_no_offline(void)
{
    bm.mod[3].online = true;
    busmaster_set_config(&bm, 4, 40, 5, 12, 0x03);
    busmaster_tick(&bm, 0);
    TEST_ASSERT_TRUE(bm.awaiting);
    tick_range(1, 40);                            /* kein ACK */

    TEST_ASSERT_EQUAL_size_t(3, count_cmd(CMD_SET_CONFIG, 4));
    const uint8_t exp[4] = { 40, 5, 12, 0x03 };
    for (size_t i = 0; i < g_ntxf; ++i) {
        TEST_ASSERT_EQUAL_UINT8(4, g_txf[i].len);  /* Wiederholung mit Payload */
        TEST_ASSERT_EQUAL_HEX8_ARRAY(exp, g_txf[i].pl, 4);
    }
    TEST_ASSERT_FALSE(bm.awaiting);
    TEST_ASSERT_EQUAL_UINT8(0, bm.mod[3].miss_count);
    TEST_ASSERT_TRUE(bm.mod[3].online);
}

static void test_identify_ack_completes(void)
{
    busmaster_identify(&bm, 3, 5);
    busmaster_tick(&bm, 0);
    TEST_ASSERT_EQUAL_UINT8(1, g_txf[0].len);
    TEST_ASSERT_EQUAL_UINT8(5, g_txf[0].pl[0]);
    inject(CMD_IDENTIFY, 3, g_txf[0].pl, 1, 1);   /* eigenes Echo: kein ACK */
    TEST_ASSERT_TRUE(bm.awaiting);
    inject(CMD_IDENTIFY, 3, NULL, 0, 1);          /* ACK */
    TEST_ASSERT_FALSE(bm.awaiting);
}

static void test_poll_config_via_queue(void)
{
    busmaster_poll_config(&bm, 3, 0);
    TEST_ASSERT_EQUAL_size_t(0, g_ntxf);
    busmaster_tick(&bm, 0);
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_CONFIG, g_txf[0].cmd);
    TEST_ASSERT_EQUAL_HEX8(3, g_txf[0].addr);

    const uint8_t cfg[4] = { 40, 17, 12, 0x03 };
    inject(CMD_GET_CONFIG, 3, cfg, 4, 1);
    TEST_ASSERT_TRUE(bm.mod[2].cfg_known);
    TEST_ASSERT_EQUAL_UINT8(40, bm.mod[2].cfg_blattzahl);
    TEST_ASSERT_EQUAL_UINT8(17, bm.mod[2].cfg_offset);
    TEST_ASSERT_EQUAL_UINT8(12, bm.mod[2].cfg_vorhalt);
    TEST_ASSERT_EQUAL_HEX8(0x03, bm.mod[2].cfg_flags);
    TEST_ASSERT_FALSE(bm.awaiting);
}

/* --- Statusabfrage, Zeitbasis, Wiederholung -------------------------- */

static void test_poll_status_updates_table(void)
{
    busmaster_poll_status(&bm, 2, 100);
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_STATUS, g_txf[0].cmd);
    TEST_ASSERT_EQUAL_HEX8(2, g_txf[0].addr);

    const uint8_t st[8] = { 17, 20, 2, 0, 40, 0x05, 0x00, 1 };
    inject(CMD_GET_STATUS, 2, st, 8, 101);

    TEST_ASSERT_TRUE(bm.mod[1].online);
    TEST_ASSERT_EQUAL_UINT8(17, bm.mod[1].ist_blatt);
    TEST_ASSERT_EQUAL_UINT8(20, bm.mod[1].ziel_blatt);
    TEST_ASSERT_EQUAL_UINT8(2, bm.mod[1].zustand);
    TEST_ASSERT_EQUAL_UINT8(40, bm.mod[1].blattzahl);
    TEST_ASSERT_EQUAL_UINT16(5, bm.mod[1].korrektur);
    TEST_ASSERT_FALSE(bm.awaiting);
}

static void test_own_echo_does_not_swallow_response(void)
{
    busmaster_poll_status(&bm, 3, 0);
    inject(CMD_GET_STATUS, 3, NULL, 0, 1);        /* eigenes Echo */
    TEST_ASSERT_TRUE(bm.awaiting);
    const uint8_t st[8] = { 5, 9, 1, 0, 40, 0, 0, 1 };
    inject(CMD_GET_STATUS, 3, st, 8, 2);
    TEST_ASSERT_FALSE(bm.awaiting);
    TEST_ASSERT_EQUAL_UINT8(5, bm.mod[2].ist_blatt);
}

/* Eine Abfrage bis zum endgueltigen Timeout durchlaufen lassen. Die alte
 * Fassung tickte im 5-ms-Raster und legte damit den zu kurzen Timeout
 * (>= 5 ms ab Sendebeginn im ms-Raster) fest. */
static void poll_and_time_out(uint8_t addr, uint32_t start)
{
    busmaster_poll_status(&bm, addr, start);
    uint32_t now = start;
    for (int i = 0; i < 100 && bm.awaiting; ++i) {
        now += 1;
        busmaster_tick(&bm, now);
    }
}

static void test_status_timeout_retries_then_offline(void)
{
    bm.mod[0].online = true;

    poll_and_time_out(1, 0);
    TEST_ASSERT_EQUAL_size_t(3, g_ntxf);          /* Original + 2 Wiederholungen */
    TEST_ASSERT_FALSE(bm.awaiting);
    TEST_ASSERT_EQUAL_UINT8(1, bm.mod[0].miss_count);
    TEST_ASSERT_TRUE(bm.mod[0].online);
    TEST_ASSERT_EQUAL_UINT32(1, bm.timeouts);

    poll_and_time_out(1, 100);
    poll_and_time_out(1, 200);
    TEST_ASSERT_EQUAL_UINT8(3, bm.mod[0].miss_count);
    TEST_ASSERT_FALSE(bm.mod[0].online);
}

static void test_timeout_measured_from_tx_end_with_clock(void)
{
    busmaster_set_clock(&bm, fake_clock, NULL);
    g_clk = 100;
    g_tx_cost_ms = 3;                             /* blockierendes Senden */
    busmaster_poll_status(&bm, 2, 90);            /* veralteter Zeitstempel */
    TEST_ASSERT_EQUAL_UINT32(103, bm.sent_ms);    /* Sendeende, nicht 90/100 */

    g_clk = 108;
    busmaster_tick(&bm, 50);                      /* uebergebene Zeit egal */
    TEST_ASSERT_EQUAL_size_t(1, g_ntxf);          /* 5 ms: noch kein Timeout */
    g_clk = 109;
    busmaster_tick(&bm, 50);
    TEST_ASSERT_EQUAL_size_t(2, g_ntxf);          /* > 5 ms: Wiederholung */
    TEST_ASSERT_EQUAL_UINT32(112, bm.sent_ms);
}

static void test_rx_timestamp_from_clock(void)
{
    busmaster_set_clock(&bm, fake_clock, NULL);
    g_clk = 500;
    busmaster_note_activity(&bm, 1);
    TEST_ASSERT_EQUAL_UINT32(500, bm.last_rx_ms);
    TEST_ASSERT_FALSE(busmaster_idle(&bm, 0));
    g_clk = 502;
    TEST_ASSERT_TRUE(busmaster_idle(&bm, 0));
}

static void test_retry_deferred_while_frame_in_progress(void)
{
    busmaster_poll_status(&bm, 2, 0);
    uint8_t buf[PROTO_MAX_FRAME];
    proto_frame_t f = { .cmd = CMD_GET_STATUS, .addr = 2, .payload_len = 8 };
    const uint8_t st[8] = { 4, 4, 0, 0, 40, 0, 0, 1 };
    memcpy(f.payload, st, 8);
    const size_t n = proto_encode(&f, buf, sizeof(buf));

    for (size_t i = 0; i < 6; ++i) {              /* Rahmenanfang bei t=4 */
        busmaster_on_rx_byte(&bm, buf[i], 4);
    }
    busmaster_tick(&bm, 6);                       /* Timeout, aber Rahmen offen */
    busmaster_tick(&bm, 7);
    TEST_ASSERT_EQUAL_size_t(1, g_ntxf);
    TEST_ASSERT_TRUE(bm.defer_active);
    for (size_t i = 6; i < n; ++i) {
        busmaster_on_rx_byte(&bm, buf[i], 8);
    }
    TEST_ASSERT_FALSE(bm.awaiting);
    TEST_ASSERT_EQUAL_UINT8(4, bm.mod[1].ist_blatt);
    tick_range(9, 30);
    TEST_ASSERT_EQUAL_size_t(1, g_ntxf);          /* keine Wiederholung */
}

static void test_retry_waits_for_bus_quiet(void)
{
    busmaster_poll_status(&bm, 2, 0);
    inject_status(5, 1, 1, 0, 0, 6);              /* fremder Rahmen bei t=6 */
    busmaster_tick(&bm, 6);
    busmaster_tick(&bm, 7);
    TEST_ASSERT_EQUAL_size_t(1, g_ntxf);          /* < 2 ms Ruhe */
    busmaster_tick(&bm, 8);
    TEST_ASSERT_EQUAL_size_t(2, g_ntxf);          /* jetzt Wiederholung */
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_STATUS, g_txf[1].cmd);
}

static void test_retry_defer_has_upper_bound(void)
{
    bm.mod[1].online = true;
    busmaster_poll_status(&bm, 2, 0);
    /* Dauerstoerung: jede ms ein Byte, nie ein gueltiger Rahmen */
    for (uint32_t t = 1; t <= 60; ++t) {
        busmaster_on_rx_byte(&bm, 0x00, t);
        busmaster_tick(&bm, t);
    }
    TEST_ASSERT_EQUAL_size_t(1, g_ntxf);          /* nie in die Stoerung gesendet */
    TEST_ASSERT_FALSE(bm.awaiting);
    TEST_ASSERT_EQUAL_UINT8(1, bm.mod[1].miss_count);
}

static void test_stale_partial_frame_releases_bus(void)
{
    const uint8_t part[4] = { PROTO_PREAMBLE_0, PROTO_PREAMBLE_1, 12, CMD_GET_STATUS };
    for (size_t i = 0; i < sizeof(part); ++i) {
        busmaster_on_rx_byte(&bm, part[i], 10);
    }
    busmaster_poll_status(&bm, 1, 13);
    TEST_ASSERT_EQUAL_size_t(0, g_ntxf);          /* Rahmen noch offen */
    busmaster_tick(&bm, 15);                      /* FRAME_STALE abgelaufen */
    busmaster_poll_status(&bm, 1, 15);
    TEST_ASSERT_EQUAL_size_t(1, g_ntxf);
}

static void test_late_reply_after_retry_guard(void)
{
    busmaster_poll_status(&bm, 2, 0);
    busmaster_tick(&bm, 6);                       /* Wiederholung, Sendeende 6 */
    TEST_ASSERT_EQUAL_size_t(2, g_ntxf);
    inject_status(2, 3, 3, 0, 0, 7);              /* spaete Antwort auf Anfrage 1 */
    TEST_ASSERT_FALSE(bm.awaiting);
    TEST_ASSERT_TRUE(bm.hold_active);
    busmaster_led_sync(&bm, 7);
    tick_range(8, 11);                            /* Antwort auf die Wiederholung kann kommen */
    TEST_ASSERT_EQUAL_size_t(2, g_ntxf);
    busmaster_tick(&bm, 12);
    TEST_ASSERT_EQUAL_size_t(3, g_ntxf);
    TEST_ASSERT_EQUAL_HEX8(CMD_LED_SYNC, g_txf[2].cmd);
}

static void test_version_timeout_does_not_count_miss(void)
{
    bm.mod[1].online = true;
    busmaster_poll_version(&bm, 2, 0);
    tick_range(1, 40);
    TEST_ASSERT_FALSE(bm.awaiting);
    TEST_ASSERT_EQUAL_UINT8(0, bm.mod[1].miss_count);
    TEST_ASSERT_TRUE(bm.mod[1].online);
    TEST_ASSERT_EQUAL_UINT8(1, bm.mod[1].ver_fails);
}

static void test_collision_code_counted(void)
{
    const uint32_t seq = bm.warn_seq;
    busmaster_poll_status(&bm, 2, 0);
    inject_status(2, 1, 1, 0, 0x06, 1);
    TEST_ASSERT_EQUAL_UINT16(1, bm.mod[1].collisions);
    TEST_ASSERT_EQUAL_UINT8(0x06, bm.mod[1].fehler);
    TEST_ASSERT_TRUE(busmaster_warnings(&bm) & BM_WARN_COLLISION);
    TEST_ASSERT_TRUE(bm.warn_seq != seq);
    busmaster_ack_warnings(&bm, BM_WARN_COLLISION);
    TEST_ASSERT_FALSE(busmaster_warnings(&bm) & BM_WARN_COLLISION);
    TEST_ASSERT_EQUAL_UINT16(1, bm.mod[1].collisions);
}

/* --- Abfrageplan -------------------------------------------------------- */

static void test_service_poll_round_robin_and_round_flag(void)
{
    uint8_t seq[8];
    size_t ns = 0;
    bool round_seen_at[9] = { false };
    for (int k = 0; k < 3; ++k) {
        bm.mod[k].ver_known = true;               /* keine Versionsabfragen */
    }
    for (uint32_t t = 100; t < 900 && ns < 8; ++t) {
        busmaster_tick(&bm, t);
        if (busmaster_service_poll(&bm, 3, t)) {
            round_seen_at[ns] = true;             /* Rueckgabe nach Antwort von 3 */
        }
        if (bm.awaiting && bm.pending_kind == BM_TX_STATUS) {
            seq[ns++] = bm.pending_addr;
            TEST_ASSERT_EQUAL_UINT32(t, bm.last_status_poll_ms);
            inject_status(bm.pending_addr, 1, 1, 0, 0, t);
        }
    }
    const uint8_t exp[6] = { 1, 2, 3, 1, 2, 3 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, seq, 6);
    TEST_ASSERT_TRUE(round_seen_at[3]);           /* nach Runde 1, vor Abfrage 4 */
    TEST_ASSERT_FALSE(round_seen_at[1]);
    TEST_ASSERT_FALSE(round_seen_at[2]);
    TEST_ASSERT_EQUAL_size_t(0, count_cmd(CMD_GET_VERSION, -1));
}

static void test_service_poll_waits_for_queue(void)
{
    const uint8_t bl[1] = { 3 };
    busmaster_show(&bm, bl, 1);
    TEST_ASSERT_FALSE(busmaster_service_poll(&bm, 1, 200));
    TEST_ASSERT_EQUAL_size_t(0, g_ntxf);          /* Warteschlange zuerst */
    busmaster_tick(&bm, 200);
    busmaster_service_poll(&bm, 1, 200);
    TEST_ASSERT_EQUAL_size_t(3, g_ntxf);
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_STATUS, g_txf[2].cmd);
}

static void test_version_poll_own_cursor_and_backoff(void)
{
    bm.mod[0].online = true;
    bm.mod[1].online = true;                       /* antwortet nie auf GET_VERSION */
    bm.mod[0].ver_known = true;
    unsigned status_per_addr[3] = { 0 };
    for (uint32_t t = 100; t < 20000; ++t) {
        busmaster_tick(&bm, t);
        busmaster_service_poll(&bm, 2, t);
        if (bm.awaiting && bm.pending_kind == BM_TX_STATUS && bm.sent_ms == t) {
            status_per_addr[bm.pending_addr]++;
            inject_status(bm.pending_addr, 1, 1, 0, 0, t);
        }
    }
    TEST_ASSERT_UINT_WITHIN(2, 99, status_per_addr[1]);
    TEST_ASSERT_UINT_WITHIN(2, 99, status_per_addr[2]);
    /* hoechstens 3 gescheiterte Abfragen zu je 3 Versuchen, dann Ruhe */
    TEST_ASSERT_EQUAL_size_t(9, count_cmd(CMD_GET_VERSION, 2));
    TEST_ASSERT_EQUAL_UINT8(3, bm.mod[1].ver_fails);
    TEST_ASSERT_EQUAL_UINT8(0, bm.mod[1].miss_count);
    TEST_ASSERT_TRUE(bm.mod[1].online);
    busmaster_invalidate_version(&bm, 2);
    TEST_ASSERT_EQUAL_UINT8(0, bm.mod[1].ver_fails);
}

/* --- Soll/Ist-Abgleich (#26) ------------------------------------------- */

/* Anzeige senden und Bus wieder frei machen. */
static void show_and_flush(const uint8_t *bl, uint8_t n, uint32_t t)
{
    busmaster_show(&bm, bl, n);
    busmaster_tick(&bm, t);
}

static void test_reconcile_sends_set_and_go(void)
{
    const uint8_t bl[2] = { 5, 7 };
    show_and_flush(bl, 2, 0);
    busmaster_poll_status(&bm, 1, 10);
    inject_status(1, 3, 5, 0, 0, 11);             /* steht, aber nicht am Soll */
    TEST_ASSERT_EQUAL_UINT8(1, bm.mod[0].rc_tries);
    busmaster_tick(&bm, 13);                      /* SET(1,5) -> ACK */
    const int i = find_cmd(CMD_SET, 0);
    TEST_ASSERT_TRUE(i >= 0);
    TEST_ASSERT_EQUAL_HEX8(1, g_txf[i].addr);
    TEST_ASSERT_EQUAL_UINT8(5, g_txf[i].pl[0]);
    TEST_ASSERT_TRUE(bm.awaiting);
    inject(CMD_SET, 1, NULL, 0, 14);
    busmaster_tick(&bm, 16);
    TEST_ASSERT_EQUAL_HEX8(CMD_GO, g_txf[g_ntxf - 1].cmd);
    TEST_ASSERT_EQUAL_size_t(2, count_cmd(CMD_GO, -1));
}

static void test_reconcile_rate_limit_and_max_tries(void)
{
    const uint8_t bl[1] = { 9 };
    show_and_flush(bl, 1, 0);
    uint32_t t = 10;
    for (int k = 0; k < 8; ++k) {                 /* alle 1 s ein Abweichungsstatus */
        busmaster_poll_status(&bm, 1, t);
        inject_status(1, 2, 2, 0, 0, t + 1);
        for (uint32_t u = t + 2; u < t + 60; ++u) {
            busmaster_tick(&bm, u);
            if (bm.awaiting && bm.pending_kind == BM_TX_ACK) {
                inject(CMD_SET, 1, NULL, 0, u);   /* Modul quittiert */
            }
        }
        t += 1000;
    }
    /* t=10, 2010, 4010 -> 3 Versuche; dazwischen gesperrt; danach aus */
    TEST_ASSERT_EQUAL_size_t(3, count_cmd(CMD_SET, 1));
    TEST_ASSERT_EQUAL_UINT8(3, bm.mod[0].rc_tries);

    /* neues Soll -> wieder Versuche */
    const uint8_t bl2[1] = { 11 };
    show_and_flush(bl2, 1, t);
    busmaster_poll_status(&bm, 1, t + 10);
    inject_status(1, 2, 11, 0, 0, t + 11);
    TEST_ASSERT_EQUAL_UINT8(1, bm.mod[0].rc_tries);
}

static void test_reconcile_success_resets_and_ziel_only_without_go(void)
{
    const uint8_t bl[1] = { 9 };
    show_and_flush(bl, 1, 0);
    busmaster_poll_status(&bm, 1, 10);
    inject_status(1, 9, 9, 0, 0, 11);             /* am Soll */
    TEST_ASSERT_EQUAL_UINT8(0, bm.mod[0].rc_tries);
    TEST_ASSERT_EQUAL_UINT8(0, bm.queue_count);

    tick_range(12, 20);
    busmaster_poll_status(&bm, 1, 30);
    inject_status(1, 9, 4, 0, 0, 31);             /* Ist stimmt, Puffer falsch */
    TEST_ASSERT_EQUAL_UINT8(1, bm.queue_count);   /* nur SET, kein GO */
    TEST_ASSERT_EQUAL_HEX8(CMD_SET, bm.queue[0].cmd);
}

static void test_reconcile_not_while_moving_error_or_unknown(void)
{
    const uint8_t bl[1] = { 9 };
    show_and_flush(bl, 1, 0);
    busmaster_poll_status(&bm, 1, 10);
    inject_status(1, 3, 9, 2, 0, 11);             /* faehrt */
    tick_range(12, 20);
    busmaster_poll_status(&bm, 1, 30);
    inject_status(1, 3, 9, 3, 0x02, 31);          /* Fehler */
    tick_range(32, 40);
    busmaster_poll_status(&bm, 1, 50);
    inject_status(1, 0, 9, 0, 0, 51);             /* Lage unbekannt */
    TEST_ASSERT_EQUAL_UINT8(0, bm.queue_count);
    TEST_ASSERT_EQUAL_UINT8(0, bm.mod[0].rc_tries);
}

static void test_reconcile_skipped_while_show_queued_and_after_stop(void)
{
    const uint8_t bl[1] = { 9 };
    show_and_flush(bl, 1, 0);
    busmaster_poll_status(&bm, 1, 10);
    const uint8_t bl2[1] = { 12 };
    busmaster_show(&bm, bl2, 1);                  /* neues Soll, noch nicht gesendet */
    inject_status(1, 9, 9, 0, 0, 11);             /* alter Stand */
    TEST_ASSERT_EQUAL_UINT8(1, bm.queue_count);   /* nur die Anzeige */
    busmaster_tick(&bm, 13);

    busmaster_stop(&bm, 1);
    busmaster_tick(&bm, 20);                      /* STOP(1) */
    inject(CMD_STOP, 1, NULL, 0, 21);
    busmaster_poll_status(&bm, 1, 30);
    inject_status(1, 5, 12, 0, 0, 31);            /* angehalten */
    TEST_ASSERT_EQUAL_UINT8(0, bm.queue_count);

    busmaster_show(&bm, bl2, 1);                  /* naechste Anzeige hebt auf */
    TEST_ASSERT_FALSE(bm.mod[0].rc_stopped);
}

static void test_clear_targets_disables_reconcile(void)
{
    const uint8_t bl[1] = { 9 };
    show_and_flush(bl, 1, 0);
    busmaster_clear_targets(&bm);
    busmaster_poll_status(&bm, 1, 10);
    inject_status(1, 3, 3, 0, 0, 11);
    TEST_ASSERT_EQUAL_UINT8(0, bm.queue_count);
}

/* --- Enumeration (direkt) ------------------------------------------------ */

/* Bis zum naechsten gesendeten Rahmen ticken; liefert seinen Index. */
static int tick_to_next_frame(uint32_t *t, uint32_t limit)
{
    const size_t before = g_ntxf;
    while (*t < limit) {
        busmaster_tick(&bm, *t);
        if (g_ntxf > before) {
            return (int)before;
        }
        (*t)++;
    }
    return -1;
}

static void test_enumeration_two_modules(void)
{
    uint32_t t = 0;
    busmaster_start_enumeration(&bm, t);
    TEST_ASSERT_TRUE(busmaster_enum_busy(&bm));
    TEST_ASSERT_EQUAL_size_t(0, g_ntxf);          /* Start erst im Tick */

    int i = tick_to_next_frame(&t, 100);
    TEST_ASSERT_EQUAL_HEX8(CMD_ENUM_RESET, g_txf[i].cmd);
    TEST_ASSERT_TRUE(bm.chain_active);
    i = tick_to_next_frame(&t, 100);
    TEST_ASSERT_EQUAL_HEX8(CMD_ENUM_RESET, g_txf[i].cmd);  /* zweimal */
    i = tick_to_next_frame(&t, 100);
    TEST_ASSERT_EQUAL_HEX8(CMD_ENUM_ASSIGN, g_txf[i].cmd);
    TEST_ASSERT_EQUAL_UINT8(1, g_txf[i].pl[0]);

    inject(CMD_ENUM_ASSIGN, 1, NULL, 0, t + 1);   /* Karte 1 */
    t += 1;
    i = tick_to_next_frame(&t, 200);
    TEST_ASSERT_EQUAL_UINT8(2, g_txf[i].pl[0]);
    inject(CMD_ENUM_ASSIGN, 2, NULL, 0, t + 1);   /* Karte 2 */
    t += 1;
    i = tick_to_next_frame(&t, 200);
    TEST_ASSERT_EQUAL_UINT8(3, g_txf[i].pl[0]);

    /* keine dritte Karte: PING(3) x3, ENUM_ASSIGN(3) erneut, PING(3) x3, ENUM_DONE */
    const int done = (int)g_ntxf;
    while (busmaster_enum_busy(&bm) && t < 1000) {
        busmaster_tick(&bm, t);
        if (bm.awaiting && bm.pending_kind == BM_TX_UID) {
            const uint8_t uid[10] = { (uint8_t)bm.pending_addr, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
            inject(CMD_GET_UID, bm.pending_addr, uid, 10, t);
        }
        t++;
    }
    TEST_ASSERT_EQUAL_size_t(6, count_cmd(CMD_PING, 3));
    TEST_ASSERT_EQUAL_size_t(2, count_cmd(CMD_ENUM_ASSIGN, -1) - 2);
    const int d = find_cmd(CMD_ENUM_DONE, (size_t)done);
    TEST_ASSERT_TRUE(d > 0);
    TEST_ASSERT_EQUAL_size_t(1, count_cmd(CMD_GET_UID, 1));
    TEST_ASSERT_EQUAL_size_t(1, count_cmd(CMD_GET_UID, 2));
    TEST_ASSERT_EQUAL_size_t(1, count_cmd(CMD_PING, PROTO_ADDR_SERVICE));

    TEST_ASSERT_EQUAL_UINT8(2, bm.module_count);
    TEST_ASSERT_FALSE(bm.chain_active);
    TEST_ASSERT_FALSE(busmaster_enum_busy(&bm));
    TEST_ASSERT_TRUE(bm.mod[0].online);
    TEST_ASSERT_TRUE(bm.mod[1].online);
    TEST_ASSERT_FALSE(bm.mod[2].online);
    TEST_ASSERT_TRUE(bm.mod[0].uid_fresh);
    TEST_ASSERT_EQUAL_UINT8(0, busmaster_warnings(&bm));
}

static void test_enum_waits_for_pending_reply(void)
{
    busmaster_poll_status(&bm, 2, 0);
    busmaster_start_enumeration(&bm, 0);
    tick_range(1, 3);
    TEST_ASSERT_EQUAL_size_t(1, g_ntxf);          /* kein ENUM_RESET in die Antwort */
    inject_status(2, 1, 1, 0, 0, 3);
    busmaster_tick(&bm, 4);
    TEST_ASSERT_EQUAL_size_t(1, g_ntxf);          /* Busruhe abwarten */
    busmaster_tick(&bm, 5);
    TEST_ASSERT_EQUAL_HEX8(CMD_ENUM_RESET, g_txf[1].cmd);
}

static void test_enum_ack_with_wrong_address_ignored(void)
{
    uint32_t t = 0;
    busmaster_start_enumeration(&bm, t);
    tick_to_next_frame(&t, 100);
    tick_to_next_frame(&t, 100);
    tick_to_next_frame(&t, 100);                  /* ENUM_ASSIGN(1) */
    inject(CMD_ENUM_ASSIGN, 77, NULL, 0, t);      /* falsche Adresse */
    TEST_ASSERT_TRUE(bm.awaiting);
    TEST_ASSERT_EQUAL_UINT8(1, bm.enum_next_addr);
    inject(CMD_ENUM_ASSIGN, 1, NULL, 0, t);
    TEST_ASSERT_EQUAL_UINT8(2, bm.enum_next_addr);
}

/* Bis ENUM_ASSIGN(1) gesendet ist; liefert dessen Sendezeit. */
static uint32_t enum_to_first_assign(void)
{
    uint32_t t = 0;
    busmaster_start_enumeration(&bm, t);
    tick_to_next_frame(&t, 100);
    tick_to_next_frame(&t, 100);
    tick_to_next_frame(&t, 100);
    TEST_ASSERT_EQUAL_UINT8(BM_TX_ENUM_ASSIGN, bm.pending_kind);
    return t;
}

static void test_enum_ack_during_deferred_timeout(void)
{
    const uint32_t sent = enum_to_first_assign();
    busmaster_on_rx_byte(&bm, PROTO_PREAMBLE_0, sent + 6);   /* Empfang beginnt */
    busmaster_tick(&bm, sent + 6);
    TEST_ASSERT_TRUE(bm.defer_active);
    proto_parser_reset(&bm.parser);
    inject(CMD_ENUM_ASSIGN, 1, NULL, 0, sent + 7);  /* spaet, aber gueltig */
    TEST_ASSERT_EQUAL_UINT8(2, bm.enum_next_addr);
    TEST_ASSERT_EQUAL_size_t(0, count_cmd(CMD_PING, 1));
}

static void test_enum_late_ack_in_check_phase(void)
{
    const uint32_t sent = enum_to_first_assign();
    /* Stoerung bis zur Obergrenze: Pruefphase beginnt ohne freien Bus */
    uint32_t t = sent + 6;
    for (; bm.enum_phase != BM_ENUM_CHECKING && t < sent + 100; ++t) {
        busmaster_on_rx_byte(&bm, 0x00, t);
        busmaster_tick(&bm, t);
    }
    TEST_ASSERT_EQUAL(BM_ENUM_CHECKING, bm.enum_phase);
    TEST_ASSERT_FALSE(bm.awaiting);
    inject(CMD_ENUM_ASSIGN, 1, NULL, 0, t);          /* ACK kommt doch noch */
    TEST_ASSERT_EQUAL_UINT8(2, bm.enum_next_addr);
    TEST_ASSERT_EQUAL(BM_ENUM_ASSIGNING, bm.enum_phase);
    TEST_ASSERT_EQUAL_size_t(0, count_cmd(CMD_PING, 1));
}

static void test_enum_limit_warns_and_clamps(void)
{
    uint32_t t = 0;
    busmaster_start_enumeration(&bm, t);
    for (int guard = 0; guard < 5000 && busmaster_enum_busy(&bm); ++guard) {
        busmaster_tick(&bm, t);
        if (bm.awaiting && bm.pending_kind == BM_TX_ENUM_ASSIGN) {
            inject(CMD_ENUM_ASSIGN, bm.pending_addr, NULL, 0, t);  /* jede Karte antwortet */
        } else if (bm.awaiting && bm.pending_kind == BM_TX_UID) {
            uint8_t uid[10] = { 0 };
            uid[0] = bm.pending_addr;
            inject(CMD_GET_UID, bm.pending_addr, uid, 10, t);
        }
        t++;
    }
    TEST_ASSERT_FALSE(busmaster_enum_busy(&bm));
    TEST_ASSERT_EQUAL_UINT8(BUSMASTER_MAX_MODULES, bm.module_count);
    TEST_ASSERT_TRUE(busmaster_warnings(&bm) & BM_WARN_ENUM_LIMIT);
    TEST_ASSERT_EQUAL_size_t(BUSMASTER_MAX_MODULES + 1u, count_cmd(CMD_ENUM_ASSIGN, -1));
}

/* Enumeration ohne Karten bis in den Verifikationslauf; liefert die Zeit. */
static uint32_t run_enum_with_acks(uint8_t cards)
{
    uint32_t t = 0;
    busmaster_start_enumeration(&bm, t);
    while (busmaster_enum_busy(&bm) && bm.enum_phase != BM_ENUM_VERIFYING && t < 2000) {
        busmaster_tick(&bm, t);
        if (bm.awaiting && bm.pending_kind == BM_TX_ENUM_ASSIGN && bm.pending_addr <= cards) {
            inject(CMD_ENUM_ASSIGN, bm.pending_addr, NULL, 0, t);
        }
        t++;
    }
    return t;
}

static void test_uid_garbled_marks_duplicate(void)
{
    uint32_t t = run_enum_with_acks(2);
    TEST_ASSERT_EQUAL(BM_ENUM_VERIFYING, bm.enum_phase);
    while (busmaster_enum_busy(&bm) && t < 3000) {
        busmaster_tick(&bm, t);
        if (bm.awaiting && bm.pending_kind == BM_TX_UID && bm.sent_ms == t) {
            const uint8_t uid[10] = { bm.pending_addr, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
            if (bm.pending_addr == 2) {
                /* zwei Karten antworten zugleich: CRC-Fehler */
                proto_frame_t f = { .cmd = CMD_GET_UID, .addr = 2, .payload_len = 10 };
                memcpy(f.payload, uid, 10);
                uint8_t buf[PROTO_MAX_FRAME];
                const size_t n = proto_encode(&f, buf, sizeof(buf));
                buf[n - 1u] ^= 0x33u;
                for (size_t i = 0; i < n; ++i) {
                    busmaster_on_rx_byte(&bm, buf[i], t);
                }
            } else {
                inject(CMD_GET_UID, bm.pending_addr, uid, 10, t);
            }
        }
        t++;
    }
    TEST_ASSERT_FALSE(busmaster_enum_busy(&bm));
    TEST_ASSERT_TRUE(bm.mod[1].uid_dup);
    TEST_ASSERT_FALSE(bm.mod[0].uid_dup);
    TEST_ASSERT_TRUE(busmaster_warnings(&bm) & BM_WARN_UID_DUP);
}

static void test_enum_probe_beyond_chain_end(void)
{
    busmaster_set_enum_hint(&bm, 5);
    uint32_t t = 0;
    busmaster_start_enumeration(&bm, t);
    /* Kette endet nach Karte 2; Karten auf 4 und 5 antworten per Rueckfall */
    while (busmaster_enum_busy(&bm) && bm.enum_phase != BM_ENUM_VERIFYING && t < 3000) {
        busmaster_tick(&bm, t);
        if (bm.awaiting && bm.pending_kind == BM_TX_ENUM_ASSIGN && bm.pending_addr <= 2) {
            inject(CMD_ENUM_ASSIGN, bm.pending_addr, NULL, 0, t);
        }
        if (bm.awaiting && bm.pending_kind == BM_TX_ENUM_PROBE && bm.pending_addr >= 4) {
            const uint8_t v = 1;
            inject(CMD_PING, bm.pending_addr, &v, 1, t);
        }
        t++;
    }
    TEST_ASSERT_EQUAL(BM_ENUM_VERIFYING, bm.enum_phase);
    TEST_ASSERT_EQUAL_UINT8(5, bm.module_count);
    TEST_ASSERT_TRUE(bm.mod[0].online);
    TEST_ASSERT_TRUE(bm.mod[1].online);
    TEST_ASSERT_FALSE(bm.mod[2].online);
    TEST_ASSERT_TRUE(bm.mod[3].online);
    TEST_ASSERT_TRUE(bm.mod[4].online);
    TEST_ASSERT_TRUE(busmaster_warnings(&bm) & BM_WARN_ENUM_GAP);
    /* Nachsondierung erst nach ENUM_DONE; je Adresse 1 Wiederholung */
    const int done = find_cmd(CMD_ENUM_DONE, 0);
    TEST_ASSERT_TRUE(done > 0);
    size_t probe3 = 0;
    for (size_t i = 0; i < g_ntxf; ++i) {
        if (g_txf[i].cmd == CMD_PING && g_txf[i].addr >= 4) {
            TEST_ASSERT_TRUE(i > (size_t)done);
        }
        if (i > (size_t)done && g_txf[i].cmd == CMD_PING && g_txf[i].addr == 3) {
            probe3++;
        }
    }
    TEST_ASSERT_EQUAL_size_t(2, probe3);
}

static void test_start_enumeration_keeps_count_until_done(void)
{
    bm.module_count = 4;
    bm.mod[0].online = true;
    busmaster_start_enumeration(&bm, 0);
    tick_range(0, 2);
    TEST_ASSERT_EQUAL_UINT8(4, bm.module_count);  /* kein Zwischenstand 0 (#38) */
    TEST_ASSERT_TRUE(bm.mod[0].online);
    busmaster_start_enumeration(&bm, 3);          /* laeuft schon: kein Neustart */
    TEST_ASSERT_EQUAL_size_t(1, count_cmd(CMD_ENUM_RESET, -1));
}

/* ====================================================================== */
/* Ko-Simulation                                                           */
/* ====================================================================== */

#define SIM_US_PER_BYTE 104u    /* 12 Bit je Byte bei 115200 Bd           */
#define SIM_TOUT_US     1100u   /* Zustellung nach Rahmenende (RX-Timeout) */
#define SIM_MAX_CARDS   6
#define SIM_MAX_RESP    64

typedef struct {
    bool     present;
    uint8_t  uid[10];
    uint8_t  eeprom;            /* gespeicherte Adresse, 0 = keine */
    uint8_t  addr;              /* Laufzeitadresse, 0 = keine */
    bool     enumerating;
    bool     chain_out;
    bool     chain_in_broken;
    bool     deaf;              /* Kollision erkannt */
    bool     no_version;
    uint8_t  ist, ziel;
    uint32_t delay_us;
    int      drop_ack;          /* so viele ENUM_ASSIGN-ACKs gehen verloren */
    int      miss_assign;       /* so viele ENUM_ASSIGN werden ueberhoert   */
    unsigned n_status, n_version;
} sim_card_t;

typedef struct {
    uint64_t start, end;
    uint8_t  buf[PROTO_MAX_FRAME];
    size_t   len;
    int      card;
    bool     lost;
    bool     garbled;
    bool     delivered;
} sim_resp_t;

static sim_card_t g_card[SIM_MAX_CARDS];
static int        g_ncard;
static sim_resp_t g_resp[SIM_MAX_RESP];
static int        g_nresp;
static uint64_t   g_us;
static unsigned   g_collisions;   /* Master sendet in eine Kartenantwort */
static unsigned   g_card_clash;   /* zwei Karten senden gleichzeitig     */
static int        g_first_done_frame;

static void sim_clear(void)
{
    memset(g_card, 0, sizeof(g_card));
    memset(g_resp, 0, sizeof(g_resp));
    g_ncard = 0;
    g_nresp = 0;
    g_us = 0;
    g_collisions = 0;
    g_card_clash = 0;
    g_first_done_frame = -1;
}

static uint32_t sim_clock(void *ctx)
{
    (void)ctx;
    return (uint32_t)(g_us / 1000u);
}

static void card_deaf(sim_card_t *c)
{
    c->deaf = true;
    c->addr = 0;
    c->chain_out = false;
    c->enumerating = false;
}

static void sim_respond(int k, uint8_t cmd, const uint8_t *pl, uint8_t len,
                        uint64_t t_req_end, bool lost)
{
    TEST_ASSERT_TRUE(g_nresp < SIM_MAX_RESP);
    sim_resp_t *r = &g_resp[g_nresp++];
    memset(r, 0, sizeof(*r));
    proto_frame_t f;
    f.cmd = cmd;
    f.addr = g_card[k].addr;
    f.payload_len = len;
    for (uint8_t i = 0; i < len; ++i) {
        f.payload[i] = pl[i];
    }
    r->len = proto_encode(&f, r->buf, sizeof(r->buf));
    r->start = t_req_end + g_card[k].delay_us;
    r->end = r->start + r->len * SIM_US_PER_BYTE;
    r->card = k;
    r->lost = lost;
    for (int i = 0; i < g_nresp - 1; ++i) {
        sim_resp_t *o = &g_resp[i];
        if (!o->delivered && r->start < o->end && o->start < r->end) {
            o->garbled = true;
            r->garbled = true;
            g_card_clash++;
            card_deaf(&g_card[o->card]);
            card_deaf(&g_card[k]);
        }
    }
}

static void sim_cards_on_frame(const uint8_t *d, size_t n, uint64_t t_end)
{
    proto_parser_t p;
    proto_parser_reset(&p);
    bool ok = false;
    for (size_t i = 0; i < n; ++i) {
        ok = (proto_parser_feed(&p, d[i]) == PARSE_FRAME_OK);
    }
    if (!ok) {
        return;
    }
    const proto_frame_t *f = &p.frame;
    bool chain_in[SIM_MAX_CARDS];
    for (int k = 0; k < g_ncard; ++k) {
        /* eine tote (fehlende) Karte gibt CHAIN nicht weiter */
        chain_in[k] = !g_card[k].chain_in_broken &&
                      (k == 0 ? bm.chain_active
                              : (g_card[k - 1].present && g_card[k - 1].chain_out));
    }
    for (int k = 0; k < g_ncard; ++k) {
        sim_card_t *c = &g_card[k];
        if (!c->present) {
            continue;
        }
        switch (f->cmd) {
        case CMD_ENUM_RESET:
            c->enumerating = true;
            c->addr = 0;
            c->chain_out = false;
            c->deaf = false;
            break;
        case CMD_ENUM_ASSIGN:
            if (c->enumerating && chain_in[k] && !c->chain_out) {
                if (c->miss_assign > 0) {
                    c->miss_assign--;
                    break;
                }
                c->addr = f->payload[0];
                c->enumerating = false;
                const bool lost = c->drop_ack > 0;
                if (lost) {
                    c->drop_ack--;
                }
                sim_respond(k, CMD_ENUM_ASSIGN, NULL, 0, t_end, lost);
                c->chain_out = true;
                c->eeprom = c->addr;
            }
            break;
        case CMD_ENUM_DONE:
            if (c->enumerating) {
                c->enumerating = false;
                c->addr = c->eeprom ? c->eeprom : PROTO_ADDR_SERVICE;
            }
            break;
        case CMD_SET_ALL:
            if (!c->enumerating && c->addr >= 1 && c->addr <= f->payload_len) {
                c->ziel = f->payload[c->addr - 1u];
            }
            break;
        case CMD_GO:
            if (!c->enumerating && c->addr != 0) {
                c->ist = c->ziel;   /* Fahrt sofort am Ziel */
            }
            break;
        default:
            if (f->addr == PROTO_ADDR_BROADCAST || c->enumerating || c->addr == 0 ||
                c->addr != f->addr) {
                break;
            }
            switch (f->cmd) {
            case CMD_GET_STATUS: {
                const uint8_t st[8] = { c->ist, c->ziel, 0, 0, 40, 0, 0, 1 };
                c->n_status++;
                sim_respond(k, CMD_GET_STATUS, st, 8, t_end, false);
                break;
            }
            case CMD_GET_VERSION: {
                c->n_version++;
                if (!c->no_version) {
                    const uint8_t v[5] = { 1, 1, 16, 0x02, 0 };
                    sim_respond(k, CMD_GET_VERSION, v, 5, t_end, false);
                }
                break;
            }
            case CMD_GET_UID:
                sim_respond(k, CMD_GET_UID, c->uid, 10, t_end, false);
                break;
            case CMD_PING: {
                const uint8_t v = 1;
                sim_respond(k, CMD_PING, &v, 1, t_end, false);
                break;
            }
            case CMD_SET:
                c->ziel = f->payload[0];
                sim_respond(k, CMD_SET, NULL, 0, t_end, false);
                break;
            case CMD_HOME:
            case CMD_STOP:
            case CMD_IDENTIFY:
            case CMD_SET_CONFIG:
                sim_respond(k, f->cmd, NULL, 0, t_end, false);
                break;
            default:
                break;
            }
            break;
        }
    }
}

/* Sendet ein Master-Rahmen: blockiert fuer die Sendedauer. */
static void sim_tx(void *ctx, const uint8_t *d, size_t n)
{
    (void)ctx;
    const uint64_t start = g_us;
    const uint64_t end = g_us + n * SIM_US_PER_BYTE;
    for (int i = 0; i < g_nresp; ++i) {
        sim_resp_t *r = &g_resp[i];
        if (!r->delivered && start < r->end && r->start < end) {
            g_collisions++;
            r->garbled = true;
            card_deaf(&g_card[r->card]);
        }
    }
    log_frame(d, n, start);
    if (g_ntxf > 0 && g_txf[g_ntxf - 1].cmd == CMD_ENUM_DONE && g_first_done_frame < 0) {
        g_first_done_frame = (int)(g_ntxf - 1);
    }
    g_us = end;
    sim_cards_on_frame(d, n, end);
}

/* Bereits zugestellte Antworten an den busmaster (wie bus_pump). */
static void sim_pump(void)
{
    for (;;) {
        int best = -1;
        uint64_t bt = UINT64_MAX;
        for (int i = 0; i < g_nresp; ++i) {
            const uint64_t dt = g_resp[i].end + SIM_TOUT_US;
            if (!g_resp[i].delivered && dt <= g_us && dt < bt) {
                best = i;
                bt = dt;
            }
        }
        if (best < 0) {
            break;
        }
        sim_resp_t *r = &g_resp[best];
        r->delivered = true;
        if (r->lost) {
            continue;
        }
        for (size_t b = 0; b < r->len; ++b) {
            uint8_t byte = r->buf[b];
            if (r->garbled && b + 1u == r->len) {
                byte ^= 0x5Au;
            }
            busmaster_on_rx_byte(&bm, byte, (uint32_t)(g_us / 1000u));
        }
    }
    int w = 0;
    for (int i = 0; i < g_nresp; ++i) {
        if (!g_resp[i].delivered) {
            g_resp[w++] = g_resp[i];
        }
    }
    g_nresp = w;
}

static void sim_setup(int ncards, uint32_t delay_us)
{
    g_ncard = ncards;
    for (int k = 0; k < ncards; ++k) {
        g_card[k].present = true;
        g_card[k].delay_us = delay_us;
        for (int i = 0; i < 10; ++i) {
            g_card[k].uid[i] = (uint8_t)(0x10 * (k + 1) + i);
        }
    }
    g_us = 1000000u;   /* 1 s nach Start */
    busmaster_init(&bm, sim_tx, NULL);
    busmaster_set_clock(&bm, sim_clock, NULL);
}

static uint32_t g_last_led;
static unsigned g_rounds;

/* loop() der Zentralsteuerung nachgebildet: Zeitstempel zu Durchlaufbeginn,
 * danach bus_pump, busmaster_tick, Anwendung, LED_SYNC, Abfrageplan. */
static void sim_run(masterapp_t *app, uint8_t fixed_count, uint32_t loop_us,
                    uint32_t duration_ms)
{
    const uint64_t end = g_us + (uint64_t)duration_ms * 1000u;
    while (g_us < end) {
        g_us += loop_us;
        const uint32_t now = (uint32_t)(g_us / 1000u);
        sim_pump();
        busmaster_tick(&bm, now);
        const uint8_t eff = fixed_count ? fixed_count : bm.module_count;
        if (app != NULL) {
            if (app->module_count != eff) {
                app->module_count = eff;
                app->have_shown = false;
            }
            masterapp_tick(app, now);
        }
        if (!busmaster_enum_busy(&bm) && now - g_last_led >= 1000u) {
            if (busmaster_led_sync(&bm, now)) {
                g_last_led = now;
            }
        }
        if (busmaster_service_poll(&bm, eff, now)) {
            g_rounds++;
        }
    }
}

static size_t count_set_all_before_done(void)
{
    const int d = g_first_done_frame;
    size_t c = 0;
    for (size_t i = 0; i < g_ntxf && (d < 0 || (int)i < d); ++i) {
        if (g_txf[i].cmd == CMD_SET_ALL || g_txf[i].cmd == CMD_GO) {
            c++;
        }
    }
    return c;
}

static void assert_cards_addressed(int n)
{
    for (int k = 0; k < n; ++k) {
        TEST_ASSERT_FALSE_MESSAGE(g_card[k].deaf, "Karte taub");
        TEST_ASSERT_EQUAL_UINT8(k + 1, g_card[k].addr);
    }
}

/* Kaltstart im Auto-Modus: Enumeration, im Durchlauf von ENUM_DONE Anzeige,
 * LED_SYNC und erster Poll. Karte 1 darf nicht kollidieren (#20). */
static void test_sim_boot_auto_no_collision(void)
{
    const uint32_t delays[3] = { 200, 1500, 3000 };   /* 3 ms = Spez.-Maximum */
    const uint32_t loops[5]  = { 250, 700, 1000, 1700, 3100 };
    for (int di = 0; di < 3; ++di) {
        for (int li = 0; li < 5; ++li) {
            g_ntxf = 0;
            sim_clear();
            sim_setup(4, delays[di]);
            g_last_led = 0;
            g_rounds = 0;
            masterapp_t app;
            masterapp_init(&app, &bm, 0);
            masterapp_set_text(&app, "AB12", 0);
            busmaster_start_enumeration(&bm, 1000);
            sim_run(&app, 0, loops[li], 3000);

            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, g_collisions, "Master sendet in Antwort");
            TEST_ASSERT_EQUAL_UINT(0, g_card_clash);
            TEST_ASSERT_EQUAL_UINT8(4, bm.module_count);
            assert_cards_addressed(4);
            TEST_ASSERT_EQUAL_size_t(0, count_set_all_before_done());
            for (int k = 0; k < 4; ++k) {
                TEST_ASSERT_TRUE(bm.mod[k].online);
                TEST_ASSERT_TRUE(g_card[k].n_status >= 5);
                TEST_ASSERT_EQUAL_UINT8(app.shown[k], g_card[k].ist);
                TEST_ASSERT_TRUE(bm.mod[k].ver_known);
                TEST_ASSERT_TRUE(bm.mod[k].uid_fresh);
            }
            TEST_ASSERT_TRUE(g_rounds >= 3);
            TEST_ASSERT_EQUAL_UINT8(0, busmaster_warnings(&bm));
        }
    }
}

/* Feste Modulzahl: masterapp sendet von Anfang an, auch waehrend der
 * Enumeration; die Warteschlange haelt die Anzeige zurueck (#20 b). */
static void test_sim_override_masterapp_during_enum(void)
{
    const uint32_t loops[4] = { 300, 900, 1000, 2100 };
    for (int li = 0; li < 4; ++li) {
        g_ntxf = 0;
        sim_clear();
        sim_setup(4, 300);
        g_last_led = 0;
        masterapp_t app;
        masterapp_init(&app, &bm, 4);
        masterapp_set_text(&app, "1234", 0);
        busmaster_start_enumeration(&bm, 1000);
        sim_run(&app, 4, loops[li], 2000);

        TEST_ASSERT_EQUAL_UINT(0, g_collisions);
        TEST_ASSERT_EQUAL_size_t(0, count_set_all_before_done());
        TEST_ASSERT_EQUAL_UINT8(4, bm.module_count);
        assert_cards_addressed(4);
        for (int k = 0; k < 4; ++k) {
            TEST_ASSERT_EQUAL_UINT8(app.shown[k], g_card[k].ist);
        }
    }
}

/* ACK verloren, Karte hat die Adresse aber uebernommen: kein zweites
 * ENUM_ASSIGN(2), sonst bekaeme Karte 3 dieselbe Adresse (#22). */
static void test_sim_lost_assign_ack_no_duplicate(void)
{
    sim_setup(3, 300);
    g_card[1].drop_ack = 1;
    busmaster_start_enumeration(&bm, 1000);
    sim_run(NULL, 0, 500, 1000);

    TEST_ASSERT_EQUAL_UINT(0, g_collisions);
    TEST_ASSERT_EQUAL_UINT8(3, bm.module_count);
    assert_cards_addressed(3);
    TEST_ASSERT_EQUAL_size_t(1, count_cmd(CMD_PING, 2));
    size_t assign2 = 0;
    for (size_t i = 0; i < g_ntxf; ++i) {
        if (g_txf[i].cmd == CMD_ENUM_ASSIGN && g_txf[i].pl[0] == 2) {
            assign2++;
        }
    }
    TEST_ASSERT_EQUAL_size_t(1, assign2);
    TEST_ASSERT_EQUAL_UINT8(0, busmaster_warnings(&bm) & BM_WARN_UID_DUP);
}

/* ENUM_ASSIGN ueberhoert: PING ohne Antwort, dann einmal wiederholen (#22). */
static void test_sim_missed_assign_is_repeated(void)
{
    sim_setup(3, 300);
    g_card[1].miss_assign = 1;
    busmaster_start_enumeration(&bm, 1000);
    sim_run(NULL, 0, 500, 1000);

    TEST_ASSERT_EQUAL_UINT8(3, bm.module_count);
    assert_cards_addressed(3);
    size_t assign2 = 0;
    for (size_t i = 0; i < g_ntxf; ++i) {
        if (g_txf[i].cmd == CMD_ENUM_ASSIGN && g_txf[i].pl[0] == 2) {
            assign2++;
        }
    }
    TEST_ASSERT_EQUAL_size_t(2, assign2);
    TEST_ASSERT_EQUAL_size_t(3, count_cmd(CMD_PING, 2));
}

/* Gleiche UID auf zwei Adressen und Wechsel zwischen zwei Laeufen (#28). */
static void test_sim_uid_duplicate_and_change(void)
{
    sim_setup(3, 300);
    memcpy(g_card[2].uid, g_card[0].uid, 10);
    busmaster_start_enumeration(&bm, 1000);
    sim_run(NULL, 0, 500, 1000);
    TEST_ASSERT_TRUE(busmaster_warnings(&bm) & BM_WARN_UID_DUP);
    TEST_ASSERT_TRUE(bm.mod[0].uid_dup);
    TEST_ASSERT_FALSE(bm.mod[1].uid_dup);
    TEST_ASSERT_TRUE(bm.mod[2].uid_dup);
    TEST_ASSERT_FALSE(busmaster_warnings(&bm) & BM_WARN_UID_CHANGED);

    /* zweiter Lauf: Karte 3 eindeutig, Karte 2 getauscht */
    g_card[2].uid[0] ^= 0xFFu;
    g_card[1].uid[9] ^= 0xFFu;
    const uint32_t seq = bm.warn_seq;
    busmaster_start_enumeration(&bm, sim_clock(NULL));
    sim_run(NULL, 0, 500, 1000);
    TEST_ASSERT_FALSE(busmaster_warnings(&bm) & BM_WARN_UID_DUP);
    TEST_ASSERT_TRUE(busmaster_warnings(&bm) & BM_WARN_UID_CHANGED);
    TEST_ASSERT_TRUE(bm.mod[1].uid_changed);
    TEST_ASSERT_TRUE(bm.mod[2].uid_changed);
    TEST_ASSERT_FALSE(bm.mod[0].uid_changed);
    TEST_ASSERT_TRUE(bm.warn_seq != seq);
}

/* Fabrikneue Karte ohne CHAIN-Anschluss landet auf 250 (#28, Spez. 4.5.2). */
static void test_sim_service_address_warning(void)
{
    sim_setup(3, 300);
    g_card[2].chain_in_broken = true;             /* Karte 3 nie erreicht, EEPROM leer */
    busmaster_start_enumeration(&bm, 1000);
    sim_run(NULL, 0, 500, 1000);
    TEST_ASSERT_EQUAL_UINT8(2, bm.module_count);
    TEST_ASSERT_EQUAL_UINT8(PROTO_ADDR_SERVICE, g_card[2].addr);
    TEST_ASSERT_TRUE(busmaster_warnings(&bm) & BM_WARN_SERVICE_ADDR);

    /* Karte entfernt: naechste periodische Abfrage nimmt die Warnung zurueck */
    g_card[2].present = false;
    const size_t pings = count_cmd(CMD_PING, PROTO_ADDR_SERVICE);
    sim_run(NULL, 0, 700, BUSMASTER_SERVICE_PROBE_MS + 500u);
    TEST_ASSERT_EQUAL_size_t(pings + 1u, count_cmd(CMD_PING, PROTO_ADDR_SERVICE));
    TEST_ASSERT_FALSE(busmaster_warnings(&bm) & BM_WARN_SERVICE_ADDR);
    TEST_ASSERT_EQUAL_UINT(0, g_collisions);
}

static void test_sim_two_cards_on_service_address(void)
{
    sim_setup(3, 300);
    g_card[1].chain_in_broken = true;             /* Karte 2 und 3 unerreicht */
    busmaster_start_enumeration(&bm, 1000);
    sim_run(NULL, 0, 500, 1000);
    TEST_ASSERT_EQUAL_UINT8(1, bm.module_count);
    TEST_ASSERT_TRUE(g_card_clash > 0);           /* beide antworten auf 250 */
    TEST_ASSERT_TRUE(bm.crc_errors > 0);
    TEST_ASSERT_TRUE(busmaster_warnings(&bm) & BM_WARN_SERVICE_ADDR);
}

/* Modul ohne GET_VERSION: Statusabfragen laufen gleichmaessig weiter,
 * kein Online/Offline-Flattern (#40, gap-enum-position1-feldfehler-3). */
static void test_sim_version_slot_does_not_starve(void)
{
    sim_setup(4, 300);
    g_card[1].no_version = true;
    busmaster_start_enumeration(&bm, 1000);
    sim_run(NULL, 0, 800, 500);
    unsigned base[4];
    for (int k = 0; k < 4; ++k) {
        base[k] = g_card[k].n_status;
    }
    sim_run(NULL, 0, 800, 10000);
    for (int k = 0; k < 4; ++k) {
        const unsigned n = g_card[k].n_status - base[k];
        TEST_ASSERT_TRUE_MESSAGE(n >= 22 && n <= 26, "Statusabfragen ungleich verteilt");
        TEST_ASSERT_TRUE(bm.mod[k].online);
        TEST_ASSERT_EQUAL_UINT8(0, bm.mod[k].miss_count);
    }
    TEST_ASSERT_EQUAL_UINT(9, g_card[1].n_version);  /* 3 Abfragen x 3 Versuche */
    TEST_ASSERT_FALSE(bm.mod[1].ver_known);
    TEST_ASSERT_TRUE(bm.mod[0].ver_known);
    TEST_ASSERT_TRUE(bm.mod[3].ver_known);
    TEST_ASSERT_EQUAL_UINT(0, g_collisions);
}

/* Modul verliert seinen Inhalt (Neustart/Homing): Abgleich stellt ihn wieder
 * her, ohne Kollision (#26). */
static void test_sim_reconcile_restores_content(void)
{
    sim_setup(3, 1000);
    masterapp_t app;
    masterapp_init(&app, &bm, 0);
    masterapp_set_text(&app, "XYZ", 0);
    busmaster_start_enumeration(&bm, 1000);
    sim_run(&app, 0, 600, 1500);
    TEST_ASSERT_EQUAL_UINT8(app.shown[1], g_card[1].ist);

    const uint8_t other = (uint8_t)((app.shown[1] % 40u) + 1u);
    g_card[1].ist = other;                        /* z. B. Neustart mit Homing */
    g_card[1].ziel = other;
    sim_run(&app, 0, 600, 1500);
    TEST_ASSERT_EQUAL_UINT8(app.shown[1], g_card[1].ist);
    TEST_ASSERT_EQUAL_UINT32(1, bm.reconciles);
    TEST_ASSERT_EQUAL_UINT(0, g_collisions);
}

/* Tote Karte an Position 3: Karten 4 und 5 sind per EEPROM-Rueckfall
 * erreichbar und werden einbezogen (#22, NF-4/A-10). */
static void test_sim_dead_card_gap_probed(void)
{
    sim_setup(5, 300);
    masterapp_t app;
    masterapp_init(&app, &bm, 0);
    masterapp_set_text(&app, "ABCDE", 0);
    busmaster_start_enumeration(&bm, 1000);
    sim_run(&app, 0, 600, 1000);
    TEST_ASSERT_EQUAL_UINT8(5, bm.module_count);

    g_card[2].present = false;                    /* Karte 3 faellt aus */
    busmaster_start_enumeration(&bm, sim_clock(NULL));
    sim_run(&app, 0, 600, 1500);
    TEST_ASSERT_EQUAL_UINT8(5, bm.module_count);  /* Feldbreite bleibt */
    TEST_ASSERT_FALSE(bm.mod[2].online);
    TEST_ASSERT_TRUE(bm.mod[3].online);
    TEST_ASSERT_TRUE(bm.mod[4].online);
    TEST_ASSERT_EQUAL_UINT8(4, g_card[3].addr);
    TEST_ASSERT_EQUAL_UINT8(5, g_card[4].addr);
    TEST_ASSERT_TRUE(busmaster_warnings(&bm) & BM_WARN_ENUM_GAP);
    TEST_ASSERT_EQUAL_UINT(0, g_collisions);

    /* Kaltstart des Masters mit Hinweis aus dem NVS */
    g_ntxf = 0;
    busmaster_init(&bm, sim_tx, NULL);
    busmaster_set_clock(&bm, sim_clock, NULL);
    busmaster_set_enum_hint(&bm, 5);
    busmaster_start_enumeration(&bm, sim_clock(NULL));
    sim_run(NULL, 0, 600, 1500);
    TEST_ASSERT_EQUAL_UINT8(5, bm.module_count);
    TEST_ASSERT_TRUE(bm.mod[4].online);
    TEST_ASSERT_TRUE(g_card[4].n_status > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_show_emits_set_all_and_go);
    RUN_TEST(test_show_coalesces_unsent_set_all);
    RUN_TEST(test_no_send_while_awaiting_then_in_order);
    RUN_TEST(test_led_sync_coalesced);
    RUN_TEST(test_unicast_ack_retries_with_payload_no_offline);
    RUN_TEST(test_identify_ack_completes);
    RUN_TEST(test_poll_config_via_queue);
    RUN_TEST(test_poll_status_updates_table);
    RUN_TEST(test_own_echo_does_not_swallow_response);
    RUN_TEST(test_status_timeout_retries_then_offline);
    RUN_TEST(test_timeout_measured_from_tx_end_with_clock);
    RUN_TEST(test_rx_timestamp_from_clock);
    RUN_TEST(test_retry_deferred_while_frame_in_progress);
    RUN_TEST(test_retry_waits_for_bus_quiet);
    RUN_TEST(test_retry_defer_has_upper_bound);
    RUN_TEST(test_stale_partial_frame_releases_bus);
    RUN_TEST(test_late_reply_after_retry_guard);
    RUN_TEST(test_version_timeout_does_not_count_miss);
    RUN_TEST(test_collision_code_counted);
    RUN_TEST(test_service_poll_round_robin_and_round_flag);
    RUN_TEST(test_service_poll_waits_for_queue);
    RUN_TEST(test_version_poll_own_cursor_and_backoff);
    RUN_TEST(test_reconcile_sends_set_and_go);
    RUN_TEST(test_reconcile_rate_limit_and_max_tries);
    RUN_TEST(test_reconcile_success_resets_and_ziel_only_without_go);
    RUN_TEST(test_reconcile_not_while_moving_error_or_unknown);
    RUN_TEST(test_reconcile_skipped_while_show_queued_and_after_stop);
    RUN_TEST(test_clear_targets_disables_reconcile);
    RUN_TEST(test_enumeration_two_modules);
    RUN_TEST(test_enum_waits_for_pending_reply);
    RUN_TEST(test_enum_ack_with_wrong_address_ignored);
    RUN_TEST(test_enum_ack_during_deferred_timeout);
    RUN_TEST(test_enum_late_ack_in_check_phase);
    RUN_TEST(test_enum_limit_warns_and_clamps);
    RUN_TEST(test_uid_garbled_marks_duplicate);
    RUN_TEST(test_enum_probe_beyond_chain_end);
    RUN_TEST(test_start_enumeration_keeps_count_until_done);
    RUN_TEST(test_sim_boot_auto_no_collision);
    RUN_TEST(test_sim_override_masterapp_during_enum);
    RUN_TEST(test_sim_lost_assign_ack_no_duplicate);
    RUN_TEST(test_sim_missed_assign_is_repeated);
    RUN_TEST(test_sim_uid_duplicate_and_change);
    RUN_TEST(test_sim_service_address_warning);
    RUN_TEST(test_sim_two_cards_on_service_address);
    RUN_TEST(test_sim_version_slot_does_not_starve);
    RUN_TEST(test_sim_reconcile_restores_content);
    RUN_TEST(test_sim_dead_card_gap_probed);
    return UNITY_END();
}
