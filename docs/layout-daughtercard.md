# Platzierungsvorschlag Daughter Card (Backlog T5)

| Feld | Wert |
|---|---|
| Bezug | `docs/schaltplan-daughtercard.md` Kapitel 8, Netzliste `hardware/daughtercard/daughtercard.net` |
| Platine | 74 × 60 mm, 2 Lagen, 1,6 mm, 35 µm Cu, HASL bleifrei |
| Befestigung | 4 × Bohrung 3,2 mm, je 4 mm von den Ecken |
| Status | **Vorplatziert.** `hardware/daughtercard/daughtercard.kicad_pcb` enthält alle 48 Bauteile mit Footprint, Netz und einer groben Position, dazu Umriss, 4 Bohrungen und die Netzklassen. Im PCB-Editor bleibt: Bauteile feinjustieren und routen. |
| Datum | 28.08.2026 |

Koordinaten in diesem Dokument: Ursprung untere linke Ecke, X nach rechts
(0…74), Y nach oben (0…60). Die `.kicad_pcb` verwendet die KiCad-Konvention
(Ursprung oben links, Y nach unten).

## Vorplatzierte Platine

```bash
/usr/bin/python3 tools/gen_daughtercard_pcb.py --png --drc
```

`tools/gen_daughtercard_pcb.py` (System-Python, nutzt `pcbnew`) liest die
Netzliste des Schaltplans und die Footprint-Tabelle aus
`tools/gen_daughtercard_sch.py` und schreibt die `.kicad_pcb`. Vorschau:
`docs/pcb-daughtercard.png`.

- Die Bauteil-Footprints tragen den Pfad des zugehörigen Schaltplansymbols;
  „Update PCB from Schematic" ordnet sie also ohne Warnung zu.
- Netzklassen in der `.kicad_pro`: `AC` (1,0 mm Bahn), `RS485` (0,3 mm Paar),
  `Power` (0,5 mm). Der 2‑mm‑Abstand der AC‑Bahnen zu Logiknetzen (Abschnitt 3)
  ist eine **Routing‑Vorgabe von Hand**, keine DRC‑Regel — am Stecker mit
  2,54‑mm‑Raster wäre er nicht einhaltbar.
- DRC meldet die unverdrahteten Netze (erwartbar) und rund vier eng benachbarte
  Bauteilpaare im gedrängten oberen Streifen (R16/JP3/R14/C2 zwischen den
  Wannensteckern J2/J3) — dort ein paar Millimeter auseinanderziehen.
- Der Courtyard der Bus-Wannenstecker `IDC-Header_2x05_Vertical` (J2, J3) ist
  21,4 mm breit. Damit ist der obere Rand die knappste Stelle. Wird es zu eng,
  sind die Hebel: J2/J3 auf einen unshrouded `PinHeader_2x05` (6,2 mm) umstellen
  oder die Platinenhöhe erhöhen (Kapitel 8.1 lässt bis knapp 100 mm zu).
- **J1** ist seit Schaltplan v0.5 eine **Buchsenleiste**
  (`PinSocket_2x05_P2.54mm_Vertical`, Courtyard ~6 mm), kein Wannenstecker mehr —
  die Karte wird board-to-board auf die Anzeigenplatine gesteckt. An der
  Unterkante ist dadurch mehr Luft.

> **Achtung Generatoren:** `gen_daughtercard_sch.py` erzeugt bei jedem Lauf neue
> UUIDs; die committete `.kicad_sch` und `.kicad_pcb` gehören zusammen. Nach einer
> Netzlistenänderung beide neu erzeugen (`gen_daughtercard_sch.py … && `
> `gen_daughtercard_pcb.py …`) und beide committen.

---

## 1. Grundaufteilung

```
  Y=60 ┌──────────────────────────────────────────────────┐
       │ J2 ▪ R16 JP3      U2 (RS-485)          ▪ J3        │  Bus-Zone (oben)
       │                                                   │
       │        C1  ┌────────────┐  Q1 Q2 Q3               │
       │            │   U1       │  R7 R8 R9   JP1 JP2      │  Logik-Zone (Mitte)
       │   R1 R3 R5 │  ATtiny    │  R10                    │
       │   C4 C5 C6 │            │        R11 R12 R13      │
       │   D1 D2 D3 └────────────┘        D4   J6          │
       │   R2 R4 R6                                        │
       │                                                   │
       │ J4 ═══════════ AC1/AC2 ═══════════════════════ J5 │  AC-Zone (unten,
  Y=0  └────────────────────── J1 ──────────────────────────┘   eine Kante)
       X=0                                              X=74
```

