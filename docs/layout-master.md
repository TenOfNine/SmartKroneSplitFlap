# Platzierung & Layout Zentralsteuerung (Master) — Backlog T11

| Feld | Wert |
|---|---|
| Bezug | `docs/schaltplan-master.md` Kapitel 8, Netzliste `hardware/master/master.net` |
| Platine | 68 × 54 mm, 2 Lagen, 1,6 mm, 35 µm Cu, HASL bleifrei |
| Befestigung | 4 × Bohrung 3,2 mm, je 4 mm von den Ecken |
| Status | **Fertig, Rev. 0.2.** Skript-Routing (`tools/patch_master_pcb.py`) + **GUI-Feinlayout vom Betreiber** (Bahnführung, Bestückungsdruck). Die committete `master.kicad_pcb` ist der finale Stand — nicht mehr regenerieren. **DRC 0 Fehler / 0 Warnungen / 0 unverdrahtet**, 2 Lagen, GND-Fläche F.Cu + B.Cu mit Stitching. Symbolprüfung `docs/symbolpruefung-master.md` **freigegeben** (10.09.2026). Bereit zur Bestellung. |
| Datum | 10.09.2026 |

**Rev. 0.2:** neu Q1 + R8 (Verpolschutz) links unten bei J1, D2 (RS-485-TVS) im
freien Feld unter dem RS-485-Block. Keine Edge.Cuts-Aussparung für U1 (Modul
steckbar/entnehmbar). Silk: „ANT: keine Cu-Fläche" am U1-Footprint entfernt
(Kupfer-Keepout bleibt), **+ / − auf F.SilkS neben J1** (`add_silk_marks.py`).
**Inkrementell geroutet** mit `tools/patch_master_pcb.py`
(Q1/R8/D2 in die Rev-0.1-Platine eingesetzt, Lücken per `finish_routes.py`
geschlossen, Masseflächen neu) — FreeRouting 2.3.0 hängt in der Dev-Umgebung
reproduzierbar; `route_master.py` hat dafür den `gui.enabled=false`-Fix +
Popen-Watchdog für spätere Vollläufe.

Koordinaten hier in KiCad-Konvention (Ursprung oben links, Y nach unten).

## Erzeugen

> **Die committete `master.kicad_pcb` ist der finale, vom Betreiber im GUI
> feinjustierte Stand (Rev. 0.2).** Nicht mehr neu erzeugen. Nur die
> abgeleiteten Ansichten und das Fertigungspaket werden aus ihr regeneriert:

```bash
/usr/bin/python3 tools/gen_master_pcb.py --preview        # docs/pcb-master.png (2D)
/usr/bin/python3 tools/gen_master_pcb.py --render         # docs/render-master-*.png (3D)
/usr/bin/python3 tools/gen_master_manufacturing.py        # Gerber/BOM/CPL
```

Der ursprüngliche Weg (bis zur GUI-Freigabe): Schaltplan aus der Netzliste, PCB
inkrementell mit `tools/patch_master_pcb.py` (Q1/R8/D2 in die geroutete
Rev-0.1-Platine, `finish_routes` + Masseflächen), dann
`tools/add_silk_marks.py --board hardware/master/master.kicad_pcb`. FreeRouting
2.3.0 (`route_master.py`) hängt in der Dev-Umgebung reproduzierbar.

