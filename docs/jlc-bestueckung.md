# JLCPCB-Bestückung der Daughter Card

| Feld | Wert |
|---|---|
| Zweck | Vorbereitung der SMT-Bestückung bei JLCPCB: LCSC-Nummern, Basic- vs. Extended-Part-Einordnung, zweite Anschlussbild-Prüfung der Symbole, offene Entscheidungen. |
| Bezug | Nutzeranforderung „so viele Basic Parts wie möglich" · CLAUDE.md Regel 1 (keine Nummer ungeprüft übernehmen) · Regel 5 (Pinbelegung gegen Datenblatt) |
| Stand | 31.08.2026 — **Entwurf, noch nicht bestellt.** Bauteilpositionen ändern sich noch. |
| Werkzeug | `tools/gen_daughtercard_pcb.py --jlc` erzeugt `hardware/daughtercard/jlc/BOM.csv` + `CPL.csv` (`.gitignore`, Bauartefakt). |

---

## 1. Warum Basic Parts

JLCPCB unterscheidet **Basic Parts** (im Bestücker dauerhaft geladen, keine
Rüstkosten) und **Extended Parts** (einmalige „Extended Part Fee", derzeit
ca. 3 USD je Position, unabhängig von der Stückzahl). Für eine Kleinserie von
10 Platinen fallen die Rüstkosten prozentual stark ins Gewicht.

Alle Chip-Widerstände und -Kondensatoren der Bauformen 0402/0603/0805/1206 bei
JLCPCB sind Basic Parts. Die Schaltung ist so ausgelegt, dass **nur die drei
aktiven Halbleiter U1, U2 und der BAT54S** zwingend Extended sind.

## 2. Stückliste mit LCSC-Zuordnung

Quelle der Basic-Nummern: JLCPCB-„Basic Parts Library"-Liste (Snapshot in
`/tmp/jlcbasic.csv` zum Bearbeitungszeitpunkt). **Jede Nummer ist vor der
Bestellung im JLCPCB-Parts-Manager gegen Wert, Bauform und Anschlussbild zu
prüfen** — der Snapshot kann veraltet sein und Basic/Extended-Einstufungen
ändern sich.

Hinterlegt in `tools/gen_daughtercard_sch.py`, Dict `LCSC`.

| Ref | Wert | Bauform | LCSC | Kategorie | Bemerkung |
|---|---|---|---|---|---|
| R1, R3, R5, R7 | 10 kΩ | 0805 | C17414 | Basic | |
| R2, R4, R6, R11, R13, R15 | 1 kΩ | 0805 | C17513 | Basic | |
| R8, R12 | 100 kΩ | 0805 | C17407 | Basic | |
| R9 | 4,7 kΩ | 0805 | C17673 | Basic | |
| R16 | 120 Ω | 0805 | C17437 | Basic | **war 1206** — siehe Entscheidung D-1 |
| R14 | 0 Ω | 1206 | C17888 | Basic | Handlöt-Brücke, Bauform bewusst groß |
| R10 | 10 kΩ | 0805 | (C17414) | — | **DNP**, nicht bestückt |
| C1, C2 | 100 nF / 50 V | 0805 | C49678 | Basic | |
| C4, C5, C6 | 10 nF / 50 V | 0805 | C1710 | Basic | |
| C3 | 10 µF / 50 V | 1206 | C13585 | Basic | Reserve-Bulk; siehe D-2 |
| Q2 | MMBT3904 | SOT-23 | C20526 | Basic | NPN, Triac-Treiberstufe |
| Q3 | MMBT3904 | SOT-23 | (C20526) | — | **DNP**, nicht bestückt |
| U1 | ATtiny1616-SNR | SOIC-20 | C614136 | **Extended** | ~1,57 USD + Rüstkosten |
| U2 | TP8485E-SR | SOIC-8 | C94206 | **Extended** | ~0,30 USD + Rüstkosten |
| D1, D2, D3 | BAT54S | SOT-23 | — | **Extended** | im Basic-Snapshot nicht vorhanden; im Cart wählen |
| Q1 | BSS84 | SOT-23 | — | offen | Kandidat **C8492** (P-MOSFET 50 V / 130 mA) — vor Bestellung Anschlussbild prüfen |
| D4 | LED grün | 0805 | — | offen | im Cart eine Basic-LED wählen (z. B. KT-0805G) |
| F1 | 0 Ω | 1206 | C17888 | Basic | 0-Ω-Brücke statt PTC (D-3, umgesetzt) |

**Von Hand zu löten (weder in BOM noch CPL):** J1–J6 (Stiftwannen,
Schraubklemmen, UPDI-Stift). **Nicht bestückt:** JP1–JP3 (Lötbrücken),
TP1–TP7 (Testpunkte), H1–H4 (Bohrungen), Q3 und R10 (DNP).

### Rüstkosten-Bilanz

