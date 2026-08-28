/*
 * Zeichenabbildung Text -> Fallblatt-Positionen, siehe docs/spezifikation.md
 * 7.4 und Anhang A (40-Blatt-Ausfuehrung).
 *
 * Hardwareunabhaengig, auf dem Host testbar.
 *
 *   Blatt 1..2  Leerbild
 *   Blatt 3..12  Ziffern 0..9
 *   Blatt 13..38 Buchstaben A..Z
 *   Blatt 39     Bindestrich
 *   Blatt 40     Punkt
 */
#ifndef KRONE_CHARMAP_H
#define KRONE_CHARMAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHARMAP_LEERBILD 1u
#define CHARMAP_BLATT_MIN 1u
#define CHARMAP_BLATT_MAX 40u

typedef enum {
    CHARMAP_ALIGN_LEFT,
    CHARMAP_ALIGN_CENTER,
    CHARMAP_ALIGN_RIGHT,
} charmap_align_t;

/*
 * Einzelnes ASCII-Zeichen auf ein Blatt abbilden. Kleinbuchstaben werden zu
 * Grossbuchstaben. Nicht darstellbare Zeichen -> Leerbild (Blatt 1).
 */
uint8_t charmap_blatt(char c);

/*
 * Rendert einen UTF-8-Text in field_width Blattpositionen.
 *
 *  - Kleinbuchstaben -> Grossbuchstaben
 *  - AE/OE/UE/SS fuer die deutschen Umlaute; reicht der Platz nicht, wird auf
 *    den Grundbuchstaben reduziert
 *  - unbekannte Zeichen -> Leerbild
 *  - danach linksbuendig / zentriert / rechtsbuendig in das Feld, mit Leerbild
 *    aufgefuellt; ueberlanger Text wird abgeschnitten
 *
 * Schreibt genau field_width Werte nach blaetter und gibt field_width zurueck.
 */
size_t charmap_render(const char *utf8, uint8_t *blaetter, size_t field_width,
                      charmap_align_t align);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_CHARMAP_H */
