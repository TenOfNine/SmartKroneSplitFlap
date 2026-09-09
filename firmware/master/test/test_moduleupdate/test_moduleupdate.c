/* Tests fuer die Sende-Seite der Firmware-Verteilung, siehe lib/moduleupdate.
 * Simuliert ein Modul: parst gesendete Rahmen und antwortet mit ACKs. */
#include <string.h>

#include <unity.h>

#include "moduleupdate.h"
#include "protocol.h"

void setUp(void) {}
void tearDown(void) {}

/* --- simuliertes Modul ------------------------------------------------ */
static moduleupdate_t g_mu;
static uint32_t       g_now;
static uint8_t        g_img[400];
static uint16_t       g_crc;

/* Verhalten des simulierten Moduls */
static bool  g_online = true;
static int   g_nak_begin = 0;     /* so oft FW_BEGIN mit nak beantworten  */
static int   g_drop_data = -1;    /* diesen DATA-Frame gar nicht antworten */
static int   g_data_seen = 0;
static bool  g_confirm_valid = true;
static uint16_t g_confirm_ver = 0x0102;
static uint32_t g_recv_bytes = 0;

static uint16_t crc16(const uint8_t *d, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
    }
    return c;
}

static void feed_ack(uint8_t cmd, uint8_t addr, uint8_t code)
{
    uint8_t pl = code;
    moduleupdate_on_frame(&g_mu, cmd, addr, &pl, 1, g_now);
}

/* Sende-Callback: parst den Rahmen und laesst das "Modul" reagieren. */
static void tx_cb(void *ctx, const uint8_t *frame, size_t len)
{
    (void)ctx;
    proto_parser_t p;
    proto_parser_reset(&p);
    proto_parse_result_t r = PARSE_NEED_MORE;
    for (size_t i = 0; i < len; i++) r = proto_parser_feed(&p, frame[i]);
    if (r != PARSE_FRAME_OK) return;
    const proto_frame_t *f = &p.frame;
    if (!g_online) return;

    switch (f->cmd) {
    case CMD_ENTER_BOOTLOADER:
        break;
    case CMD_FW_BEGIN:
        g_recv_bytes = 0;
        if (g_nak_begin > 0) { g_nak_begin--; feed_ack(CMD_FW_BEGIN, f->addr, 0x00); }
        else feed_ack(CMD_FW_BEGIN, f->addr, 0x01);
        break;
    case CMD_FW_DATA: {
        int idx = g_data_seen++;
        if (idx == g_drop_data) break;   /* Antwort verschlucken -> Retry */
        uint32_t off = f->payload[0] | (f->payload[1] << 8);
        uint8_t n = (uint8_t)(f->payload_len - 2);
        if (off == g_recv_bytes) g_recv_bytes += n;
        feed_ack(CMD_FW_DATA, f->addr, off == 0 || off <= g_recv_bytes ? 0x01 : 0x00);
        break;
    }
    case CMD_FW_END:
        feed_ack(CMD_FW_END, f->addr, 0x01);
        break;
    case CMD_GET_VERSION: {
        uint8_t pl[5] = { 1, (uint8_t)(g_confirm_ver >> 8), (uint8_t)g_confirm_ver,
                          (uint8_t)(g_confirm_valid ? 0x02 : 0x00), 1 };
        moduleupdate_on_frame(&g_mu, CMD_GET_VERSION, f->addr, pl, 5, g_now);
        break;
    }
    default: break;
    }
}

static void setup_case(uint32_t img_len)
{
    for (uint32_t i = 0; i < img_len; i++) g_img[i] = (uint8_t)((i * 7u + 3u) & 0xFF);
    g_crc = crc16(g_img, img_len);
    g_now = 0;
    g_online = true;
    g_nak_begin = 0;
    g_drop_data = -1;
    g_data_seen = 0;
    g_confirm_valid = true;
    g_confirm_ver = 0x0102;
    g_recv_bytes = 0;
    moduleupdate_init(&g_mu, tx_cb, NULL, g_img, img_len, g_crc, 0x0102);
}

