/* Tests fuer die Sende-Seite der Firmware-Verteilung, siehe lib/moduleupdate.
 *
 * Ko-Simulation: die simulierten Module fuehren die echte Bootloader-Logik
 * (module/lib/fwupdate: fwboot + fwupdate) aus, die App ist ein kleines Modell
 * (GET_VERSION, ENTER_BOOTLOADER). Antworten laufen mit einem Takt Verzug ueber
 * den simulierten Bus zurueck; Anfragen und Antworten lassen sich gezielt
 * verlieren. Das fruehere Modell quittierte jedes Duplikat und nahm FW_BEGIN
 * jederzeit an und verdeckte so die Befunde aus #30. */
#include <string.h>

#include <unity.h>

#include "fwboot.h"
#include "moduleupdate.h"
#include "protocol.h"

void setUp(void) {}
void tearDown(void) {}

#define SIM_APP_MAX  512u
#define TARGET_VER   0x0102u
#define OLD_VER      0x0101u
#define STEP_MS      10u
/* Obergrenze FW_BEGIN je Modul: (MAX_RETRIES + 1) * (MAX_RESTARTS + 1) */
#define MAX_BEGINS   8

/* --- Bus und Module ---------------------------------------------------- */

typedef struct {
    bool     present;
    bool     in_boot;              /* sonst laeuft die App */
    fwboot_t bt;
    uint8_t  flash[SIM_APP_MAX];   /* App-Bereich */
    uint8_t  marker;               /* EEPROM-Byte "App gueltig" */
    uint32_t app_max;
    uint32_t deadline;             /* Zeitablauf des Bootloaders */
    int      bad_starts;           /* App-Starts mit nicht startbarem Abbild */
    int      fail_page_writes;     /* so viele Seitenschreibungen scheitern */
} sim_mod_t;

typedef struct {
    uint8_t cmd, addr, len;
    uint8_t pl[8];
} reply_t;

static moduleupdate_t g_mu;
static uint32_t       g_now;
static uint8_t        g_img[400];
static uint32_t       g_img_len;
static sim_mod_t      g_mod[MU_MAX_ADDR + 1u];

static reply_t g_rq[16];
static int     g_rqn;

static int  g_req_count[256];     /* gesendete Anfragen je Kommando       */
static int  g_rep_count[256];     /* erzeugte Antworten je Kommando       */
static int  g_drop_req[256];      /* n-te Anfrage geht verloren (-1: keine) */
static int  g_drop_rep[256];      /* n-te Antwort geht verloren (-1: keine) */
static bool g_gpior_survives;     /* Marker ENTER_BOOTLOADER uebersteht Reset */
static int  g_force_nak_begin;    /* so oft FW_BEGIN ohne Pruefung ablehnen */
static uint16_t g_app_ver_override;
static uint32_t g_stall_from, g_stall_ms;   /* Master haengt (kein Tick, kein Empfang) */

static uint16_t crc16(const uint8_t *d, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
    }
    return c;
}

static uint8_t mod_addr(const sim_mod_t *m) { return (uint8_t)(m - g_mod); }

static bool sim_write_page(void *ctx, uint32_t addr, const uint8_t *page)
{
    sim_mod_t *m = (sim_mod_t *)ctx;
    TEST_ASSERT_EQUAL_HEX8(FWBOOT_MARKER_UPDATE, m->marker);   /* Marker vor Seite */
    if (m->fail_page_writes > 0) { m->fail_page_writes--; return false; }
    TEST_ASSERT_TRUE(addr + FWUPDATE_PAGE <= SIM_APP_MAX);
    memcpy(&m->flash[addr], page, FWUPDATE_PAGE);
    return true;
}

static void sim_set_marker(void *ctx, uint8_t v) { ((sim_mod_t *)ctx)->marker = v; }

static bool sim_app_startable(const sim_mod_t *m)
{
    return fwboot_app_valid((uint16_t)(m->flash[0] | (m->flash[1] << 8)), m->marker);
}

static void sim_boot(sim_mod_t *m, bool forced)
{
    fwboot_init(&m->bt, mod_addr(m), forced, sim_app_startable(m), m->app_max, 2u,
                sim_write_page, sim_set_marker, m);
    m->in_boot = true;
    m->deadline = g_now + fwboot_timeout_ms(&m->bt);
}

static void sim_start_app(sim_mod_t *m)
{
    if (!sim_app_startable(m)) m->bad_starts++;   /* #29: darf nie passieren */
    m->in_boot = false;
}

