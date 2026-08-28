/* Tests fuer den Bewegungs-Zustandsautomat, siehe motion.c / Spezifikation 6. */
#include <unity.h>

#include "config.h"
#include "motion.h"

static motion_t m;
static module_config_t cfg;
static uint32_t t;

void setUp(void)
{
    config_defaults(&cfg);
    t = 0;
}
void tearDown(void) {}

static void advance(uint32_t dt)
{
    t += dt;
    motion_tick(&m, t);
}

/* Blattimpuls mit Mindestabstand (Sperrzeit eingehalten). */
static bool blatt(void)
{
    t += MOTION_TIME_PER_BLATT_MS;
    return motion_on_blatt_pulse(&m, t);
}

static void leer(void)
{
    t += 100;
    motion_on_leer_pulse(&m, t);
}

/* --- Start ---------------------------------------------------------- */

static void test_cold_start_homes(void)
{
    motion_init(&m, &cfg, 0, t);
    TEST_ASSERT_EQUAL(MOTION_HOMING, m.state);
    TEST_ASSERT_TRUE(m.motor_on);
    TEST_ASSERT_FALSE(m.synced);
}

static void test_warm_start_with_saved_position(void)
{
    cfg.flags = CONFIG_FLAG_POSITION_SAVE;   /* Autohoming aus */
    motion_init(&m, &cfg, 17, t);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_TRUE(m.synced);
    TEST_ASSERT_EQUAL_UINT8(17, m.current);
    TEST_ASSERT_FALSE(m.motor_on);
}

static void test_autohome_flag_forces_homing(void)
{
    cfg.flags = CONFIG_FLAG_POSITION_SAVE | CONFIG_FLAG_AUTOHOME;
    motion_init(&m, &cfg, 17, t);
    TEST_ASSERT_EQUAL(MOTION_HOMING, m.state);
}

/* --- Homing ------------------------------------------------------- */

static void test_homing_syncs_on_leerbild(void)
{
    m.detected_blattzahl = 40;   /* Erkennung schon gelaufen -> nur syncen */
    cfg.flags = CONFIG_FLAG_AUTOHOME;
    motion_init(&m, &cfg, 0, t);
    m.detected_blattzahl = 40;
    m.counting = false;

    leer();
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_TRUE(m.synced);
    TEST_ASSERT_EQUAL_UINT8(1, m.current);   /* Offset 0 -> Blatt 1 */
    TEST_ASSERT_FALSE(m.motor_on);
}

static void test_homing_timeout_is_error_02(void)
{
    motion_init(&m, &cfg, 0, t);
    advance(MOTION_HOMING_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(MOTION_ERROR, m.state);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_NO_LEER, m.error);
    TEST_ASSERT_FALSE(m.motor_on);
}

static void test_blattzahl_detection(void)
{
    motion_init(&m, &cfg, 0, t);   /* counting = true (kalt) */
    TEST_ASSERT_TRUE(m.counting);

    leer();                        /* 1. Leerbild: sync, Zaehler = 0 */
    for (int i = 0; i < 40; ++i) {
        blatt();
    }
    leer();                        /* 2. Leerbild: Erkennung abschliessen */
    TEST_ASSERT_EQUAL_UINT8(40, m.detected_blattzahl);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_FALSE(m.counting);
}

static void test_blattzahl_implausible_is_error_03(void)
{
    motion_init(&m, &cfg, 0, t);
    leer();
    for (int i = 0; i < 37; ++i) {
        blatt();
    }
    leer();
    TEST_ASSERT_EQUAL(MOTION_ERROR, m.state);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_BLATTZAHL, m.error);
}

/* --- Bewegung --------------------------------------------------- */

static void reach_idle_at(uint8_t blatt_no)
{
    cfg.flags = CONFIG_FLAG_POSITION_SAVE;
    motion_init(&m, &cfg, blatt_no, t);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
}

static void test_move_forward_counts_blaetter(void)
{
    reach_idle_at(38);
    TEST_ASSERT_TRUE(motion_set_target(&m, 2));
    motion_go(&m, t);
    TEST_ASSERT_EQUAL(MOTION_MOVING, m.state);
    TEST_ASSERT_TRUE(m.motor_on);

    /* (2 - 38) mod 40 = 4 Blaetter vorwaerts: 39, 40, 1, 2 */
    blatt(); TEST_ASSERT_EQUAL_UINT8(39, m.current);
    blatt(); TEST_ASSERT_EQUAL_UINT8(40, m.current);
    blatt(); TEST_ASSERT_EQUAL_UINT8(1, m.current);
    blatt();
    TEST_ASSERT_EQUAL_UINT8(2, m.current);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_FALSE(m.motor_on);
}

static void test_go_when_already_at_target_stays_idle(void)
{
    reach_idle_at(10);
    motion_set_target(&m, 10);
    motion_go(&m, t);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
}

