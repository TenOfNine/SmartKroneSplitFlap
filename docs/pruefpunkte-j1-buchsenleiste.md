# Prüfpunkt J1 — Wannenstecker → Buchsenleiste

| Feld | Wert |
|---|---|
| Zweck | J1 (Verbindung zur Anzeigenplatine) wird von einem kodierten Wannenstecker auf eine nicht kodierte Buchsenleiste 2×5 geändert, weil die Daughter Card board-to-board direkt auf den Pfostenstecker der Anzeigenplatine gesteckt wird. |
| Bezug | CLAUDE.md „Was nicht zu tun ist" — keine Änderung an J1 ohne Rücksprache · Schaltplan 4.1 · Spezifikation 4.6 |
| Auslöser | Betreiber, im Chat, 31.08.2026: Vorschlag BKL 10120960 (Buchsenleiste 2×5, 2,54 mm, gerade). Rückfrage zur mechanischen Anbindung beantwortet: **„direkt aufgesteckt (Board-to-Board)"**. |
| Status | **Umgesetzt** in Schaltplan v0.5 / Spez. v0.8. Offener mechanischer Punkt J1-M unten. |
| Datum | 31.08.2026 |

---

## Was sich ändert

| | vorher | nachher |
|---|---|---|
| Bauteil | Wannenstecker 2×5 (Stifte im kodierten Kragen) | Buchsenleiste 2×5, gerade (Referenz BKL 10120960) |
| Footprint | `Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical` | `Connector_PinSocket_2.54mm:PinSocket_2x05_P2.54mm_Vertical` |
| Gegenstück | Flachbandkabel mit IDC-Buchse | Pfostenstecker der Anzeigenplatine, direkt |
| Verpolschutz | mechanisch durch die Kragenkodierung | **keiner am Stecker** — siehe unten |
| Pinbelegung 1…10 | GND, AC1, GND, AC2, VSENS, VSENS, PULSE_NULL, PULSE_LEER, TRIAC_CTRL, PULSE_BLATT | **unverändert** |

Warum überhaupt: Ein Wannenstecker trägt Stifte in einem Kragen. Der
Pfostenstecker der Anzeigenplatine trägt ebenfalls Stifte. Stift auf Stift passt
nicht — für das beabsichtigte Aufstecken ist eine Buchsenleiste die richtige
Bauform. Der Wannenstecker war für ein Flachbandkabel gedacht, das hier entfällt.

## Sicherheitsbewertung

J1 Pin 2 und Pin 4 führen die **42 V~** (potenzialfrei). Unmittelbar daneben
liegen GND, VSENS und die Logik-Impulseingänge.

Wird die Karte **um 180° verdreht** aufgesteckt (Punktspiegelung des 2×5-Rasters),
passt die Buchsenleiste mechanisch trotzdem auf den Pfostenstecker. Dann trifft:

- Anzeige Pin 2 (42 V~) → Karten-Pin 9 (TRIAC_CTRL) → Q1/Q2, R7–R9
- Anzeige Pin 4 (42 V~) → Karten-Pin 7 (PULSE_NULL) → R-Netz an U1
- Anzeige Pin 1 (GND) → Karten-Pin 10, usw.

Ergebnis: 42 V~ auf Logiknetzen, U1 + U2 + die Triac-Treiberstufe zerstört.
Das ist genau das Szenario, vor dem CLAUDE.md und Schaltplan 4.1 warnen — nur
ohne die bisherige mechanische Sperre.

## Absicherung (drei Ebenen, alle drei umsetzen)

1. **Mechanik — verbindlich, noch offen (J1-M).** Die Kartenbefestigung so
   ausführen, dass nur eine Drehlage passt: asymmetrisch gesetzter
   Abstandsbolzen, eine Nase am Gehäuse, ein Kodierstift in einer der
   Pfostenstecker-Positionen (Pin ziehen, Buchse verschließen), oder eine
   zusätzliche einreihige Stiftleiste außerhalb der Symmetrie. Die vier
   symmetrischen M3-Eckbohrungen (4 mm von den Ecken) genügen **nicht** — bei
   180° fluchten sie ebenso.
2. **Bestückungsdruck.** Pin-1-Dreieck auf der Bauteilseite, dazu Klartext
   „PIN1 → ANZEIGE PIN1" direkt neben J1. In die Layout-Checkliste aufgenommen.
3. **Durchgangsprüfung vor dem ersten Einschalten.** Prüfliste 9.1 im Schaltplan
   und Inbetriebnahme Kap. 10: J1.2 / J1.4 gegen die 42-V~-Pins der Anzeige
   messen, nicht gegen die Versorgung. Zählrichtung der Anzeige: Lötseite mit
   2, 4, 6, 8, 10 in einer Reihe beschriftet.

## Offener Punkt

| Nr | Punkt | Verantwortung | Status |
|---|---|---|---|
| J1-M | Mechanische Kodierung der Drehlage beim Aufstecken festlegen (Bolzenbild / Kodierstift / Gehäuse). Fließt in das Platinen-Layout und die Gehäusekonstruktion ein. | Betreiber | **offen** — als GitHub-Issue führen |

## Freigabe

| Ergebnis | Bestätigt von | Datum |
|---|---|---|
| J1 als nicht kodierte Buchsenleiste 2×5, board-to-board — Bauform freigegeben, Verpolschutz über Mechanik + Druck + Durchgangsprüfung | Betreiber (phi.hoffmann@hotmail.de), im Chat | 31.08.2026 |
| Mechanische Kodierung J1-M festgelegt | — | offen |
