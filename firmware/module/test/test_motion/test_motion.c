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

/* Ersetzt test_triac_invert (v1.15), der "Gate high in IDLE" als korrekt
 * festschrieb. Laut Netzliste gilt PA7 high = Motor an in jeder Bestueckung;
 * Flag-Bit 2 ist reserviert (#18). Gate == motor_on in allen Zustaenden,
 * auch mit ungeprueften Flags 0x07/0xFF. */
static void test_gate_ignores_reserved_bit2(void)
{
    cfg.flags = CONFIG_FLAG_POSITION_SAVE | CONFIG_FLAG_RESERVED_BIT2;  /* 0x05 */
    motion_init(&m, &cfg, 5, t);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_FALSE(motion_triac_gate(&m));          /* IDLE: aus */
    motion_set_target(&m, 20);
    motion_go(&m, t);
    TEST_ASSERT_TRUE(motion_triac_gate(&m));           /* MOVING: an */
    blatt();
    motion_stop(&m, t);
    TEST_ASSERT_FALSE(motion_triac_gate(&m));          /* nach STOP: aus */
}

static void test_gate_off_in_error_with_ff_flags(void)
{
    cfg.flags = 0xFF;   /* Abbild eines geloeschten EEPROMs, ungeprueft */
    cfg.abschaltvorhalt_ms = 60;
    motion_init(&m, &cfg, 5, t);
    TEST_ASSERT_EQUAL(MOTION_HOMING, m.state);         /* Autohoming-Bit */
    TEST_ASSERT_TRUE(motion_triac_gate(&m));
    advance(MOTION_HOMING_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(MOTION_ERROR, m.state);
    TEST_ASSERT_FALSE(motion_triac_gate(&m));          /* ERROR: aus */
    advance(60000);
    TEST_ASSERT_FALSE(motion_triac_gate(&m));          /* bleibt aus */
    motion_stop(&m, t);
    TEST_ASSERT_FALSE(motion_triac_gate(&m));          /* STOP schaltet nichts ein */
}

static void test_gate_off_after_validated_legacy_images(void)
{
    const uint8_t images[][CONFIG_SIZE] = {
        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
        { 40, 0, 60, 0xFF, 3, 60 },
        { 40, 5, 60, 0x07, 1, 60 },
    };
    for (unsigned i = 0; i < sizeof(images) / sizeof(images[0]); ++i) {
        config_from_bytes(&cfg, images[i]);
        (void)config_validate(&cfg);
        cfg.flags = CONFIG_FLAG_POSITION_SAVE;   /* Warmstart -> IDLE */
        t = 0;
        motion_init(&m, &cfg, 7, t);
        TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
        TEST_ASSERT_FALSE(motion_triac_gate(&m));
        motion_set_target(&m, 30);
        motion_go(&m, t);
        TEST_ASSERT_TRUE(motion_triac_gate(&m));
        motion_stop(&m, t);
        TEST_ASSERT_FALSE(motion_triac_gate(&m));
        motion_go(&m, t);
        advance(MOTION_RUNTIME_LIMIT_MS);        /* keine Impulse -> Fehler */
        TEST_ASSERT_EQUAL(MOTION_ERROR, m.state);
        TEST_ASSERT_FALSE(motion_triac_gate(&m));
    }
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

/* --- Zielpuffer (#25) ----------------------------------------- */

/* Blattimpuls mit Mindestabstand plus Zeitfortschritt (Laufzeitueberwachung). */
static void blatt_tick(void)
{
    blatt();
    motion_tick(&m, t);
}

/* Faehrt Impulse, bis MOVING endet; Rueckgabe: Anzahl Impulse. */
static int run_until_stopped(int max_pulses)
{
    int n = 0;
    while (m.state == MOTION_MOVING && n < max_pulses) {
        blatt_tick();
        n++;
    }
    return n;
}

static void reach_idle_with_vorhalt(uint8_t blatt_no, uint8_t vorhalt)
{
    cfg.abschaltvorhalt_ms = vorhalt;
    reach_idle_at(blatt_no);
}

static void test_set_during_move_keeps_active_target(void)
{
    reach_idle_at(10);
    motion_set_target(&m, 13);
    motion_go(&m, t);
    blatt();                                   /* 11 */
    TEST_ASSERT_TRUE(motion_set_target(&m, 30));
    TEST_ASSERT_EQUAL_UINT8(30, m.target);     /* gepuffert */
    TEST_ASSERT_EQUAL_UINT8(13, m.active_target);
    blatt();                                   /* 12 */
    blatt();                                   /* 13 */
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_EQUAL_UINT8(13, m.current);
    TEST_ASSERT_FALSE(m.motor_on);             /* ohne GO keine Fahrt */
}

/* Repro Cluster V06: Fahrt 10 -> 13, SET 30 nach dem Gate-Aus durch den
 * Vorhalt. Frueher: IDLE mit current = 30, physisch 13. */
static void test_v06_set_after_gate_cut_no_desync(void)
{
    const uint8_t vorhalte[] = { 10, 20, 30 };
    for (unsigned i = 0; i < sizeof(vorhalte); ++i) {
        for (int final_pulse = 0; final_pulse <= 1; ++final_pulse) {
            setUp();
            reach_idle_with_vorhalt(10, vorhalte[i]);
            motion_set_target(&m, 13);
            motion_go(&m, t);
            blatt();
            blatt();                                         /* 12, Rest 1 */
            advance((uint32_t)(MOTION_TIME_PER_BLATT_MS - vorhalte[i]));
            TEST_ASSERT_FALSE(m.motor_on);                   /* Gate per Vorhalt aus */
            motion_set_target(&m, 30);
            if (final_pulse) {
                t += vorhalte[i];
                motion_on_blatt_pulse(&m, t);                /* Schlussimpuls */
            } else {
                advance(3u * MOTION_TIME_PER_BLATT_MS);      /* bleibt aus */
            }
            TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
            TEST_ASSERT_EQUAL_UINT8(13, m.current);
            TEST_ASSERT_EQUAL_UINT8(30, m.target);
            TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_NONE, m.error);
        }
    }
}

/* modul-sicherheit-17: Ziel = Ist waehrend der Fahrt loeste 45 statt 19
 * Impulse aus. */
static void test_target_equal_current_during_move_no_extra_turn(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 20);
    motion_go(&m, t);
    for (int i = 0; i < 5; ++i) {
        blatt();
    }
    TEST_ASSERT_EQUAL_UINT8(6, m.current);
    motion_set_target(&m, 6);                  /* = Ist */
    const int n = run_until_stopped(100);
    TEST_ASSERT_EQUAL_INT(14, n);              /* 6 -> 20 */
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_EQUAL_UINT8(20, m.current);
}