static void queue_reply(uint8_t cmd, uint8_t addr, const uint8_t *pl, uint8_t len)
{
    const int idx = g_rep_count[cmd]++;
    if (idx == g_drop_rep[cmd]) return;            /* Antwort geht verloren */
    TEST_ASSERT_TRUE(g_rqn < (int)(sizeof(g_rq) / sizeof(g_rq[0])));
    reply_t *r = &g_rq[g_rqn++];
    r->cmd = cmd;
    r->addr = addr;
    r->len = len;
    memcpy(r->pl, pl, len);
}

static uint16_t sim_app_version(const sim_mod_t *m)
{
    if (g_app_ver_override) return g_app_ver_override;
    return memcmp(m->flash, g_img, g_img_len) == 0 ? TARGET_VER : OLD_VER;
}

static void sim_rx(sim_mod_t *m, const proto_frame_t *f)
{
    if (!m->in_boot) {
        if (f->addr != mod_addr(m)) return;
        if (f->cmd == CMD_GET_VERSION) {
            const uint16_t v = sim_app_version(m);
            const uint8_t pl[5] = { 1, (uint8_t)(v >> 8), (uint8_t)v,
                                    PROTO_VER_FLAG_BOOTLOADER | PROTO_VER_FLAG_APP_VALID, 0 };
            queue_reply(CMD_GET_VERSION, mod_addr(m), pl, 5);
        } else if (f->cmd == CMD_ENTER_BOOTLOADER) {
            sim_boot(m, g_gpior_survives);
        }
        return;                                    /* FW_* ignoriert die App */
    }

    if (f->cmd == CMD_FW_BEGIN && f->addr == m->bt.addr && g_force_nak_begin > 0) {
        g_force_nak_begin--;
        const uint8_t nak = 0x00;
        queue_reply(CMD_FW_BEGIN, m->bt.addr, &nak, 1);
        return;
    }
    const fwboot_action_t a = fwboot_on_frame(&m->bt, f->cmd, f->addr, f->payload, f->payload_len);
    if (a == FWBOOT_IGNORE) return;
    m->deadline = g_now + fwboot_timeout_ms(&m->bt);
    if (a == FWBOOT_REPLY || a == FWBOOT_REPLY_RESET) {
        queue_reply(m->bt.reply_cmd, m->bt.addr, m->bt.reply, m->bt.reply_len);
    }
    if (a == FWBOOT_REPLY_RESET) {
        sim_boot(m, false);                        /* SW-Reset, kein Marker */
    } else if (a == FWBOOT_START_APP) {
        sim_start_app(m);                          /* Rahmen ist damit verbraucht */
    }
}

/* Sende-Callback des Masters: Rahmen geht an alle Module am Bus. */
static void tx_cb(void *ctx, const uint8_t *frame, size_t len)
{
    (void)ctx;
    proto_parser_t p;
    proto_parser_reset(&p);
    proto_parse_result_t r = PARSE_NEED_MORE;
    for (size_t i = 0; i < len; i++) r = proto_parser_feed(&p, frame[i]);
    TEST_ASSERT_EQUAL_INT(PARSE_FRAME_OK, r);
    const int idx = g_req_count[p.frame.cmd]++;
    if (idx == g_drop_req[p.frame.cmd]) return;   /* Anfrage geht verloren */
    for (uint8_t a = 1; a <= MU_MAX_ADDR; a++) {
        if (g_mod[a].present) sim_rx(&g_mod[a], &p.frame);
    }
}

static void sim_tick(void)
{
    for (uint8_t a = 1; a <= MU_MAX_ADDR; a++) {
        sim_mod_t *m = &g_mod[a];
        if (!m->present || !m->in_boot || g_now < m->deadline) continue;
        if (fwboot_on_timeout(&m->bt) == FWBOOT_START_APP) {
            sim_start_app(m);
        } else {
            m->deadline = g_now + fwboot_timeout_ms(&m->bt);
        }
    }
}

static void deliver(void)
{
    reply_t q[16];
    const int n = g_rqn;
    memcpy(q, g_rq, sizeof(q));
    g_rqn = 0;
    for (int i = 0; i < n; i++) {
        moduleupdate_on_frame(&g_mu, q[i].cmd, q[i].addr, q[i].pl, q[i].len, g_now);
    }
}

static bool master_stalled(void)
{
    return g_stall_ms && g_now >= g_stall_from && g_now < g_stall_from + g_stall_ms;
}

