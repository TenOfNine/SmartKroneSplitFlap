/* Siehe config.h und docs/spezifikation.md Abschnitt 6.3. */
#include "config.h"

void config_defaults(module_config_t *cfg)
{
    cfg->blattzahl = 40;
    cfg->blatt_offset = 0;
    cfg->abschaltvorhalt_ms = 0;
    cfg->flags = CONFIG_FLAGS_DEFAULT;
    cfg->bus_address = 0;
    cfg->t_enum_s = 10;
}

void config_from_bytes(module_config_t *cfg, const uint8_t *bytes)
{
    cfg->blattzahl = bytes[0];
    cfg->blatt_offset = bytes[1];
    cfg->abschaltvorhalt_ms = bytes[2];
    cfg->flags = bytes[3];
    cfg->bus_address = bytes[4];
    cfg->t_enum_s = bytes[5];
}

void config_to_bytes(const module_config_t *cfg, uint8_t *bytes)
{
    bytes[0] = cfg->blattzahl;
    bytes[1] = cfg->blatt_offset;
    bytes[2] = cfg->abschaltvorhalt_ms;
    bytes[3] = cfg->flags;
    bytes[4] = cfg->bus_address;
    bytes[5] = cfg->t_enum_s;
}

static uint8_t clamp_u8(uint8_t v, uint8_t lo, uint8_t hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

bool config_validate(module_config_t *cfg)
{
    bool ok = true;

    if ((cfg->flags & (uint8_t)~CONFIG_FLAGS_KNOWN_MASK) != 0u) {
        /* Nie definierte Flag-Bits gesetzt: geloeschtes oder fremdes EEPROM.
         * Dann sind auch die uebrigen Felder nicht vertrauenswuerdig
         * (0xFF ergaebe Vorhalt 60, T_enum 60). Nur eine gueltige Busadresse
         * bleibt, damit die Karte ohne Neu-Enumeration erreichbar ist. */
        const uint8_t addr = cfg->bus_address;
        config_defaults(cfg);
        if (addr >= 1u && addr <= 250u) {
            cfg->bus_address = addr;
        }
        ok = false;
    }
    if ((cfg->flags & (uint8_t)~CONFIG_FLAGS_VALID_MASK) != 0u) {
        cfg->flags &= CONFIG_FLAGS_VALID_MASK;   /* Bit 2 reserviert, #18 */
        ok = false;
    }
    if (cfg->blattzahl != 40 && cfg->blattzahl != 64 && cfg->blattzahl != 80) {
        cfg->blattzahl = 40;
        ok = false;
    }
    if (cfg->blatt_offset >= cfg->blattzahl) {
        cfg->blatt_offset = 0;
        ok = false;
    }
    if (cfg->abschaltvorhalt_ms > 60) {
        cfg->abschaltvorhalt_ms = 0;   /* Vorgabe statt Obergrenze */
        ok = false;
    }
    if (cfg->bus_address > 250) {
        cfg->bus_address = 0;
        ok = false;
    }
    {
        const uint8_t t = clamp_u8(cfg->t_enum_s, 1, 60);
        if (t != cfg->t_enum_s) {
            cfg->t_enum_s = t;
            ok = false;
        }
    }
    return ok;
}
