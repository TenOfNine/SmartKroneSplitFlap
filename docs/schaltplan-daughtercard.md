# Schaltplan Daughter Card — KRONE REW Fallblattanzeige

| Feld | Wert |
|---|---|
| Baugruppe | Modulsteuerung, eine je Anzeigenmodul |
| Version | 0.1 |
| Datum | 27.08.2026 |
| Bezug | Technische Spezifikation v0.3 |
| Status | Entwurf, freigegeben zum Layout |

**Zum Gebrauch:** Kapitel 3 bis 6 zusammen ergeben den vollständigen Schaltplan. Kapitel 5 zeigt die Blöcke als Prinzipschaltbild, Kapitel 6 ist die verbindliche Netzliste. Bei Abweichungen zwischen beiden gilt die Netzliste.

---

## 1. Konventionen

- Alle passiven SMD-Bauteile in Bauform 0805, sofern nicht anders angegeben. 0805 lässt sich noch gut von Hand nacharbeiten.
- Widerstände 1 %, Kondensatoren X7R, 50 V.
- Nicht bestückte Positionen sind mit **DNP** gekennzeichnet.
- Netznamen sind in Großbuchstaben gesetzt.

---

## 2. Blockschaltbild

```
   J4 42V~ in ──┬──────────────────────────────────┬── J5 42V~ out
                └────────────► J1.2 / J1.4 ◄───────┘

   J2 Bus in ──┬── +5V ──┬─────────────────────────┬── J3 Bus out
               │         ├── U1 ATtiny1616         │
               │         ├── U2 TP8485E            │
               │         └── R14 ── VSENS ── J1.5/6│
               │                                    │
               ├── A / B ──── U2 ───── U1 USART ────┤
               │                                    │
               └── CHAIN ──► U1 PA1      U1 PA2 ────┘ (CHAIN)

                            U1 PA4 ◄── Filter ◄── J1.10  Blattimpuls
                            U1 PA5 ◄── Filter ◄── J1.8   Leerbildimpuls
                            U1 PA6 ◄── Filter ◄── J1.7   Nullimpuls
                            U1 PA7 ──► Treiber ──► J1.9  Triac
                            U1 PA0 ◄──────────────  J6   UPDI
                            U1 PA3 ──► D4                Status-LED
```

---

## 3. Stückliste

### 3.1 Aktive Bauteile

| Ref | Bauteil | Gehäuse | Anmerkung |
|---|---|---|---|
| U1 | ATtiny1616-SNR | SOIC-20 (300 mil) | Verfügbarkeit bei JLCPCB prüfen, sonst nachbestücken |
| U2 | TP8485E-SR | SOIC-8 | LCSC C94206 |
| Q1 | BSS84 | SOT-23 | P-Kanal, High-Side im Quelle-Zweig |
| Q2 | MMBT3904 | SOT-23 | Pegelwandler für Q1 |
| Q3 | MMBT3904 | SOT-23 | **DNP**, Senke-Zweig, Alternative zu Q1/Q2 |
| D1–D3 | BAT54S | SOT-23 | Doppel-Schottky als Klemmdioden |
| D4 | LED grün | 0805 | Status |

### 3.2 Passive Bauteile

| Ref | Wert | Funktion |
|---|---|---|
| R1, R3, R5 | 10 kΩ | Pull-up der Impulseingänge nach +5V |
| R2, R4, R6 | 1 kΩ | Serienwiderstand der Impulseingänge |
| C4, C5, C6 | 10 nF | Tiefpass der Impulseingänge, τ = 10 µs |
| R7 | 10 kΩ | Basiswiderstand Q2 |
| R8 | 100 kΩ | Gate-Pull-up Q1 nach VDRV |
| R9 | 4,7 kΩ | R_S, Vorwiderstand Triac-Eingang. Bei 5-V-Betrieb auf 1,2 kΩ ändern, siehe O-2 |
| R10 | 10 kΩ | Basiswiderstand Q3, **DNP** |
| R11 | 1 kΩ | Serienwiderstand CHAIN_IN |
| R12 | 100 kΩ | Pull-down CHAIN_IN |
| R13 | 1 kΩ | Serienwiderstand CHAIN_OUT |
| R14 | 0 Ω | Brücke +5V nach VSENS, Bauform 1206 |
| R15 | 1 kΩ | Vorwiderstand D4 |
| R16 | 120 Ω | Busabschluss, über JP3 zuschaltbar, 0,25 W |
| C1 | 100 nF | Abblockung U1, direkt an Pin 1/20 |
| C2 | 100 nF | Abblockung U2, direkt an Pin 8/5 |
| C3 | 10 µF / 16 V | Stützkondensator +5V, Keramik oder Tantal |
| F1 | PTC 0,5 A | Rückstellsicherung in der +5V-Zuführung, optional |