/* Uhr vorstellen, bis fertig oder Zeitgrenze. */
static void run(uint32_t until_ms)
{
    while (g_now < until_ms && moduleupdate_busy(&g_mu)) {
        sim_tick();
        if (!master_stalled()) {
            deliver();
            moduleupdate_tick(&g_mu, g_now);
        }
        g_now += STEP_MS;
    }
}

/* Nur die Module weiterlaufen lassen (Master still). */
static void idle(uint32_t ms)
{
    const uint32_t until = g_now + ms;
    while (g_now < until) {
        sim_tick();
        g_rqn = 0;
        g_now += STEP_MS;
    }
}

static void add_module(uint8_t addr)
{
    sim_mod_t *m = &g_mod[addr];
    m->present = true;
    m->in_boot = false;                            /* alte App laeuft */
    memset(m->flash, 0x11, sizeof(m->flash));
    m->marker = FWBOOT_MARKER_BLANK;               /* Werksflash */
    m->app_max = SIM_APP_MAX;
    m->bad_starts = 0;
    m->fail_page_writes = 0;
}

static void setup_case(uint32_t img_len)
{
    g_img_len = img_len;
    for (uint32_t i = 0; i < img_len; i++) g_img[i] = (uint8_t)((i * 7u + 3u) & 0xFF);
    g_img[0] = 0x0C;                               /* erstes Wort != 0xFFFF */
    g_now = 0;
    g_rqn = 0;
    memset(g_mod, 0, sizeof(g_mod));
    memset(g_req_count, 0, sizeof(g_req_count));
    memset(g_rep_count, 0, sizeof(g_rep_count));
    for (int i = 0; i < 256; i++) { g_drop_req[i] = -1; g_drop_rep[i] = -1; }
    g_gpior_survives = false;                      /* ungeklaert, Fensterpfad ist der strengere */
    g_force_nak_begin = 0;
    g_app_ver_override = 0;
    g_stall_from = 0;
    g_stall_ms = 0;
    moduleupdate_init(&g_mu, tx_cb, NULL, g_img, img_len, crc16(g_img, img_len), TARGET_VER);
}

static void enqueue(uint32_t mask)
{
    moduleupdate_enqueue(&g_mu, mask, mask);
}

/* Modul traegt das neue Abbild, ist gueltig markiert und laeuft in der App. */
static void assert_updated(uint8_t addr)
{
    const sim_mod_t *m = &g_mod[addr];
    TEST_ASSERT_EQUAL(MU_RES_OK, g_mu.result[addr]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_img, m->flash, g_img_len);
    TEST_ASSERT_EQUAL_HEX8(FWBOOT_MARKER_VALID, m->marker);
    TEST_ASSERT_FALSE(m->in_boot);
    TEST_ASSERT_EQUAL_INT(0, m->bad_starts);
}

/* --- Tests ------------------------------------------------------------- */

static void happy_path(bool gpior_survives)
{
    setup_case(200);
    g_gpior_survives = gpior_survives;
    add_module(3);
    enqueue(1u << 2);
    TEST_ASSERT_TRUE(moduleupdate_busy(&g_mu));
    run(60000);
    TEST_ASSERT_FALSE(moduleupdate_busy(&g_mu));
    assert_updated(3);
    TEST_ASSERT_EQUAL_UINT8(1, g_mu.done_ok);
    TEST_ASSERT_EQUAL_UINT8(0, g_mu.done_fail);
    TEST_ASSERT_EQUAL_INT(1, g_req_count[CMD_FW_BEGIN]);
    TEST_ASSERT_EQUAL_INT(7, g_req_count[CMD_FW_DATA]);       /* ceil(200/30), kein Retry */
    TEST_ASSERT_EQUAL_INT(1, g_req_count[CMD_FW_END]);
    TEST_ASSERT_EQUAL_INT(1, g_req_count[CMD_GET_VERSION]);   /* T_APP > Startfenster */
}

static void test_happy_path_window(void)   { happy_path(false); }
static void test_happy_path_forced(void)   { happy_path(true); }

static void test_flash_all_multiple(void)
{
    setup_case(130);
    add_module(1);
    add_module(2);
    add_module(5);
    enqueue((1u << 0) | (1u << 1) | (1u << 4));
    run(120000);
    TEST_ASSERT_FALSE(moduleupdate_busy(&g_mu));
    assert_updated(1);
    assert_updated(2);
    assert_updated(5);
    TEST_ASSERT_EQUAL_UINT8(3, g_mu.done_ok);
}

