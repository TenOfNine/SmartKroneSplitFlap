# Platzierung & Layout Zentralsteuerung (Master) — Backlog T11

| Feld | Wert |
|---|---|
| Bezug | `docs/schaltplan-master.md` Kapitel 8, Netzliste `hardware/master/master.net` |
| Platine | 68 × 54 mm, 2 Lagen, 1,6 mm, 35 µm Cu, HASL bleifrei |
| Befestigung | 4 × Bohrung 3,2 mm, je 4 mm von den Ecken |
| Status | **Geroutet (Planungsstand).** `tools/gen_master_pcb.py` platziert, `tools/route_master.py` (FreeRouting 2.3.0 + `finish_routes.py` + Masseflächen) verdrahtet: **DRC 0 Fehler, 0 unverdrahtet**, 2 Lagen, GND-Fläche F.Cu + B.Cu mit Stitching. Es bleiben 3 kosmetische Silk-Warnungen (Referenztext von R4/U2 über Lötstopp, ein Textüberlapp) — im GUI beim Feinlayout zu bereinigen. Freigabe erst nach `docs/symbolpruefung-master.md`. |
| Datum | 01.09.2026 |

Koordinaten hier in KiCad-Konvention (Ursprung oben links, Y nach unten).

## Erzeugen

```bash
python  tools/build_krone_master_symbols.py
python  tools/gen_master_sch.py --erc --pdf --png
/usr/bin/python3 tools/gen_master_pcb.py --png --drc      # Platzierung + Vorschau
/usr/bin/python3 tools/route_master.py                    # FreeRouting + Flächen (+ Silk-Marks)
/usr/bin/python3 tools/gen_master_pcb.py --render         # 3D-Ansicht oben/unten -> docs/render-master-*.png
/usr/bin/python3 tools/gen_master_manufacturing.py        # Gerber/BOM/CPL
```

`docs/render-master-top.png` / `-bottom.png` sind für die Sichtprüfung der
U1-Einbaulage: **Pin-1-Punkt (= 5V) rechts oben** neben dem „USB-C"-Aufdruck,
„ANT: keine Cu-Flaeche" an der Unterkante. Das Modul selbst hat kein 3D-Modell —
der Bestückungsdruck trägt die Aussage.

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
       │ │  U1        │ C4         │Bus │                 │
       │ │ ESP32-C3   │      C1 C5 └────┘   ┌J3┐          │
       │ │ (USB-C ↑)  │   R1 R2 R3  U2      │IO│  (rechte │  Logik-Zone (Mitte)
       │ │            │   U3 C2 R5 R4 R7    │RSV│  Kante)  │
       │ │  ANT ↓     │                     └──┘          │
       │ └────────────┘   TP1..TP7                        │
       │ ┌J1┐  FB1 C3   R6 D1        ┌───── J4 ──────┐     │  Versorgung + LED
  Y=54 └─┴──┴────────────────────────┴───────────────┴─────┘   (unten)
```

| Bereich | Inhalt |
|---|---|
| **U1 links** | Modul belegt das linke Drittel. Antenne + Cu-Keepout an der Unterkante des Moduls. Der Modulkörper endet ~2,7 mm vor der Oberkante; die USB-C-Buchse ragt knapp daran. **Für klaren Überstand im GUI eine kleine `Edge.Cuts`-Aussparung unter der USB-C-Buchse einfügen** — U1 weiter hochsetzen sprengt den Routingkanal an der Oberkante (getestet: 13 `copper_edge_clearance`-Fehler). |
| **Logik Mitte** | U2 (RS-485) nahe J2, U3 (CHAIN) darunter, Bias/Abschluss R1–R3 zwischen U2 und J2. |
| **Bus oben rechts** | J2 (Wannenstecker), Flachband nach oben. |
| **Versorgung unten links** | J1 (Schraubklemme), C3 (Bulk), FB1 (Ferrit) in Richtung U1. |
| **LED + Ader 9 unten** | D1 an der Kante sichtbar, JP1, J4 (Boost-Steckplatz, DNP). |
| **rechte Kante** | J3 (Reserve-Header), im bestückten Zustand zugänglich. |

## Leiterbahn-Vorgaben

| Netz | Breite |
|---|---|
| +5V, +5V_IN, +3V3, +15V, ADER9 | 0,8 mm (Netzklasse „Power") |
| alle Signale | 0,5 mm (Netzklasse „Default") |
| GND | Massefläche F.Cu + B.Cu, Stitching-Raster 5 mm |

Kein AC-Netz. Massefläche durchgehend, ausgespart nur im Antennenbereich unter U1
(Footprint-Keepout).

## Checkliste vor dem Routen / vor der Fertigung

- [ ] `docs/symbolpruefung-master.md` freigegeben (M-1: U1-Pinbelegung + Einbaulage)
- [ ] U1-Footprint in der 3D-Ansicht: USB-C oben, Antenne unten, 5V-Pad rechts oben,
      BOOT/RST im eingesteckten Zustand erreichbar
- [ ] J2 Pin 1 im Silk markiert, Aderbelegung gegen Daughter-Card-J2/J3 geprüft
- [ ] JP1 „ADER9 / 5V / 15V" beschriftet, Auslieferung offen
- [ ] Antennen-Keepout: keine Massefläche, keine Vias unter der Modul-Unterkante
- [ ] 4 Befestigungsbohrungen frei von Bahnen
- [ ] Modulbezeichnung / Revision auf dem Silk

## Offene Punkte mit Layout-Bezug

| Nr | Wirkung |
|---|---|
| M-1 | Modul-Footprint + Einbaulage (Gate). |
| M-2 | J4-Steckplatz (Boost) vorgesehen, unbestückt — Layout ändert sich nicht. |
| M-3 | U3 vs. R7-Brücke — beide Plätze vorhanden. |
| O-2 | JP1-Stellung / ob J4 bestückt wird. |
