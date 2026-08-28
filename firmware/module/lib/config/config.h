/*
 * Konfigurationsparameter der Modulsteuerung (EEPROM-Abbild).
 *
 * Hardwareunabhaengig: nur Struktur, Vorgaben, Serialisierung und Bereichs-
 * pruefung. Das eigentliche Lesen und Schreiben des EEPROM macht src/main.c.
 *
 * Grundlage: docs/spezifikation.md Abschnitt 6.3.
 *
 *   Byte 0  Blattzahl              40, 64 oder 80          Vorgabe 40
 *   Byte 1  Blatt-Offset           0..79                   Vorgabe 0  (O-6)
 *   Byte 2  Abschaltvorhalt in ms  0..60                   Vorgabe 0  (empirisch)
 *   Byte 3  Flags                  siehe unten             Vorgabe 0x03
 *   Byte 4  zuletzt zugewiesene Busadresse  0..250         Vorgabe 0
 *   Byte 5  T_enum in Sekunden     1..60                   Vorgabe 10
 */
#ifndef KRONE_CONFIG_H
#define KRONE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_SIZE 6u

/* Flags in Byte 3. */
#define CONFIG_FLAG_POSITION_SAVE 0x01u  /* Bit 0: Position nach Stillstand ins EEPROM */
#define CONFIG_FLAG_AUTOHOME      0x02u  /* Bit 1: beim Start selbsttaetig homen */
#define CONFIG_FLAG_TRIAC_INVERT  0x04u  /* Bit 2: Ausgangspolaritaet PA7 invertieren */

#define CONFIG_FLAGS_DEFAULT (CONFIG_FLAG_POSITION_SAVE | CONFIG_FLAG_AUTOHOME)

typedef struct {
    uint8_t blattzahl;
    uint8_t blatt_offset;
    uint8_t abschaltvorhalt_ms;
    uint8_t flags;
    uint8_t bus_address;
    uint8_t t_enum_s;
} module_config_t;

/* Setzt cfg auf die Vorgaben aus Abschnitt 6.3. */
void config_defaults(module_config_t *cfg);

/* Liest CONFIG_SIZE Bytes in cfg. */
void config_from_bytes(module_config_t *cfg, const uint8_t *bytes);

/* Schreibt cfg in CONFIG_SIZE Bytes. */
void config_to_bytes(const module_config_t *cfg, uint8_t *bytes);

/*
 * Prueft und korrigiert alle Felder auf ihren zulaessigen Bereich.
 * Rueckgabe: true, wenn nichts korrigiert werden musste.
 * Ungueltige Blattzahl -> 40. Bus-/T_enum-/Offset-/Vorhalt-Werte werden
 * in den gueltigen Bereich geklemmt.
 */
bool config_validate(module_config_t *cfg);

static inline bool config_flag(const module_config_t *cfg, uint8_t mask)
{
    return (cfg->flags & mask) != 0u;
}

#ifdef __cplusplus
}
#endif

#endif /* KRONE_CONFIG_H */