static void test_offline_skipped(void)
{
    setup_case(64);
    add_module(1);
    /* Adresse 4 angefragt, aber nicht online */
    moduleupdate_enqueue(&g_mu, (1u << 3) | (1u << 0), (1u << 0));
    run(60000);
    TEST_ASSERT_EQUAL(MU_RES_SKIPPED, g_mu.result[4]);
    assert_updated(1);
}

static void test_begin_nak_then_recover(void)
{
    setup_case(96);
    add_module(1);
    g_force_nak_begin = 2;   /* zweimal NAK, dann ok */
    enqueue(1u << 0);
    run(60000);
    assert_updated(1);
    TEST_ASSERT_EQUAL_INT(3, g_req_count[CMD_FW_BEGIN]);
}

/* #30: NAK auf FW_BEGIN wird gezaehlt, keine Endlosschleife */
static void test_begin_nak_forever_is_bounded(void)
{
    setup_case(96);
    add_module(1);
    g_mod[1].app_max = 64;   /* Bootloader lehnt das Abbild immer ab */
    enqueue(1u << 0);
    run(60000);
    TEST_ASSERT_FALSE(moduleupdate_busy(&g_mu));
    TEST_ASSERT_EQUAL(MU_RES_FAILED, g_mu.result[1]);
    TEST_ASSERT_EQUAL_INT(MAX_BEGINS, g_req_count[CMD_FW_BEGIN]);
    TEST_ASSERT_EQUAL_INT(2, g_req_count[CMD_ENTER_BOOTLOADER]);   /* ein Neubeginn */
    TEST_ASSERT_EQUAL_INT(0, g_req_count[CMD_FW_DATA]);
    /* nichts geschrieben: die alte App startet wieder */
    TEST_ASSERT_EQUAL_HEX8(FWBOOT_MARKER_BLANK, g_mod[1].marker);
    idle(5000);
    TEST_ASSERT_FALSE(g_mod[1].in_boot);
    TEST_ASSERT_EQUAL_INT(0, g_mod[1].bad_starts);
}

static void test_data_request_lost_retry(void)
{
    setup_case(150);
    add_module(1);
    g_drop_req[CMD_FW_DATA] = 2;   /* dritter DATA-Rahmen kommt nicht an */
    enqueue(1u << 0);
    run(120000);
    assert_updated(1);
    TEST_ASSERT_EQUAL_INT(1, g_req_count[CMD_FW_BEGIN]);
    TEST_ASSERT_EQUAL_INT(6, g_req_count[CMD_FW_DATA]);
}

/* #30: verlorenes DATA-ACK -> Wiederholung wird idempotent bestaetigt */
static void test_data_ack_lost_duplicate_acked(void)
{
    setup_case(150);
    add_module(1);
    g_drop_rep[CMD_FW_DATA] = 2;   /* drittes DATA-ACK geht verloren */
    enqueue(1u << 0);
    run(120000);
    assert_updated(1);
    TEST_ASSERT_EQUAL_INT(1, g_req_count[CMD_FW_BEGIN]);       /* kein Neubeginn */
    TEST_ASSERT_EQUAL_INT(6, g_req_count[CMD_FW_DATA]);
}

/* #30: verlorenes BEGIN-ACK -> erneutes FW_BEGIN startet sauber neu */
static void test_begin_ack_lost(void)
{
    setup_case(150);
    add_module(1);
    g_drop_rep[CMD_FW_BEGIN] = 0;
    enqueue(1u << 0);
    run(120000);
    assert_updated(1);
    TEST_ASSERT_EQUAL_INT(2, g_req_count[CMD_FW_BEGIN]);
}

/* #30: verlorenes END-ACK -> Erfolg per GET_VERSION statt FAILED */
static void test_end_ack_lost_verified_by_version(void)
{
    setup_case(200);
    add_module(2);
    g_drop_rep[CMD_FW_END] = 0;
    enqueue(1u << 1);
    run(120000);
    assert_updated(2);
    TEST_ASSERT_EQUAL_UINT8(1, g_mu.done_ok);
    TEST_ASSERT_EQUAL_UINT8(0, g_mu.done_fail);
    TEST_ASSERT_EQUAL_INT(1, g_req_count[CMD_FW_END]);
}

/* FW_END kommt nicht an: Bootloader meldet ungueltige App -> FW_END erneut */
static void test_end_request_lost_resent(void)
{
    setup_case(200);
    add_module(2);
    g_drop_req[CMD_FW_END] = 0;
    enqueue(1u << 1);
    run(120000);
    assert_updated(2);
    TEST_ASSERT_EQUAL_INT(2, g_req_count[CMD_FW_END]);
    TEST_ASSERT_EQUAL_INT(1, g_req_count[CMD_FW_BEGIN]);
}