### 3.3 Mechanik

| Ref | Bauteil | Anmerkung |
|---|---|---|
| J1 | Wannenstecker 2×5, 2,54 mm, gerade | zur Anzeigenplatine |
| J2, J3 | Wannenstecker 2×5, 2,54 mm, gerade | Bus ein / Bus aus |
| J4, J5 | Schraubklemme 2-polig, 5,08 mm | 42 V~ ein / aus |
| J6 | Stiftleiste 1×3, 2,54 mm | UPDI |
| JP1, JP2 | Lötjumper 2 Pad | Auswahl VDRV |
| JP3 | Lötjumper 2 Pad | Busabschluss |
| TP1–TP7 | Testpads Ø 1,5 mm | siehe 6.4 |

---

## 4. Steckverbinder

### 4.1 J1 — zur Anzeigenplatine (KRONE 6281 3 160-00)

| Pin | Netz | Richtung |
|---|---|---|
| 1 | GND | — |
| 2 | AC1 | zur Anzeige |
| 3 | GND | — |
| 4 | AC2 | zur Anzeige |
| 5 | VSENS | zur Anzeige |
| 6 | VSENS | zur Anzeige |
| 7 | PULSE_NULL_RAW | von der Anzeige |
| 8 | PULSE_LEER_RAW | von der Anzeige |
| 9 | TRIAC_CTRL | zur Anzeige |
| 10 | PULSE_BLATT_RAW | von der Anzeige |

> ⚠️ **Vor dem ersten Einschalten Durchgang prüfen.** Ein spiegelverkehrt aufgelegtes Flachbandkabel legt die 42 V~ auf die 5-V-Schiene und zerstört U1 und U2. Die Zählrichtung des Wannensteckers auf der Anzeigenplatine ist auf der Lötseite mit 2, 4, 6, 8, 10 in einer Reihe beschriftet. Prüfen, dass Pin 1 der Karte auf Pin 1 der Anzeige landet.

### 4.2 J2 / J3 — Bus

Identische Belegung, alle Netze werden durchverbunden **außer CHAIN**.

| Pin | J2 (Bus in) | J3 (Bus out) |
|---|---|---|
| 1 | +5V | +5V |
| 2 | GND | GND |
| 3 | RS485_A | RS485_A |
| 4 | GND | GND |
| 5 | RS485_B | RS485_B |
| 6 | GND | GND |
| 7 | **CHAIN_IN** | **CHAIN_OUT** |
| 8 | GND | GND |
| 9 | +15V | +15V |
| 10 | GND | GND |

Pin 7 ist der einzige Pin, der **nicht** durchverbunden wird. Genau dadurch bricht die Kette an jeder Karte auf, was die positionsabhängige Enumeration erst möglich macht. J2 Pin 7 der ersten Karte geht an den CHAIN-Ausgang des Masters.

Ader 9 bleibt unbelegt, falls O-2 ergibt, dass 5 V zur Ansteuerung genügen.

### 4.3 J4 / J5 — Motorspannung

| Pin | Netz |
|---|---|
| 1 | AC1 |
| 2 | AC2 |

J4 und J5 sind direkt parallel geschaltet und dienen dem Durchschleifen von Karte zu Karte.

> ⚠️ AC1 und AC2 sind **potenzialfrei** gegenüber GND. Sie dürfen an keiner Stelle mit der Logikmasse verbunden werden.

### 4.4 J6 — UPDI

| Pin | Netz |
|---|---|
| 1 | GND |
| 2 | UPDI |
| 3 | +5V |

Der für SerialUPDI übliche Widerstand von 4,7 kΩ sitzt im Adapter, nicht auf der Karte. An UPDI darf kein Kondensator liegen.

---

## 5. Schaltungsblöcke

### 5.1 Versorgung

