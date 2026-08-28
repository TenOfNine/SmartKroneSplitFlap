/* Tests fuer die EEPROM-Konfiguration, siehe config.c / Spezifikation 6.3. */
#include <string.h>

#include <unity.h>

#include "config.h"

void setUp(void) {}
void tearDown(void) {}

static void test_defaults(void)
{
    module_config_t c;
    config_defaults(&c);
    TEST_ASSERT_EQUAL_UINT8(40, c.blattzahl);
    TEST_ASSERT_EQUAL_UINT8(0, c.blatt_offset);
    TEST_ASSERT_EQUAL_UINT8(0, c.abschaltvorhalt_ms);
    TEST_ASSERT_EQUAL_HEX8(0x03, c.flags);
    TEST_ASSERT_EQUAL_UINT8(0, c.bus_address);
    TEST_ASSERT_EQUAL_UINT8(10, c.t_enum_s);
    TEST_ASSERT_TRUE(config_flag(&c, CONFIG_FLAG_POSITION_SAVE));
    TEST_ASSERT_TRUE(config_flag(&c, CONFIG_FLAG_AUTOHOME));
    TEST_ASSERT_FALSE(config_flag(&c, CONFIG_FLAG_TRIAC_INVERT));
}

static void test_bytes_roundtrip(void)
{
    const uint8_t raw[CONFIG_SIZE] = { 64, 12, 25, 0x07, 42, 15 };
    module_config_t c;
    config_from_bytes(&c, raw);
    TEST_ASSERT_EQUAL_UINT8(64, c.blattzahl);
    TEST_ASSERT_EQUAL_UINT8(12, c.blatt_offset);
    TEST_ASSERT_EQUAL_UINT8(25, c.abschaltvorhalt_ms);
    TEST_ASSERT_EQUAL_HEX8(0x07, c.flags);
    TEST_ASSERT_EQUAL_UINT8(42, c.bus_address);
    TEST_ASSERT_EQUAL_UINT8(15, c.t_enum_s);

    uint8_t out[CONFIG_SIZE];
    config_to_bytes(&c, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(raw, out, CONFIG_SIZE);
}

static void test_validate_ok(void)
{
    module_config_t c = { 80, 79, 60, 0x00, 250, 60 };
    TEST_ASSERT_TRUE(config_validate(&c));
}

static void test_validate_fixes_blattzahl(void)
{
    module_config_t c = { 41, 0, 0, 0, 0, 10 };
    TEST_ASSERT_FALSE(config_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(40, c.blattzahl);
}

static void test_validate_clamps(void)
{
    module_config_t c = { 40, 200, 100, 0, 255, 0 };
    TEST_ASSERT_FALSE(config_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(0, c.blatt_offset);     /* >= blattzahl -> 0 */
    TEST_ASSERT_EQUAL_UINT8(60, c.abschaltvorhalt_ms);
    TEST_ASSERT_EQUAL_UINT8(0, c.bus_address);      /* > 250 -> 0 */
    TEST_ASSERT_EQUAL_UINT8(1, c.t_enum_s);         /* < 1 -> 1 */
}

static void test_validate_empty_eeprom(void)
{
    /* Frische EEPROM-Zellen lesen sich als 0xFF. */
    uint8_t raw[CONFIG_SIZE];
    memset(raw, 0xFF, sizeof(raw));
    module_config_t c;
    config_from_bytes(&c, raw);
    TEST_ASSERT_FALSE(config_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(40, c.blattzahl);
    TEST_ASSERT_EQUAL_UINT8(60, c.t_enum_s);        /* 255 -> 60 */
    TEST_ASSERT_EQUAL_UINT8(0, c.bus_address);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_bytes_roundtrip);
    RUN_TEST(test_validate_ok);
    RUN_TEST(test_validate_fixes_blattzahl);
    RUN_TEST(test_validate_clamps);
    RUN_TEST(test_validate_empty_eeprom);
    return UNITY_END();
}