| Position | Extended? | einmalige Fee |
|---|---|---|
| U1 ATtiny1616 | ja | ~3 USD |
| U2 TP8485E | ja | ~3 USD |
| D1–D3 BAT54S (eine Position) | ja | ~3 USD |
| Q1 BSS84 | evtl. Basic (C8492) | 0–3 USD |
| D4 LED | Basic wählbar | 0 USD |
| F1 (0-Ω-Brücke, C17888) | Basic | 0 USD |

Realistisch **3, maximal 5 Extended-Positionen → 9–15 USD** einmalig statt der
zuvor grob geschätzten 12–18 USD. Bei 10 Platinen sind das ca. 1 USD/Platine.

## 3. Zweite Anschlussbild-Prüfung (Pin 1 an der richtigen Stelle)

Gegenprüfung der KiCad-Symbole (`hardware/daughtercard/symbols/krone.kicad_sym`)
gegen die JLCPCB-/LCSC-Anschlussbilder. Ergänzt `docs/symbolpruefung.md` (T3)
und `docs/pruefpunkte-t4.md` (P-2).

| Bauteil | LCSC | Erwartet | Symbol krone | Ergebnis |
|---|---|---|---|---|
| MMBT3904 | C20526 | 1 = Basis, 2 = Emitter, 3 = Kollektor (SOT-23) | B/E/C an 1/2/3 | ✅ stimmt |
| BSS84 | C8492 (Kandidat) | 1 = Gate, 2 = Source, 3 = Drain (SOT-23) | G/S/D an 1/2/3 | ✅ stimmt |
| BAT54S | — | 1 = Anode D1, 2 = Kathode D2, 3 = gemeinsam (Reihenschaltung 1→3→2) | wie Datenblatt | ✅ stimmt; P-2-Belegung (1→GND, 2→+5V, 3→Signal) ist eine gültige Klemmschaltung |
| TP8485E-SR | C94206 | Standard-MAX485-Pinout: 1 RO, 2 /RE, 3 DE, 4 DI, 5 GND, 6 A, 7 B, 8 VCC | identisch | ✅ stimmt |
| ATtiny1616-SNR | C614136 | SOIC-20, Pin 1 = VDD, Pin 20 = GND | 1 = VDD, 20 = GND | ✅ stimmt |
| LED grün 0805 | — | Kathode = Pin 1 (bei JLC-Bestückung auf Polaritätsmarkierung im CPL achten) | K an Pin 1 | ✅ stimmt; **Einbaudrehung bei der Bestellprüfung kontrollieren** |

> ⚠️ Bei SOT-23 und der LED entscheidet zusätzlich die **Drehung im
> CPL/Pick&Place** über die korrekte Bestückung. `--jlc` übernimmt die Drehung
> aus der `.kicad_pcb`; nach der finalen Platzierung im JLC-Vorschaufenster
> (Preview) jede Diode/LED/Transistor-Orientierung einzeln kontrollieren.

## 4. Offene Entscheidungen

### D-1 — R16 von 1206 auf 0805

**Umgesetzt (31.08.2026), bei Bedarf zurückdrehbar.** 120 Ω gibt es im
Basic-Snapshot nur in 0805 (C17437), nicht in 1206. R16 ist der
RS-485-Abschluss­widerstand; die tatsächliche Verlustleistung liegt bei rund
50 mW (‹ 125 mW einer 0805). Footprint in `gen_daughtercard_sch.py` auf
`R_0805_2012Metric` geändert, Schaltplan und PCB neu erzeugt, ERC 0/0.

### D-2 — C3 (10 µF Reserve-Bulk)

Bleibt vorerst **1206 / 50 V (C13585, Basic)**. Alternative 0805 / 25 V
(C15850, ebenfalls Basic) spart etwas Platz; 25 V genügen an +5 V reichlich.
Umstellung nur sinnvoll, wenn im Layout der Platz an C3 knapp wird. Keine
Rüstkosten in beiden Fällen.

### D-3 — F1: 0-Ω-Brücke statt PTC ✅ umgesetzt (31.08.2026)

F1 liegt im **Durchgangspfad von +5V** (J2.1 → F1 → +5V-Schiene → J3.1). Bei
10 Modulen in Reihe führt der erste Modulabschnitt die Summe aller nachfolgenden
Module (~0,6 A) — über dem Hold-Strom einer 0,5-A-PTC. Eine PTC an dieser Stelle
würde bei voller Kette auslösen bzw. dauerhaft im hochohmigen Bereich hängen.

**Umgesetzt (Betreiberfreigabe 31.08.2026):** F1 ist eine 0-Ω-Brücke.
Symbol `krone:R`, Wert `0R`, Footprint `Resistor_SMD:R_1206_3216Metric`,
LCSC **C17888** (Basic, wie R14). Fail-Safe übernehmen der Firmware-Watchdog
(1 s) und die Laufzeitgrenze nach Fehlercode 0x05 (Spez. 8), nicht die
Versorgungssicherung. Schaltplan v0.6, Änderungshistorie fortgeschrieben.

### D-4 — Q1 BSS84: LCSC-Nummer festlegen