```
   J2.1 ──[F1 PTC 0,5A]──┬── +5V ──┬── U1.1 (VDD)
                         │         ├── U2.8 (VCC)
                         │         ├── JP1 ── VDRV
                         │         └──[R14 0Ω]── VSENS ── J1.5, J1.6
                         │
                        [C3 10µ]
                         │
   J2.2, .4, .6, .8, .10 ┴── GND

   C1 100n direkt zwischen U1.1 und U1.20
   C2 100n direkt zwischen U2.8 und U2.5

   J2.9 ── +15V ── JP2 ── VDRV
```

VSENS ist über R14 fest mit +5V verbunden. Ergibt Teststufe 1, dass die Hall-Sensoren bei 5,0 V unzuverlässig arbeiten — die Originaldokumentation nennt +5 bis +6 V — lässt sich R14 auslöten und VSENS über TP6 aus einer separaten Quelle speisen.

**JP1 und JP2 dürfen niemals gleichzeitig geschlossen sein.** Genau einer der beiden wird gebrückt.

### 5.2 RS-485-Anbindung

```
   U1.10 (PB1, RXD) ◄──────── U2.1 (RO)
   U1.8  (PB3, XDIR) ───────► U2.3 (DE)
   U1.9  (PB2, TXD) ────────► U2.4 (DI)
                              U2.2 (/RE) ── GND
                              U2.5 (GND) ── GND
                              U2.8 (VCC) ── +5V

   U2.6 (A) ──┬── J2.3, J3.3
              │
            [JP3]
              │
           [R16 120Ω]
              │
   U2.7 (B) ──┴── J2.5, J3.5
```

/RE liegt fest auf Masse, der Empfänger ist also dauerhaft aktiv. Die Karte liest ihre eigene Sendung zurück; die Firmware verwirft dieses Echo im Normalbetrieb und nutzt es zur Kollisionserkennung.

JP3 wird **nur auf den beiden Karten an den physikalischen Enden des Busses** geschlossen. Die Fail-Safe-Vorspannung sitzt ausschließlich am Master.

### 5.3 Impulseingänge (dreimal identisch)

```
   +5V ──[R1 10k]──┬── J1.10 (PULSE_BLATT_RAW)
                   │
                   └──[R2 1k]──┬── U1.2 (PA4)
                               │
                        [C4 10n]  [D1 BAT54S]
                               │        │
                              GND   +5V / GND
```

| Signal | Anzeige-Pin | Pull-up | Serie | C | Diode | ATtiny |
|---|---|---|---|---|---|---|
| PULSE_BLATT | J1.10 | R1 | R2 | C4 | D1 | Pin 2, PA4 |
| PULSE_LEER | J1.8 | R3 | R4 | C5 | D2 | Pin 3, PA5 |
| PULSE_NULL | J1.7 | R5 | R6 | C6 | D3 | Pin 4, PA6 |

Ruhepegel high, der Hall-Sensor zieht im Impuls nach Masse. Auswertende Flanke ist die **fallende**. Der Senkenstrom beträgt 0,5 mA je Eingang.

PULSE_NULL wird von der Firmware nicht ausgewertet und dient allein als Messpunkt.

### 5.4 Triac-Ansteuerung

Zwei alternative Zweige. **Nur einer wird bestückt.**

**Quelle-Zweig (Vorgabe, bestückt)**

```
                    VDRV
                     │
                  [R8 100k]
                     │
   U1.5 ──[R7 10k]── │ ── Basis Q2 (MMBT3904)
   (PA7)             │        Emitter ── GND
                     │        Kollektor ──┐
                     ├────────────────────┘
                     │
                 Gate Q1 (BSS84)
                     │
   VDRV ── Source Q1 ── Drain Q1 ──[R9 4k7]── J1.9 (TRIAC_CTRL)
```

PA7 high schaltet Q2 durch, zieht das Gate von Q1 nach Masse und schaltet damit VDRV über R9 auf Pin 9 der Anzeige. R8 hält Q1 im Ruhezustand sicher gesperrt. Bei VDRV = 15 V liegt V_GS von Q1 bei −15 V und damit innerhalb der zulässigen ±20 V des BSS84.

Der Zweig funktioniert unverändert mit VDRV = 5 V oder 15 V; nur R9 ist anzupassen.