Drei Zonen, räumlich getrennt:

1. **AC-Zone**, gesamte Unterkante. Nur J4, J5, J1 und die AC-Bahnen. Kein Logikbauteil.
2. **Logik-Zone**, Mitte. U1, U2, alle Kleinsignalteile.
3. **Bus-Zone**, Oberkante. J2, J3, RS-485-Beschaltung.

---

## 2. Bauteile nach Zone

### AC-Zone (Unterkante, Y ≈ 0…12)

| Ref | Ungefähre Position | Begründung |
|---|---|---|
| J4 (42 V~ in) | X 4, Y 4, Schraubklemme nach links/unten | Einspeisung, an der Kante |
| J5 (42 V~ out) | X 66, Y 4, Schraubklemme nach rechts/unten | Durchschleifen zur nächsten Karte |
| J1 (zur Anzeige) | X 30…44, Y 3, Buchsenleiste 2×5 | mittig unten. AC1/AC2 laufen kurz von J4/J5 zu J1.2/J1.4; die Impuls- und Triac-Adern von J1.7–J1.10 gehen von hier nach oben zu U1. Pin-1-Lage und Drehsinn müssen zum Pfostenstecker der Anzeigenplatine passen (board-to-board, nicht kodiert) |

**AC1, AC2:** ≥ 1,0 mm Leiterbahn, entlang der Unterkante von J4 über J5 nach J1,
durchgehend ≥ 2,0 mm Abstand zu jedem Logiknetz. Die Massefläche auf der Rückseite
ist in diesem Streifen **unterbrochen**. Bei 1 A und 35 µm ergibt 1,0 mm eine
Erwärmung < 10 K.

### Logik-Zone (Mitte, Y ≈ 15…50)

| Ref | Position | Begründung |
|---|---|---|
| U1 (ATtiny1616) | X 26…40, Y 26…40, Pin 1 oben links | Zentrum. Portpins PA4–PA7 zeigen zur Impulseingangs- und Triac-Seite |
| C1 (100 n) | direkt an U1 Pin 1 / Pin 20 | Abblockung, kürzest möglich, Stich auf die Rückseiten-Massefläche |
| U2 (TP8485E) | X 33…45, Y 48…54, nahe J2/J3 | kurze RS-485-Paarführung zur Bus-Zone |
| C2 (100 n) | direkt an U2 Pin 8 / Pin 5 | Abblockung U2 |
| C3 (10 µF) | X 20, Y 45, nahe der +5V-Einspeisung (F1 / J2.1) | Stützkondensator |
| F1 (PTC) | X 14, Y 50, in der +5V-Zuführung von J2.1 | Rückstellsicherung (optional bestückt) |
| R1, R3, R5 (Pull-up 10 k) | X 8…16, Y 30…42 | an +5V, nahe U1 |
| R2, R4, R6 (Serie 1 k) | **nahe J1** (X 20…28, Y 10…18) | wirksame Tiefpass-Reihenfolge: R vor C, R dicht am Steckereingang |
| C4, C5, C6 (10 n) | **nahe den Portpins von U1** (X 18…24, Y 24…34) | Tiefpass-Kondensator dicht am µC |
| D1, D2, D3 (BAT54S) | zwischen C4/C5/C6 und U1 | Klemmdioden am gefilterten Signal (Pin 3 COM), Pin 1 → GND, Pin 2 → +5V |
| Q1 (BSS84), Q2 (MMBT3904) | X 46…54, Y 30…40, nahe U1 PA7 | Triac-Treiber, kurze Gate-Wege |
| Q3 (MMBT3904, DNP) | X 46, Y 24 | Alternativzweig, unbestückt |
| R7, R8 | bei Q1/Q2 | Basis-/Gate-Widerstände |
| R9 (R_S) | X 56, Y 32, zwischen Q1.D und J1.9 | Vorwiderstand Triac-Eingang; Wert nach O-2 |
| R10 (DNP) | bei Q3 | Alternativzweig |
| JP1 „5V" / JP2 „15V" | X 50…56, Y 44…50, nebeneinander, Silk „5V" / „15V" | VDRV-Auswahl. Genau einer gebrückt (Vorgabe JP2) |
| R11, R12, R13 (CHAIN) | X 50…60, Y 18…26 | R11/R12 nahe J2.7, R13 nahe J3.7 |
| R15 + D4 (Status-LED) | X 60, Y 12, D4 an der Kante sichtbar | Betriebsanzeige |
| J6 (UPDI) | X 66, Y 24, Stiftleiste 1×3 an der rechten Kante | im bestückten Zustand zugänglich, Silk 1-2-3 = GND-UPDI-+5V |