/* #30: bleibender DATA-NAK (Schreibfehler) -> Neubeginn per ENTER/BEGIN */
static void test_restart_after_persistent_data_nak(void)
{
    setup_case(200);
    add_module(1);
    g_mod[1].fail_page_writes = 1;
    enqueue(1u << 0);
    run(120000);
    assert_updated(1);
    TEST_ASSERT_EQUAL_INT(2, g_req_count[CMD_ENTER_BOOTLOADER]);
    TEST_ASSERT_EQUAL_INT(2, g_req_count[CMD_FW_BEGIN]);
    TEST_ASSERT_EQUAL_UINT8(1, g_mu.done_ok);
}

/* #29: Master haengt mitten in FW_DATA laenger als jeder Bootloader-Zeitablauf */
static void test_master_stall_during_data(void)
{
    setup_case(400);
    add_module(1);
    g_stall_from = 900;      /* DATA laeuft ab ~830 ms */
    g_stall_ms = 10000;
    enqueue(1u << 0);
    run(120000);
    assert_updated(1);       /* bad_starts == 0: nie in ein halbes Abbild */
    TEST_ASSERT_EQUAL_INT(1, g_req_count[CMD_FW_BEGIN]);
}

/* #29/#30: Master startet waehrend der Uebertragung neu, Bus lange still */
static void test_master_restart_mid_transfer(void)
{
    setup_case(400);
    add_module(1);
    enqueue(1u << 0);
    run(900);                /* mitten in FW_DATA abbrechen */
    TEST_ASSERT_TRUE(moduleupdate_busy(&g_mu));
    TEST_ASSERT_EQUAL_HEX8(FWBOOT_MARKER_UPDATE, g_mod[1].marker);
    idle(20000);             /* Master bootet, WLAN ... */
    TEST_ASSERT_TRUE(g_mod[1].in_boot);
    TEST_ASSERT_EQUAL_INT(0, g_mod[1].bad_starts);

    /* neuer Master-Lauf, Karte gilt als offline -> explizit angefragt */
    moduleupdate_init(&g_mu, tx_cb, NULL, g_img, g_img_len, crc16(g_img, g_img_len), TARGET_VER);
    enqueue(1u << 0);
    run(g_now + 60000);
    assert_updated(1);
}

/* Karte haengt seit einem frueheren Abbruch im Bootloader (EEPROM[8] = 0x00) */
static void test_card_stuck_in_bootloader_recovered(void)
{
    setup_case(200);
    add_module(4);
    memset(g_mod[4].flash, 0x22, 128);   /* halb geschriebenes Abbild */
    g_mod[4].marker = FWBOOT_MARKER_UPDATE;
    sim_boot(&g_mod[4], false);
    idle(10000);
    TEST_ASSERT_TRUE(g_mod[4].in_boot);
    enqueue(1u << 3);
    run(g_now + 60000);
    assert_updated(4);
}

static void test_confirm_wrong_version_fails(void)
{
    setup_case(64);
    add_module(1);
    g_app_ver_override = 0x0999;   /* stimmt nicht mit TARGET_VER */
    enqueue(1u << 0);
    run(60000);
    TEST_ASSERT_EQUAL(MU_RES_FAILED, g_mu.result[1]);
    TEST_ASSERT_EQUAL_UINT8(1, g_mu.done_fail);
    TEST_ASSERT_EQUAL_INT(1, g_req_count[CMD_FW_BEGIN]);   /* kein sinnloser Neubeginn */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_happy_path_window);
    RUN_TEST(test_happy_path_forced);
    RUN_TEST(test_flash_all_multiple);
    RUN_TEST(test_offline_skipped);
    RUN_TEST(test_begin_nak_then_recover);
    RUN_TEST(test_begin_nak_forever_is_bounded);
    RUN_TEST(test_data_request_lost_retry);
    RUN_TEST(test_data_ack_lost_duplicate_acked);
    RUN_TEST(test_begin_ack_lost);
    RUN_TEST(test_end_ack_lost_verified_by_version);
    RUN_TEST(test_end_request_lost_resent);
    RUN_TEST(test_restart_after_persistent_data_nak);
    RUN_TEST(test_master_stall_during_data);
    RUN_TEST(test_master_restart_mid_transfer);
    RUN_TEST(test_card_stuck_in_bootloader_recovered);
    RUN_TEST(test_confirm_wrong_version_fails);
    return UNITY_END();
}