**Senke-Zweig (Alternative, DNP)**

```
   U1.5 ──[R10 10k]── Basis Q3 (MMBT3904)
   (PA7)                  Emitter ── GND
                          Kollektor ── J1.9 (TRIAC_CTRL)
```

Falls die Messung ergibt, dass Pin 9 der Anzeige nach Masse gezogen werden muss statt bestromt zu werden. Dann werden Q1, Q2, R7, R8, R9 nicht bestückt und stattdessen Q3 und R10 gesetzt.

In beiden Zweigen gilt: **PA7 high bedeutet Motor an.** Eine Invertierung in der Firmware ist nicht nötig, das Flag-Bit 2 der EEPROM-Konfiguration bleibt als Reserve bestehen.

### 5.5 CHAIN

```
   J2.7 ──[R11 1k]──┬── U1.17 (PA1, CHAIN_IN)
                    │
              [R12 100k]
                    │
                   GND

   U1.18 (PA2, CHAIN_OUT) ──[R13 1k]── J3.7
```

R12 sorgt dafür, dass ein offener CHAIN-Eingang als inaktiv gelesen wird — wichtig für die erste Karte im Werkstattbetrieb und bei gezogenem Buskabel.

### 5.6 Status-LED und UPDI

```
   U1.19 (PA3) ──[R15 1k]──▶|── GND        (D4)

   U1.16 (PA0) ──────────────── J6.2       (UPDI)
```

### 5.7 Motorspannung

```
   J4.1 ──┬── AC1 ──┬── J1.2
          │         │
          └─────────┴── J5.1

   J4.2 ──┬── AC2 ──┬── J1.4
          │         │
          └─────────┴── J5.2
```

Reine Durchverbindung, kein aktives Bauteil im AC-Pfad.

---

## 6. Netzliste

### 6.1 Versorgungsnetze

| Netz | Angeschlossene Pins |
|---|---|
| +5V | F1.2, U1.1, U2.8, C1.1, C2.1, C3.1, R1.1, R3.1, R5.1, R14.1, JP1.1, J3.1 |
| GND | U1.20, U2.2, U2.5, C1.2, C2.2, C3.2, C4.2, C5.2, C6.2, Q2.E, Q3.E, R12.2, R15.2 (über D4), J1.1, J1.3, J2.2, J2.4, J2.6, J2.8, J2.10, J3.2, J3.4, J3.6, J3.8, J3.10, J6.1, D1.3, D2.3, D3.3 |
| +15V | J2.9, J3.9, JP2.1 |
| VDRV | JP1.2, JP2.2, R8.1, Q1.S |
| VSENS | R14.2, J1.5, J1.6 |
| +5V_IN | J2.1, F1.1, J6.3 |

Ist F1 nicht bestückt, wird +5V_IN mit +5V gebrückt.

### 6.2 Signalnetze

| Netz | Angeschlossene Pins |
|---|---|
| RS485_A | U2.6, J2.3, J3.3, JP3.1 |
| RS485_B | U2.7, J2.5, J3.5, R16.2 |
| RS485_TERM | JP3.2, R16.1 |
| RO | U2.1, U1.10 (PB1) |
| DI | U2.4, U1.9 (PB2) |
| DE | U2.3, U1.8 (PB3) |
| PULSE_BLATT_RAW | J1.10, R1.2, R2.1 |
| PULSE_BLATT | R2.2, C4.1, D1.2, U1.2 (PA4) |
| PULSE_LEER_RAW | J1.8, R3.2, R4.1 |
| PULSE_LEER | R4.2, C5.1, D2.2, U1.3 (PA5) |
| PULSE_NULL_RAW | J1.7, R5.2, R6.1 |
| PULSE_NULL | R6.2, C6.1, D3.2, U1.4 (PA6) |
| TRIAC_DRV | U1.5 (PA7), R7.1, R10.1 |
| Q2_BASE | R7.2, Q2.B |
| PFET_GATE | Q2.C, R8.2, Q1.G |
| TRIAC_CTRL | Q1.D über R9, Q3.C, J1.9 |
| CHAIN_IN_RAW | J2.7, R11.1 |
| CHAIN_IN | R11.2, R12.1, U1.17 (PA1) |
| CHAIN_OUT | U1.18 (PA2), R13.1 |
| CHAIN_OUT_EXT | R13.2, J3.7 |
| UPDI | U1.16 (PA0), J6.2 |
| LED_A | U1.19 (PA3), R15.1 |
| AC1 | J4.1, J5.1, J1.2 |
| AC2 | J4.2, J5.2, J1.4 |