static void test_go_during_move_is_executed_at_idle(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 5);
    motion_go(&m, t);
    blatt();                                   /* 2 */
    motion_set_target(&m, 10);
    motion_go(&m, t);                          /* vormerken */
    TEST_ASSERT_TRUE(m.go_pending);
    TEST_ASSERT_EQUAL_UINT8(5, m.active_target);
    blatt();
    blatt();
    blatt();                                   /* 5 erreicht -> weiter nach 10 */
    TEST_ASSERT_EQUAL_UINT8(5, m.current);
    TEST_ASSERT_EQUAL(MOTION_MOVING, m.state);
    TEST_ASSERT_EQUAL_UINT8(10, m.active_target);
    TEST_ASSERT_FALSE(m.go_pending);
    TEST_ASSERT_TRUE(m.motor_on);
    const int n = run_until_stopped(100);
    TEST_ASSERT_EQUAL_INT(5, n);
    TEST_ASSERT_EQUAL_UINT8(10, m.current);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
}

static void test_go_during_homing_is_executed_after_sync(void)
{
    cfg.flags = CONFIG_FLAG_AUTOHOME;
    motion_init(&m, &cfg, 0, t);
    m.detected_blattzahl = 40;   /* Erkennung schon gelaufen -> nur syncen */
    m.counting = false;
    TEST_ASSERT_TRUE(motion_set_target(&m, 13));
    motion_go(&m, t);
    TEST_ASSERT_EQUAL(MOTION_HOMING, m.state);
    TEST_ASSERT_TRUE(m.go_pending);

    leer();                                    /* Sync auf Blatt 1, dann Fahrt */
    TEST_ASSERT_EQUAL(MOTION_MOVING, m.state);
    TEST_ASSERT_EQUAL_UINT8(1, m.current);
    TEST_ASSERT_EQUAL_UINT8(13, m.active_target);
    const int n = run_until_stopped(100);
    TEST_ASSERT_EQUAL_INT(12, n);
    TEST_ASSERT_EQUAL_UINT8(13, m.current);
}

