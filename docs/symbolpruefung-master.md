# Symbolprüfung Master — ESP32-C3 Super Mini, 74LVC1G17, TP8485E-SR

| Feld | Wert |
|---|---|
| Zweck | Gegenprüfung der Schaltplan-Symbolpins gegen Datenblatt / Board-Silk, vor dem finalen Layout |
| Bezug | CLAUDE.md, harte Regel 5 · `docs/schaltplan-master.md` · Backlog T11 |
| Status | **freigegeben** (Betreiber, im Chat): 74LVC1G17 / TP8485E / ESP32-C3 am 01.09.2026, AO3401A / SM712 / PCB-Sichtprüfung am 10.09.2026. |
| Datum | 10.09.2026 |

> Ein erfolgreicher ERC-Lauf beweist nicht, dass eine Pinbelegung stimmt. ERC läuft
> auch bei vertauschtem VCC und GND fehlerfrei durch. Diese Prüfung ist der manuelle
> Schritt, der das ausschließt. Sie endet mit einer Freigabe durch den Menschen.

Projektbibliothek: `hardware/master/symbols/krone_master.kicad_sym`, erzeugt von
`tools/build_krone_master_symbols.py`.

---

## 1. ESP32-C3 Super Mini (U1) — Modulsymbol + Footprint

**Es gibt kein KiCad-Symbol/Footprint für dieses Aftermarket-Modul.** Beide sind
von Hand erzeugt:

- Symbol `ESP32-C3-SuperMini` — 16 Pins (2×8), `tools/build_krone_master_symbols.py`.
- Footprint `modules:ESP32-C3-SuperMini` — 2×8 THT, 2,54 mm, Reihenabstand
  **15,24 mm**, Körper 18 × 22,52 mm.

### 1.1 Quelle

Fotos des Betreibers (01.09.2026): Pinout-Diagramm des konkreten Boards und
zwei bemaßte 3D-Renderings (18,00 mm Breite, 15,24 mm Reihenabstand,
22,52 mm Länge). Zusätzlich die verbreitete Standard-Pinbelegung des
ESP32-C3-Super-Mini-Klons.

### 1.2 Pin-für-Pin (Symbol ↔ Board-Silk ↔ Netzliste)

