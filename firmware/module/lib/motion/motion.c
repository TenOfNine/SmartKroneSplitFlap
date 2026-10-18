/* Siehe motion.h und docs/spezifikation.md Kapitel 6. */
#include "motion.h"

/* Verbleibende Blaetter bis zum aktiven Ziel, nur vorwaerts (Faktenblatt). */
static uint8_t remaining(const motion_t *m)
{
    if (m->current == 0 || m->active_target == 0) {
        return 0;
    }
    const uint8_t z = m->blattzahl;
    return (uint8_t)((m->active_target + z - m->current) % z);
}

/* Ist-Blatt eins weiterschalten (1-basiert, umlaufend). */
static void advance_current(motion_t *m)
{
    m->current = (uint8_t)((m->current % m->blattzahl) + 1u);
}

static void enter(motion_t *m, motion_state_t s, uint32_t now)
{
    m->state = s;
    m->state_since_ms = now;
    if (s != MOTION_MOVING && s != MOTION_HOMING) {
        m->motor_on = false;
        m->gate_cut_early = false;
    }
}

/* Laufenden Selbsttest abschliessen; ohne zweites Leerbild bleibt die
 * Umlaufzeit 0 (nicht vollendet). */
static void test_end(motion_t *m)
{
    if (m->test_state == MOTION_TEST_RUNNING) {
        m->test_state = MOTION_TEST_DONE;
    }
}

/* Motorfehler: Motor aus, ERROR. Nach jedem Motorfehler ist die Lage nicht
 * mehr gesichert (Impulse ausgeblieben, Laufzeit ueberschritten, Blattzahl
 * falsch, Motor lief ohne Ansteuerung): nicht synchronisiert, Ist unbekannt.
 * Das naechste GO homt deshalb, statt aus einer falschen Lage zu fahren. */
static void fail(motion_t *m, uint8_t code, uint32_t now)
{
    m->error = code;
    m->motor_on = false;
    m->gate_cut_early = false;
    m->go_pending = false;
    m->counting = false;
    m->synced = false;
    m->current = 0;
    test_end(m);
    enter(m, MOTION_ERROR, now);
}

static void start_homing(motion_t *m, uint32_t now)
{
    m->synced = false;
    m->current = 0;               /* unbekannt bis zum Leerbild */
    m->seen_first_leer = false;
    m->counting = (m->detected_blattzahl == 0);
    m->count_value = 0;
    m->retries = 0;
    m->stray_count = 0;
    m->motor_on = true;
    m->gate_cut_early = false;
    /* Nur die Zeitfenster beginnen jetzt; die Sperrzeit bleibt auf die letzte
     * echte Flanke bezogen, sonst ginge ein Impuls kurz nach dem Start
     * verloren (#39). */
    m->last_blatt_ms = now;
    m->motor_since_ms = now;
    enter(m, MOTION_HOMING, now);
}

/* Fahrt zum gepufferten Ziel starten; nur aus IDLE aufrufen. */
static void start_move(motion_t *m, uint32_t now)
{
    if (!m->synced) {
        m->error = MOTION_ERR_UNSYNCED;   /* 0x04: Position verloren */
        return;
    }
    m->active_target = m->target;
    if (m->active_target == 0 || remaining(m) == 0) {
        return;  /* schon am Ziel */
    }
    m->retries = 0;
    m->stray_count = 0;
    m->gate_cut_early = false;
    m->motor_on = true;
    m->motor_since_ms = now;
    m->last_blatt_ms = now;       /* Fenster fuer 0x01, nicht die Sperrzeit */
    enter(m, MOTION_MOVING, now);
}

/* Regulaerer Uebergang nach IDLE; ein vorgemerktes GO startet sofort (#25). */
static void enter_idle(motion_t *m, uint32_t now)
{
    const bool was_moving = (m->state == MOTION_MOVING);
    const uint32_t motor_since = m->motor_since_ms;

    m->motor_on = false;
    enter(m, MOTION_IDLE, now);
    if (m->go_pending) {
        m->go_pending = false;
        start_move(m, now);
        if (was_moving && m->state == MOTION_MOVING) {
            /* Fahrt schliesst ohne Pause an: die Laufzeitueberwachung zaehlt
             * durch (0x05 gilt fuer "> 4 s durchgehend", Spez. 6.4). */
            m->motor_since_ms = motor_since;
        }
    }
}

