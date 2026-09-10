# Schaltplan Zentralsteuerung (Master) — KRONE REW Fallblattanzeige

| Feld | Wert |
|---|---|
| Baugruppe | Zentralsteuerung / RS-485-Master, Spezifikation Kapitel 7 |
| CPU | ESP32-C3 Super Mini (Aftermarket-Modul), steckbar in Buchsenleisten |
| Revision | 0.2 (10.09.2026) |
| Erzeugt von | `tools/gen_master_sch.py` → `hardware/master/master.kicad_sch` |
| Projektbibliothek | `hardware/master/symbols/krone_master.kicad_sym` (`tools/build_krone_master_symbols.py`) |
| Status | ERC 0/0, PCB geroutet (DRC 0 Fehler). Symbolprüfung `docs/symbolpruefung-master.md` **freigegeben** (01.09.2026; AO3401A + SM712 nachgetragen). M-3 (CHAIN-Pegel): U3 wird bestückt (Betreiber-Entscheidung). M-2 (Boost) an O-2 gebunden. |

**Rev. 0.2 (10.09.2026):** Verpolschutz am 5-V-Eingang (P-FET Q1 + R8) und
RS-485-TVS am Bus (D2). CHAIN-Pegelwandler U3 als feste Bestückung bestätigt. Für
U1 keine Edge.Cuts-Aussparung — das Modul ist zum Flashen aus den Buchsenleisten
entnehmbar. PCB inkrementell geroutet (`tools/patch_master_pcb.py`), ERC 0/0,
DRC 0/0, Fertigungspaket regeneriert.

**Verbindliche Quelle** für Netzliste und Bestückung ist Kapitel 6 dieses
Dokuments. Bei Widerspruch zur Spezifikation gilt die Netzliste (analog
`docs/schaltplan-daughtercard.md`).

---

## 1. Konventionen

- Bezeichner englisch, Fließtext deutsch (CLAUDE.md).
- Netznamen entstehen aus lokalen Pin-Labels; gleiche Namen = gleiches Netz.
- Alle Kleinteile 0805, ICs SOIC-8 / SOT-23-5. Der Master ist ein **Einzelstück**;
  die komplette Handbestückung ist zumutbar, JLCPCB-SMT ist optional.
- Die **42 V~ kommen nicht auf diese Platine.** Der Ringkerntrafo 2×18 V wird
  extern in Reihe geschaltet und direkt an die Schraubklemme der ersten
  Daughter Card verdrahtet (Spez. 8.1). Auf dem Master gibt es kein AC-Netz.

---

## 2. Blockschaltbild

```
  +5 V           Q1 AO3401A
  Netzteil       Verpolschutz          ESP32-C3 Super Mini (U1, steckbar)
  (J1)           (P-FET, High-Side)    ┌──────────────────────────────┐
    │ +5V_RAW    S ──┬── D  +5V_IN     │ 5V ─ Onboard-LDO ─ 3V3-Pin   │
    ├────────────────┤   │             │ UART1: TX(GPIO3) RX(GPIO4)    │
    │   R8 100k ─────┘   │             │ DE(GPIO10)  CHAIN(GPIO5)      │
    │   (G→GND, V_GS)    │             │ LED(GPIO6)   RSV GPIO0/1/7    │
    │                    ├── C3 47µF   └──┬─────┬──────┬──────┬────────┘
    │                    │               3V3  TX/RX   DE    CHAIN
    │                  FB1 60R ── +5V ─────┼──────┼─────┼──── U3 VCC (5 V)
    │                                 U3 VCC │      │     │       │
    │  +3V3 ── U2 (TP8485E) VCC ────── C4/C2 │      │     │   ┌───┴──┐ 74LVC1G17
    │            /RE = GND                   │      │     │   │  U3  │ 3,3→5 V
    │       DI ──┘   RO ── GPIO4             │      │     │   └───┬──┘ (nicht inv.)
    │       DE ──────────────────────────── GPIO10  │  R4 10k↓GND │
    │       A ─┬─ R2 680Ω → +3V3   (Fail-Safe-Bias, nur am Master) │
    │       B ─┤─ R3 680Ω → GND                                    │
    │         ├─ R1 120Ω  (Abschluss, fest)                        │
    │         ├─ D2  SM712: Pin1↔GND / Pin2↔GND je 7 V/12 V (TVS)  │
    │         └─ A,B ──────────────────────────────► J2 Ader 3 / 5 │
    │                              CHAIN ─ R5 100Ω ─► J2 Ader 7 ◄──┘
    ├─ +5V_IN ─ JP1 (offen / +5V / +15V) ──────────► J2 Ader 9
    └─ +5V_IN ─────────────────────────────────────► J2 Ader 1 (+5V)
                                                     J2 Ader 2/4/6/8/10 = GND

  Step-up 5 V→15 V (J4, Modul, DNP) ── +15V ── JP1 Pin 3   (nur falls O-2 Deutung 1)
```

