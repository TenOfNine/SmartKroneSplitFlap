/*
 * Positions-Ringpuffer im EEPROM (Spezifikation 6.2, Positionsspeicherung).
 *
 * Hardwareunabhaengig: EEPROM-Zugriffe laufen ueber Callbacks (in der Firmware
 * die Schreibwarteschlange in src/main.c), auf dem Host testbar.
 *
 * Layout: `slots` Paare (seq, pos) ab `base`, Slot i an base + 2*i.
 *   seq  0..254, zyklisch modulo 255. 0xFF = leerer Slot, wird nie geschrieben.
 *   pos  1..254 = Blattlage. 0 = ungueltig (Fahrt begonnen), 0xFF = ungueltig.
 *
 * Neuester Eintrag = Ende der laengsten lueckenlosen seq-Kette (bei
 * Gleichstand die groessere seq, Abstand < 128). Jeder neue Eintrag geht in
 * den Slot hinter dem neuesten, dadurch verteilt sich der Verschleiss
 * gleichmaessig.
 *
 * Schreibfolge eines Eintrags: erst pos, dann seq. Faellt die Spannung
 * dazwischen aus, traegt der Slot noch die alte (aelteste) seq und gilt nicht
 * als neuester; der vorige Eintrag bleibt massgeblich. Eintraege werden nie
 * an Ort und Stelle geaendert.
 */
#ifndef KRONE_POSRING_H
#define KRONE_POSRING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POSRING_SEQ_MOD  255u   /* seq-Werte 0..254 */
#define POSRING_EMPTY    0xFFu  /* seq eines leeren Slots */
#define POSRING_INVALID  0u     /* pos-Marke "keine gueltige Position" */
#define POSRING_NONE     0xFFu  /* newest: kein Eintrag vorhanden */

typedef uint8_t (*posring_read_fn)(void *ctx, uint16_t addr);
typedef void (*posring_write_fn)(void *ctx, uint16_t addr, uint8_t value);

typedef struct {
    posring_read_fn  read;
    posring_write_fn write;
    void    *ctx;
    uint16_t base;
    uint8_t  slots;        /* 1..127 */
    uint8_t  newest;       /* Slotindex des neuesten Eintrags, POSRING_NONE = keiner */
    uint8_t  newest_seq;
    uint8_t  newest_pos;
} posring_t;

/* Liest den Ring einmal ein und merkt sich den neuesten Eintrag. */
void posring_init(posring_t *r, uint16_t base, uint8_t slots,
                  posring_read_fn read, posring_write_fn write, void *ctx);

/* Gespeicherte Blattlage des neuesten Eintrags; 0 = keine gueltige. */
uint8_t posring_position(const posring_t *r);

/* Neuen Eintrag mit Blattlage pos (1..254) anlegen. */
void posring_store(posring_t *r, uint8_t pos);

/* Gespeicherte Position ungueltig machen (Eintrag mit pos = 0), z. B. bei
 * Fahrtbeginn. Schreibt nur, wenn der neueste Eintrag gueltig ist.
 * Rueckgabe: true, wenn ein Eintrag geschrieben wurde. */
bool posring_invalidate(posring_t *r);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_POSRING_H */
