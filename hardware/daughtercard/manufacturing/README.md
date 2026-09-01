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

### Vor der Bestellung LCSC-Nummer nachtragen

Folgende Positionen haben in `BOM.csv` noch keine LCSC-Nummer und
muessen im JLCPCB-Warenkorb zugeordnet werden:

- **D1** -- BAT54S
- **D2** -- BAT54S
- **D3** -- BAT54S
- **Q1** -- BSS84 (P-MOSFET)
- **D4** -- LED gruen 0805

Hintergrund und Kandidaten: `docs/jlc-bestueckung.md` (D-4 fuer Q1,
Abschnitt 2 fuer D1-D3 / D4).

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