---

## 3. Stückliste

### 3.1 Aktive Bauteile

| Ref | Bauteil | Gehäuse | LCSC | Anmerkung |
|---|---|---|---|---|
| U1 | ESP32-C3 Super Mini | Modul 18×22,5 mm, 2×8 Header | — | Aftermarket, steckbar; Einbaulage siehe `symbolpruefung-master.md` |
| U2 | TP8485E-SR | SOIC-8 | C94206 | RS-485, 3,3 V, Full-Fail-Safe, /RE fest auf GND |
| U3 | 74LVC1G17 | SOT-23-5 | C19829593 | CHAIN 3,3 V → 5 V, Schmitt-Buffer. Feste Bestückung (M-3 entschieden) |
| Q1 | AO3401A | SOT-23 | C15127 | Verpolschutz 5-V-Eingang, P-Kanal High-Side (−30 V, −4 A, R_DS(on) ≈ 55 mΩ). JLC Basic |
| D1 | LED grün | 0805 | C2297 | Betriebsanzeige (GPIO6) |
| D2 | SM712 / PSM712-LF-T7 | SOT-23 | C32677 | RS-485-TVS-Array am Bus, 7 V / 12 V asymmetrisch (−7…+12 V). JLC Basic. Pinbelegung `symbolpruefung-master.md` |

### 3.2 Passive Bauteile

| Ref | Wert | Gehäuse | LCSC | Funktion |
|---|---|---|---|---|
| R1 | 120 Ω | 0805 | C17437 | RS-485-Abschluss, **fest** (Spez. 7.1) |
| R2, R3 | 680 Ω | 0805 | C17798 | Fail-Safe-Bias A→+3V3 / B→GND (Spez. 5.1) |
| R4 | 10 kΩ | 0805 | C17414 | DE-Pulldown (kein Bus-Treiben im Reset) |
| R5 | 100 Ω | 0805 | C17408 | CHAIN-Serienwiderstand zum Bus |
| R6 | 1 kΩ | 0805 | C17513 | Vorwiderstand Status-LED |
| R7 | 0 Ω | 0805 | — | **DNP.** Reserve-Bypass von U3, falls sich 3,3 V direkt doch als ausreichend erweist (M-3) |
| R8 | 100 kΩ | 0805 | C149504 | V_GS-Widerstand für Q1 (Gate → Source, definierter Aus-Zustand) |
| C1, C2, C4 | 100 nF | 0805 | C49678 | Abblockung U2 / U3 / U1-5V |
| C3 | 47 µF / 25 V | 1206 | C76659 | Bulk am 5-V-Eingang (hinter Q1) |
| C5 | 10 µF | 0805 | C15850 | Stützkondensator +3V3 |
| FB1 | 60 Ω @ 100 MHz, 3,5 A | 0805 | C18305 | Ferrit, filtert **nur** den lokalen Logikzweig |

### 3.3 Steckverbinder / Mechanik (Handlötung)

| Ref | Bauteil | Funktion |
|---|---|---|
| — | 2× Buchsenleiste 1×8, 2,54 mm | Sockel für U1 (Modul entnehmbar) |
| J1 | Schraubklemme 2-polig, RM 5,08 mm | +5 V vom Netzteil |
| J2 | Wannenstecker 2×5, 2,54 mm | Bus zur ersten Daughter Card (Ader-Belegung Spez. 5.2) |
| J3 | Stiftleiste 1×4, 2,54 mm | Reserve: GPIO0, GPIO1, GPIO7, GND |
| J4 | Stiftleiste 1×4, 2,54 mm | Aufwärtswandler-Modul 5 V→15 V (**DNP**, siehe 5.6) |
| JP1 | Lötbrücke 3-Wege | Ader 9 = offen (Vorgabe) / +5 V / +15 V |
| H1–H4 | Bohrung 3,2 mm | Befestigung M3 |
| TP1–TP7 | Ø 1,5 mm | RS485_A, RS485_B, CHAIN_BUS, +3V3, +15V, GND, ADER9 |

