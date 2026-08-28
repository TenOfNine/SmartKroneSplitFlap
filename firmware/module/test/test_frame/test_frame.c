/* Tests fuer Rahmenaufbau und Streaming-Parser, siehe protocol.c / Spez. 5.3. */
#include <string.h>

#include <unity.h>

#include "protocol.h"

void setUp(void) {}
void tearDown(void) {}

static size_t make(proto_frame_t *f, uint8_t cmd, uint8_t addr,
                   const uint8_t *pl, uint8_t pl_len, uint8_t *out)
{
    memset(f, 0, sizeof(*f));
    f->cmd = cmd;
    f->addr = addr;
    f->payload_len = pl_len;
    if (pl_len) {
        memcpy(f->payload, pl, pl_len);
    }
    return proto_encode(f, out, PROTO_MAX_FRAME);
}

/* Fuettert Bytes und haelt beim ersten Ergebnis != NEED_MORE an. */
static proto_parse_result_t feed_all(proto_parser_t *p, const uint8_t *buf, size_t n)
{
    proto_parse_result_t r = PARSE_NEED_MORE;
    for (size_t i = 0; i < n && r == PARSE_NEED_MORE; ++i) {
        r = proto_parser_feed(p, buf[i]);
    }
    return r;
}

static void test_encode_layout(void)
{
    proto_frame_t f;
    uint8_t buf[PROTO_MAX_FRAME];
    const uint8_t pl[1] = { 40 };
    const size_t n = make(&f, CMD_SET, 3, pl, 1, buf);

    TEST_ASSERT_EQUAL_size_t(8, n);            /* 2 + LEN + CMD + ADDR + 1 + 2 */
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x55, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(5, buf[2]);         /* LEN = payload(1) + overhead(4) */
    TEST_ASSERT_EQUAL_HEX8(CMD_SET, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(3, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(40, buf[5]);

    const uint16_t crc = proto_crc16(&buf[2], 4); /* LEN, CMD, ADDR, PAYLOAD */
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc & 0xFF), buf[6]);  /* Little Endian */
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), buf[7]);
}

static void test_roundtrip(void)
{
    proto_frame_t f;
    uint8_t buf[PROTO_MAX_FRAME];
    const uint8_t pl[4] = { 0x28, 0x00, 0x05, 0x03 };
    const size_t n = make(&f, CMD_SET_CONFIG, 12, pl, 4, buf);

    proto_parser_t p;
    proto_parser_reset(&p);
    TEST_ASSERT_EQUAL(PARSE_FRAME_OK, feed_all(&p, buf, n));
    TEST_ASSERT_EQUAL_HEX8(CMD_SET_CONFIG, p.frame.cmd);
    TEST_ASSERT_EQUAL_HEX8(12, p.frame.addr);
    TEST_ASSERT_EQUAL_UINT8(4, p.frame.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pl, p.frame.payload, 4);
}

static void test_empty_payload(void)
{
    proto_frame_t f;
    uint8_t buf[PROTO_MAX_FRAME];
    const size_t n = make(&f, CMD_GO, PROTO_ADDR_BROADCAST, NULL, 0, buf);
    TEST_ASSERT_EQUAL_size_t(7, n);

    proto_parser_t p;
    proto_parser_reset(&p);
    TEST_ASSERT_EQUAL(PARSE_FRAME_OK, feed_all(&p, buf, n));
    TEST_ASSERT_EQUAL_UINT8(0, p.frame.payload_len);
    TEST_ASSERT_EQUAL_HEX8(CMD_GO, p.frame.cmd);
}