/* Uhr vorstellen + ticken, bis fertig oder Deadlock. */
static void run(uint32_t max_ms)
{
    for (uint32_t t = 0; t < max_ms && moduleupdate_busy(&g_mu); t += 50) {
        g_now = t;
        moduleupdate_tick(&g_mu, g_now);
    }
}

static void test_happy_path_one_module(void)
{
    setup_case(200);
    moduleupdate_enqueue(&g_mu, 1u << 2 /* Adresse 3 */, 1u << 2);
    TEST_ASSERT_TRUE(moduleupdate_busy(&g_mu));
    run(60000);
    TEST_ASSERT_FALSE(moduleupdate_busy(&g_mu));
    TEST_ASSERT_EQUAL(MU_RES_OK, g_mu.result[3]);
    TEST_ASSERT_EQUAL_UINT8(1, g_mu.done_ok);
    TEST_ASSERT_EQUAL_UINT8(0, g_mu.done_fail);
    TEST_ASSERT_EQUAL_UINT32(200, g_recv_bytes);
}

static void test_flash_all_multiple(void)
{
    setup_case(130);
    uint32_t mask = (1u << 0) | (1u << 1) | (1u << 4);  /* Adressen 1,2,5 */
    moduleupdate_enqueue(&g_mu, mask, mask);
    run(120000);
    TEST_ASSERT_FALSE(moduleupdate_busy(&g_mu));
    TEST_ASSERT_EQUAL(MU_RES_OK, g_mu.result[1]);
    TEST_ASSERT_EQUAL(MU_RES_OK, g_mu.result[2]);
    TEST_ASSERT_EQUAL(MU_RES_OK, g_mu.result[5]);
    TEST_ASSERT_EQUAL_UINT8(3, g_mu.done_ok);
}

static void test_offline_skipped(void)
{
    setup_case(64);
    /* Adresse 4 angefragt, aber nicht online */
    moduleupdate_enqueue(&g_mu, (1u << 3) | (1u << 0), (1u << 0));
    run(60000);
    TEST_ASSERT_EQUAL(MU_RES_SKIPPED, g_mu.result[4]);
    TEST_ASSERT_EQUAL(MU_RES_OK, g_mu.result[1]);
}

static void test_begin_nak_then_recover(void)
{
    setup_case(96);
    g_nak_begin = 2;   /* zweimal nak, dann ok */
    moduleupdate_enqueue(&g_mu, 1u << 0, 1u << 0);
    run(60000);
    TEST_ASSERT_EQUAL(MU_RES_OK, g_mu.result[1]);
}

static void test_data_timeout_retry(void)
{
    setup_case(150);
    g_drop_data = 2;   /* dritter DATA-Frame ohne Antwort -> Timeout -> Retry */
    moduleupdate_enqueue(&g_mu, 1u << 0, 1u << 0);
    run(120000);
    TEST_ASSERT_EQUAL(MU_RES_OK, g_mu.result[1]);
}

static void test_confirm_wrong_version_fails(void)
{
    setup_case(64);
    g_confirm_ver = 0x0999;   /* stimmt nicht mit target_ver 0x0102 */
    moduleupdate_enqueue(&g_mu, 1u << 0, 1u << 0);
    run(60000);
    TEST_ASSERT_EQUAL(MU_RES_FAILED, g_mu.result[1]);
    TEST_ASSERT_EQUAL_UINT8(1, g_mu.done_fail);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_happy_path_one_module);
    RUN_TEST(test_flash_all_multiple);
    RUN_TEST(test_offline_skipped);
    RUN_TEST(test_begin_nak_then_recover);
    RUN_TEST(test_data_timeout_retry);
    RUN_TEST(test_confirm_wrong_version_fails);
    return UNITY_END();
}