---

## 4. Steckverbinder

### 4.1 J1 — Versorgung

| Pin | Signal |
|---|---|
| 1 | +5 V vom **separaten** Schaltnetzteil 5 V / 2 A (Spez. 8.2). Nicht mit dem 42-V~-Kreis verbunden. |
| 2 | GND |

Reihenfolge J1 → **Q1 (Verpolschutz)** → `+5V_IN` → C3 (Bulk) → FB1 (Ferrit) →
lokale Logik. Der Bus (J2.1) und der Ader-9-Jumper hängen an der geschützten,
aber **ungefilterten** Schiene `+5V_IN`, damit der Ferrit nicht den Kettenstrom
(bis ~2 A bei 10 Modulen) führt. `+5V_RAW` ist nur das kurze Stück Klemme → Q1.

**Verpolschutz.** Q1 (AO3401A, P-Kanal, High-Side): Source an `+5V_RAW`, Drain an
`+5V_IN`, Gate an GND, R8 = 100 kΩ Gate→Source. Bei richtiger Polung zieht
V_GS = −5 V den FET voll durch (R_DS(on) ≈ 55 mΩ → bei 2 A ~0,1 V Abfall, ~0,2 W).
Bei vertauschter Klemme sperrt der FET **und** seine Body-Diode → die gesamte
Kette (auch J2/Bus) bleibt geschützt. V_GS = 5 V liegt weit innerhalb ±12 V, kein
Gate-Zener nötig.

### 4.2 J2 — Bus zur ersten Daughter Card

Wannenstecker 2×5, Belegung **identisch zu Daughter-Card-J2/J3** (Spez. 5.2):

| Ader | Signal | Ader | Signal |
|---|---|---|---|
| 1 | +5 V | 2 | GND |
| 3 | RS-485 A | 4 | GND |
| 5 | RS-485 B | 6 | GND |
| 7 | CHAIN | 8 | GND |
| 9 | +15 V (Triac-Treiber, nur falls O-2) | 10 | GND |

Der 120-Ω-Abschluss (R1) sitzt fest am Master (ein Busende). Das andere Ende ist
die letzte Daughter Card mit gesetztem JP3.

### 4.3 J3 — Reserve

| Pin | Signal | Hinweis |
|---|---|---|
| 1 | GPIO0 | ADC1, frei; z. B. Helligkeitssensor für Auto-Dimmen |
| 2 | GPIO1 | ADC1, frei |
| 3 | GPIO7 | frei |
| 4 | GND | |

### 4.4 J4 — Aufwärtswandler-Modul (DNP)

Steckplatz (1×4) für ein handelsübliches Boost-Modul (z. B. MT3608-Breakout):

| Pin | Signal |
|---|---|
| 1 | +5 V (Eingang) |
| 2 | GND |
| 3 | +15 V (Ausgang) |
| 4 | GND |

Bleibt unbestückt, bis O-2 zeigt, dass die Anzeige an Pin 9 mehr als 5 V braucht.
Dann Modul-Trimmer auf **15,0 V** stellen, einstecken, JP1 auf +15 V.

---

## 5. Schaltungsblöcke

### 5.1 Versorgung

- Eingang 5 V an J1 von einem **eigenen** 5-V-Schaltnetzteil. Der 42-V~-Kreis
  (Motoren) und der 5-V-Kreis (Logik) bleiben vollständig getrennt — keine
  gemeinsame Ader, kein gemeinsamer Trafo (Spez. 8, CLAUDE.md Regel 4).
- **Q1 (Verpolschutz)** direkt hinter J1, siehe 4.1. Alles ab `+5V_IN` liegt
  hinter dem FET.
- Die ESP32-C3-Super-Mini versorgt sich über ihren **5V-Pin** aus dieser Schiene
  und erzeugt 3,3 V mit ihrem **Onboard-LDO** (bis ~500 mA).
- Der **3V3-Pin des Moduls** speist U2 (TP8485E), die Bias-Kette und C5 zurück
  ins Trägerboard. Kein separater Regler auf dem Master.
- FB1 (Ferrit) + C4/C2 entkoppeln den Logikzweig (`+5V`) von der Busschiene.
- Symbol-Pintyp: 5V/GND = `power_in`, 3V3 = `power_out` → PWR_FLAG auf
  `+5V_RAW`, `+5V_IN`, `+5V`, `GND` und `+15V` (Boost-Ausgang).