Footprint-Pads: **rechte Reihe = Pad 1…8** (5V-Seite), **linke Reihe = Pad 9…16**.
Einbaulage laut Betreiber: **USB-C/BOOT/RST an der Oberkante, Antenne (Aufdruck
„ESP32-C3 Super Mini") an der Unterkante, 5V-Pad rechts oben.**

| Pad | Board-Silk | Symbol-Pinname | Symbol-Pintyp | Netzliste (Kap. 6.3) | Bewertung |
|---:|---|---|---|---|---|
| 1 | 5V | `5V` | power_in | +5V_IN | ✅ |
| 2 | G (GND) | `GND` | power_in | GND | ✅ |
| 3 | 3V3 | `3V3` | power_out | +3V3 | ✅ 3,3-V-Ausgang des Onboard-LDO |
| 4 | 4 | `GPIO4` | bidirectional | RO (UART1 RX) | ✅ |
| 5 | 3 | `GPIO3` | bidirectional | DI (UART1 TX) | ✅ |
| 6 | 2 | `GPIO2` | bidirectional | — (NC) | ✅ Strapping |
| 7 | 1 | `GPIO1` | bidirectional | IO1_RSV | ✅ |
| 8 | 0 | `GPIO0` | bidirectional | IO0_RSV | ✅ |
| 9 | 5 | `GPIO5` | bidirectional | CHAIN_GPIO | ✅ |
| 10 | 6 | `GPIO6` | bidirectional | LED_DRV | ✅ |
| 11 | 7 | `GPIO7` | bidirectional | IO7_RSV | ✅ |
| 12 | 8 | `GPIO8` | bidirectional | — (NC) | ✅ Strapping, Onboard-LED |
| 13 | 9 | `GPIO9` | bidirectional | — (NC) | ✅ Strapping, BOOT-Taster |
| 14 | 10 | `GPIO10` | bidirectional | DE | ✅ |
| 15 | 20 | `GPIO20` | bidirectional | — (NC) | ✅ UART0 RX (USB-Konsole) |
| 16 | 21 | `GPIO21` | bidirectional | — (NC) | ✅ UART0 TX (USB-Konsole) |

### 1.3 Kritische Einzelprüfung Versorgung + Einbaulage

CLAUDE.md nennt die Verwechslung von VCC und GND (bzw. eine spiegelverkehrte
Modul-Fläche) als teuersten denkbaren Fehler.

| | Board-Silk | Footprint-Pad | Netz |
|---|---|---|---|
| Versorgung positiv | „5V", **rechts oben** | Pad **1** (rechte Reihe, oberstes) | +5V_IN |
| Bezugspotenzial | „G", direkt unter 5V | Pad **2** | GND |

**Prüfschritte des Betreibers:**

1. In der KiCad-3D-Ansicht das Modul-Footprint gegen ein echtes Board halten:
   USB-C oben, Antenne unten, 5V-Pad rechts oben, BOOT/RST im eingebauten
   Zustand erreichbar.
2. Bestätigen, dass die linke Reihe (Pad 9…16) von oben nach unten
   GPIO5,6,7,8,9,10,20,21 trägt.
3. Antennen-Keepout: unter der Modul-Unterkante liegt **keine** Massefläche
   (Footprint-Zone `ANT_keepout`, Copperpour + Vias verboten).

Die Buchsenleisten machen einen Bestückungsfehler zerstörungsfrei korrigierbar,
solange die **Kupferbelegung** stimmt — deshalb diese Prüfung vor dem Fertigen.

---

## 2. 74LVC1G17 (U3) — CHAIN-Pegelwandler 3,3 V → 5 V

**Symbol:** `74LVC1G17` (KiCad `74xGxx`, unverändert in `krone_master` kopiert).
**Footprint:** `Package_TO_SOT_SMD:SOT-23-5`.
**Bauteil:** 74LVC1G17**GV**, LCSC C19829593, Gehäuse SOT-23-5 (= SOT753 / SC-74A).

### 2.1 Datenblatt

Nexperia, *74LVC1G17 — Single Schmitt trigger buffer*, Rev. 16.1, 03.09.2024,
Abschnitt 6.1 „Pinning", **GV package (SOT753, SC-74A)** — heruntergeladen
01.09.2026.

### 2.2 Pin-für-Pin

| Pin | DS Nexperia (GV / SOT753) | KiCad-Symbol `74LVC1G17` | Pintyp | Netzliste | Bewertung |
|---:|---|---|---|---|---|
| 1 | n.c. | `NC` | no_connect | — (NC-Flag) | ✅ |
| 2 | **A** (Eingang) | `~` (A) | input | CHAIN_GPIO (von GPIO5) | ✅ |
| 3 | **GND** | `GND` | power_in | GND | ✅ |
| 4 | **Y** (Ausgang) | `~` (Y) | output | CHAIN_OUT (→ R5 → Bus) | ✅ |
| 5 | **VCC** | `VCC` | power_in | +5V | ✅ |

### 2.3 Kritische Einzelprüfung Versorgung

| | Datenblatt | Symbol | Netzliste |
|---|---|---|---|
| Versorgung positiv | Pin **5** = VCC | Pin 5 = `VCC` (power_in) | Pin 5 → +5V |
| Bezugspotenzial | Pin **3** = GND | Pin 3 = `GND` (power_in) | Pin 3 → GND |

→ **Symbol und Netzliste stimmen mit dem Datenblatt überein.** Der Eingang (Pin 2)
liegt an GPIO5 (3,3 V), der Ausgang (Pin 4) treibt über R5 die CHAIN-Leitung mit
5-V-Pegel. Nicht invertierend (Typ „17" = Buffer, nicht „14"/„04").

### 2.4 Hinweis M-3

Wird U3 nicht bestückt, überbrückt **R7 (0 Ω)** die Strecke Pin 2 ↔ Pin 4
(CHAIN_GPIO ↔ CHAIN_OUT); GPIO5 liegt dann direkt auf CHAIN_BUS.

---

## 3. TP8485E-SR (U2)

Symbol und Pinbelegung sind **identisch** zur Daughter Card — Erzeugung über
denselben `make_tp8485e` (aus `tools/build_krone_symbols.py` importiert).
Die Datenblatt-Gegenprüfung steht in `docs/symbolpruefung.md` Abschnitt 3
(freigegeben 27.08.2026) und gilt hier unverändert:

| Pin | Funktion | Netzliste Master (Kap. 6.2) |
|---:|---|---|
| 1 | RO (Empfängerausgang) | RO → U1 GPIO4 |
| 2 | /RE (low-aktiv) | GND (Dauerempfang) |
| 3 | DE (high-aktiv) | DE → U1 GPIO10 |
| 4 | DI (Treibereingang) | DI → U1 GPIO3 |
| 5 | GND | GND |
| 6 | A | RS485_A → J2.3 |
| 7 | B | RS485_B → J2.5 |
| 8 | VCC (3–5,5 V) | +3V3 |

Betrieb hier bei **3,3 V** (Spez. 7.1), Daughter Card bei 5 V — derselbe Baustein,
zulässiger Mischbetrieb.

---

## 4. Passive / triviale Symbole

| Symbol | Herkunft | Prüfung |
|---|---|---|
| `R`, `C`, `LED` | KiCad `Device`, verbatim | 2-Pin; `LED` Pin 1 = K, Pin 2 = A (wie Daughter Card) |
| `FerriteBead_Small` | KiCad `Device`, verbatim | 2-Pin passiv |
| `Screw_Terminal_01x02` | KiCad `Connector` | Pin 1 = +5V_IN, Pin 2 = GND |
| `Conn_02x05_Odd_Even` | KiCad, verbatim | Odd/Even-Nummerierung → J2-Aderbelegung Kap. 4.2 |
| `Conn_01x04` | KiCad, verbatim | J3 (Reserve) / J4 (Boost, DNP) |
| `SolderJumper_3_Open` | KiCad `Jumper` | Pin 2 (Mitte) = gemeinsam = ADER9; Pin 1 = +5V_IN, Pin 3 = +15V |
| `TestPoint` | KiCad `Connector` | 1-Pin |
| `PWR_FLAG` | KiCad `power` | auf +5V_RAW, +5V_IN, +5V, GND, +15V |

---

## 4a. AO3401A (Q1) — Verpolschutz-P-FET

Symbol: KiCad `Transistor_FET:AO3401A` (leitet von `TP0610T` ab), verbatim
übernommen und abgeflacht. Footprint `Package_TO_SOT_SMD:SOT-23`.

| | AO3401A-Datenblatt (SOT-23) | Symbol | Netzliste (Kap. 6.1) |
|---|---|---|---|
| Pin 1 | **G** (Gate) | Pin 1 = `G` (input) | Pin 1 → GND |
| Pin 2 | **S** (Source) | Pin 2 = `S` (passive) | Pin 2 → +5V_RAW |
| Pin 3 | **D** (Drain) | Pin 3 = `D` (passive) | Pin 3 → +5V_IN |

→ Standard-AO3401A-Belegung (1=G, 2=S, 3=D). High-Side-Verpolschutz: Source am
Eingang, Drain zur Last, Gate an GND. Body-Diode Anode→S, Kathode→D: leitet bei
richtiger Polung S→D (lädt die Last), dann V_GS = −5 V → FET voll durch. Bei
Verpolung sperren FET und Body-Diode. V_GS,max ±12 V — die 5 V liegen sicher
darunter, R8 (100 kΩ) hält V_GS definiert.

## 4b. SM712 / PSM712 (D2) — RS-485-TVS-Array

Symbol: KiCad `Diode:SM712_SOT23`, verbatim. Footprint
`Package_TO_SOT_SMD:SOT-23`. Bauteil: PSM712-LF-T7 (ProTek, LCSC C32677) —
SM712-Standard, austauschbar mit Littelfuse/Semtech/Bourns.

Geprüft gegen das **Bourns-CDSOT23-SM712-Datenblatt** (Electrical Characteristics
referenzieren „Pin 3 - 1 / Pin 3 - 2" mit V_WM 7 V und „Pin 1 - 3 / Pin 2 - 3"
mit 12 V → Pin 3 ist der gemeinsame Anschluss (GND), Pin 1 und Pin 2 sind die
zwei geschützten Leitungen, symmetrisch).

| | Datenblatt (SOT-23) | Symbol | Netzliste (Kap. 6.2) |
|---|---|---|---|
| Pin 1 | I/O 1 (7 V / 12 V asym.) | Pin 1 = `A1` (passive) | Pin 1 → RS485_A |
| Pin 2 | I/O 2 (7 V / 12 V asym.) | Pin 2 = `A2` (passive) | Pin 2 → RS485_B |
| Pin 3 | GND (gemeinsam) | Pin 3 = `common` (input) | Pin 3 → GND |

→ Symbol, Footprint und Netzliste stimmen mit dem Datenblatt überein. Das
7 V/12 V-Profil deckt den RS-485-Gleichtaktbereich −7…+12 V ab. Die
PSM712-Belegung (ProTek) ist die branchenübliche SM712-Belegung. **M-4
geschlossen.**

---

## 5. Freigabe

| Baustein | Symbol | Ergebnis | Bestätigt von | Datum |
|---|---|---|---|---|
| 74LVC1G17GV | `74LVC1G17` | Pinbelegung deckungsgleich mit Nexperia Rev. 16.1 §6.1 (GV/SOT753) | Betreiber, im Chat | 01.09.2026 |
| TP8485E-SR | `TP8485E-SR` | unverändert ggü. `docs/symbolpruefung.md` (freigegeben 27.08.2026) | Betreiber | 27.08.2026 |
| ESP32-C3 Super Mini | `ESP32-C3-SuperMini` (Symbol + Footprint) | Pin-Reihenfolge + Einbaulage aus den Fotos, Abschnitt 1.2 / 1.3 | Betreiber, im Chat | 01.09.2026 |
| AO3401A | `AO3401A` | SOT-23 1=G / 2=S / 3=D (Abschnitt 4a) | Betreiber, im Chat | 10.09.2026 |
| SM712 / PSM712 | `SM712_SOT23` | Pin 1/2 = I/O, Pin 3 = GND (Abschnitt 4b, Bourns-Datenblatt) | Betreiber, im Chat | 10.09.2026 |

**M-1 ist damit geschlossen.** Der Betreiber gleicht die U1-Einbaulage zusätzlich
an `docs/render-master-top.png` mit einem echten Modul ab (Pin-1-Punkt = 5V rechts
oben, neben „USB-C"); eine reine Sichtkontrolle, kein Änderungsbedarf am Symbol
oder Footprint erwartet.

**Rev. 0.2 (10.09.2026):** AO3401A, SM712/PSM712 und die U1-/PCB-Prüfung vom
Betreiber freigegeben. M-3 und M-4 geschlossen. Am Silk auf Wunsch des Betreibers:
„ANT: keine Cu-Fläche" am U1-Footprint entfernt (Keepout bleibt), + / − neben J1.
