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
| F1 | PTC 0,5 A | 1206 | — | offen | siehe Entscheidung D-3 |

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
| F1 PTC | siehe D-3 | 0–3 USD |

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

### D-3 — F1 (Rückstellsicherung, laut Spezifikation optional)

`schaltplan-daughtercard.md` 3 / 8: „PTC 0,5 A, optional. Ist F1 nicht bestückt,
wird +5V_IN mit +5V gebrückt." Drei Wege:

1. **Echte PTC** (z. B. Littelfuse/Bourns 0,5 A Hold, 1206) — meist Extended,
   ~3 USD Rüstkosten + Bauteil. Behält die Schutzfunktion.
2. **0-Ω-Brücke 1206 (C17888, Basic)** an F1s Stelle — keine Kosten, keine
   Schutzfunktion. Erfordert, F1s Footprint von `Fuse:Fuse_1206` auf
   `Resistor_SMD:R_1206` zu ändern (elektrisch/mechanisch identische Pads).
3. **F1 als DNP**, +5V_IN↔+5V von Hand mit Lötklecks über die 1206-Pads
   gebrückt.

**Empfehlung:** Weg 2 für die erste Serie (Kosten minimal, Absicherung
übernimmt ohnehin der Firmware-Watchdog + Laufzeitgrenze laut Spez. 8).
Die PTC bleibt eine spätere Nachrüstoption. **Noch nicht umgesetzt** — wartet
auf Freigabe, weil es eine Footprint-Änderung ist.

### D-4 — Q1 BSS84: LCSC-Nummer festlegen

C8492 aus dem Basic-Snapshot ist ein P-Kanal-MOSFET 50 V / 130 mA in SOT-23 und
entspricht dem BSS84-Datenblatt. **Vor der Bestellung** im JLCPCB-Parts-Manager
Anschlussbild (G/S/D an 1/2/3) und Basic-Status bestätigen, dann in `LCSC`
eintragen.

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