static void test_go_during_counting_homing_is_executed(void)
{
    motion_init(&m, &cfg, 0, t);   /* Kaltstart: Blattzahlerkennung */
    motion_set_target(&m, 5);
    motion_go(&m, t);
    leer();
    for (int i = 0; i < 40; ++i) {
        blatt();
    }
    leer();                        /* Erkennung fertig -> IDLE -> Fahrt */
    TEST_ASSERT_EQUAL_UINT8(40, m.detected_blattzahl);
    TEST_ASSERT_EQUAL(MOTION_MOVING, m.state);
    TEST_ASSERT_EQUAL_INT(4, run_until_stopped(100));
    TEST_ASSERT_EQUAL_UINT8(5, m.current);
}

static void test_go_in_error_does_not_move(void)
{
    reach_idle_at(5);
    motion_set_target(&m, 20);
    m.state = MOTION_ERROR;
    m.error = MOTION_ERR_NO_BLATT;
    motion_go(&m, t);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);   /* nur Quittung */
    TEST_ASSERT_FALSE(m.go_pending);
    TEST_ASSERT_FALSE(m.motor_on);
    motion_go(&m, t);                          /* naechstes GO faehrt */
    TEST_ASSERT_EQUAL(MOTION_MOVING, m.state);
}

static void test_go_in_error_unsynced_homes_without_move(void)
{
    motion_init(&m, &cfg, 0, t);
    m.detected_blattzahl = 40;
    m.counting = false;
    advance(MOTION_HOMING_TIMEOUT_MS);         /* 0x02 */
    TEST_ASSERT_EQUAL(MOTION_ERROR, m.state);
    motion_set_target(&m, 9);
    motion_go(&m, t);                          /* Fehler quittieren -> Homing */
    TEST_ASSERT_EQUAL(MOTION_HOMING, m.state);
    TEST_ASSERT_FALSE(m.go_pending);
    m.counting = false;
    leer();
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);   /* keine Fahrt ohne neues GO */
    TEST_ASSERT_EQUAL_UINT8(1, m.current);
}

static void test_pending_go_at_target_stays_idle(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 3);
    motion_go(&m, t);
    motion_go(&m, t);                          /* gleiches Ziel erneut */
    blatt();
    blatt();
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_EQUAL_UINT8(3, m.current);
    TEST_ASSERT_FALSE(m.motor_on);
    TEST_ASSERT_FALSE(m.go_pending);
}

static void test_stop_clears_pending_go(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 5);
    motion_go(&m, t);
    motion_set_target(&m, 30);
    motion_go(&m, t);
    blatt();
    motion_stop(&m, t);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_FALSE(m.go_pending);
    TEST_ASSERT_FALSE(m.motor_on);
    advance(1000);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
}

static void test_home_clears_pending_go(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 5);
    motion_go(&m, t);
    motion_go(&m, t);
    motion_home(&m, t);
    TEST_ASSERT_FALSE(m.go_pending);
    m.counting = false;
    leer();
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
}

