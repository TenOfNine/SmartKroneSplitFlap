# Symbolprüfung — ATtiny1616 und TP8485E-SR (Backlog T3)

| Feld | Wert |
|---|---|
| Zweck | Gegenprüfung der Schaltplan-Symbolpins gegen die Datenblätter, vor dem Layout |
| Bezug | CLAUDE.md, harte Regel 5 · Backlog T3 |
| Status | **wartet auf menschliche Bestätigung** |
| Datum | 27.08.2026 |

> Ein erfolgreicher ERC-Lauf beweist nicht, dass eine Pinbelegung stimmt. ERC läuft
> auch bei vertauschtem VDD und GND fehlerfrei durch. Diese Prüfung ist der manuelle
> Schritt, der das ausschließt. Sie endet mit einer Freigabe durch den Menschen.

---

## 1. Verwendete Quellen

| Kürzel | Quelle | Bezug |
|---|---|---|
| **DS-ATtiny** | Microchip, *ATtiny1614/1616/1617 tinyAVR 1-series*, DS40002204A, Abschnitt 4.2 „20-Pin SOIC" (S. 15) und Abschnitt 39.3 „20-Pin SOIC" (Gehäusezeichnung C04-00094) | heruntergeladen 27.08.2026 |
| **DS-TP8485E** | 3PEAK, *TP8485E — Full Fail-Safe RS-485 Transceiver*, Rev. D, Abschnitt „Pin Configuration (Top View)", 8-Pin SOIC, Suffix `-S` | heruntergeladen 27.08.2026 |
| **LIB** | KiCad-Symbolbibliothek `kicad-symbols`, Stand `7800d914` (26.08.2026), Format `version 20251024`, `generator_version "10.0"` | offizielle GitLab-Bibliothek |
| **NL** | `docs/schaltplan-daughtercard.md`, Kapitel 6.3 (U1) und 6.2 (Signalnetze U2) | dieses Repository |

Die Bibliothek in KiCad 9 kann eine ältere Revision sein als LIB. Maßgeblich für die
Freigabe ist die Übereinstimmung **Datenblatt ↔ Symbol**; die konkrete Symboldatei,
die T4 verwendet, wird in das Projekt kopiert (siehe Abschnitt 4), damit CI und
Layout reproduzierbar auf derselben Fassung arbeiten.

---

## 2. ATtiny1616, SOIC-20 (300 mil)