C8492 aus dem Basic-Snapshot ist ein P-Kanal-MOSFET 50 V / 130 mA in SOT-23 und
entspricht dem BSS84-Datenblatt. **Vor der Bestellung** im JLCPCB-Parts-Manager
Anschlussbild (G/S/D an 1/2/3) und Basic-Status bestätigen, dann in `LCSC`
eintragen.

### D-5 — Spannungsabfall der +5V-Kette bei 0,5-mm-Bahnen

Der Betreiber hat vorgegeben, alle Leiterbahnen außer AC auf 0,5 mm zu ziehen.
0,5 mm / 35 µm trägt nach IPC-2221 rund 1,3 A — der Kettenstrom von ~0,6 A auf
+5V bei 10 Modulen ist damit stromtragfähig. Der **Spannungsabfall** steigt
jedoch: on-board grob 0,3 V über die 10-Modul-Kette, dazu Stecker- und
Flachbandkabel-Widerstand. +5V −5 % ist 4,75 V; die Hall-Sensoren auf der
Anzeigenplatine brauchen ihre Nennspannung.

**Optionen:**

1. Zentrale 5-V-Schiene am Netzteil auf ~5,1–5,2 V einstellen.
2. Weniger Module je Netzteil-Einspeisung (Kette in zwei Stränge teilen).
3. Falls doch breite +5V-Bahnen gewünscht sind: in
   `tools/gen_daughtercard_pcb.py` die `Power`-Netzklasse wieder auf 1,5 mm
   setzen und neu routen (`tools/route_daughtercard.py`).

Ergibt die Messung O-5b, dass VSENS ohnehin aus einer separaten 6-V-Quelle
gespeist werden muss (R14 raus, Einspeisung über TP6), entschärft das den
Punkt für die Sensorversorgung.

## Fertigung — schwarze Platine

Betreiber-Vorgabe: **schwarze Lötstoppmaske, weißer Bestückungsdruck.** Im
Lagenaufbau der `.kicad_pcb` gesetzt (`tools/add_silk_marks.py`: Maske „Black",
Silk „White"). Bei der JLCPCB-Bestellung die entsprechende Option wählen —
schwarze Maske ist dort ohne Aufpreis, kann aber die Lieferzeit um ein bis zwei
Tage verlängern.

**Maker-Kennzeichnung** auf der Rückseiten-Silkscreen:

- GitHub-Marke (octicon `mark-github`), ~8,5 mm, als Footprint
  `footprints/logos.pretty/Logo_GitHub.kicad_mod` (aus dem SVG getraced).
- Text „TenOfNine" (Repository-Owner), 2,2 mm.

Beides erzeugt `tools/add_silk_marks.py` idempotent aus der vorhandenen
`.kicad_pcb`.

## 5. Export-Werkzeug

```bash
# nach der finalen Bauteilplatzierung:
/usr/bin/python3 tools/gen_daughtercard_pcb.py --jlc
```

Erzeugt aus der committeten `daughtercard.kicad_pcb`:

- **`jlc/BOM.csv`** — Spalten `Comment, Designator, Footprint, LCSC Part #`,
  nach Wert/Bauform/LCSC gruppiert. Positionen ohne LCSC-Nummer werden auf der
  Konsole gemeldet.
- **`jlc/CPL.csv`** — Spalten `Designator, Mid X, Mid Y, Layer, Rotation`,
  Koordinaten über `kicad-cli pcb export pos` (Ursprung = Bohrdatei-Ursprung,
  Y-Achse nach oben — JLCPCB-Konvention).

Ausgeschlossen: `#`-Referenzen, H\*, TP\*, JP\*, J1–J6, DNP (Q3, R10).

`jlc/` steht in `.gitignore` — die Dateien werden **nicht** versioniert, weil sie
sich mit jeder Platzierungsänderung ändern und aus dem Repo jederzeit neu
erzeugbar sind.

## 6. Fertigungspaket (committet)

`tools/gen_manufacturing.py` legt das vollständige Bestellpaket unter
`hardware/daughtercard/manufacturing/` ab — **committet**, weil das Layout
jetzt steht:

| Datei | Inhalt |
|---|---|
| `daughtercard-gerbers.zip` | Gerber (F/B Cu, Paste, Silk, Mask, Edge.Cuts) + Excellon-Bohrdatei + Map + `.gbrjob` |
| `gerber/` | dieselben Dateien einzeln |
| `BOM.csv` / `CPL.csv` | SMT-Bestückung, JLCPCB-Format — **ohne J1–J6** (Handlötung), ohne DNP/JP/TP/H |
| `README.md` | Bestellhinweise: schwarze Maske / weißer Druck, LCSC-Nachtrag für D1–D3/Q1/D4, JLC-Drehungsprüfung, Handlöt-Liste, J1-Verpolwarnung |

Bei jeder Layoutänderung neu erzeugen. Die 5 Positionen ohne LCSC-Nummer
(D1–D3, Q1, D4) trägt der Nutzer im JLCPCB-Warenkorb nach.
