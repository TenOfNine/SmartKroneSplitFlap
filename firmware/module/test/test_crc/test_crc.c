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

/* Rahmen aus Logic-Analyzer-Mitschnitten am Geraet (#16, #20): die CRC
 * ueber LEN, CMD, ADDR und Nutzlast steht dort low-Byte zuerst hinter dem
 * Rahmen. Unabhaengige Referenz statt Vergleich der Funktion mit sich selbst. */
static void test_frames_from_bus_capture(void)
{
    const uint8_t poll2[] = { 0x04, 0x10, 0x02 };              /* AA 55 04 10 02 BC 00 */
    TEST_ASSERT_EQUAL_HEX16(0x00BC, proto_crc16(poll2, sizeof(poll2)));
    const uint8_t poll1[] = { 0x04, 0x10, 0x01 };              /* AA 55 04 10 01 FC 01 */
    TEST_ASSERT_EQUAL_HEX16(0x01FC, proto_crc16(poll1, sizeof(poll1)));
    const uint8_t done[] = { 0x04, 0x52, 0x00 };               /* AA 55 04 52 00 0D 61 */
    TEST_ASSERT_EQUAL_HEX16(0x610D, proto_crc16(done, sizeof(done)));
    const uint8_t ack[] = { 0x04, 0x51, 0x02 };                /* AA 55 04 51 02 8C 50 */
    TEST_ASSERT_EQUAL_HEX16(0x508C, proto_crc16(ack, sizeof(ack)));
    /* Statusantwort Karte 2: AA 55 0C 10 02 00 01 03 02 00 00 00 01 89 B2 */
    const uint8_t st[] = { 0x0C, 0x10, 0x02, 0x00, 0x01, 0x03, 0x02, 0x00, 0x00, 0x00, 0x01 };
    TEST_ASSERT_EQUAL_HEX16(0xB289, proto_crc16(st, sizeof(st)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_value);
    RUN_TEST(test_empty);
    RUN_TEST(test_single_byte);
    RUN_TEST(test_frames_from_bus_capture);
    return UNITY_END();
}
