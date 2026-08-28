/*
 * Uhrzeit als anzeigbarer Text, siehe docs/spezifikation.md 7.6 und 7.7.
 *
 * Der Fallblattsatz kennt kein ':'. Als Trennzeichen kommen nur Zeichen aus
 * Anhang A in Frage, in der Praxis '.' (Vorgabe) oder '-'.
 */
#ifndef KRONE_CLOCKTEXT_H
#define KRONE_CLOCKTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Schreibt "HH.MM" (with_seconds = false) oder "HH.MM.SS" nach out.
 * sep ist das Trennzeichen; '.' und '-' sind zulaessig, alles andere wird zu '.'.
 * Rueckgabe: Laenge ohne Nullterminierung (5 bzw. 8), 0 bei ungueltiger Zeit.
 */
size_t clocktext_format(char *out, size_t out_size, uint8_t hh, uint8_t mm,
                        uint8_t ss, bool with_seconds, char sep);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_CLOCKTEXT_H */