**C1, C2** mit kurzen Stichen an eine **durchgehende Massefläche auf der Rückseite**.
**C4, C5, C6** an die Portpins, **R2/R4/R6** an J1 — die Reihenfolge zählt für den Tiefpass.

### Bus-Zone (Oberkante, Y ≈ 48…60)

| Ref | Position | Begründung |
|---|---|---|
| J2 (Bus in) | X 4, Y 54, Wannenstecker links oben | Kette von der vorherigen Karte |
| J3 (Bus out) | X 66, Y 54, Wannenstecker rechts oben | Kette zur nächsten Karte |
| R16 (120 Ω, 1206) | X 10, Y 48, **nahe J2** | Busabschluss |
| JP3 „TERM" | X 14, Y 48, nahe R16, Silk „TERM" | Abschluss zuschaltbar, nur an den Busenden geschlossen |
| R14 (0 Ω, 1206) | X 22, Y 46 | Brücke +5V → VSENS; auslötbar, dann VSENS über TP6 |

**RS485_A / RS485_B** als Paar mit konstantem Abstand (0,3 mm) von U2 zu J2/J3,
keine Stichleitungen. R16 und JP3 unmittelbar an J2.

### Testpunkte

| Ref | Netz | Position |
|---|---|---|
| TP1, TP2 | PB5, PB4 (Reserve) | frei, am Rand der Logik-Zone |
| TP3 | PULSE_NULL_RAW | nahe J1.7 |
| TP4 | TRIAC_CTRL | nahe J1.9 / R9 |
| TP5 | CHAIN_IN | nahe R11/R12 |
| TP6 | VSENS | nahe R14 |
| TP7 | GND | direkt neben TP3 und TP4 (Tastkopf-Masse) |

Ø 1,5 mm, auf der Bestückungsseite.

---

## 3. Leiterbahn-Vorgaben (aus Schaltplan 8.2)

| Netz | Breite | Abstand |
|---|---|---|
| AC1, AC2 | ≥ 1,0 mm | ≥ 2,0 mm zu allen Logiknetzen |
| +5V, VSENS, GND | ≥ 0,5 mm | Standard |
| RS485_A, RS485_B | 0,3 mm, als Paar | Standard |
| übrige Signale | 0,25 mm | Standard |

Massefläche auf der Rückseite durchgehend, unterbrochen nur im AC-Streifen an der
Unterkante.

---

## 4. Checkliste vor dem Routen

- [ ] Pin 1 von J1 (Buchsenleiste), J2 und J3 im Silk eindeutig markiert
- [ ] J1: Pin-1-Dreieck **und** Klartext „PIN1 → ANZEIGE PIN1" auf der
      Bestückungsseite (J1 ist nicht kodiert, siehe `docs/pruefpunkte-j1-buchsenleiste.md`)
- [ ] J1-Belegung gegen die Anzeigenplatine 6281 3 160-00 geprüft (Schaltplan 4.1)
- [ ] JP1 „5V", JP2 „15V", JP3 „TERM" beschriftet
- [ ] Feld für die Modulnummer auf dem Silk vorgesehen
- [ ] AC-Streifen frei von Logik, Rückseiten-Massefläche dort unterbrochen
- [ ] C1/C2 an den IC-Versorgungspins, nicht am Stecker
- [ ] 4 Befestigungsbohrungen 3,2 mm, 4 mm von den Ecken, keine Bahn darunter

---

## 5. Offene Punkte mit Layout-Bezug

| Nr | Wirkung |
|---|---|
| O-2 | Bestückungsvariante Q-15 / Q-5 / S und Wert von R9. Alle Positionen sind vorgesehen, das Layout ändert sich nicht. |
| O-5 | Werte von R1/R3/R5; ggf. R14 auslöten und VSENS über TP6 einspeisen. |
| P-1 / P-2 | In `docs/pruefpunkte-t4.md` freigegeben (28.08.2026). |
