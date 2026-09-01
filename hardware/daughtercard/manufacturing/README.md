# Fertigungspaket Daughter Card

Erzeugt von `tools/gen_manufacturing.py` aus der committeten
`daughtercard.kicad_pcb`. Bei jeder Layoutaenderung neu erzeugen.

| Datei | Zweck |
|---|---|
| `daughtercard-gerbers.zip` | Gerber + Excellon-Bohrdatei, komplett. Bei JLCPCB hochladen. |
| `gerber/` | dieselben Dateien einzeln (Kontrolle im Gerber-Viewer) |
| `BOM.csv` | Bestueckliste fuer den SMT-Dienst (JLCPCB-Format) |
| `CPL.csv` | Bestueckungsplan / Pick&Place (JLCPCB-Format) |

## Platine

- 74 x 60 mm, 2 Lagen, 1,6 mm FR4, 35 um Kupfer.
- **Loetstoppmaske schwarz, Bestueckungsdruck weiss** (im Lagenaufbau gesetzt; bei JLCPCB die passende Option waehlen).
- Oberflaeche: HASL bleifrei genuegt.
- Maker-Kennzeichnung (GitHub-Marke + "TenOfNine") auf der Rueckseiten-Silkscreen.

## SMT-Bestueckung

`BOM.csv` / `CPL.csv` enthalten **30 SMD-Bauteile**, alle auf der
Oberseite. Nicht enthalten: DNP (Q3, R10), Loetjumper JP1-JP3, Testpunkte
TP1-TP7, Bohrungen -- und **die Steckverbinder J1-J6, die von Hand geloetet
werden**.

> Die `CPL.csv`-Drehungen kommen unveraendert aus KiCad. JLCPCB rechnet
> fuer manche Gehaeuse (SOT-23, SOIC, LED) eine eigene Referenzdrehung an.
> Im JLC-Vorschaufenster **jedes SOT-23 (Q1, Q2, D1-D3), U1, U2 und die
> LED D4 einzeln auf Polaritaet/Pin-1 pruefen** und die Drehung dort
> korrigieren, nicht in der CSV.

### Bauteil-Hinweise

- Alle Positionen haben eine LCSC-Nummer -- die Bestueckung bleibt im
  **Economic PCBA** (die gruene LED ist als 0805 `C2297` gesetzt, nicht 0201).
- **Extended** (je einmalig ~3 USD Ruestkosten, Economic-tauglich):
  U1 `C614136`, U2 `C94206`, Q1 `C8492` (BSS84), D1-D3 `C19726` (BAT54S).
- **U1 ATtiny1616-SN (`C614136`)**: JLC-Lager ist knapp (Groessenordnung
  einige Dutzend). Vor der Bestellung Bestand pruefen; ggf. selbst nachloeten
  (SOIC-20, 300 mil, gut handlötbar) und aus der BOM/CPL nehmen.

## Von Hand zu loeten (nicht im SMT-Auftrag)

| Ref | Wert | Footprint | Referenzteil |
|---|---|---|---|
| J1 | Anzeige | `PinSocket_2x05_P2.54mm_Vertical` | Buchsenleiste 2x5, 2,54 mm, gerade (BKL 10120960) |
| J2 | Bus in | `IDC-Header_2x05_P2.54mm_Vertical` | Wannenstecker 2x5, 2,54 mm, gerade |
| J3 | Bus out | `IDC-Header_2x05_P2.54mm_Vertical` | Wannenstecker 2x5, 2,54 mm, gerade |
| J4 | 42V~ in | `TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal` | Schraubklemme 2-polig, RM 5,08 mm |
| J5 | 42V~ out | `TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal` | Schraubklemme 2-polig, RM 5,08 mm |
| J6 | UPDI | `PinHeader_1x03_P2.54mm_Vertical` | Stiftleiste 1x3, 2,54 mm |

> ⚠️ **J1 ist nicht kodiert** und fuehrt an Pin 2/4 die 42 V~. Vor dem
> ersten Einschalten die Drehlage und den Durchgang gegen die
> Anzeigenplatine pruefen -- siehe `docs/pruefpunkte-j1-buchsenleiste.md`
> und Schaltplan Kapitel 4.1 / Pruefliste 9.

## Netzklassen im Layout

Alle Leiterbahnen 0,5 mm (an SOIC-Pins auf 0,375 mm verjuengt), nur
AC1/AC2 = 1,5 mm. GND ueber die Masseflaeche auf beiden Lagen. Zum
Spannungsabfall der +5V-Kette bei 0,5 mm siehe `docs/jlc-bestueckung.md` D-5.
