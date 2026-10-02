/*
 * Tests fuer den Enumerations-Automaten, siehe enumeration.c / Spezifikation 4.5.
 *
 * Vom Backlog T6 ausdruecklich gefordert:
 *   - Kollisionserkennung              -> test_collision_*
 *   - Rueckfall auf die EEPROM-Adresse  -> test_fallback_eeprom_*
 *   - Rueckfall auf die Serviceadresse 250 -> test_fallback_service_*
 */
#include <unity.h>

#include "enumeration.h"
#include "protocol.h"

static enum_fsm_t fsm;

void setUp(void) {}
void tearDown(void) {}

static void reset(uint8_t eeprom_addr, uint8_t t_enum_s)
{
    enum_fsm_init(&fsm, eeprom_addr, t_enum_s);
}

static void send(uint8_t cmd, const uint8_t *pl, uint8_t len, bool chain_in)
{
    enum_fsm_on_frame(&fsm, cmd, PROTO_ADDR_BROADCAST, pl, len, chain_in);
}

/* --- Start / Grundzustand ------------------------------------------------ */

static void test_init_empty_eeprom_is_unaddressed(void)
{
    reset(0, 10);
    TEST_ASSERT_EQUAL(ENUM_UNADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(0, enum_fsm_address(&fsm));
}

static void test_init_with_eeprom_addr_comes_up_addressed(void)
{
    reset(7, 10);
    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(7, enum_fsm_address(&fsm));
    TEST_ASSERT_FALSE(fsm.is_service_addr);
}

static void test_t_enum_is_clamped(void)
{
    reset(0, 0);
    TEST_ASSERT_EQUAL_UINT16(1000, fsm.t_enum_ms);
    reset(0, 200);
    TEST_ASSERT_EQUAL_UINT16(60000, fsm.t_enum_ms);
    reset(0, 5);
    TEST_ASSERT_EQUAL_UINT16(5000, fsm.t_enum_ms);
}

/* --- Regulaere Enumeration --------------------------------------------- */

static void test_enum_reset_enters_enumerating(void)
{
    reset(7, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);
    TEST_ASSERT_EQUAL(ENUM_ENUMERATING, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(0, enum_fsm_address(&fsm));   /* antwortet nicht mehr */
    TEST_ASSERT_FALSE(fsm.chain_out_active);
}

/* Geaendert gegenueber v1.15: EEPROM-Schreiben und CHAIN_OUT folgen nicht
 * mehr direkt auf ENUM_ASSIGN, sondern erst auf das fehlerfrei gesendete ACK
 * (#20, Spez. 4.5.1 Schritt 3: bestaetigen, schreiben, CHAIN_OUT). */
static void test_first_card_takes_assignment(void)
{
    reset(0, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);

    const uint8_t addr[1] = { 1 };
    send(CMD_ENUM_ASSIGN, addr, 1, /*chain_in=*/true);

    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(1, enum_fsm_address(&fsm));
    TEST_ASSERT_TRUE(fsm.want_ack);
    TEST_ASSERT_FALSE(fsm.chain_out_active);      /* noch nicht */
    TEST_ASSERT_FALSE(fsm.want_eeprom_write);     /* noch nicht */
    TEST_ASSERT_EQUAL_UINT8(0, fsm.eeprom_address);

    enum_fsm_on_ack_sent(&fsm, true);
    TEST_ASSERT_TRUE(fsm.chain_out_active);
    TEST_ASSERT_TRUE(fsm.want_eeprom_write);
    TEST_ASSERT_EQUAL_UINT8(1, fsm.eeprom_address);

    enum_fsm_clear_outputs(&fsm);
    TEST_ASSERT_FALSE(fsm.want_ack);
    TEST_ASSERT_FALSE(fsm.want_eeprom_write);
}

/* Echo-Abweichung beim ACK (Feldfehler Position 1, #20): kein EEPROM-Eintrag,
 * kein CHAIN_OUT, Rueckfall auf die alte EEPROM-Adresse nach T_enum. */
static void test_assign_ack_collision_keeps_eeprom_and_chain(void)
{
    reset(5, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);
    const uint8_t a[1] = { 1 };
    send(CMD_ENUM_ASSIGN, a, 1, true);
    enum_fsm_on_ack_sent(&fsm, false);

    TEST_ASSERT_EQUAL(ENUM_UNADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(0, enum_fsm_address(&fsm));
    TEST_ASSERT_FALSE(fsm.chain_out_active);
    TEST_ASSERT_FALSE(fsm.want_eeprom_write);
    TEST_ASSERT_EQUAL_UINT8(5, fsm.eeprom_address);
    TEST_ASSERT_EQUAL_HEX8(ENUM_ERR_COLLISION, fsm.last_error);

    /* ENUM_DONE gilt nur fuer ENUMERATING; die Karte wartet T_enum ab. */
    send(CMD_ENUM_DONE, NULL, 0, false);
    TEST_ASSERT_EQUAL(ENUM_UNADDRESSED, fsm.state);
    enum_fsm_on_tick(&fsm, 10000);
    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(5, enum_fsm_address(&fsm));
    TEST_ASSERT_FALSE(fsm.chain_out_active);
    TEST_ASSERT_FALSE(fsm.want_eeprom_write);
}

static void test_ack_result_ignored_without_assignment(void)
{
    reset(0, 10);
    enum_fsm_on_ack_sent(&fsm, true);
    TEST_ASSERT_EQUAL(ENUM_UNADDRESSED, fsm.state);
    TEST_ASSERT_FALSE(fsm.chain_out_active);
    TEST_ASSERT_FALSE(fsm.want_eeprom_write);

    reset(7, 10);                                 /* ADDRESSED aus dem EEPROM */
    enum_fsm_on_ack_sent(&fsm, true);
    TEST_ASSERT_FALSE(fsm.chain_out_active);
    TEST_ASSERT_FALSE(fsm.want_eeprom_write);
    enum_fsm_on_ack_sent(&fsm, false);
    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state); /* kein Kollisionsweg ohne ACK */
}

static void test_card_without_chain_in_ignores_assignment(void)
{
    reset(0, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);

    const uint8_t a1[1] = { 5 };
    send(CMD_ENUM_ASSIGN, a1, 1, /*chain_in=*/false);
    TEST_ASSERT_EQUAL(ENUM_ENUMERATING, fsm.state);
    TEST_ASSERT_FALSE(fsm.want_ack);

    /* Vordermann hat jetzt CHAIN_OUT aktiviert -> unser CHAIN_IN wird aktiv. */
    const uint8_t a2[1] = { 6 };
    send(CMD_ENUM_ASSIGN, a2, 1, /*chain_in=*/true);
    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(6, enum_fsm_address(&fsm));
}

static void test_second_assignment_is_not_taken_twice(void)
{
    reset(0, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);
    const uint8_t a1[1] = { 1 };
    send(CMD_ENUM_ASSIGN, a1, 1, true);
    enum_fsm_on_ack_sent(&fsm, true);
    enum_fsm_clear_outputs(&fsm);

    /* Naechstes ENUM_ASSIGN gilt der naechsten Karte, nicht uns. */
    const uint8_t a2[1] = { 2 };
    send(CMD_ENUM_ASSIGN, a2, 1, true);
    TEST_ASSERT_EQUAL_UINT8(1, enum_fsm_address(&fsm));
    TEST_ASSERT_FALSE(fsm.want_ack);
}

static void test_enum_assign_bad_address_ignored(void)
{
    reset(0, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);
    const uint8_t bad[1] = { 251 };  /* ausserhalb 1..250 */
    send(CMD_ENUM_ASSIGN, bad, 1, true);
    TEST_ASSERT_EQUAL(ENUM_ENUMERATING, fsm.state);
}

static void test_enum_done_keeps_assigned_card(void)
{
    reset(0, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);
    const uint8_t a1[1] = { 3 };
    send(CMD_ENUM_ASSIGN, a1, 1, true);
    send(CMD_ENUM_DONE, NULL, 0, false);
    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(3, enum_fsm_address(&fsm));
    TEST_ASSERT_FALSE(fsm.is_service_addr);
}

/* --- Rueckfall auf die EEPROM-Adresse (Backlog T6) --------------------- */

static void test_fallback_eeprom_on_timeout(void)
{
    reset(42, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);
    TEST_ASSERT_EQUAL(ENUM_ENUMERATING, fsm.state);

    enum_fsm_on_tick(&fsm, 5000);
    TEST_ASSERT_EQUAL(ENUM_ENUMERATING, fsm.state);   /* noch nicht abgelaufen */
    enum_fsm_on_tick(&fsm, 5000);                     /* jetzt 10 s */

    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(42, enum_fsm_address(&fsm));
    TEST_ASSERT_FALSE(fsm.is_service_addr);
    TEST_ASSERT_FALSE(fsm.want_eeprom_write);         /* kein Schreiben beim Rueckfall */
}

static void test_fallback_eeprom_on_enum_done(void)
{
    reset(9, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);
    send(CMD_ENUM_DONE, NULL, 0, false);   /* greift sofort, 4.5.2 */
    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(9, enum_fsm_address(&fsm));
}

/* --- Rueckfall auf die Serviceadresse 250 (Backlog T6) ---------------- */

static void test_fallback_service_on_timeout(void)
{
    reset(0, 3);   /* fabrikneu, EEPROM leer */
    send(CMD_ENUM_RESET, NULL, 0, false);
    enum_fsm_on_tick(&fsm, 3000);

    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(PROTO_ADDR_SERVICE, enum_fsm_address(&fsm));
    TEST_ASSERT_EQUAL_UINT8(250, PROTO_ADDR_SERVICE);
    TEST_ASSERT_TRUE(fsm.is_service_addr);
    TEST_ASSERT_FALSE(fsm.want_eeprom_write);
}

static void test_fallback_service_on_enum_done(void)
{
    reset(0, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);
    send(CMD_ENUM_DONE, NULL, 0, false);
    TEST_ASSERT_EQUAL_UINT8(PROTO_ADDR_SERVICE, enum_fsm_address(&fsm));
    TEST_ASSERT_TRUE(fsm.is_service_addr);
}

static void test_no_timeout_outside_enumerating(void)
{
    reset(5, 2);
    /* ADDRESSED aus EEPROM, kein ENUM_RESET */
    enum_fsm_on_tick(&fsm, 60000);
    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(5, enum_fsm_address(&fsm));
}

/* --- Kollisionserkennung (Backlog T6) -------------------------------- */

static void test_collision_drops_address(void)
{
    reset(5, 10);   /* ADDRESSED unter Adresse 5 */
    TEST_ASSERT_TRUE(enum_fsm_responds_to(&fsm, 5));

    enum_fsm_on_echo_mismatch(&fsm);

    TEST_ASSERT_EQUAL(ENUM_UNADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(0, enum_fsm_address(&fsm));
    TEST_ASSERT_EQUAL_HEX8(ENUM_ERR_COLLISION, fsm.last_error);
    TEST_ASSERT_EQUAL_HEX8(0x06, fsm.last_error);
    TEST_ASSERT_FALSE(enum_fsm_responds_to(&fsm, 5));
}

static void test_collision_ignored_when_not_addressed(void)
{
    reset(0, 10);   /* UNADDRESSED */
    enum_fsm_on_echo_mismatch(&fsm);
    TEST_ASSERT_EQUAL(ENUM_UNADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_HEX8(ENUM_ERR_NONE, fsm.last_error);
}

static void test_collision_recovers_via_new_enumeration(void)
{
    reset(5, 10);
    enum_fsm_on_echo_mismatch(&fsm);
    TEST_ASSERT_EQUAL(ENUM_UNADDRESSED, fsm.state);

    /* Der Master merkt die Kollision und enumeriert neu. */
    send(CMD_ENUM_RESET, NULL, 0, false);
    TEST_ASSERT_EQUAL(ENUM_ENUMERATING, fsm.state);
    const uint8_t a[1] = { 8 };
    send(CMD_ENUM_ASSIGN, a, 1, true);
    enum_fsm_on_ack_sent(&fsm, true);
    TEST_ASSERT_EQUAL_UINT8(8, enum_fsm_address(&fsm));
    TEST_ASSERT_EQUAL_UINT8(8, fsm.eeprom_address);
    TEST_ASSERT_EQUAL_HEX8(ENUM_ERR_NONE, fsm.last_error);   /* Zuweisung loest auf */
}

/* Ohne neue Enumeration nicht dauerhaft taub (#20): Rueckfall nach T_enum. */
static void test_collision_falls_back_after_t_enum(void)
{
    reset(5, 10);
    enum_fsm_on_echo_mismatch(&fsm);
    TEST_ASSERT_TRUE(fsm.collision_wait);
    enum_fsm_on_tick(&fsm, 9999);
    TEST_ASSERT_EQUAL(ENUM_UNADDRESSED, fsm.state);
    TEST_ASSERT_FALSE(enum_fsm_responds_to(&fsm, 5));
    enum_fsm_on_tick(&fsm, 1);
    TEST_ASSERT_EQUAL(ENUM_ADDRESSED, fsm.state);
    TEST_ASSERT_EQUAL_UINT8(5, enum_fsm_address(&fsm));
    TEST_ASSERT_FALSE(fsm.collision_wait);
    TEST_ASSERT_FALSE(fsm.want_eeprom_write);
    TEST_ASSERT_EQUAL_HEX8(ENUM_ERR_COLLISION, fsm.last_error);   /* bis zur Meldung */

    /* weitere Ticks aendern nichts */
    enum_fsm_on_tick(&fsm, 60000);
    TEST_ASSERT_EQUAL_UINT8(5, enum_fsm_address(&fsm));
}

static void test_collision_on_service_address_falls_back_to_service(void)
{
    reset(0, 2);
    send(CMD_ENUM_RESET, NULL, 0, false);
    enum_fsm_on_tick(&fsm, 2000);                 /* -> 250 */
    TEST_ASSERT_TRUE(fsm.is_service_addr);
    enum_fsm_on_echo_mismatch(&fsm);
    TEST_ASSERT_EQUAL(ENUM_UNADDRESSED, fsm.state);
    enum_fsm_on_tick(&fsm, 2000);
    TEST_ASSERT_EQUAL_UINT8(PROTO_ADDR_SERVICE, enum_fsm_address(&fsm));
    TEST_ASSERT_TRUE(fsm.is_service_addr);
}

static void test_enum_reset_during_collision_wait(void)
{
    reset(5, 10);
    enum_fsm_on_echo_mismatch(&fsm);
    enum_fsm_on_tick(&fsm, 8000);
    send(CMD_ENUM_RESET, NULL, 0, false);
    TEST_ASSERT_EQUAL(ENUM_ENUMERATING, fsm.state);
    TEST_ASSERT_FALSE(fsm.collision_wait);
    enum_fsm_on_tick(&fsm, 9999);                 /* T_enum ab ENUM_RESET */
    TEST_ASSERT_EQUAL(ENUM_ENUMERATING, fsm.state);
    enum_fsm_on_tick(&fsm, 1);
    TEST_ASSERT_EQUAL_UINT8(5, enum_fsm_address(&fsm));
}

static void test_fresh_card_stays_unaddressed_without_enumeration(void)
{
    reset(0, 1);
    enum_fsm_on_tick(&fsm, 60000);
    TEST_ASSERT_EQUAL(ENUM_UNADDRESSED, fsm.state);
    TEST_ASSERT_FALSE(fsm.collision_wait);
}

/* 0x06 wird genau einmal gemeldet (#28). */
static void test_take_error_reports_once(void)
{
    reset(5, 10);
    TEST_ASSERT_EQUAL_HEX8(ENUM_ERR_NONE, enum_fsm_take_error(&fsm));
    enum_fsm_on_echo_mismatch(&fsm);
    enum_fsm_on_tick(&fsm, 10000);
    TEST_ASSERT_EQUAL_HEX8(0x06, enum_fsm_take_error(&fsm));
    TEST_ASSERT_EQUAL_HEX8(ENUM_ERR_NONE, enum_fsm_take_error(&fsm));
}

/* --- Antwortlogik --------------------------------------------------- */

static void test_responds_to_broadcast_always(void)
{
    reset(0, 10);   /* UNADDRESSED */
    TEST_ASSERT_TRUE(enum_fsm_responds_to(&fsm, PROTO_ADDR_BROADCAST));
    send(CMD_ENUM_RESET, NULL, 0, false);
    TEST_ASSERT_TRUE(enum_fsm_responds_to(&fsm, PROTO_ADDR_BROADCAST));
}

static void test_no_unicast_response_while_enumerating(void)
{
    reset(7, 10);
    send(CMD_ENUM_RESET, NULL, 0, false);
    TEST_ASSERT_FALSE(enum_fsm_responds_to(&fsm, 7));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_empty_eeprom_is_unaddressed);
    RUN_TEST(test_init_with_eeprom_addr_comes_up_addressed);
    RUN_TEST(test_t_enum_is_clamped);
    RUN_TEST(test_enum_reset_enters_enumerating);
    RUN_TEST(test_first_card_takes_assignment);
    RUN_TEST(test_assign_ack_collision_keeps_eeprom_and_chain);
    RUN_TEST(test_ack_result_ignored_without_assignment);
    RUN_TEST(test_card_without_chain_in_ignores_assignment);
    RUN_TEST(test_second_assignment_is_not_taken_twice);
    RUN_TEST(test_enum_assign_bad_address_ignored);
    RUN_TEST(test_enum_done_keeps_assigned_card);
    RUN_TEST(test_fallback_eeprom_on_timeout);
    RUN_TEST(test_fallback_eeprom_on_enum_done);
    RUN_TEST(test_fallback_service_on_timeout);
    RUN_TEST(test_fallback_service_on_enum_done);
    RUN_TEST(test_no_timeout_outside_enumerating);
    RUN_TEST(test_collision_drops_address);
    RUN_TEST(test_collision_ignored_when_not_addressed);
    RUN_TEST(test_collision_recovers_via_new_enumeration);
    RUN_TEST(test_collision_falls_back_after_t_enum);
    RUN_TEST(test_collision_on_service_address_falls_back_to_service);
    RUN_TEST(test_enum_reset_during_collision_wait);
    RUN_TEST(test_fresh_card_stays_unaddressed_without_enumeration);
    RUN_TEST(test_take_error_reports_once);
    RUN_TEST(test_responds_to_broadcast_always);
    RUN_TEST(test_no_unicast_response_while_enumerating);
    return UNITY_END();
}
