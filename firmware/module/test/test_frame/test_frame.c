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

/* Alle Bytes fuettern; zaehlt Fehler und gueltige Rahmen (#56). */
static void feed_count(proto_parser_t *p, const uint8_t *buf, size_t n,
                       unsigned *errs, unsigned *oks)
{
    for (size_t i = 0; i < n; ++i) {
        const proto_parse_result_t r = proto_parser_feed(p, buf[i]);
        if (r == PARSE_FRAME_OK) {
            (*oks)++;
        } else if (r != PARSE_NEED_MORE) {
            (*errs)++;
        }
    }
}

/* Rahmen A verliert ein Byte und verschluckt so den Anfang von B: B wird im
 * Neuaufsatz aus den schon gelesenen Bytes gefunden. */
static void test_resync_recovers_swallowed_frame(void)
{
    proto_frame_t f;
    uint8_t a[PROTO_MAX_FRAME], b[PROTO_MAX_FRAME], bus[2 * PROTO_MAX_FRAME];
    const uint8_t st[8] = { 4, 4, 0, 0, 40, 0, 0, 1 };
    const size_t na = make(&f, CMD_GET_STATUS, 1, st, 8, a);
    const uint8_t pl[1] = { 9 };
    const size_t nb = make(&f, CMD_SET, 2, pl, 1, b);
    size_t n = 0;
    for (size_t i = 0; i < na; ++i) {
        if (i != 6) {
            bus[n++] = a[i];                  /* Byte 6 fehlt */
        }
    }
    memcpy(&bus[n], b, nb);
    n += nb;

    proto_parser_t p;
    proto_parser_reset(&p);
    unsigned errs = 0, oks = 0;
    feed_count(&p, bus, n, &errs, &oks);
    TEST_ASSERT_EQUAL_UINT(1, errs);
    TEST_ASSERT_EQUAL_UINT(1, oks);
    TEST_ASSERT_EQUAL_HEX8(CMD_SET, p.frame.cmd);
    TEST_ASSERT_EQUAL_HEX8(2, p.frame.addr);
    TEST_ASSERT_EQUAL_HEX8(9, p.frame.payload[0]);
}

/* Ungueltige Laenge direkt hinter einer Scheinpraeambel: die echte
 * Praeambel steckt im verworfenen LEN-Byte. */
static void test_resync_after_len_error(void)
{
    proto_frame_t f;
    uint8_t b[PROTO_MAX_FRAME], bus[PROTO_MAX_FRAME + 2];
    const size_t nb = make(&f, CMD_GET_STATUS, 1, NULL, 0, b);
    bus[0] = PROTO_PREAMBLE_0;
    bus[1] = PROTO_PREAMBLE_1;
    memcpy(&bus[2], b, nb);                   /* AA 55 | AA 55 04 10 01 .. */

    proto_parser_t p;
    proto_parser_reset(&p);
    unsigned errs = 0, oks = 0;
    feed_count(&p, bus, nb + 2u, &errs, &oks);
    TEST_ASSERT_EQUAL_UINT(1, errs);          /* LEN 0xAA */
    TEST_ASSERT_EQUAL_UINT(1, oks);
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_STATUS, p.frame.cmd);
}

/* Ein vollstaendiger Rahmen mitten in einem Scheinrahmen. */
static void test_resync_finds_embedded_frame(void)
{
    proto_frame_t f;
    uint8_t b[PROTO_MAX_FRAME], bus[3 + PROTO_MAX_FRAME + 8];
    const uint8_t pl[1] = { 33 };
    const size_t nb = make(&f, CMD_SET, 4, pl, 1, b);
    size_t n = 0;
    bus[n++] = PROTO_PREAMBLE_0;
    bus[n++] = PROTO_PREAMBLE_1;
    const uint8_t fake_len = (uint8_t)(PROTO_LEN_OVERHEAD + nb + 3u);   /* schliesst B ein */
    bus[n++] = fake_len;
    memcpy(&bus[n], b, nb);
    n += nb;
    while (n < 3u + fake_len) {
        bus[n++] = 0x11;                      /* Rest des Scheinrahmens */
    }

    proto_parser_t p;
    proto_parser_reset(&p);
    unsigned errs = 0, oks = 0;
    feed_count(&p, bus, n, &errs, &oks);
    TEST_ASSERT_EQUAL_UINT(1, oks);
    TEST_ASSERT_EQUAL_UINT(0, errs);          /* Fund ersetzt den CRC-Fehler */
    TEST_ASSERT_EQUAL_HEX8(4, p.frame.addr);
    TEST_ASSERT_EQUAL_HEX8(33, p.frame.payload[0]);
}

/* Reiner Muell ohne Praeambel nach einem CRC-Fehler: kein Fund, Parser
 * danach sofort wieder bereit. */
static void test_resync_garbage_then_clean_frame(void)
{
    proto_frame_t f;
    uint8_t a[PROTO_MAX_FRAME], b[PROTO_MAX_FRAME];
    const size_t na = make(&f, CMD_GET_STATUS, 1, NULL, 0, a);
    a[na - 1] ^= 0xFFu;
    const uint8_t pl[1] = { 5 };
    const size_t nb = make(&f, CMD_SET, 3, pl, 1, b);
    proto_parser_t p;
    proto_parser_reset(&p);
    unsigned errs = 0, oks = 0;
    feed_count(&p, a, na, &errs, &oks);
    TEST_ASSERT_EQUAL_UINT(1, errs);
    TEST_ASSERT_EQUAL_UINT(0, oks);
    feed_count(&p, b, nb, &errs, &oks);
    TEST_ASSERT_EQUAL_UINT(1, oks);
    TEST_ASSERT_EQUAL_HEX8(5, p.frame.payload[0]);
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
    RUN_TEST(test_resync_recovers_swallowed_frame);
    RUN_TEST(test_resync_after_len_error);
    RUN_TEST(test_resync_finds_embedded_frame);
    RUN_TEST(test_resync_garbage_then_clean_frame);
    return UNITY_END();
}