Die Anodenpins von D1–D3 (BAT54S, Pin 1) liegen jeweils auf +5V, die Kathoden (Pin 3) auf GND, der Mittelabgriff (Pin 2) am jeweiligen gefilterten Signal.

### 6.3 Belegung U1 (ATtiny1616, SOIC-20)

| Pin | Port | Netz |
|---|---|---|
| 1 | VDD | +5V |
| 2 | PA4 | PULSE_BLATT |
| 3 | PA5 | PULSE_LEER |
| 4 | PA6 | PULSE_NULL |
| 5 | PA7 | TRIAC_DRV |
| 6 | PB5 | TP1 (Reserve) |
| 7 | PB4 | TP2 (Reserve) |
| 8 | PB3 | DE |
| 9 | PB2 | DI |
| 10 | PB1 | RO |
| 11 | PB0 | TP3 (Reserve) |
| 12 | PC0 | TP4 (Reserve) |
| 13 | PC1 | TP5 (Reserve) |
| 14 | PC2 | offen |
| 15 | PC3 | offen |
| 16 | PA0 | UPDI |
| 17 | PA1 | CHAIN_IN |
| 18 | PA2 | CHAIN_OUT |
| 19 | PA3 | LED_A |
| 20 | GND | GND |

> ⚠️ Die Pinbelegung des SOIC-20 vor dem Layout **einmal gegen das Datenblatt** prüfen. Eine Verwechslung von VDD und GND ist der teuerste denkbare Fehler auf dieser Karte.

### 6.4 Testpads

| Ref | Netz | Zweck |
|---|---|---|
| TP1 | PB5 | Reserve, Firmware-Debug |
| TP2 | PB4 | Reserve |
| TP3 | PULSE_NULL_RAW | Untersuchung des Nullimpulses |
| TP4 | TRIAC_CTRL | Messung des Ansteuerpegels, O-2 |
| TP5 | CHAIN_IN | Diagnose der Enumeration |
| TP6 | VSENS | Einspeisung einer höheren Sensorspannung, falls nötig |
| TP7 | GND | Massepunkt für den Tastkopf, direkt neben TP3 und TP4 |

---

## 7. Bestückungsvarianten

| Variante | Bedingung | Bestückt | Nicht bestückt |
|---|---|---|---|
| **Q-15** (Vorgabe) | Pin 9 der Anzeige braucht 12–20 V | Q1, Q2, R7, R8, R9 = 4,7 kΩ, JP2 | Q3, R10, JP1 |
| **Q-5** | Pin 9 der Anzeige ist logikpegelfähig | Q1, Q2, R7, R8, R9 = 1,2 kΩ, JP1 | Q3, R10, JP2 |
| **S** | Pin 9 muss nach Masse gezogen werden | Q3, R10 | Q1, Q2, R7, R8, R9, JP1, JP2 |

Die Entscheidung fällt nach O-2 und lässt sich nach der Fertigung durch Umbestücken treffen. Für die Erstserie würde ich alle zehn Karten in **Q-15** bestücken lassen und nach dem Test von Modul 1 gegebenenfalls umrüsten.

| Karte | JP3 (Busabschluss) |
|---|---|
| erste und letzte am Bus | geschlossen |
| alle übrigen | offen |

---

## 8. Layout-Vorgaben

### 8.1 Platine

| Parameter | Wert |
|---|---|
| Abmessungen | 74 × 60 mm |
| Lagen | 2 |
| Materialstärke | 1,6 mm |
| Kupfer | 35 µm (1 oz) |
| Oberfläche | HASL bleifrei genügt |
| Befestigung | 4 × Bohrung 3,2 mm, 4 mm von den Ecken |

74 mm entspricht der Modulbreite ((1 × 75) − 1 mm). Die 60 mm Höhe hält die Karte unter der 100-×-100-mm-Grenze der günstigen Fertigungsstufen bei beiden Anbietern.

### 8.2 Netzspezifische Vorgaben