static void test_homing_failure_clears_pending_go(void)
{
    motion_init(&m, &cfg, 0, t);
    motion_set_target(&m, 5);
    motion_go(&m, t);
    advance(MOTION_HOMING_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(MOTION_ERROR, m.state);
    TEST_ASSERT_FALSE(m.go_pending);
    TEST_ASSERT_FALSE(m.motor_on);
}

/* Ein-Blatt-Fahrt ohne Schlussimpuls: current = aktives, nicht gepuffertes Ziel. */
static void test_missing_final_pulse_uses_active_target(void)
{
    reach_idle_with_vorhalt(5, 0);
    motion_set_target(&m, 6);
    motion_go(&m, t);
    motion_set_target(&m, 25);
    advance(MOTION_TIME_PER_BLATT_MS);         /* Gate aus */
    TEST_ASSERT_FALSE(m.motor_on);
    advance(2u * MOTION_TIME_PER_BLATT_MS);    /* Schlussimpuls bleibt aus */
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_EQUAL_UINT8(6, m.current);
    TEST_ASSERT_EQUAL_UINT8(25, m.target);
}

/* Ein vorgemerktes GO schliesst ohne Pause an: 0x05 zaehlt durch
 * ("> 4 s durchgehend", Spez. 6.4). 38 + 38 Blatt = 4,56 s. */
static void test_chained_moves_count_runtime_continuously(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 39);
    motion_go(&m, t);
    const uint32_t start = t;
    blatt_tick();
    motion_set_target(&m, 37);
    motion_go(&m, t);
    run_until_stopped(200);
    TEST_ASSERT_EQUAL(MOTION_ERROR, m.state);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_RUNTIME, m.error);
    TEST_ASSERT_TRUE((uint32_t)(t - start) <= MOTION_RUNTIME_LIMIT_MS + MOTION_TIME_PER_BLATT_MS);
    TEST_ASSERT_FALSE(m.motor_on);
}

static void test_short_chain_completes(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 20);
    motion_go(&m, t);
    blatt_tick();
    motion_set_target(&m, 38);
    motion_go(&m, t);
    const int n = run_until_stopped(200);
    TEST_ASSERT_EQUAL_INT(36, n);              /* 1 -> 20 -> 38 = 37 Blatt, 1 schon gefahren */
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_NONE, m.error);
    /* Nach einer Pause beginnt die Laufzeit neu. */
    advance(10);
    motion_set_target(&m, 36);
    motion_go(&m, t);
    run_until_stopped(200);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_EQUAL_UINT8(36, m.current);
}

static void test_status_reports_buffered_target(void)
{
    reach_idle_at(1);
    motion_set_target(&m, 5);
    motion_go(&m, t);
    motion_set_target(&m, 9);
    uint8_t st[8];
    motion_fill_status(&m, st, 1);
    TEST_ASSERT_EQUAL_UINT8(1, st[0]);
    TEST_ASSERT_EQUAL_UINT8(9, st[1]);         /* Statusbyte 1 = gepuffertes Ziel */
    TEST_ASSERT_EQUAL_UINT8(2, st[2]);         /* Moving */
}

/* --- SET_CONFIG zur Laufzeit (#23) ----------------------------- */

static void test_apply_config_vorhalt_immediate(void)
{
    reach_idle_with_vorhalt(10, 0);
    motion_set_target(&m, 12);
    motion_go(&m, t);
    blatt();                                   /* 11, Rest 1 */
    module_config_t c = cfg;
    c.abschaltvorhalt_ms = 30;
    motion_apply_config(&m, &c, t);
    TEST_ASSERT_EQUAL(MOTION_MOVING, m.state);
    advance(30);                               /* 60 - 30 */
    TEST_ASSERT_FALSE(m.motor_on);
    TEST_ASSERT_TRUE(m.gate_cut_early);
}

static void test_apply_config_blattzahl_change_unsyncs(void)
{
    reach_idle_at(10);
    motion_set_target(&m, 20);
    motion_go(&m, t);
    motion_go(&m, t);                          /* vorgemerkt */
    blatt();
    module_config_t c = cfg;
    c.blattzahl = 64;
    motion_apply_config(&m, &c, t);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_FALSE(m.motor_on);
    TEST_ASSERT_FALSE(motion_triac_gate(&m));
    TEST_ASSERT_FALSE(m.synced);
    TEST_ASSERT_FALSE(m.go_pending);
    TEST_ASSERT_EQUAL_UINT8(0, m.current);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_UNSYNCED, m.error);
    TEST_ASSERT_EQUAL_UINT8(64, m.blattzahl);

    /* kein selbsttaetiges Anlaufen, auch nicht per SET+GO */
    TEST_ASSERT_TRUE(motion_set_target(&m, 50));   /* 50 erst mit 64 Blatt gueltig */
    motion_go(&m, t);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_FALSE(m.motor_on);
    advance(10000);
    TEST_ASSERT_FALSE(m.motor_on);

    motion_home(&m, t);                        /* HOME synchronisiert neu */
    TEST_ASSERT_EQUAL(MOTION_HOMING, m.state);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_NONE, m.error);
}