static void test_debounce_rejects_fast_second_pulse(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 20);
    motion_go(&m, t);

    t += MOTION_TIME_PER_BLATT_MS;
    TEST_ASSERT_TRUE(motion_on_blatt_pulse(&m, t));
    t += MOTION_DEBOUNCE_MS - 1u;
    TEST_ASSERT_FALSE(motion_on_blatt_pulse(&m, t));   /* zu frueh */
    TEST_ASSERT_EQUAL_UINT8(2, m.current);             /* nur ein Blatt gezaehlt */
}

static void test_go_without_sync_is_error_04(void)
{
    motion_init(&m, &cfg, 0, t);      /* HOMING, nicht synced */
    m.state = MOTION_IDLE;            /* kuenstlich: IDLE aber synced == false */
    m.synced = false;
    motion_set_target(&m, 5);
    motion_go(&m, t);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_UNSYNCED, m.error);
}

static void test_go_recovers_from_error(void)
{
    reach_idle_at(5);
    m.state = MOTION_ERROR;
    m.error = MOTION_ERR_RUNTIME;
    motion_go(&m, t);   /* Diagramm 6.1: GO fuehrt aus ERROR */
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_NONE, m.error);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);   /* war synced */
}

static void test_stop_halts_motor(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 30);
    motion_go(&m, t);
    blatt();
    motion_stop(&m, t);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_FALSE(m.motor_on);
    TEST_ASSERT_FALSE(motion_triac_gate(&m));
}

static void test_runtime_limit_is_error_05(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 40);
    motion_go(&m, t);
    /* Blattimpulse bleiben aus -> zuerst greift die Laufzeitueberwachung */
    advance(MOTION_RUNTIME_LIMIT_MS);
    TEST_ASSERT_EQUAL(MOTION_ERROR, m.state);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_RUNTIME, m.error);
}

static void test_no_pulse_error_after_three_retries(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 5);
    motion_go(&m, t);
    blatt();  /* ein Fortschritt, dann Stillstand */

    for (uint8_t i = 0; i < MOTION_MAX_RETRIES; ++i) {
        advance(MOTION_NO_PULSE_MS);
        TEST_ASSERT_EQUAL(MOTION_MOVING, m.state);
    }
    advance(MOTION_NO_PULSE_MS);
    TEST_ASSERT_EQUAL(MOTION_ERROR, m.state);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_NO_BLATT, m.error);
    TEST_ASSERT_TRUE(m.correction_count >= MOTION_MAX_RETRIES);
}

/* --- Ausgaben ------------------------------------------------- */

static void test_triac_invert(void)
{
    cfg.flags = CONFIG_FLAG_POSITION_SAVE | CONFIG_FLAG_TRIAC_INVERT;
    motion_init(&m, &cfg, 5, t);
    /* IDLE, Motor aus -> logisch 0, invertiert -> Gate 1 */
    TEST_ASSERT_TRUE(motion_triac_gate(&m));
    motion_set_target(&m, 20);
    motion_go(&m, t);
    /* MOVING, Motor an -> logisch 1, invertiert -> Gate 0 */
    TEST_ASSERT_FALSE(motion_triac_gate(&m));
}

static void test_status_layout(void)
{
    reach_idle_at(7);
    motion_set_target(&m, 12);
    uint8_t st[8];
    motion_fill_status(&m, st, 3);
    TEST_ASSERT_EQUAL_UINT8(7, st[0]);    /* Ist */
    TEST_ASSERT_EQUAL_UINT8(12, st[1]);   /* Ziel */
    TEST_ASSERT_EQUAL_UINT8(0, st[2]);    /* Idle */
    TEST_ASSERT_EQUAL_UINT8(0, st[3]);    /* kein Fehler */
    TEST_ASSERT_EQUAL_UINT8(3, st[7]);    /* FW-Version */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cold_start_homes);
    RUN_TEST(test_warm_start_with_saved_position);
    RUN_TEST(test_autohome_flag_forces_homing);
    RUN_TEST(test_homing_syncs_on_leerbild);
    RUN_TEST(test_homing_timeout_is_error_02);
    RUN_TEST(test_blattzahl_detection);
    RUN_TEST(test_blattzahl_implausible_is_error_03);
    RUN_TEST(test_move_forward_counts_blaetter);
    RUN_TEST(test_go_when_already_at_target_stays_idle);
    RUN_TEST(test_debounce_rejects_fast_second_pulse);
    RUN_TEST(test_go_without_sync_is_error_04);
    RUN_TEST(test_go_recovers_from_error);
    RUN_TEST(test_stop_halts_motor);
    RUN_TEST(test_runtime_limit_is_error_05);
    RUN_TEST(test_no_pulse_error_after_three_retries);
    RUN_TEST(test_triac_invert);
    RUN_TEST(test_status_layout);
    return UNITY_END();
}
