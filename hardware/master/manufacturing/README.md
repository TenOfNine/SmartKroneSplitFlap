# Fertigungspaket Zentralsteuerung (Master)

Erzeugt von `tools/gen_master_manufacturing.py` aus der committeten
`master.kicad_pcb`. Bei jeder Layoutaenderung neu erzeugen.

| Datei | Zweck |
|---|---|
| `master-gerbers.zip` | Gerber + Excellon-Bohrdatei, komplett. Bei JLCPCB hochladen. |
| `gerber/` | dieselben Dateien einzeln (Kontrolle im Gerber-Viewer) |
| `BOM.csv` | Bestueckliste fuer den SMT-Dienst (JLCPCB-Format) |
| `CPL.csv` | Bestueckungsplan / Pick&Place (JLCPCB-Format) |

## Platine

- 68 x 54 mm, 2 Lagen, 1,6 mm FR4, 35 um Kupfer.
- **Loetstoppmaske schwarz, Bestueckungsdruck weiss** (im Lagenaufbau gesetzt;
  bei JLCPCB die Option "Black solder mask, White silkscreen" waehlen).
- Oberflaeche: HASL bleifrei genuegt.
- Maker-Kennzeichnung (GitHub-Marke + "TenOfNine") auf der Rueckseiten-Silkscreen.
- **Antennenbereich unter U1** (Unterkante des Moduls): Massefläche ist dort
  ausgespart (Footprint-Keepout). Wenn moeglich das Modul so einbauen, dass
  die Antenne ueber die Platinenkante ragt.

## Bestueckung

`BOM.csv` / `CPL.csv` enthalten **15 SMD-Bauteile** (alle Oberseite,
0805 / SOIC-8 / SOT-23-5). Der Master ist ein Einzelstueck -- die komplette
Handbestueckung ist zumutbar; JLCPCB-SMT ist optional.

**Immer von Hand** zu bestuecken (nicht im SMT-Auftrag):

| Ref | Teil |
|---|---|
| J1 | Schraubklemme 2-polig, RM 5,08 mm (+5V IN) |
| J2 | Wannenstecker 2x5, 2,54 mm, gerade (Bus zur ersten Daughter Card) |
| J3 | Stiftleiste 1x4, 2,54 mm (Reserve GPIO0/1/7 + GND) |
| J4 | Aufwaertswandler-Modul 5 V -> 15 V (nur falls O-2 Deutung 1), DNP |
| JP1 | Loetbruecke 3-Wege: Ader 9 = offen / +5V / +15V |
| U1 | ESP32-C3 Super Mini, gesteckt in 2x 1x8 Buchsenleisten 2,54 mm |

> Die `CPL.csv`-Drehungen kommen unveraendert aus KiCad. JLCPCB rechnet
> fuer SOT-23 / SOIC / LED eine eigene Referenzdrehung an -- im
> JLC-Vorschaufenster **U2, U3 und D1 einzeln auf Pin 1 / Polaritaet pruefen**.

## Bauteil-Hinweise

- **U1 ESP32-C3 Super Mini:** Aftermarket-Modul, Pinbelegung + Einbaulage in
  `docs/symbolpruefung-master.md`. USB-C/BOOT/RST an der Oberkante, Antenne
  (Aufdruck "ESP32-C3 Super Mini") an der Unterkante. 5V-Pin = rechts oben.
- **U3 74LVC1G17** hebt CHAIN von 3,3 V auf 5 V. Zeigt der Test, dass 3,3 V
  direkt genuegen (M-3), wird U3 weggelassen und **R7 (0 Ohm)** bestueckt.
- **J4 / Step-up** bleibt unbestueckt, bis O-2 zeigt, dass die Anzeige an
  Pin 9 mehr als 5 V braucht. Dann Modul auf 15,0 V einstellen, JP1 auf +15V.
- **JP1** bei Auslieferung offen (Ader 9 unbeschaltet).