static void test_apply_config_offset_change_unsyncs_in_idle(void)
{
    reach_idle_at(10);
    module_config_t c = cfg;
    c.blatt_offset = 5;
    motion_apply_config(&m, &c, t);
    TEST_ASSERT_EQUAL(MOTION_IDLE, m.state);
    TEST_ASSERT_FALSE(m.synced);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_UNSYNCED, m.error);
    motion_home(&m, t);
    m.counting = false;
    leer();
    TEST_ASSERT_EQUAL_UINT8(6, m.current);     /* neuer Offset wirkt */
}

static void test_apply_config_same_geometry_keeps_moving(void)
{
    reach_idle_at(10);
    motion_set_target(&m, 20);
    motion_go(&m, t);
    module_config_t c = cfg;
    c.flags = 0;
    motion_apply_config(&m, &c, t);
    TEST_ASSERT_EQUAL(MOTION_MOVING, m.state);
    TEST_ASSERT_TRUE(m.motor_on);
    TEST_ASSERT_TRUE(m.synced);
}

static void test_apply_config_position_save_flag(void)
{
    reach_idle_at(10);
    TEST_ASSERT_TRUE(m.position_save);
    module_config_t c = cfg;
    c.flags = 0;
    motion_apply_config(&m, &c, t);
    TEST_ASSERT_FALSE(m.position_save);
}

/* --- Start nach Watchdog-Reset (#19) --------------------------- */

static void test_enter_error_motor_off(void)
{
    motion_init(&m, &cfg, 0, t);               /* Autohoming -> Motor an */
    TEST_ASSERT_TRUE(motion_triac_gate(&m));
    motion_enter_error(&m, MOTION_ERR_RUNTIME, t);
    TEST_ASSERT_EQUAL(MOTION_ERROR, m.state);
    TEST_ASSERT_EQUAL_HEX8(MOTION_ERR_RUNTIME, m.error);
    TEST_ASSERT_FALSE(motion_triac_gate(&m));
    advance(60000);
    TEST_ASSERT_FALSE(motion_triac_gate(&m));
    motion_go(&m, t);                          /* erst GO quittiert und homt */
    TEST_ASSERT_EQUAL(MOTION_HOMING, m.state);
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
    RUN_TEST(test_gate_ignores_reserved_bit2);
    RUN_TEST(test_gate_off_in_error_with_ff_flags);
    RUN_TEST(test_gate_off_after_validated_legacy_images);
    RUN_TEST(test_status_layout);
    RUN_TEST(test_set_during_move_keeps_active_target);
    RUN_TEST(test_v06_set_after_gate_cut_no_desync);
    RUN_TEST(test_target_equal_current_during_move_no_extra_turn);
    RUN_TEST(test_go_during_move_is_executed_at_idle);
    RUN_TEST(test_go_during_homing_is_executed_after_sync);
    RUN_TEST(test_go_during_counting_homing_is_executed);
    RUN_TEST(test_go_in_error_does_not_move);
    RUN_TEST(test_go_in_error_unsynced_homes_without_move);
    RUN_TEST(test_pending_go_at_target_stays_idle);
    RUN_TEST(test_stop_clears_pending_go);
    RUN_TEST(test_home_clears_pending_go);
    RUN_TEST(test_homing_failure_clears_pending_go);
    RUN_TEST(test_missing_final_pulse_uses_active_target);
    RUN_TEST(test_chained_moves_count_runtime_continuously);
    RUN_TEST(test_short_chain_completes);
    RUN_TEST(test_status_reports_buffered_target);
    RUN_TEST(test_apply_config_vorhalt_immediate);
    RUN_TEST(test_apply_config_blattzahl_change_unsyncs);
    RUN_TEST(test_apply_config_offset_change_unsyncs_in_idle);
    RUN_TEST(test_apply_config_same_geometry_keeps_moving);
    RUN_TEST(test_apply_config_position_save_flag);
    RUN_TEST(test_enter_error_motor_off);
    return UNITY_END();
}
