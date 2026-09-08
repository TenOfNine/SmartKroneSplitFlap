/* Tests fuer den .kota-Container-Parser, siehe lib/otaverify. */
#include <string.h>

#include <unity.h>

#include "otaverify.h"

void setUp(void) {}
void tearDown(void) {}

/* Ein formal gueltiger Header mit img_len = 4096. */
static void good_header(uint8_t *b)
{
    memset(b, 0, OTA_HEADER_LEN);
    memcpy(b, OTA_MAGIC, 4);
    b[4] = OTA_FORMAT_VER;
    b[8] = 0x00; b[9] = 0x10; b[10] = 0x00; b[11] = 0x00;   /* 4096 LE */
    for (int i = 0; i < 32; i++) b[12 + i] = (uint8_t)(i + 1);
    for (int i = 0; i < 64; i++) b[44 + i] = (uint8_t)(0x80 + i);
}

static void test_ok(void)
{
    uint8_t b[OTA_HEADER_LEN];
    ota_header_t h;
    good_header(b);
    TEST_ASSERT_EQUAL_INT(OTA_HDR_OK, otaverify_parse_header(b, sizeof(b), 0x100000, &h));
    TEST_ASSERT_EQUAL_UINT32(4096, h.img_len);
    TEST_ASSERT_EQUAL_UINT8(1, h.img_sha256[0]);
    TEST_ASSERT_EQUAL_UINT8(32, h.img_sha256[31]);
    TEST_ASSERT_EQUAL_UINT8(0x80, h.sig[0]);
}

static void test_short_buffer(void)
{
    uint8_t b[OTA_HEADER_LEN];
    ota_header_t h;
    good_header(b);
    TEST_ASSERT_EQUAL_INT(OTA_HDR_SHORT,
                          otaverify_parse_header(b, OTA_HEADER_LEN - 1, 0x100000, &h));
}

static void test_bad_magic(void)
{
    uint8_t b[OTA_HEADER_LEN];
    ota_header_t h;
    good_header(b);
    b[1] = 'X';
    TEST_ASSERT_EQUAL_INT(OTA_HDR_BAD_MAGIC,
                          otaverify_parse_header(b, sizeof(b), 0x100000, &h));
}

static void test_bad_version(void)
{
    uint8_t b[OTA_HEADER_LEN];
    ota_header_t h;
    good_header(b);
    b[4] = 2;
    TEST_ASSERT_EQUAL_INT(OTA_HDR_BAD_VERSION,
                          otaverify_parse_header(b, sizeof(b), 0x100000, &h));
}

static void test_zero_length(void)
{
    uint8_t b[OTA_HEADER_LEN];
    ota_header_t h;
    good_header(b);
    b[8] = b[9] = b[10] = b[11] = 0;
    TEST_ASSERT_EQUAL_INT(OTA_HDR_BAD_LENGTH,
                          otaverify_parse_header(b, sizeof(b), 0x100000, &h));
}

static void test_length_over_partition(void)
{
    uint8_t b[OTA_HEADER_LEN];
    ota_header_t h;
    good_header(b);
    /* img_len = 0x00200000 (2 MiB) > max_img 0x100000 */
    b[8] = 0; b[9] = 0; b[10] = 0x20; b[11] = 0;
    TEST_ASSERT_EQUAL_INT(OTA_HDR_BAD_LENGTH,
                          otaverify_parse_header(b, sizeof(b), 0x100000, &h));
}

static void test_max_img_zero_skips_check(void)
{
    uint8_t b[OTA_HEADER_LEN];
    ota_header_t h;
    good_header(b);
    b[8] = 0; b[9] = 0; b[10] = 0x20; b[11] = 0;
    TEST_ASSERT_EQUAL_INT(OTA_HDR_OK, otaverify_parse_header(b, sizeof(b), 0, &h));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ok);
    RUN_TEST(test_short_buffer);
    RUN_TEST(test_bad_magic);
    RUN_TEST(test_bad_version);
    RUN_TEST(test_zero_length);
    RUN_TEST(test_length_over_partition);
    RUN_TEST(test_max_img_zero_skips_check);
    return UNITY_END();
}
