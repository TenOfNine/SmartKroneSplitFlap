/* Siehe motion.h und docs/spezifikation.md Kapitel 6. */
#include "motion.h"

/* Verbleibende Blaetter bis zum Ziel, nur vorwaerts (Faktenblatt). */
static uint8_t remaining(const motion_t *m)
{
    if (m->current == 0 || m->target == 0) {
        return 0;
    }
    const uint8_t z = m->blattzahl;
    return (uint8_t)((m->target + z - m->current) % z);
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

static void fail(motion_t *m, uint8_t code, uint32_t now)
{
    m->error = code;
    m->motor_on = false;
    m->gate_cut_early = false;
    enter(m, MOTION_ERROR, now);
}

static void start_homing(motion_t *m, uint32_t now)
{
    m->synced = false;
    m->seen_first_leer = false;
    m->counting = (m->detected_blattzahl == 0);
    m->count_value = 0;
    m->retries = 0;
    m->motor_on = true;
    m->gate_cut_early = false;
    m->last_leer_ms = now;
    m->last_blatt_ms = now;
    m->motor_since_ms = now;
    enter(m, MOTION_HOMING, now);
}

void motion_init(motion_t *m, const module_config_t *cfg,
                 uint8_t stored_position, uint32_t now_ms)
{
    m->blattzahl = cfg->blattzahl;
    m->blatt_offset = cfg->blatt_offset;
    m->abschaltvorhalt_ms = cfg->abschaltvorhalt_ms;
    m->triac_invert = config_flag(cfg, CONFIG_FLAG_TRIAC_INVERT);
    m->position_save = config_flag(cfg, CONFIG_FLAG_POSITION_SAVE);

    m->current = 0;
    m->target = 0;
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
    m->last_blatt_ms = now_ms;
    m->last_leer_ms = now_ms;
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

/* Gemeinsame Entprellung fuer beide Impulseingaenge. */
static bool debounced(uint32_t now, uint32_t last)
{
    return (uint32_t)(now - last) >= MOTION_DEBOUNCE_MS;
}

bool motion_on_blatt_pulse(motion_t *m, uint32_t now_ms)
{
    if (!debounced(now_ms, m->last_blatt_ms)) {
        return false;
    }
    m->last_blatt_ms = now_ms;

    if (m->counting) {
        m->count_value++;
    }

    if (m->state == MOTION_MOVING) {
        advance_current(m);
        m->retries = 0;
        if (remaining(m) == 0) {
            enter(m, MOTION_IDLE, now_ms);  /* Ziel erreicht */
        }
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
        if (m->counting) {
            /* Bis zum zweiten Leerbildimpuls weiterlaufen. Eigenes Zeitfenster,
             * da eine volle Umdrehung laenger als der Homing-Timeout dauern
             * kann. */
            m->state_since_ms = now_ms;
            return true;
        }
        m->motor_on = false;
        enter(m, MOTION_IDLE, now_ms);
        return true;
    }

    /* Zweiter Leerbildimpuls: Blattzahlerkennung abschliessen. */
    m->detected_blattzahl = m->count_value;
    m->counting = false;
    if (m->detected_blattzahl != 40 && m->detected_blattzahl != 64 &&
        m->detected_blattzahl != 80) {
        fail(m, MOTION_ERR_BLATTZAHL, now_ms);
        return true;
    }
    m->motor_on = false;
    enter(m, MOTION_IDLE, now_ms);
    return true;
}

bool motion_set_target(motion_t *m, uint8_t blatt)
{
    if (blatt < 1 || blatt > m->blattzahl) {
        return false;
    }
    m->target = blatt;
    return true;
}

void motion_go(motion_t *m, uint32_t now_ms)
{
    if (m->state == MOTION_ERROR) {
        /* Diagramm 6.1: GO / SET fuehrt aus ERROR zurueck. */
        m->error = MOTION_ERR_NONE;
        if (m->synced) {
            enter(m, MOTION_IDLE, now_ms);
        } else {
            start_homing(m, now_ms);
        }
        return;
    }
    if (m->state != MOTION_IDLE) {
        return;
    }
    if (!m->synced) {
        m->error = MOTION_ERR_UNSYNCED;   /* 0x04: Position verloren */
        return;
    }
    if (m->target == 0 || remaining(m) == 0) {
        return;  /* schon am Ziel */
    }
    m->retries = 0;
    m->gate_cut_early = false;
    m->motor_on = true;
    m->motor_since_ms = now_ms;
    m->last_blatt_ms = now_ms;
    enter(m, MOTION_MOVING, now_ms);
}

void motion_stop(motion_t *m, uint32_t now_ms)
{
    if (m->state == MOTION_MOVING || m->state == MOTION_HOMING) {
        m->motor_on = false;
        enter(m, MOTION_IDLE, now_ms);
    } else {
        m->motor_on = false;
    }
}

void motion_home(motion_t *m, uint32_t now_ms)
{
    m->error = MOTION_ERR_NONE;
    start_homing(m, now_ms);
}

void motion_tick(motion_t *m, uint32_t now_ms)
{
    switch (m->state) {
    case MOTION_HOMING:
        if ((uint32_t)(now_ms - m->state_since_ms) >= MOTION_HOMING_TIMEOUT_MS) {
            if (m->seen_first_leer) {
                /* Schon synchronisiert, nur die Blattzahlerkennung ist nicht
                 * fertig geworden -> mit der konfigurierten Blattzahl arbeiten. */
                m->counting = false;
                m->motor_on = false;
                enter(m, MOTION_IDLE, now_ms);
            } else {
                fail(m, MOTION_ERR_NO_LEER, now_ms);   /* 0x02 */
            }
        }
        break;

    case MOTION_MOVING: {
        if ((uint32_t)(now_ms - m->motor_since_ms) >= MOTION_RUNTIME_LIMIT_MS) {
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
        /* Kommt der letzte Impuls nach dem Vorhalt nicht, gilt das Ziel als
         * erreicht (der Rotor ist ausgelaufen). */
        if (m->gate_cut_early &&
            (uint32_t)(now_ms - m->last_blatt_ms) >= 3u * MOTION_TIME_PER_BLATT_MS) {
            m->current = m->target;
            enter(m, MOTION_IDLE, now_ms);
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
    return m->motor_on ^ m->triac_invert;
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