### 5.2 RS-485-Anbindung

- U2 = TP8485E-SR bei **3,3 V** (Spez. 7.1). Master und Modul teilen sich den
  Bausteintyp trotz unterschiedlicher Logikspannung (3–5,5 V-Bereich).
- `/RE` fest auf GND → Dauerempfang; die Firmware verwirft das Sendeecho.
- `DE` an GPIO10, zusätzlich R4 = 10 kΩ nach GND: bei Reset/Boot treibt der
  Transceiver den Bus nicht.
- ESP32-C3 hat nur zwei UARTs; UART0 = USB-C-Konsole. **RS-485 läuft auf UART1**
  (per GPIO-Matrix frei auf GPIO3/GPIO4/GPIO10 gelegt). In der Firmware umgesetzt
  (T12, `firmware/master/`): `pio run -e esp32c3` kompiliert, Host-Tests grün.
- Abschluss R1 = 120 Ω fest, Bias R2/R3 = 2 × 680 Ω (A→+3V3, B→GND).
- **D2 (RS-485-TVS)** im A/B-Pfad zwischen Bias und J2: SM712-Profil,
  7 V Arbeitsspannung Richtung GND und 12 V Richtung Leitung — deckt den
  RS-485-Gleichtaktbereich −7…+12 V ab. Klemmt ESD/EFT/Surge, bevor sie U2
  erreichen. `Pin 1 → RS485_A, Pin 2 → RS485_B, Pin 3 → GND`
  (`symbolpruefung-master.md`, gegen Bourns-CDSOT23-SM712-Datenblatt geprüft).

### 5.3 CHAIN-Pegelwandler

Die Karte-zu-Karte-CHAIN-Leitung ist 5-V-Logik (ATtiny push-pull). Nur der erste
Link Master→Karte 1 ist 3,3 V. U3 = 74LVC1G17 (Schmitt-Buffer, aus +5V versorgt,
nicht invertierend) hebt GPIO5 auf 5 V. R5 = 100 Ω in Serie zum Bus.

**M-3 entschieden (Betreiber, 10.09.2026):** U3 wird bestückt. Der ATtiny1616
erkennt 3,3 V am UPDI/GPIO-Eingang nicht garantiert als High (V_IH bis 0,8·VDD =
4,0 V bei 5-V-Betrieb) — der Pegelwandler bleibt. **R7 (0 Ω)** bleibt als
unbestückter Reserveplatz, falls ein Tisch-Test das doch widerlegt.

### 5.4 Status-LED

GPIO6 → R6 (1 kΩ) → D1 (grün) → GND. Zusätzlich hat das Modul selbst eine LED an
GPIO8 (im eingebauten Zustand verdeckt).

### 5.5 Ader 9 / Triac-Treiberspannung

JP1 (3-Wege-Lötbrücke): Mitte = Ader 9 (J2.9). Pin 1 = +5V_IN, Pin 3 = +15V.
Auslieferzustand **offen** — Ader 9 unbeschaltet, bis O-2 entschieden ist.

### 5.6 Aufwärtswandler (DNP)

J4 als Steckplatz für ein Boost-Modul, Eingang +5V_IN, Ausgang +15V → JP1 Pin 3.
Kein diskreter Regler auf dem Master (spart die Datenblatt-Pinprüfung eines
Boost-ICs; Regel 1). Falls ein diskreter Aufbau gewünscht wird, ist er als
eigener Revisionsschritt zu ergänzen.

---

## 6. Netzliste

Quelle: `tools/gen_master_sch.py`, Dict `NETS`. 20 Netze, 89 Pinverbindungen.

### 6.1 Versorgungsnetze

| Netz | Knoten |
|---|---|
| `+5V_RAW` | J1.1, Q1.2, R8.2 |
| `+5V_IN` | Q1.3, C3.1, FB1.1, J2.1, JP1.1, J4.1 |
| `+5V` | FB1.2, U1.1, C4.1, C2.1, U3.5 |
| `+3V3` | U1.3, U2.8, C1.1, C5.1, R2.1, TP4.1 |
| `+15V` | J4.3, JP1.3, TP5.1 |
| `GND` | J1.2, U1.2, U2.2, U2.5, U3.3, Q1.1, R8.1, D2.3, C1.2, C2.2, C3.2, C4.2, C5.2, R3.2, R4.2, D1.1, J2.2/4/6/8/10, J3.4, J4.2, J4.4, TP6.1 |

