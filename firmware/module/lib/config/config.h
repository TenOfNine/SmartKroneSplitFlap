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

/* Byte-Index der Busadresse im Abbild (Enumeration schreibt nur dieses Byte). */
#define CONFIG_BYTE_BUS_ADDRESS 4u

/* Flags in Byte 3. */
#define CONFIG_FLAG_POSITION_SAVE 0x01u  /* Bit 0: Position nach Stillstand ins EEPROM */
#define CONFIG_FLAG_AUTOHOME      0x02u  /* Bit 1: beim Start selbsttaetig homen */
/* Bit 2: reserviert. Frueher "Triac-Polaritaet invertieren"; laut Netzliste
 * (schaltplan-daughtercard.md 5.4) gilt in beiden Bestueckungszweigen
 * PA7 high = Motor an, eine invertierte Variante gibt es nicht. Das Bit wird
 * ignoriert und beim Pruefen geloescht (#18). */
#define CONFIG_FLAG_RESERVED_BIT2 0x04u

/* Bits, die gesetzt sein duerfen. */
#define CONFIG_FLAGS_VALID_MASK (CONFIG_FLAG_POSITION_SAVE | CONFIG_FLAG_AUTOHOME)
/* Bits, die je definiert waren. Steht etwas ausserhalb (z. B. 0xFF eines
 * geloeschten EEPROMs), gilt das ganze Abbild als ungueltig. */
#define CONFIG_FLAGS_KNOWN_MASK (CONFIG_FLAGS_VALID_MASK | CONFIG_FLAG_RESERVED_BIT2)

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
 *   Flags mit Bits ausserhalb 0x07 (geloeschtes EEPROM)
 *                          -> alle Felder auf Vorgabe, eine gueltige
 *                             Busadresse (1..250) bleibt erhalten
 *   Flags                  -> immer auf 0x03 maskiert (Bit 2 reserviert)
 *   ungueltige Blattzahl   -> 40
 *   Offset >= Blattzahl    -> 0
 *   Vorhalt > 60           -> 0 (Vorgabe)
 *   Busadresse > 250       -> 0
 *   T_enum                 -> auf 1..60 geklemmt
 */
bool config_validate(module_config_t *cfg);

static inline bool config_flag(const module_config_t *cfg, uint8_t mask)
{
    return (cfg->flags & mask) != 0u;
}

/* Positionsspeicherung wirksam: Bit 0 an UND Autohoming aus. Mit Autohoming
 * wird die gespeicherte Position beim Start nie genutzt, Schreiben waere nur
 * Verschleiss (#24). */
static inline bool config_position_store_active(const module_config_t *cfg)
{
    return config_flag(cfg, CONFIG_FLAG_POSITION_SAVE) &&
           !config_flag(cfg, CONFIG_FLAG_AUTOHOME);
}

#ifdef __cplusplus
}
#endif

#endif /* KRONE_CONFIG_H */