`docs/render-master-{top,bottom}.png` = 3D-Ansicht (`kicad-cli pcb render`); das
ESP32-C3-Modul hat kein 3D-Modell → als Pad-Feld sichtbar, der Bestückungsdruck
(USB-C oben, „U1" unten) trägt die Aussage.

- `gen_master_pcb.py` verweigert den Neuaufbau, wenn die `.kicad_pcb` schon
  Leiterbahnen hat (`--force` überschreibt). Die Vorschau der gerouteten Platine
  erzeugt `route_master.py` selbst (`docs/pcb-master.png`).
- `add_silk_marks.py --board hardware/master/master.kicad_pcb` läuft **am Ende von
  `route_master.py` automatisch** — Silk-Texte und der Stackup-Block überleben den
  SES-Import nicht und müssen nach dem Routen gesetzt werden.

## Zonen

```
  Y=0  ┌─────────────────────────────────────────────────┐
       │ ┌────────────┐            ┌ J2 ┐   MountingHole  │  Bus-Zone (oben)
       │ │  U1        │ C4      C5 └────┘   ┌J3┐          │
       │ │ ESP32-C3   │      C1  D2          │IO│          │
       │ │ (USB-C ↑)  │   R1 R2 R3  U2      │RSV│ (rechte  │  Logik-Zone (Mitte)
       │ │            │   R7 U3 C2 R5 R4     └──┘  Kante)  │
       │ │  ANT ↓     │                                    │
       │ └────────────┘   TP1..TP7                         │
       │ ┌J1┐ Q1 R8  FB1 C3   R6 D1   ┌──── J4 ────┐       │  Versorgung + LED
  Y=54 └─┴──┴─────────────────────────┴────────────┴───────┘   (unten)
```

| Bereich | Inhalt |
|---|---|
| **U1 links** | Modul belegt das linke Drittel. Antenne + Cu-Keepout an der Unterkante des Moduls. Der Modulkörper endet ~2,7 mm vor der Oberkante. **Keine `Edge.Cuts`-Aussparung** — das Modul steckt in Buchsenleisten und wird zum Flashen entnommen (Betreiber-Entscheidung 10.09.2026). |
| **Logik Mitte** | U2 (RS-485) nahe J2, U3 (CHAIN) darunter, Bias/Abschluss R1–R3 zwischen U2 und J2, **D2 (RS-485-TVS)** im A/B-Pfad zwischen R1–R3 und J2. |
| **Bus oben rechts** | J2 (Wannenstecker), Flachband nach oben. |
| **Versorgung unten links** | J1 (Schraubklemme) → **Q1 + R8 (Verpolschutz)** → C3 (Bulk), FB1 (Ferrit) in Richtung U1. |
| **LED + Ader 9 unten** | D1 an der Kante sichtbar, JP1, J4 (Boost-Steckplatz, DNP). |
| **rechte Kante** | J3 (Reserve-Header), im bestückten Zustand zugänglich. |

## Leiterbahn-Vorgaben

| Netz | Breite |
|---|---|
| +5V, +5V_RAW, +5V_IN, +3V3, +15V, ADER9 | 0,8 mm (Netzklasse „Power") |
| alle Signale | 0,5 mm (Netzklasse „Default") |
| GND | Massefläche F.Cu + B.Cu, Stitching-Raster 5 mm |

Kein AC-Netz. Massefläche durchgehend, ausgespart nur im Antennenbereich unter U1
(Footprint-Keepout).

## Checkliste vor dem Routen / vor der Fertigung

- [ ] `docs/symbolpruefung-master.md` freigegeben (inkl. AO3401A + SM712, M-4)
- [ ] U1-Footprint in der 3D-Ansicht: USB-C oben, Antenne unten, 5V-Pad rechts oben
- [ ] J2 Pin 1 im Silk markiert, Aderbelegung gegen Daughter-Card-J2/J3 geprüft
- [ ] JP1 „ADER9 / 5V / 15V" beschriftet, Auslieferung offen
- [x] J1 mit + / − beschriftet (links = +5V, rechts = GND)
- [x] Referenztexte U2 / D2 / R4 unter das Bauteil gesetzt (`patch_master_pcb.py`)
- [ ] Antennen-Keepout: keine Massefläche, keine Vias unter der Modul-Unterkante
- [ ] 4 Befestigungsbohrungen frei von Bahnen
- [ ] Modulbezeichnung / Revision auf dem Silk

## Offene Punkte mit Layout-Bezug

| Nr | Wirkung |
|---|---|
| M-1 | **geschlossen** — Modul-Footprint + Einbaulage. |
| M-2 | J4-Steckplatz (Boost) vorgesehen, unbestückt — Layout ändert sich nicht. |
| M-3 | **entschieden** — U3 wird bestückt, R7 bleibt DNP-Reserveplatz. |
| M-4 | D2 (RS-485-TVS): Pinbelegung gegen Bourns-Datenblatt geprüft, ProTek-Spot-Check vor Bestellung. |
| O-2 | JP1-Stellung / ob J4 bestückt wird. |