**Symbol:** `MCU_Microchip_ATtiny:ATtiny1616-S`
(erbt von `ATtiny406-S`; Property `Footprint` = `Package_SO:SOIC-20W_7.5x12.8mm_P1.27mm`;
Property `Description` = „20MHz, 16kB Flash, 2kB SRAM, 256B EEPROM, SOIC-20")

Der Bezeichner `-S` steht bei Microchip für das Gehäuse **SOIC300** (Suffix `SN`/`SNR`),
also das in der Spezifikation gewählte `ATtiny1616-SNR`. Das Schwestersymbol
`ATtiny1616-M` ist das VQFN-Gehäuse und wird **nicht** verwendet.

### 2.1 Pin-für-Pin-Vergleich

| Pin | DS-ATtiny (§4.2) | LIB Symbol `ATtiny1616-S` | LIB Pintyp | NL (Kap. 6.3) | Bewertung |
|---:|---|---|---|---|---|
| 1  | VDD              | `VCC`            | power_in       | +5V (VDD)        | ✅ gleiche Funktion, Namensvariante VDD/VCC |
| 2  | PA4              | `PA4`            | bidirectional  | PULSE_BLATT      | ✅ |
| 3  | PA5              | `PA5`            | bidirectional  | PULSE_LEER       | ✅ |
| 4  | PA6              | `PA6`            | bidirectional  | PULSE_NULL       | ✅ |
| 5  | PA7              | `PA7`            | bidirectional  | TRIAC_DRV        | ✅ |
| 6  | PB5              | `PB5`            | bidirectional  | TP1 (Reserve)    | ✅ |
| 7  | PB4              | `PB4`            | bidirectional  | TP2 (Reserve)    | ✅ |
| 8  | TOSC1/PB3        | `PB3`            | bidirectional  | DE              | ✅ Pin-Nr. identisch, TOSC1 ist nur Alt-Funktion |
| 9  | TOSC2/PB2        | `PB2`            | bidirectional  | DI              | ✅ |
| 10 | PB1              | `PB1`            | bidirectional  | RO              | ✅ |
| 11 | PB0              | `PB0`            | bidirectional  | TP3 (Reserve)    | ✅ |
| 12 | PC0              | `PC0`            | bidirectional  | TP4 (Reserve)    | ✅ |
| 13 | PC1              | `PC1`            | bidirectional  | TP5 (Reserve)    | ✅ |
| 14 | PC2              | `PC2`            | bidirectional  | offen           | ✅ |
| 15 | PC3              | `PC3`            | bidirectional  | offen           | ✅ |
| 16 | PA0/RESET/UPDI   | `~{RESET}/PA0`   | bidirectional  | UPDI            | ✅ |
| 17 | PA1              | `PA1`            | bidirectional  | CHAIN_IN        | ✅ |
| 18 | PA2              | `PA2`            | bidirectional  | CHAIN_OUT       | ✅ |
| 19 | PA3/EXTCLK       | `PA3`            | bidirectional  | LED_A           | ✅ |
| 20 | GND              | `GND`           | power_in       | GND             | ✅ |

### 2.2 Kritische Einzelprüfung Versorgung

CLAUDE.md nennt die Verwechslung von VDD und GND als teuersten denkbaren Fehler.

| | Datenblatt | Symbol | Netzliste |
|---|---|---|---|
| Versorgung positiv | Pin **1** = VDD | Pin **1** = `VCC` (power_in) | Pin 1 → +5V |
| Bezugspotenzial | Pin **20** = GND | Pin **20** = `GND` (power_in) | Pin 20 → GND |

→ Symbol und Netzliste stimmen mit dem Datenblatt überein. Pin 1 ist positiv, Pin 20 ist Masse.

### 2.3 Hinweise für T4 (ERC)

- Beide Versorgungspins sind im Symbol `power_in`. Auf den Netzen `+5V` und `GND`
  muss je ein `PWR_FLAG` gesetzt werden, sonst meldet ERC „Input Power pin not driven".
- Das Symbol führt UPDI/Reset als einen Pin (16). Das entspricht dem Bauteil; die
  UPDI-Stiftleiste J6 hängt direkt daran, kein Kondensator (siehe Schaltplan 4.4 J6).

---

## 3. TP8485E-SR, SOIC-8

**In der offiziellen KiCad-Bibliothek existiert kein Symbol für den TP8485E.**
Der Baustein ist pinkompatibel zum Industriestandard (75176 / MAX485 / MAX3485 /
SP3485 / ADM3485 — alle mit identischer 8-Pin-Belegung). Als Referenzsymbol dient
`Interface_UART:MAX3485` (erbt von `LTC2850xS8`), das denselben 3–5,5-V-Bereich
abdeckt wie der TP8485E.

### 3.1 Pin-für-Pin-Vergleich

| Pin | DS-TP8485E (Top View, `-S`) | Funktion | LIB `MAX3485` | LIB Pintyp | NL (Kap. 6.2) | Bewertung |
|---:|---|---|---|---|---|---|
| 1 | `R`     | Empfängerausgang RO                     | `RO`      | output    | RO → U1.10 (PB1)          | ✅ |
| 2 | `/RE`   | Empfänger-Freigabe, **low-aktiv**       | `~{RE}`   | input     | GND (dauerhaft aktiv)     | ✅ |
| 3 | `DE`    | Treiber-Freigabe, **high-aktiv**        | `DE`      | input     | DE → U1.8 (PB3, XDIR)     | ✅ |
| 4 | `D`     | Treibereingang DI                       | `DI`      | input     | DI → U1.9 (PB2)           | ✅ |
| 5 | `GND`   | Masse                                   | `GND`     | power_in  | GND                      | ✅ |
| 6 | `A/Y`   | nichtinvertierend, Treiber-Aus/Empf.-Ein| `A`       | bidirectional | RS485_A → J2.3/J3.3   | ✅ |
| 7 | `B/Z`   | invertierend                            | `B`       | bidirectional | RS485_B → J2.5/J3.5   | ✅ |
| 8 | `VCC`   | Versorgung 3–5,5 V                      | `VCC`     | power_in  | +5V                      | ✅ |

### 3.2 Polaritäten aus der Wahrheitstabelle des Datenblatts

| Signal | Datenblatt | Schaltplan-Nutzung | Bewertung |
|---|---|---|---|
| `DE`  | H = Treiber aktiv | XDIR (PB3) steuert DE byte-genau, Ruhepegel low = Empfangen | ✅ passt zu Spec 4.2 |
| `/RE` | L = Empfänger aktiv | fest auf GND → Empfänger dauerhaft aktiv, Sendeecho für Kollisionserkennung | ✅ passt zu Spec 4.5.3 |

### 3.3 Nebenbefund (nicht T3, zur Kenntnis)

DS-TP8485E Rev. D nennt „Data Rate: Up to 250 kbps". Der Bus läuft mit 115200 Bd
und liegt damit innerhalb der Spezifikation des Bausteins. Die Slew-Rate-Begrenzung
ist gewollt (EMV). Kein Handlungsbedarf, aber beim Beschaffen darauf achten, dass
die gelieferte Variante (LCSC C94206) mindestens 115,2 kBd zusichert.

---

## 4. Vorschlag zur Symbolablage

Damit T4, CI und das Layout reproduzierbar dieselben Symbole verwenden:

1. Projekt-Symbolbibliothek `hardware/daughtercard/symbols/krone.kicad_sym` anlegen.
2. `ATtiny1616-S` (inkl. Basis `ATtiny406-S`) aus LIB unverändert hineinkopieren.
3. Neues Symbol **`TP8485E-SR`** anlegen, das `MAX3485` erbt oder eine unveränderte
   Kopie davon ist, mit:
   - `Value` = `TP8485E-SR`
   - `Footprint` = `Package_SO:SOIC-8_3.9x4.9mm_P1.27mm`
   - Zusatzfeld `LCSC` = `C94206`, `MPN` = `TP8485E-SR`
   - Pinbelegung **unverändert** gegenüber `MAX3485` (durch obige Prüfung gedeckt)

Kein Pin wird dabei umnummeriert oder umbenannt.

---

## 5. Freigabe

| Baustein | Symbol | Ergebnis | Bestätigt von | Datum |
|---|---|---|---|---|
| ATtiny1616-SNR | `ATtiny1616-S` | Pinbelegung deckungsgleich mit DS40002204A §4.2 | _offen_ | |
| TP8485E-SR | `MAX3485` (als `TP8485E-SR`) | Pinbelegung deckungsgleich mit TP8485E Rev. D | _offen_ | |

Nach Bestätigung wird diese Datei mit Namen und Datum ergänzt und T3 gilt als erledigt.
