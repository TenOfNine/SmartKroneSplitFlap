/*
 * Implementierung des Enumerations-Automaten, siehe enumeration.h und
 * docs/spezifikation.md Abschnitt 4.5.
 */
#include "enumeration.h"

static uint16_t clamp_t_enum_ms(uint8_t seconds)
{
    if (seconds < ENUM_T_ENUM_MIN_S) {
        seconds = ENUM_T_ENUM_MIN_S;
    } else if (seconds > ENUM_T_ENUM_MAX_S) {
        seconds = ENUM_T_ENUM_MAX_S;
    }
    return (uint16_t)(seconds * 1000u);
}

static bool eeprom_addr_valid(uint8_t addr)
{
    return addr >= PROTO_ADDR_MIN && addr <= PROTO_ADDR_MAX;
}

void enum_fsm_init(enum_fsm_t *fsm, uint8_t eeprom_address, uint8_t t_enum_seconds)
{
    fsm->eeprom_address = eeprom_address;
    fsm->t_enum_ms = clamp_t_enum_ms(t_enum_seconds);
    fsm->elapsed_ms = 0;
    fsm->chain_out_active = false;
    fsm->last_error = ENUM_ERR_NONE;
    fsm->want_eeprom_write = false;
    fsm->want_ack = false;
    fsm->is_service_addr = false;

    if (eeprom_addr_valid(eeprom_address)) {
        fsm->state = ENUM_ADDRESSED;
        fsm->address = eeprom_address;
    } else {
        fsm->state = ENUM_UNADDRESSED;
        fsm->address = 0;
    }
}

/* Rueckfall nach Abschnitt 4.5.2: gueltige EEPROM-Adresse -> diese,
 * sonst Serviceadresse 250. */
static void apply_fallback(enum_fsm_t *fsm)
{
    if (eeprom_addr_valid(fsm->eeprom_address)) {
        fsm->address = fsm->eeprom_address;
        fsm->is_service_addr = false;
    } else {
        fsm->address = PROTO_ADDR_SERVICE;
        fsm->is_service_addr = true;
    }
    fsm->state = ENUM_ADDRESSED;
    fsm->elapsed_ms = 0;
}

static void on_enum_reset(enum_fsm_t *fsm)
{
    fsm->state = ENUM_ENUMERATING;
    fsm->address = 0;             /* nicht mehr auf die bisherige Adresse antworten */
    fsm->is_service_addr = false;
    fsm->chain_out_active = false;
    fsm->elapsed_ms = 0;
    /* last_error und Mechanik bleiben unberuehrt. */
}

static void on_enum_assign(enum_fsm_t *fsm, const uint8_t *payload,
                           uint8_t payload_len, bool chain_in_active)
{
    if (payload_len != 1 || payload == NULL) {
        return;
    }
    const uint8_t new_addr = payload[0];
    if (!eeprom_addr_valid(new_addr)) {
        return;
    }

    /* Nur die Karte mit aktivem CHAIN_IN, die in dieser Runde noch keine
     * Adresse uebernommen hat (CHAIN_OUT noch inaktiv). */
    if (fsm->state != ENUM_ENUMERATING || !chain_in_active ||
        fsm->chain_out_active) {
        return;
    }

    fsm->address = new_addr;
    fsm->is_service_addr = false;
    fsm->eeprom_address = new_addr;
    fsm->want_eeprom_write = true;
    fsm->chain_out_active = true;   /* gibt die naechste Karte frei */
    fsm->want_ack = true;
    fsm->state = ENUM_ADDRESSED;
    fsm->last_error = ENUM_ERR_NONE; /* eine frische Zuweisung loest eine Kollision auf */
}

void enum_fsm_on_frame(enum_fsm_t *fsm, uint8_t cmd, uint8_t addr,
                       const uint8_t *payload, uint8_t payload_len,
                       bool chain_in_active)
{
    (void)addr;  /* Enumerationskommandos sind immer Broadcast. */

    switch (cmd) {
    case CMD_ENUM_RESET:
        on_enum_reset(fsm);
        break;
    case CMD_ENUM_ASSIGN:
        on_enum_assign(fsm, payload, payload_len, chain_in_active);
        break;
    case CMD_ENUM_DONE:
        if (fsm->state == ENUM_ENUMERATING) {
            apply_fallback(fsm);  /* greift sofort, siehe 4.5.2 */
        }
        break;
    default:
        /* Alle anderen Kommandos aendern den Enumerationszustand nicht. */
        break;
    }
}

void enum_fsm_on_tick(enum_fsm_t *fsm, uint16_t dt_ms)
{
    if (fsm->state != ENUM_ENUMERATING) {
        return;
    }
    /* Saettigend addieren, damit ein grosser dt nicht ueberlaeuft. */
    if ((uint32_t)fsm->elapsed_ms + dt_ms >= fsm->t_enum_ms) {
        apply_fallback(fsm);
    } else {
        fsm->elapsed_ms = (uint16_t)(fsm->elapsed_ms + dt_ms);
    }
}

void enum_fsm_on_echo_mismatch(enum_fsm_t *fsm)
{
    if (fsm->state != ENUM_ADDRESSED) {
        return;
    }
    fsm->address = 0;
    fsm->is_service_addr = false;
    fsm->chain_out_active = false;
    fsm->last_error = ENUM_ERR_COLLISION;
    fsm->state = ENUM_UNADDRESSED;
    /* EEPROM-Adresse bleibt stehen; die Aufloesung erfolgt ueber eine neue
     * Enumeration, die EEPROM-Byte 4 ohnehin ueberschreibt. */
}

uint8_t enum_fsm_address(const enum_fsm_t *fsm)
{
    return (fsm->state == ENUM_ADDRESSED) ? fsm->address : 0u;
}

bool enum_fsm_responds_to(const enum_fsm_t *fsm, uint8_t frame_addr)
{
    if (frame_addr == PROTO_ADDR_BROADCAST) {
        return true;
    }
    return fsm->state == ENUM_ADDRESSED && frame_addr == fsm->address;
}

void enum_fsm_clear_outputs(enum_fsm_t *fsm)
{
    fsm->want_eeprom_write = false;
    fsm->want_ack = false;
}
