/* Tests fuer die EEPROM-Konfiguration, siehe config.c / Spezifikation 6.3. */
#include <string.h>

#include <unity.h>

#include "config.h"

void setUp(void) {}
void tearDown(void) {}

static void expect_cfg(const module_config_t *c, uint8_t blattzahl, uint8_t offset,
                       uint8_t vorhalt, uint8_t flags, uint8_t addr, uint8_t t_enum)
{
    TEST_ASSERT_EQUAL_UINT8(blattzahl, c->blattzahl);
    TEST_ASSERT_EQUAL_UINT8(offset, c->blatt_offset);
    TEST_ASSERT_EQUAL_UINT8(vorhalt, c->abschaltvorhalt_ms);
    TEST_ASSERT_EQUAL_HEX8(flags, c->flags);
    TEST_ASSERT_EQUAL_UINT8(addr, c->bus_address);
    TEST_ASSERT_EQUAL_UINT8(t_enum, c->t_enum_s);
}

static void test_defaults(void)
{
    module_config_t c;
    config_defaults(&c);
    expect_cfg(&c, 40, 0, 0, 0x03, 0, 10);
    TEST_ASSERT_TRUE(config_flag(&c, CONFIG_FLAG_POSITION_SAVE));
    TEST_ASSERT_TRUE(config_flag(&c, CONFIG_FLAG_AUTOHOME));
    TEST_ASSERT_FALSE(config_flag(&c, CONFIG_FLAG_RESERVED_BIT2));
}

static void test_bytes_roundtrip(void)
{
    const uint8_t raw[CONFIG_SIZE] = { 64, 12, 25, 0x07, 42, 15 };
    module_config_t c;
    config_from_bytes(&c, raw);
    expect_cfg(&c, 64, 12, 25, 0x07, 42, 15);

    uint8_t out[CONFIG_SIZE];
    config_to_bytes(&c, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(raw, out, CONFIG_SIZE);
}

static void test_validate_ok(void)
{
    module_config_t c = { 80, 79, 60, 0x00, 250, 60 };
    TEST_ASSERT_TRUE(config_validate(&c));
    module_config_t d = { 40, 0, 0, 0x03, 1, 10 };
    TEST_ASSERT_TRUE(config_validate(&d));
}

static void test_validate_fixes_blattzahl(void)
{
    module_config_t c = { 41, 0, 0, 0, 0, 10 };
    TEST_ASSERT_FALSE(config_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(40, c.blattzahl);
}

/* Geaendert gegenueber v1.15: Vorhalt > 60 faellt auf die Vorgabe 0 statt auf
 * die Obergrenze 60 (#18, 60 hiesse "Gate sofort aus" bei jedem Restblatt). */
static void test_validate_clamps(void)
{
    module_config_t c = { 40, 200, 100, 0, 255, 0 };
    TEST_ASSERT_FALSE(config_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(0, c.blatt_offset);     /* >= blattzahl -> 0 */
    TEST_ASSERT_EQUAL_UINT8(0, c.abschaltvorhalt_ms);
    TEST_ASSERT_EQUAL_UINT8(0, c.bus_address);      /* > 250 -> 0 */
    TEST_ASSERT_EQUAL_UINT8(1, c.t_enum_s);         /* < 1 -> 1 */
}

/* Geaendert gegenueber v1.15: der alte Test verlangte fuer ein leeres EEPROM
 * T_enum 60 und liess Flags 0xFF (inkl. Bit 2) stehen -- genau der Fehler
 * aus #18. Jetzt: komplette Vorgaben. */
static void test_validate_empty_eeprom(void)
{
    uint8_t raw[CONFIG_SIZE];
    memset(raw, 0xFF, sizeof(raw));   /* frische EEPROM-Zellen */
    module_config_t c;
    config_from_bytes(&c, raw);
    TEST_ASSERT_FALSE(config_validate(&c));
    expect_cfg(&c, 40, 0, 0, 0x03, 0, 10);
}

/* Bestandskarte mit 0xFF-Flags (Werksflash) und schon vergebener Adresse:
 * Vorgaben, Adresse bleibt. */
static void test_validate_ff_flags_keeps_address(void)
{
    const uint8_t raw[CONFIG_SIZE] = { 40, 0, 60, 0xFF, 3, 60 };
    module_config_t c;
    config_from_bytes(&c, raw);
    TEST_ASSERT_FALSE(config_validate(&c));
    expect_cfg(&c, 40, 0, 0, 0x03, 3, 10);
}

static void test_validate_unknown_bits_drop_invalid_address(void)
{
    module_config_t c = { 64, 10, 20, 0x80, 251, 5 };
    TEST_ASSERT_FALSE(config_validate(&c));
    expect_cfg(&c, 40, 0, 0, 0x03, 0, 10);
}

/* Flags 0x07 aus der UI-Rundreise: nur Bit 2 faellt weg, der Rest bleibt. */
static void test_validate_masks_reserved_bit2(void)
{
    const uint8_t raw[CONFIG_SIZE] = { 40, 5, 60, 0x07, 1, 60 };
    module_config_t c;
    config_from_bytes(&c, raw);
    TEST_ASSERT_FALSE(config_validate(&c));      /* -> load_config schreibt zurueck */
    expect_cfg(&c, 40, 5, 60, 0x03, 1, 60);

    module_config_t d = { 40, 0, 0, CONFIG_FLAG_RESERVED_BIT2, 0, 10 };
    TEST_ASSERT_FALSE(config_validate(&d));
    TEST_ASSERT_EQUAL_HEX8(0x00, d.flags);
}

/* Nach einer Korrektur ist das Abbild stabil (Migration schreibt genau einmal). */
static void test_validate_converges(void)
{
    const uint8_t images[][CONFIG_SIZE] = {
        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
        { 40, 0, 60, 0xFF, 3, 60 },
        { 40, 5, 60, 0x07, 1, 60 },
        { 41, 99, 200, 0x04, 255, 0 },
    };
    for (unsigned i = 0; i < sizeof(images) / sizeof(images[0]); ++i) {
        module_config_t c;
        config_from_bytes(&c, images[i]);
        (void)config_validate(&c);
        TEST_ASSERT_TRUE(config_validate(&c));
        TEST_ASSERT_EQUAL_HEX8(0x00, c.flags & (uint8_t)~CONFIG_FLAGS_VALID_MASK);
    }
}

static void test_position_store_active(void)
{
    module_config_t c;
    config_defaults(&c);
    c.flags = CONFIG_FLAG_POSITION_SAVE | CONFIG_FLAG_AUTOHOME;
    TEST_ASSERT_FALSE(config_position_store_active(&c));   /* Autohoming: nie genutzt */
    c.flags = CONFIG_FLAG_POSITION_SAVE;
    TEST_ASSERT_TRUE(config_position_store_active(&c));
    c.flags = 0;
    TEST_ASSERT_FALSE(config_position_store_active(&c));
    c.flags = CONFIG_FLAG_AUTOHOME;
    TEST_ASSERT_FALSE(config_position_store_active(&c));
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
    RUN_TEST(test_validate_ff_flags_keeps_address);
    RUN_TEST(test_validate_unknown_bits_drop_invalid_address);
    RUN_TEST(test_validate_masks_reserved_bit2);
    RUN_TEST(test_validate_converges);
    RUN_TEST(test_position_store_active);
    return UNITY_END();
}