void motion_init(motion_t *m, const module_config_t *cfg,
                 uint8_t stored_position, uint32_t now_ms)
{
    m->blattzahl = cfg->blattzahl;
    m->blatt_offset = cfg->blatt_offset;
    m->abschaltvorhalt_ms = cfg->abschaltvorhalt_ms;
    m->position_save = config_flag(cfg, CONFIG_FLAG_POSITION_SAVE);

    m->current = 0;
    m->target = 0;
    m->active_target = 0;
    m->go_pending = false;
    m->synced = false;
    m->error = MOTION_ERR_NONE;
    m->detected_blattzahl = 0;
    m->correction_count = 0;
    m->retries = 0;
    m->counting = false;
    m->count_value = 0;
    m->seen_first_leer = false;
    m->motor_on = false;
    m->gate_cut_early = false;
    m->stray_count = 0;
    m->stray_since_ms = now_ms;
    m->test_state = MOTION_TEST_NONE;
    m->test_rot_ms = 0;
    m->test_min_ms = 0;
    m->test_max_ms = 0;
    m->test_leer_ms = now_ms;
    m->test_prev_ms = 0;
    m->last_blatt_ms = now_ms;
    /* Flanken sofort annehmen: Sperrzeit gilt erst nach einer Flanke. */
    m->blatt_edge_ms = now_ms - MOTION_DEBOUNCE_MS;
    m->last_leer_ms = now_ms - MOTION_DEBOUNCE_MS;
    m->motor_since_ms = now_ms;

    const bool have_pos = m->position_save && stored_position >= 1 &&
                          stored_position <= m->blattzahl;
    if (have_pos && !config_flag(cfg, CONFIG_FLAG_AUTOHOME)) {
        m->current = stored_position;
        m->synced = true;
        enter(m, MOTION_IDLE, now_ms);
    } else {
        start_homing(m, now_ms);
    }
}

void motion_apply_config(motion_t *m, const module_config_t *cfg, uint32_t now_ms)
{
    m->abschaltvorhalt_ms = cfg->abschaltvorhalt_ms;
    m->position_save = config_flag(cfg, CONFIG_FLAG_POSITION_SAVE);

    if (cfg->blattzahl == m->blattzahl && cfg->blatt_offset == m->blatt_offset) {
        return;
    }
    /* Blattzaehlung passt nicht mehr zur Geometrie: anhalten, nicht selbst
     * anlaufen, Neusynchronisation ueber HOME anfordern (0x04). */
    m->blattzahl = cfg->blattzahl;
    m->blatt_offset = cfg->blatt_offset;
    m->go_pending = false;
    m->counting = false;
    test_end(m);
    m->synced = false;
    m->current = 0;
    m->target = 0;
    m->active_target = 0;
    m->error = MOTION_ERR_UNSYNCED;
    enter(m, MOTION_IDLE, now_ms);   /* schaltet den Motor ab */
}

/* Gemeinsame Entprellung fuer beide Impulseingaenge. */
static bool debounced(uint32_t now, uint32_t last)
{
    return (uint32_t)(now - last) >= MOTION_DEBOUNCE_MS;
}

uint32_t motion_homing_timeout_ms(const motion_t *m)
{
    return (uint32_t)MOTION_HOMING_TIMEOUT_MS * m->blattzahl / 40u;
}

uint32_t motion_runtime_limit_ms(const motion_t *m)
{
    const uint32_t limit = (uint32_t)MOTION_RUNTIME_LIMIT_MS * m->blattzahl / 40u;
    return (m->state == MOTION_HOMING) ? 2u * limit : limit;
}

/* Blattimpuls bei abgeschaltetem Motor: einzelne Nachlaufimpulse sind
 * normal, eine Folge im Takt eines laufenden Motors nicht (Triac oder
 * Ansteuerung defekt, Trommel von Hand gedreht). */
static void stray_pulse(motion_t *m, uint32_t now)
{
    if (m->stray_count == 0u ||
        (uint32_t)(now - m->stray_since_ms) > MOTION_STRAY_WINDOW_MS) {
        m->stray_count = 0;
        m->stray_since_ms = now;
    }
    m->stray_count++;
    if (m->stray_count >= MOTION_STRAY_PULSES) {
        m->stray_count = 0;
        fail(m, MOTION_ERR_STRAY, now);   /* 0x07 */
    }
}

/* Abstand zweier Blattimpulse waehrend der Messumdrehung erfassen. Der erste
 * Impuls nach dem Leerbild setzt nur den Bezug (Lage zum Leerbild ist O-6). */
static void test_interval(motion_t *m, uint32_t now)
{
    if (m->test_prev_ms != 0u) {
        const uint32_t d = now - m->test_prev_ms;
        const uint8_t v = d > 255u ? 255u : (uint8_t)d;
        if (m->test_min_ms == 0u || v < m->test_min_ms) {
            m->test_min_ms = v;
        }
        if (v > m->test_max_ms) {
            m->test_max_ms = v;
        }
    }
    m->test_prev_ms = now != 0u ? now : 1u;
}

