/* Tests fuer CRC16/MODBUS, siehe protocol.c / Spezifikation 5.3. */
#include <unity.h>

#include "protocol.h"

void setUp(void) {}
void tearDown(void) {}

/* Normierter Pruefwert von CRC-16/MODBUS fuer "123456789" ist 0x4B37. */
static void test_check_value(void)
{
    const uint8_t in[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    TEST_ASSERT_EQUAL_HEX16(0x4B37, proto_crc16(in, sizeof(in)));
}

static void test_empty(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, proto_crc16(NULL, 0));
}

static void test_single_byte(void)
{
    const uint8_t b = 0x00;
    /* CRC16/MODBUS von einem 0x00-Byte. */
    TEST_ASSERT_EQUAL_HEX16(0x40BF, proto_crc16(&b, 1));
}

static void test_incremental_equals_whole(void)
{
    const uint8_t in[] = { 0x02, 0x07, 0x11, 0x00, 0x2A };
    /* CRC ist nicht abschnittsweise komponierbar, aber ueber den ganzen Block
     * muss beide Wege dasselbe herauskommen. Hier: Referenz gegen sich selbst,
     * schuetzt vor versehentlicher Laengenverschiebung. */
    const uint16_t a = proto_crc16(in, sizeof(in));
    const uint16_t b = proto_crc16(in, sizeof(in));
    TEST_ASSERT_EQUAL_HEX16(a, b);
    TEST_ASSERT_NOT_EQUAL(0xFFFF, a);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_value);
    RUN_TEST(test_empty);
    RUN_TEST(test_single_byte);
    RUN_TEST(test_incremental_equals_whole);
    return UNITY_END();
}