PWR_FLAG: `+5V_RAW`, `+5V_IN`, `+5V`, `GND`, `+15V`.

Q1 (AO3401A): Pin 1 = G → GND, Pin 2 = S → `+5V_RAW`, Pin 3 = D → `+5V_IN`.

### 6.2 Signalnetze

| Netz | Knoten | Bemerkung |
|---|---|---|
| `RS485_A` | U2.6, R1.1, R2.2, D2.1, J2.3, TP1.1 | D2 = SM712 Pin 1 |
| `RS485_B` | U2.7, R1.2, R3.1, D2.2, J2.5, TP2.1 | D2 = SM712 Pin 2 |
| `RO` | U2.1, U1.4 | GPIO4 (UART1 RX) |
| `DI` | U2.4, U1.5 | GPIO3 (UART1 TX) |
| `DE` | U2.3, U1.14, R4.1 | GPIO10 |
| `CHAIN_GPIO` | U1.9, U3.2, R7.1 | GPIO5 → Buffer-Eingang / Bypass |
| `CHAIN_OUT` | U3.4, R5.1, R7.2 | Buffer-Ausgang |
| `CHAIN_BUS` | R5.2, J2.7, TP3.1 | 5-V-Pegel zum Bus |
| `LED_DRV` | U1.10, R6.1 | GPIO6 |
| `LED_A` | R6.2, D1.2 | |
| `ADER9` | JP1.2, J2.9, TP7.1 | Ader 9 (offen / +5V / +15V) |
| `IO0_RSV` | U1.8, J3.1 | GPIO0 |
| `IO1_RSV` | U1.7, J3.2 | GPIO1 |
| `IO7_RSV` | U1.11, J3.3 | GPIO7 |

### 6.3 Belegung U1 (ESP32-C3 Super Mini, 2×8)

Pinnummern = Footprint-Pads. Rechte Reihe (Pad 1..8) = 5V-Seite, linke Reihe
(Pad 9..16). **Einbaulage:** USB-C oben, Antenne unten, 5V-Pad rechts oben.

| Pad | Signal (Modul-Silk) | Netz | Funktion |
|---|---|---|---|
| 1 | 5V | +5V | Versorgung (hinter Q1 + FB1, Onboard-LDO) |
| 2 | GND | GND | |
| 3 | 3V3 | +3V3 | 3,3-V-Ausgang des Moduls → Transceiver |
| 4 | GPIO4 | RO | UART1 RX ← U2 |
| 5 | GPIO3 | DI | UART1 TX → U2 |
| 6 | GPIO2 | — | NC (Strapping) |
| 7 | GPIO1 | IO1_RSV | Reserve → J3.2 |
| 8 | GPIO0 | IO0_RSV | Reserve → J3.1 |
| 9 | GPIO5 | CHAIN_GPIO | → U3 |
| 10 | GPIO6 | LED_DRV | Status-LED |
| 11 | GPIO7 | IO7_RSV | Reserve → J3.3 |
| 12 | GPIO8 | — | NC (Strapping, Onboard-LED) |
| 13 | GPIO9 | — | NC (Strapping, BOOT-Taster) |
| 14 | GPIO10 | DE | Sende-Freigabe U2 |
| 15 | GPIO20 | — | NC (UART0 RX, USB-Konsole) |
| 16 | GPIO21 | — | NC (UART0 TX, USB-Konsole) |

### 6.4 Testpads

| Ref | Netz |
|---|---|
| TP1 | RS485_A |
| TP2 | RS485_B |
| TP3 | CHAIN_BUS |
| TP4 | +3V3 |
| TP5 | +15V |
| TP6 | GND |
| TP7 | ADER9 |

---

## 7. Bestückungsvarianten

| Variante | Bestückt | Wann |
|---|---|---|
| **Standard** | U3, Q1, R8, D2 bestückt; R7 nicht | Auslieferzustand |
| CHAIN direkt (M-3-Reserve) | R7 (0 Ω), U3 nicht | nur falls ein Tisch-Test zeigt, dass 3,3 V die ATtiny-Eingänge sicher schalten (unwahrscheinlich, siehe 5.3) |
| Ader 9 = +15 V (O-2 Deutung 1) | J4-Boost-Modul, JP1 auf Pin 3 | wenn die Anzeige an Pin 9 > 5 V braucht |
| Ader 9 = +5 V (O-2 Deutung 2) | JP1 auf Pin 1 | wenn 5 V an Pin 9 genügen |

