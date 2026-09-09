/* Tests fuer den empfangsseitigen Seiten-Sammler, siehe lib/fwupdate. */
#include <string.h>

#include <unity.h>

#include "fwupdate.h"

void setUp(void) {}
void tearDown(void) {}

/* Mitschreiben, was der Callback bekommt. */
#define MAXPAGES 32
static uint8_t  g_written[MAXPAGES][FWUPDATE_PAGE];
static uint32_t g_addr[MAXPAGES];
static int      g_npages;
static bool     g_fail_at_page = false;
static int      g_fail_page = -1;

static bool sink(void *ctx, uint32_t addr, const uint8_t *page)
{
    (void)ctx;
    if (g_fail_at_page && g_npages == g_fail_page) return false;
    if (g_npages < MAXPAGES) {
        g_addr[g_npages] = addr;
        memcpy(g_written[g_npages], page, FWUPDATE_PAGE);
    }
    g_npages++;
    return true;
}

static void reset_sink(void)
{
    memset(g_written, 0, sizeof(g_written));
    memset(g_addr, 0, sizeof(g_addr));
    g_npages = 0;
    g_fail_at_page = false;
    g_fail_page = -1;
}

static uint16_t crc16(const uint8_t *d, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
    }
    return c;
}

/* Bild in Bruchstuecken von `chunk` Byte einspeisen. */
static fwupdate_result_t feed(fwupdate_t *fu, const uint8_t *img, uint32_t len, uint8_t chunk)
{
    for (uint32_t off = 0; off < len; off += chunk) {
        uint8_t n = (uint8_t)((len - off < chunk) ? (len - off) : chunk);
        fwupdate_result_t r = fwupdate_chunk(fu, off, img + off, n);
        if (r != FWUPDATE_OK) return r;
    }
    return fwupdate_finish(fu);
}

static void test_exact_pages(void)
{
    reset_sink();
    uint8_t img[128];
    for (int i = 0; i < 128; i++) img[i] = (uint8_t)(i ^ 0x5A);
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, feed(&fu, img, sizeof(img), 30));
    TEST_ASSERT_EQUAL_INT(2, g_npages);
    TEST_ASSERT_EQUAL_UINT32(0, g_addr[0]);
    TEST_ASSERT_EQUAL_UINT32(64, g_addr[1]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img, g_written[0], 64);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img + 64, g_written[1], 64);
}

static void test_partial_last_page_padded(void)
{
    reset_sink();
    uint8_t img[100];
    for (int i = 0; i < 100; i++) img[i] = (uint8_t)(i + 1);
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, feed(&fu, img, sizeof(img), 16));
    TEST_ASSERT_EQUAL_INT(2, g_npages);
    /* zweite Seite: 36 Nutzbytes + 28x 0xFF */
    for (int i = 36; i < 64; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, g_written[1][i]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(img + 64, g_written[1], 36);
}

static void test_realistic_5433(void)
{
    reset_sink();
    static uint8_t img[5433];
    for (size_t i = 0; i < sizeof(img); i++) img[i] = (uint8_t)((i * 31u + 7u) & 0xFF);
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, feed(&fu, img, sizeof(img), 30));
    TEST_ASSERT_EQUAL_INT(85, g_npages);   /* ceil(5433/64) */
}

static void test_crc_mismatch(void)
{
    reset_sink();
    uint8_t img[64];
    memset(img, 0xAB, sizeof(img));
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), 0x1234 /* falsch */);
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_CRC, feed(&fu, img, sizeof(img), 30));
}

static void test_length_mismatch(void)
{
    reset_sink();
    uint8_t img[64];
    memset(img, 0x11, sizeof(img));
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, 80 /* mehr angekuendigt */, crc16(img, 80));
    for (uint32_t o = 0; o < 64; o += 30) {
        uint8_t n = (o + 30 <= 64) ? 30 : (uint8_t)(64 - o);
        fwupdate_chunk(&fu, o, img + o, n);
    }
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_LENGTH, fwupdate_finish(&fu));
}

static void test_out_of_order_rejected(void)
{
    reset_sink();
    uint8_t data[30] = {0};
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, 128, 0);
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 0, data, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_ORDER, fwupdate_chunk(&fu, 60, data, 30)); /* Luecke */
}

static void test_range_exceeded(void)
{
    reset_sink();
    uint8_t data[30] = {0};
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, 40, 0);
    TEST_ASSERT_EQUAL_INT(FWUPDATE_OK, fwupdate_chunk(&fu, 0, data, 30));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_RANGE, fwupdate_chunk(&fu, 30, data, 30)); /* 60 > 40 */
}

static void test_write_failure_propagates(void)
{
    reset_sink();
    g_fail_at_page = true;
    g_fail_page = 1;
    uint8_t img[200];
    memset(img, 7, sizeof(img));
    fwupdate_t fu;
    fwupdate_begin(&fu, sink, NULL, sizeof(img), crc16(img, sizeof(img)));
    TEST_ASSERT_EQUAL_INT(FWUPDATE_ERR_WRITE, feed(&fu, img, sizeof(img), 30));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_exact_pages);
    RUN_TEST(test_partial_last_page_padded);
    RUN_TEST(test_realistic_5433);
    RUN_TEST(test_crc_mismatch);
    RUN_TEST(test_length_mismatch);
    RUN_TEST(test_out_of_order_rejected);
    RUN_TEST(test_range_exceeded);
    RUN_TEST(test_write_failure_propagates);
    return UNITY_END();
}