bool motion_on_blatt_pulse(motion_t *m, uint32_t now_ms)
{
    if (!debounced(now_ms, m->blatt_edge_ms)) {
        return false;
    }
    m->blatt_edge_ms = now_ms;
    m->last_blatt_ms = now_ms;

    if (m->counting) {
        m->count_value++;
    }

    switch (m->state) {
    case MOTION_MOVING:
        advance_current(m);
        m->retries = 0;
        if (remaining(m) == 0) {
            enter_idle(m, now_ms);  /* Ziel erreicht */
        }
        break;
    case MOTION_HOMING:
        /* Nach dem ersten Leerbild laeuft die Lage mit, damit ein Abbruch der
         * Blattzahlerkennung (Timeout, STOP) die richtige Position hinterlaesst
         * (#39). */
        if (m->seen_first_leer) {
            advance_current(m);
            if (m->counting && m->test_state == MOTION_TEST_RUNNING) {
                test_interval(m, now_ms);
            }
        }
        break;
    case MOTION_IDLE:
    case MOTION_ERROR:
    default:
        stray_pulse(m, now_ms);
        break;
    }
    return true;
}

bool motion_on_leer_pulse(motion_t *m, uint32_t now_ms)
{
    if (!debounced(now_ms, m->last_leer_ms)) {
        return false;
    }
    m->last_leer_ms = now_ms;

    if (m->state != MOTION_HOMING) {
        return true;  /* im Betrieb nur Messpunkt */
    }

    if (!m->seen_first_leer) {
        /* Erster Leerbildimpuls: Blattzaehler auf den Offset setzen (6.2).
         * Die Karte gilt ab hier als synchronisiert. */
        m->seen_first_leer = true;
        m->current = (uint8_t)((m->blatt_offset % m->blattzahl) + 1u);
        m->synced = true;
        m->count_value = 0;
        m->test_leer_ms = now_ms;
        m->test_prev_ms = 0;
        if (m->counting) {
            /* Bis zum zweiten Leerbildimpuls weiterlaufen. Eigenes Zeitfenster,
             * da eine volle Umdrehung laenger als der Homing-Timeout dauern
             * kann. */
            m->state_since_ms = now_ms;
            return true;
        }
        enter_idle(m, now_ms);
        return true;
    }

    /* Zweiter Leerbildimpuls: Blattzahlerkennung abschliessen. Die erkannte
     * Zahl muss zur konfigurierten passen (6.2); sonst stimmt jede
     * Zielrechnung nicht. Sie steht danach im Statusbyte 4. */
    m->detected_blattzahl = m->count_value;
    m->counting = false;
    m->current = (uint8_t)((m->blatt_offset % m->blattzahl) + 1u);
    if (m->test_state == MOTION_TEST_RUNNING) {
        const uint32_t rot = now_ms - m->test_leer_ms;
        m->test_rot_ms = rot > 0xFFFFu ? 0xFFFFu : (uint16_t)rot;
        test_end(m);
    }
    if (m->detected_blattzahl != m->blattzahl) {
        fail(m, MOTION_ERR_BLATTZAHL, now_ms);
        return true;
    }
    enter_idle(m, now_ms);
    return true;
}

bool motion_set_target(motion_t *m, uint8_t blatt)
{
    if (blatt < 1 || blatt > m->blattzahl) {
        return false;
    }
    m->target = blatt;   /* nur puffern; die laufende Fahrt behaelt ihr Ziel */
    return true;
}

void motion_go(motion_t *m, uint32_t now_ms)
{
    switch (m->state) {
    case MOTION_ERROR:
        /* Diagramm 6.1: GO fuehrt aus ERROR zurueck, ohne sofortige Fahrt. */
        m->error = MOTION_ERR_NONE;
        if (m->synced) {
            enter(m, MOTION_IDLE, now_ms);
        } else {
            start_homing(m, now_ms);
        }
        break;
    case MOTION_MOVING:
    case MOTION_HOMING:
        m->go_pending = true;   /* nach Erreichen von IDLE ausfuehren */
        break;
    case MOTION_IDLE:
    default:
        start_move(m, now_ms);
        break;
    }
}

void motion_stop(motion_t *m, uint32_t now_ms)
{
    m->go_pending = false;
    m->motor_on = false;
    if (m->state == MOTION_HOMING) {
        /* Erkennung abgebrochen, beim naechsten Homing neu. Vor dem ersten
         * Leerbild bleibt die Karte unsynchronisiert (Ist 0), danach gilt die
         * mitgezaehlte Lage. */
        m->counting = false;
        test_end(m);
    }
    if (m->state == MOTION_MOVING || m->state == MOTION_HOMING) {
        enter(m, MOTION_IDLE, now_ms);
    }
}

void motion_home(motion_t *m, uint32_t now_ms)
{
    m->error = MOTION_ERR_NONE;
    m->go_pending = false;
    test_end(m);
    start_homing(m, now_ms);
}