---

## 8. Layout-Vorgaben

### 8.1 Platine

| Feld | Wert |
|---|---|
| Maß | 68 × 54 mm, 2 Lagen, 1,6 mm FR4, 35 µm Cu, HASL bleifrei |
| Farbe | Lötstoppmaske schwarz, Bestückungsdruck weiß |
| Befestigung | 4 × Bohrung 3,2 mm, 4 mm von den Ecken |
| Maker-Mark | GitHub-Marke + „TenOfNine" auf B.SilkS |

### 8.2 Netzklassen

| Klasse | Bahnbreite | Netze |
|---|---|---|
| Power | 0,8 mm | +5V, +5V_RAW, +5V_IN, +3V3, +15V, ADER9 |
| Default | 0,5 mm | alle Signale |
| GND | — | Massefläche F.Cu + B.Cu, Stitching 5 mm |

Kein AC-Netz. `tools/route_master.py` (FreeRouting 2.3.0 + `finish_routes.py` +
Masseflächen) routet zweilagig; DRC 0/0.

### 8.3 ESP32-C3-Modul (U1)

- Buchsenleisten, Modul entnehmbar. USB-C/BOOT/RST an der Oberkante, überragt die
  Platinenkante (im eingesteckten Zustand bedienbar).
- **Antennenbereich an der Unterkante des Moduls: keine Kupferfläche.** Der
  Footprint enthält ein Keepout (Copperpour + Vias verboten, Pads/Tracks
  erlaubt). Wenn möglich das Modul so einbauen, dass die Antenne über die
  Platinenkante ragt.
- ⚠️ **Sicherheitskritisch:** eine spiegelverkehrte Modul-Fläche vertauscht
  5V/GND und zerstört den ESP32-C3. Vor dem Routen `symbolpruefung-master.md`
  freigeben und die KiCad-3D-Ansicht prüfen (analog J1-M der Daughter Card).

---

## 9. Prüfliste vor der ersten Inbetriebnahme

- [ ] `symbolpruefung-master.md` freigegeben (M-1: ESP32-C3-Pinbelegung ↔ Silk)
- [ ] U1-Footprint in der 3D-Ansicht gegen ein echtes Modul gehalten (5V rechts oben)
- [ ] J2-Belegung gegen Daughter-Card-J2/J3 geprüft (Spez. 5.2)
- [ ] JP1 offen (Ader 9 unbeschaltet) bis O-2 entschieden
- [ ] Spannung an J1: 5,0–5,5 V, verpolungssicher angeklemmt
- [ ] 3V3-Pin des Moduls misst 3,3 V, U2 VCC = 3,3 V
- [ ] Vor dem Aufstecken von U1: kein Kurzschluss +5V ↔ GND ↔ +3V3

---

## 10. Offene Punkte

| Nr | Punkt | Wirkung | Status |
|---|---|---|---|
| M-1 | ESP32-C3-Super-Mini Symbol/Footprint ↔ Board-Silk + Einbaulage | Gate fürs Layout | ✅ freigegeben 01.09.2026 (`symbolpruefung-master.md`); Sichtkontrolle am Render durch den Betreiber |
| M-2 | Aufwärtswandler-Modul | J4 bleibt DNP | wartet auf O-2 |
| M-3 | CHAIN 3,3 V → 5 V: U3 nötig oder R7-Brücke | Bestückungsvariante | auf dem Tisch zu prüfen |
| O-2 | 5 V oder 12–20 V an Anzeige-Pin 9 (Spez.) | JP1-Stellung, Boost | offen |
| T12 | Master-Firmware auf ESP32-C3 portieren (UART1, GPIO-Konstanten) | Firmware | ✅ kompiliert (`pio run -e esp32c3`), am Gerät noch nicht getestet |

---

## 11. Änderungshistorie

| Rev | Datum | Änderung |
|---|---|---|
| 0.1 | 01.09.2026 | Erstfassung. ESP32-C3 Super Mini als Master-CPU (Buchsenleisten), 5-V-Eingang, TP8485E bei 3,3 V, Fail-Safe-Bias + fester 120-Ω-Abschluss, CHAIN-Pegelwandler 74LVC1G17 (M-3), Step-up-Steckplatz DNP (M-2 / O-2). Schaltplan + geroutete PCB generiert (`tools/gen_master_*.py`, `tools/route_master.py`), ERC 0/0, DRC 0/0. |