static void test_max_payload(void)
{
    proto_frame_t f;
    uint8_t buf[PROTO_MAX_FRAME];
    uint8_t pl[PROTO_MAX_PAYLOAD];
    for (unsigned i = 0; i < PROTO_MAX_PAYLOAD; ++i) {
        pl[i] = (uint8_t)(i + 1);
    }
    const size_t n = make(&f, CMD_SET_ALL, PROTO_ADDR_BROADCAST, pl,
                          PROTO_MAX_PAYLOAD, buf);
    TEST_ASSERT_EQUAL_size_t(PROTO_MAX_FRAME, n);

    proto_parser_t p;
    proto_parser_reset(&p);
    TEST_ASSERT_EQUAL(PARSE_FRAME_OK, feed_all(&p, buf, n));
    TEST_ASSERT_EQUAL_UINT8(PROTO_MAX_PAYLOAD, p.frame.payload_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pl, p.frame.payload, PROTO_MAX_PAYLOAD);
}

static void test_payload_too_large_rejected_on_encode(void)
{
    proto_frame_t f;
    memset(&f, 0, sizeof(f));
    f.cmd = CMD_SET_ALL;
    f.payload_len = PROTO_MAX_PAYLOAD + 1;
    uint8_t buf[PROTO_MAX_FRAME];
    TEST_ASSERT_EQUAL_size_t(0, proto_encode(&f, buf, sizeof(buf)));
}

static void test_bad_crc(void)
{
    proto_frame_t f;
    uint8_t buf[PROTO_MAX_FRAME];
    const uint8_t pl[1] = { 10 };
    const size_t n = make(&f, CMD_SET, 1, pl, 1, buf);
    buf[n - 1] ^= 0xFF;  /* CRC-High kippen */

    proto_parser_t p;
    proto_parser_reset(&p);
    TEST_ASSERT_EQUAL(PARSE_ERR_CRC, feed_all(&p, buf, n));
}

static void test_bad_len(void)
{
    /* Praeambel korrekt, LEN unter dem Minimum (3 < PROTO_LEN_OVERHEAD). */
    const uint8_t buf[] = { 0xAA, 0x55, 0x03, 0x01, 0x01, 0x00, 0x00 };
    proto_parser_t p;
    proto_parser_reset(&p);
    TEST_ASSERT_EQUAL(PARSE_ERR_LEN, feed_all(&p, buf, sizeof(buf)));
}

static void test_resync_after_garbage(void)
{
    proto_frame_t f;
    uint8_t frame[PROTO_MAX_FRAME];
    const uint8_t pl[1] = { 7 };
    const size_t n = make(&f, CMD_SET, 2, pl, 1, frame);

    proto_parser_t p;
    proto_parser_reset(&p);

    const uint8_t junk[] = { 0x00, 0xFF, 0xAA, 0x12, 0x55, 0x01 };
    TEST_ASSERT_EQUAL(PARSE_NEED_MORE, feed_all(&p, junk, sizeof(junk)));
    TEST_ASSERT_EQUAL(PARSE_FRAME_OK, feed_all(&p, frame, n));
    TEST_ASSERT_EQUAL_HEX8(2, p.frame.addr);
    TEST_ASSERT_EQUAL_HEX8(7, p.frame.payload[0]);
}

static void test_two_frames_back_to_back(void)
{
    proto_frame_t f;
    uint8_t a[PROTO_MAX_FRAME], b[PROTO_MAX_FRAME];
    const uint8_t p1[1] = { 1 }, p2[1] = { 40 };
    const size_t na = make(&f, CMD_SET, 1, p1, 1, a);
    const size_t nb = make(&f, CMD_SET, 2, p2, 1, b);

    proto_parser_t p;
    proto_parser_reset(&p);
    TEST_ASSERT_EQUAL(PARSE_FRAME_OK, feed_all(&p, a, na));
    TEST_ASSERT_EQUAL_HEX8(1, p.frame.payload[0]);
    TEST_ASSERT_EQUAL(PARSE_FRAME_OK, feed_all(&p, b, nb));
    TEST_ASSERT_EQUAL_HEX8(40, p.frame.payload[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_encode_layout);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_empty_payload);
    RUN_TEST(test_max_payload);
    RUN_TEST(test_payload_too_large_rejected_on_encode);
    RUN_TEST(test_bad_crc);
    RUN_TEST(test_bad_len);
    RUN_TEST(test_resync_after_garbage);
    RUN_TEST(test_two_frames_back_to_back);
    return UNITY_END();
}