void motion_selftest(motion_t *m, uint32_t now_ms)
{
    motion_home(m, now_ms);
    m->detected_blattzahl = 0;     /* Erkennung erzwingen */
    m->counting = true;
    m->test_state = MOTION_TEST_RUNNING;
    m->test_rot_ms = 0;
    m->test_min_ms = 0;
    m->test_max_ms = 0;
    m->test_prev_ms = 0;
}

void motion_fill_test(const motion_t *m, uint8_t out[7])
{
    out[0] = m->test_state;
    out[1] = m->detected_blattzahl;
    out[2] = (uint8_t)(m->test_rot_ms & 0xFFu);
    out[3] = (uint8_t)(m->test_rot_ms >> 8);
    out[4] = m->test_min_ms;
    out[5] = m->test_max_ms;
    out[6] = m->error;
}

void motion_enter_error(motion_t *m, uint8_t code, uint32_t now_ms)
{
    fail(m, code, now_ms);
}

void motion_tick(motion_t *m, uint32_t now_ms)
{
    switch (m->state) {
    case MOTION_HOMING:
        /* Rueckfallebene: beide Homing-Phasen sind einzeln begrenzt, die
         * Laufzeitueberwachung deckt das Homing trotzdem mit ab (#39). */
        if ((uint32_t)(now_ms - m->motor_since_ms) >= motion_runtime_limit_ms(m)) {
            fail(m, MOTION_ERR_RUNTIME, now_ms);   /* 0x05 */
            break;
        }
        if ((uint32_t)(now_ms - m->state_since_ms) >= motion_homing_timeout_ms(m)) {
            if (m->seen_first_leer) {
                /* Schon synchronisiert, nur die Blattzahlerkennung ist nicht
                 * fertig geworden -> mit der konfigurierten Blattzahl arbeiten. */
                m->counting = false;
                test_end(m);
                enter_idle(m, now_ms);
            } else {
                fail(m, MOTION_ERR_NO_LEER, now_ms);   /* 0x02 */
            }
        }
        break;

    case MOTION_MOVING: {
        if ((uint32_t)(now_ms - m->motor_since_ms) >= motion_runtime_limit_ms(m)) {
            fail(m, MOTION_ERR_RUNTIME, now_ms);   /* 0x05 */
            break;
        }
        if ((uint32_t)(now_ms - m->last_blatt_ms) >= MOTION_NO_PULSE_MS &&
            !m->gate_cut_early) {
            if (m->retries >= MOTION_MAX_RETRIES) {
                fail(m, MOTION_ERR_NO_BLATT, now_ms);   /* 0x01 */
                break;
            }
            m->retries++;
            m->correction_count++;
            m->last_blatt_ms = now_ms;   /* Fenster fuer den naechsten Versuch */
        }
        /* Abschaltvorhalt: das Gate vor dem letzten Blattimpuls abschalten. */
        if (remaining(m) == 1 && !m->gate_cut_early) {
            const uint32_t cut_after =
                MOTION_TIME_PER_BLATT_MS - m->abschaltvorhalt_ms;
            if ((uint32_t)(now_ms - m->last_blatt_ms) >= cut_after) {
                m->motor_on = false;
                m->gate_cut_early = true;
            }
        }
        /* Kommt der letzte Impuls nach dem Vorhalt nicht, gilt das aktive Ziel
         * als erreicht (der Rotor ist ausgelaufen). Ein inzwischen gepuffertes
         * neues Ziel aendert daran nichts (#25). */
        if (m->gate_cut_early &&
            (uint32_t)(now_ms - m->last_blatt_ms) >= 3u * MOTION_TIME_PER_BLATT_MS) {
            m->current = m->active_target;
            enter_idle(m, now_ms);
        }
        break;
    }

    case MOTION_IDLE:
    case MOTION_ERROR:
    default:
        break;
    }
}

bool motion_triac_gate(const motion_t *m)
{
    /* Laut Netzliste gilt in beiden Bestueckungszweigen PA7 high = Motor an;
     * keine Invertierung (#18). In IDLE und ERROR ist motor_on immer false. */
    return m->motor_on;
}

uint8_t motion_state_code(const motion_t *m)
{
    switch (m->state) {
    case MOTION_IDLE:   return 0;
    case MOTION_HOMING: return 1;
    case MOTION_MOVING: return 2;
    case MOTION_ERROR:  return 3;
    default:            return 3;
    }
}

void motion_fill_status(const motion_t *m, uint8_t out[8], uint8_t fw_version)
{
    out[0] = m->current;
    out[1] = m->target;
    out[2] = motion_state_code(m);
    out[3] = m->error;
    out[4] = m->detected_blattzahl;
    out[5] = (uint8_t)(m->correction_count & 0xFFu);
    out[6] = (uint8_t)(m->correction_count >> 8);
    out[7] = fw_version;
}