| Netz | Leiterbahnbreite | Abstand |
|---|---|---|
| AC1, AC2 | ≥ 1,0 mm | ≥ 2,0 mm zu allen Logiknetzen |
| +5V, VSENS, GND | ≥ 0,5 mm | Standard |
| RS485_A, RS485_B | 0,3 mm, als Paar geführt | Standard |
| übrige Signale | 0,25 mm | Standard |

AC1 und AC2 werden entlang **einer** Platinenkante von J4 über J5 nach J1 geführt, räumlich getrennt vom Bereich um U1 und U2. Bei 1 A und 35 µm Kupfer ergeben 1,0 mm Breite eine Erwärmung deutlich unter 10 K.

### 8.3 Weitere Hinweise

- C1 und C2 direkt an den Versorgungspins der jeweiligen ICs platzieren, Anbindung über kurze Stiche an eine durchgehende Massefläche auf der Rückseite.
- RS485_A und RS485_B als Paar mit konstantem Abstand führen, keine Stichleitungen. R16 und JP3 nahe an J2 setzen.
- C4, C5, C6 nahe an den Portpins von U1 platzieren, die Widerstände R2, R4, R6 nahe an J1. Das ist die wirksame Reihenfolge für den Tiefpass.
- Massefläche auf der Rückseite durchgehend, unterbrochen nur im Bereich der AC-Führung.
- Silkscreen: Pin 1 aller Steckverbinder deutlich markieren, JP1/JP2 mit „5V" und „15V" beschriften, JP3 mit „TERM".
- Beschriftungsfeld für die Modulnummer vorsehen, auch wenn die Adresse per Enumeration vergeben wird — es hilft bei der Fehlersuche.

### 8.4 Fertigung

| Anbieter | Hinweis |
|---|---|
| JLCPCB | Bestückung nur sinnvoll, wenn U1 im Teilekatalog verfügbar ist. Vorab prüfen. U2 ist als C94206 gelistet. Andernfalls U1 nachbestücken lassen oder von Hand löten — SOIC-20 im 300-mil-Gehäuse ist gut handlötbar. |
| PCBWay | Flexibler bei Fremdbauteilen, dafür bei Kleinserien meist etwas teurer. |

Bei zehn Karten würde ich fünfzehn fertigen lassen. Der Aufpreis ist gering und du hast Reserve für Umbestückungsversuche.

---

## 9. Prüfliste vor der ersten Inbetriebnahme

1. Durchgang J1 gegen die Anzeigenplatine prüfen, Pin für Pin. Insbesondere sicherstellen, dass J1.2 und J1.4 auf den 42-V~-Pins der Anzeige landen und nicht auf der Versorgung.
2. Widerstand zwischen +5V und GND messen. Ein Wert unter 1 kΩ deutet auf einen Bestückungsfehler.
3. Widerstand zwischen AC1 und GND messen. Muss hochohmig sein, die Motorspannung ist potenzialfrei.
4. Prüfen, dass genau einer von JP1 und JP2 geschlossen ist.
5. Karte allein mit 5 V versorgen, Stromaufnahme messen. Erwartet werden unter 20 mA ohne aktive Buskommunikation.
6. UPDI-Verbindung herstellen und die Device-ID auslesen, bevor das erste Mal Firmware geschrieben wird.
7. Erst danach die Anzeige anstecken und mit Teststufe 1 nach Kapitel 10.1 der Spezifikation beginnen.

---

## 10. Offene Punkte mit Einfluss auf dieses Dokument

| Nr | Punkt | Auswirkung auf den Schaltplan |
|---|---|---|
| O-2 | Ansteuerpegel an Pin 9 | Bestimmt Bestückungsvariante nach Kapitel 7 und den Wert von R9. Das Layout ist davon unabhängig, alle Positionen sind vorgesehen. |
| O-5 | Zulässiger Senkenstrom der Hall-Sensoren | Bei einem sehr schwachen Ausgangstreiber sind R1, R3, R5 auf 22 kΩ zu erhöhen. |
| O-5b | Ausreichende Sensorspannung bei 5,0 V | Falls nicht, R14 entfernen und VSENS über TP6 aus einer 6-V-Quelle speisen. |

---

## 11. Änderungshistorie

| Version | Datum | Änderung |
|---|---|---|
| 0.1 | 27.08.2026 | Erstfassung auf Basis der Spezifikation v0.3. |
