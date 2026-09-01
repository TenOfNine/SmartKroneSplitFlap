# Technische Spezifikation: Steuerung für KRONE REW Fallblattanzeige

---

## 1. Dokumentübersicht

| Feld | Wert |
|---|---|
| Titel | Steuerung für KRONE REW Fallblattanzeige (Palettenmodulreihe A, 40 Blatt) |
| Version | 0.11 |
| Datum | 01.09.2026 |
| Status | Entwurf — enthält offene Punkte, siehe Kapitel 11. Änderungen seit v0.8 in Anhang D. |
| Dokumenttyp | Technische Spezifikation (TSD) |

**Hinweis zur Lesart:** Punkte, die noch nicht durch Messung oder Dokumentation belegt sind, sind mit ⚠️ markiert. Wo eine Entscheidung von einer ausstehenden Messung abhängt, sind beide Varianten ausgearbeitet.

---

## 2. Systemüberblick

### 2.1 Ziel

Ersatz der originalen KRONE-Anzeigersteuerung und der Palettensteuerungen (PST, Zeichnung 6412 2 100-00, IC HMCS44C) durch eine Eigenbau-Lösung. Die mechanischen Anzeigenmodule und deren Leiterplatten (6281 3 160-00) bleiben unverändert und werden weiterverwendet.

### 2.2 Architektur

```
                    ┌──────────────────────────────┐
   230 V ~ ─────────┤ Netzteilbaugruppe            │
                    │  · Ringkerntrafo 2x18 V      │──── 42 V~ (Motoren)
                    │  · Netzteil 5 V / 2 A        │──── 5 V (Logik + Sensoren)
                    │  · optional 15 V (Ader 9)    │──── 15 V (Triac-Treiber)
                    └──────────────────────────────┘
                                   │
                    ┌──────────────┴───────────────┐
                    │ Zentralsteuerung (ESP32-C3)  │
                    │  · WLAN, Web-UI, REST, MQTT  │
                    │  · NTP-Uhr                   │
                    │  · RS-485-Master             │
                    └──────────────┬───────────────┘
                                   │  RS-485 half duplex + CHAIN
        ┌──────────┬───────────────┼───────────────┬──────────┐
        │          │               │               │          │
   ┌────┴────┐ ┌───┴─────┐    ┌────┴────┐     ┌────┴────┐    ...
   │ DC #1   │ │ DC #2   │    │ DC #3   │     │ DC #n   │
   │ATtiny   │ │ATtiny   │    │ATtiny   │     │ATtiny   │
   └────┬────┘ └───┬─────┘    └────┬────┘     └────┬────┘
        │ 2x5      │               │               │
   ┌────┴────┐ ┌───┴─────┐    ┌────┴────┐     ┌────┴────┐
   │Anzeige 1│ │Anzeige 2│    │Anzeige 3│     │Anzeige n│
   └─────────┘ └─────────┘    └─────────┘     └─────────┘

   DC = Daughter Card (eine je Anzeigenmodul)
```

### 2.3 Auslegung

| Parameter | Wert |
|---|---|
| Anzahl Anzeigenmodule | 10 (Auslegung bis 32) |
| Modultyp | Palettenmodulreihe A, Modulgröße 1, 40 Blatt |
| Zeichenvorrat | 38 Zeichen + Leerbild, siehe Anhang A |
| Vollständiges Display-Update | ≤ 2,4 s (eine volle Umdrehung) |

---

## 3. Bestandsanalyse: Anzeigenmodul

### 3.1 Mechanik und Positionserfassung

Der Blattsatz wird von einem Synchronmotor über ein Untersetzungsgetriebe angetrieben. Eine volle Umdrehung des Blattsatzes entspricht einer Umdrehung des großen Zahnrades (1:1). Übersetzung Motor zu großem Zahnrad bei 40 Blatt: 15 Zähne Ritzel zu 150 Zähnen, also 10:1.

Auf dem Impulsrad sitzen vier Zählimpuls-Magnete und ein Leerbild-Magnet, abgetastet von zwei Hall-Sensoren.

| Größe | Wert | Herleitung |
|---|---|---|
| Zählimpulse je Motorumdrehung | 4 | 4 Magnete |
| Motorumdrehungen je Blattsatzumdrehung | 10 | Übersetzung 10:1 |
| Zählimpulse je Blattsatzumdrehung | 40 | 10 × 4 |
| **Zählimpulse je Blatt** | **exakt 1** | 40 Impulse / 40 Blatt |
| Leerbildimpulse je Blattsatzumdrehung | 1 | Steuernocken am großen Zahnrad |
| Motordrehzahl | 250 U/min bei 50 Hz | Typenschild |
| Zykluszeit je Blatt | 60 ms | = 3 Netzperioden bei 50 Hz |
| Volle Umdrehung | 2,40 s | 40 × 60 ms |
| Mittlere Positionierzeit | 1,20 s | halbe Umdrehung |

Das System ist netzfrequenzstarr: 60 ms entsprechen exakt drei Netzperioden. Positionierzeiten sind damit deterministisch und lassen sich als Plausibilitätsprüfung nutzen.

### 3.2 Identifizierte Bauteile

| Bauteil | Typ | Anmerkung |
|---|---|---|
| Leiterplatte | KRONE 6281 3 160-00 K1/2, Code W | einseitig, Messpunkte MP1…MP8 beschriftet |
| Motor | Berger RSM 42/12, 42 V, 50/60 Hz, 250/300 U/min, 1,8 VA | Baujahr 10.90 |
| Triac | Teccor L201E3, TO-92 | 200 V, 1 A, sensitives Gate ca. 3 mA in allen vier Quadranten, V_GT max. 1,3 V |
| Treiberstufe | MPSA42 (NPN, 300 V) + MPSA92 (PNP, 300 V) | komplementäres Paar, Gate-Treiber |
| Überspannungsschutz | Siemens SIOV S14K60 | Varistor, 60 V~ Dauerbetrieb, Klemmung ca. 100 V |
| Motorkondensator | 2,2 µF / 100 V | Betriebskondensator, zieht ca. 29 mA Blindstrom |
| Hall-Sensoren | Beschriftung „03F 936" | 2 Stück, verdeckt unter dem Impulsrad, Open Collector ⚠️ |
| Filterkondensatoren | WIMA 0,047 µF und 0,1 µF, 63 V | im Treiberzweig |

### 3.3 Elektrische Daten (aus Originaldokumentation)

| Größe | Wert |
|---|---|
| Logikversorgung | +5 V ±5 %, ca. 0,8 mA |
| Hall-Sensoren | +5 V…+6 V, typ. 15 mA |
| Triac-Steuerspannung | +12 V…+20 V, 6,4…1,7 mA |
| Motor | 42 V~ +10 % / −15 % → **35,7 … 46,2 V**, Mod. 1 und 2 typ. 100 mA |
| Temperaturbereich | −20 °C bis +60 °C |
| rel. Feuchte | bis 95 % bei +40 °C, ohne Kondensation |
| MTBF | 1,5 · 10⁶ Rotorumdrehungen |

### 3.4 Anschlussbelegung 10-poliger Stecker

Bauform: Pfostenstecker 2×5, Raster 2,54 mm. Auf der Lötseite sind die geraden Pins (2, 4, 6, 8, 10) in einer Reihe beschriftet, die ungeraden liegen in der zweiten Reihe.

| Pin | Signal | Status |
|---|---|---|
| 1 | Masse | ✅ Originaldoku |
| 2 | 42 V~ | ✅ Originaldoku |
| 3 | Masse, mit Pin 1 verbunden | ✅ Originaldoku |
| 4 | 42 V~ | ✅ Originaldoku |
| 5 | Versorgung, mit Pin 6 verbunden | ✅ Originaldoku |
| 6 | Versorgung +5…+6,6 V | ✅ Originaldoku |
| 7 | Nullimpuls, Funktion unbekannt | ⚠️ wird nicht ausgewertet |
| 8 | Leerbild-Impuls, Ruhepegel high, Impuls zieht nach Masse | ✅ Originaldoku |
| 9 | Triac-Schalteingang | ✅ Originaldoku |
| 10 | Blatt-Impuls (Zählimpuls), Ruhepegel high, Impuls zieht nach Masse | ✅ Originaldoku |

**Es gibt keine getrennten Potenzialbereiche.** Masse und Versorgung liegen jeweils doppelt auf dem Stecker, sind aber untereinander verbunden. Die Doppelbelegung dient der Stromtragfähigkeit, nicht der Trennung.

Eine frühere Widerstandsmessung am abgezogenen Stecker hatte Pin 1 gegen Pin 3 und Pin 5 gegen Pin 6 als getrennt ausgewiesen. Das ist kein Widerspruch: Die Verbindung wird auf der Seite der Palettensteuerung hergestellt, nicht auf der Anzeigenplatine. Da die originale PST die Pins gebrückt hat, ist das Brücken auf der Daughter Card durch den Originalbetrieb belegt und unbedenklich.

**Die 42 V~ an Pin 2 und Pin 4 sind gegenüber der Logikmasse potenzialfrei.** Die Trafowicklung darf nicht mit der Masse verbunden werden. Der Gate-Kreis des Triacs ist auf MT1 bezogen und schwimmt damit auf dem Wechselspannungspotenzial. Genau das erklärt die 300-V-Typen MPSA42 und MPSA92 auf der Anzeigenplatine: Sie bilden die Pegelverschiebung zwischen dem massebezogenen Steuereingang an Pin 9 und dem potenzialfreien Gate-Kreis.

### 3.5 Blattbelegung

Siehe Anhang A. Kurzform: Blatt 1 und 2 sind Leerbilder, Blatt 3–12 tragen die Ziffern 0–9, Blatt 13–38 die Buchstaben A–Z, Blatt 39 den Bindestrich, Blatt 40 den Punkt.

---

## 4. Modulsteuerung (Daughter Card)

### 4.1 Auswahl der CPU

**Gewählt: Microchip ATtiny1616, SOIC-20**

| Kriterium | Begründung |
|---|---|
| Versorgung 5 V | identisches Potenzial wie Hall-Sensoren, keine Pegelwandler nötig |
| USART mit RS-485-Modus | XDIR-Pin steuert den Bustreiber hardwareseitig byte-genau |
| 16 KB Flash, 2 KB RAM | reichlich für den Anwendungsumfang |
| 256 B EEPROM | Adresse, Blattzahl, Offset, Abschaltvorhalt ohne Neuflashen änderbar |
| 20 MHz interner Oszillator, werkskalibriert | kein Quarz erforderlich |
| UPDI-Programmierung | eine Leitung, Adapter aus FTDI-Wandler und 4,7 kΩ |
| −40 °C bis +105 °C | deckt −20…+60 °C mit Reserve |
| Preis ca. 1,50 € | angemessen bei 10 Stück |

Verworfene Alternativen: CH32V003 (3,3 V, Pegelwandlung nötig, Vorteil erst ab großen Stückzahlen), ESP32-C3 je Modul (Kosten, Stromaufnahme und Komplexität ohne Gegenwert bei fest verkabeltem Aufbau).

### 4.2 Pinbelegung

| Pin | Port | Funktion |
|---|---|---|
| 9 | PB2 | USART0 TXD → RS-485-Treiber DI |
| 8 | PB3 | USART0 RXD ← RS-485-Treiber RO |
| 11 | PB0 | USART0 XDIR → DE (byte-genaue Senderichtung) |
| 2 | PA4 | Blatt-Impuls, Flanken-Interrupt (PORTA) |
| 3 | PA5 | Leerbild-Impuls, Flanken-Interrupt (PORTA) |
| 4 | PA6 | Nullimpuls, nur Messpunkt, Firmware wertet nicht aus |
| 5 | PA7 | Triac-Ansteuerung → Transistorschalter auf Pin 9 |
| 17 | PA1 | CHAIN_IN |
| 18 | PA2 | CHAIN_OUT |
| 19 | PA3 | Status-LED |
| 6, 7 | PB5, PB4 | Reserve, herausgeführt auf Testpad TP1 bzw. TP2 |
| 10, 12–15 | PB1, PC0…PC3 | Reserve, im Schaltplan unbeschaltet (kein Pad). PB1 ist USART0 XCK, im Async-Betrieb ungenutzt. Siehe `docs/pruefpunkte-t4.md` P-1 und `docs/pruefpunkte-t7.md` P-3. |
| 16 | PA0 | UPDI |
| 1, 20 | VDD, GND | Versorgung |

USART0 liegt auf der Standard-MUX-Position (`PORTMUX.USART0 = DEFAULT`).
Die alternative Position (PA1…PA4) ist durch CHAIN, Status-LED und den
Blattimpuls belegt. Belegt über `pruefpunkte-t7.md` P-3.

**Hinweis zur Beschaltung des Bustreibers:** XDIR steuert ausschließlich DE. Der Empfängerfreigabeeingang /RE wird fest auf GND gelegt, der Empfänger ist also dauerhaft aktiv. Die Karte liest damit ihre eigene Sendung zurück. Die Firmware verwirft dieses Echo im Normalbetrieb und nutzt es zur Kollisionserkennung, siehe 4.5.

### 4.3 Signalanpassung Impulseingänge

Je Eingang (Blatt an Pin 10, Leerbild an Pin 8, Nullimpuls an Pin 7):

- Pull-up 10 kΩ nach +5 V (Hall-Sensoren mit Open-Collector-Ausgang)
- Serienwiderstand 1 kΩ
- Kondensator 10 nF nach GND (Grenzfrequenz ca. 16 kHz, τ = 10 µs)
- Klemmdioden BAT54S gegen +5 V und GND

**Auswertende Flanke: fallend.** Der Ruhepegel ist high, der Impuls zieht die Leitung nach Masse. Die Firmware wertet ausschließlich die fallende Flanke aus.

Die Eingänge des ATtiny sind Schmitt-Trigger-Eingänge, ein zusätzlicher Trigger-Baustein entfällt.

Softwareseitige Entprellung: Der Mindestabstand zweier Blattimpulse beträgt 60 ms. Jede Flanke innerhalb von 20 ms nach der vorhergehenden wird verworfen.

### 4.4 Triac-Ansteuerung

Der Triac (L201E3) sitzt mit seiner Treiberstufe auf der Anzeigenplatine. Die Daughter Card bedient ausschließlich den Steuereingang an **Pin 9**, der auf die gemeinsame Masse bezogen ist.

**Optokoppler und isolierter DC/DC-Wandler entfallen.** Sie waren nur nötig, solange von einer getrennten Triac-Domäne ausgegangen wurde. Da Masse und Versorgung durchgehend gemeinsam sind, genügt ein einfacher Transistorschalter. Das spart je Karte rund 1,50 € und eine Bauteilkategorie.

**Offen bleibt die Ansteuerpolarität und der Pegel.** Die Originaldokumentation nennt für den Triac-Steuerkreis 12 bis 20 V bei 6,4 bis 1,7 mA. Der Stecker führt aber keine eigene 12–20-V-Versorgung, sondern nur die 5-V-Schiene an Pin 5 und 6. Zwei Deutungen sind möglich:

- **Deutung 1:** Pin 9 erwartet einen Strom aus einer 12–20-V-Quelle. Dann muss die Daughter Card diese Spannung bereitstellen.
- **Deutung 2:** Die Anzeigenplatine erzeugt die 12–20 V selbst aus den 42 V~, und Pin 9 ist ein logikpegelfähiger Eingang.

Die Angabe der Originaldoku ist in sich unstimmig, da der Strom mit steigender Spannung fällt. Das lässt sich mit einem einfachen Vorwiderstand nicht erklären und spricht für eine aktive Stufe hinter Pin 9. Klärung über O-2.

**Ausgeführte Schaltung, deckt beide Deutungen ab:**

```
                    ┌─── Lötjumper JP1 ──── +5 V
   VDRV ────────────┤
                    └─── Lötjumper JP2 ──── +15 V (Buskabel Ader 9)

                       VDRV
                         │
                        [R_S]        Quelle (Vorgabe)
                         │
   PA7 ──[10k]── BSS84 ──┴────────── Pin 9
              (P-Kanal, High-Side)

   PA7 ──[10k]── MMBT3904 ────────── Pin 9        Senke (Alternative)
                    │
                   GND
```

Bestückt wird jeweils nur ein Zweig. Vorgabe ist die Quelle mit R_S = 4,7 kΩ und VDRV = 15 V, was rund 3 mA ergibt und damit im Zündbereich des L201E3 liegt. Zeigt die Messung, dass 5 V genügen, wird JP1 gesetzt und R_S auf 1,2 kΩ reduziert.

Die Wahl wird zusätzlich in Flag-Bit 2 der EEPROM-Konfiguration hinterlegt, damit die Firmware die Ausgangspolarität an PA7 passend invertiert.

**Rückfallebene:** Der L201E3 zündet mit rund 3 mA in allen vier Quadranten. Sollte sich die Originalstufe als unbrauchbar erweisen, kann das Gate direkt über einen Optotriac-Treiber (MOC3052 oder MOC3063) bedient werden. Das erfordert einen Eingriff auf der Anzeigenplatine und ist nicht der bevorzugte Weg.

### 4.5 Adressierung

Die Busadresse wird ausschließlich über die CHAIN-Leitung vergeben. Ein DIP-Schalter ist **nicht** vorgesehen. Für den Werkstattbetrieb wird ein eigener Master eingesetzt; die Serviceadresse nach Abschnitt „Rückfallverhalten" deckt den Einzelkartenfall ab.

#### 4.5.1 Enumeration

1. Master sendet `ENUM_RESET` als Broadcast. Alle Karten wechseln in den Zustand ENUMERATING, antworten nicht mehr auf ihre bisherige Adresse und ziehen CHAIN_OUT inaktiv.
2. Master aktiviert die CHAIN-Leitung zur ersten Karte.
3. Master sendet `ENUM_ASSIGN` mit der nächsten freien Adresse. Nur die Karte mit aktivem CHAIN_IN und ohne Laufzeitadresse übernimmt sie, bestätigt, schreibt sie ins EEPROM und aktiviert CHAIN_OUT.
4. Schritt 3 wiederholt sich, bis keine Bestätigung mehr kommt.
5. Master sendet `ENUM_DONE` als Broadcast. Alle Karten, die noch in ENUMERATING sind, verlassen den Zustand unverzüglich.

Für zehn Karten ist die Enumeration in deutlich unter einer Sekunde abgeschlossen. Die Adresse entspricht damit der physischen Position im Anzeigenfeld, und der Master kennt die Modulanzahl ohne Konfiguration. Ein Kartentausch erfordert keinerlei Einstellung.

Die Enumeration ist von der Mechanik entkoppelt: Eine laufende Positionierung wird durch `ENUM_RESET` nicht abgebrochen.

#### 4.5.2 Rückfallverhalten

Bleibt `ENUM_ASSIGN` aus, weil die CHAIN-Leitung gestört ist oder der Master ausfällt, darf die Karte nicht dauerhaft unerreichbar bleiben. Nach Ablauf von T_enum verlässt sie den Zustand ENUMERATING selbsttätig:

| Situation | Adresse nach Ablauf von T_enum |
|---|---|
| EEPROM enthält eine gültige Adresse aus einer früheren Enumeration | diese Adresse |
| EEPROM leer, Karte war noch nie enumeriert | Serviceadresse 250 |

T_enum beträgt 10 s, parametrierbar von 1 bis 60 s. Wird `ENUM_DONE` empfangen, greift der Rückfall sofort; der Timeout wirkt also nur, wenn der Master während der Enumeration ausfällt.

**Begründung der zweistufigen Lösung.** Eine für alle Karten identische, fest einkompilierte Rückfalladresse wäre nur so lange brauchbar, wie höchstens eine Karte am Bus hängt. Scheitert die Enumeration im fertigen Anzeigenfeld, meldeten sich sonst zehn Karten gleichzeitig auf derselben Adresse und der Bus wäre unbrauchbar — also genau in dem Fall unbrauchbar, für den der Rückfall gedacht ist. Die im EEPROM abgelegte letzte Zuweisung ist dagegen je Karte eindeutig, überlebt Spannungsausfälle und kostet ein Byte. Die feste Serviceadresse 250 bleibt für den einzigen Fall erhalten, in dem sie eindeutig ist: eine fabrikneue Karte allein am Bus.

**Serviceadresse 250 gilt ausdrücklich nur für den Einzelbetrieb.** Hängen mehrere unkonfigurierte Karten am selben Bus, kollidieren sie. Der Master meldet diesen Zustand als Warnung.

#### 4.5.3 Kollisionserkennung

Da /RE fest auf GND liegt, liest jede Karte ihre eigene Sendung zurück. Weicht ein zurückgelesenes Byte vom gesendeten ab, liegt eine Kollision vor. Die Karte bricht die Sendung ab, verwirft ihre Laufzeitadresse, geht in den Zustand UNADDRESSED und setzt ein Fehlerbit. Eine doppelt vergebene Adresse meldet sich damit selbst, statt sporadisch verfälschte Antworten zu erzeugen.

#### 4.5.4 Verifikationslauf

Nach jeder Enumeration fragt der Master über `GET_UID` die Seriennummer jeder Adresse ab. Der ATtiny stellt sie im SIGROW-Bereich bereit. Doppelte oder zwischen zwei Durchläufen wechselnde Seriennummern zeigen eine misslungene Enumeration an und werden in der Web-UI gemeldet.

### 4.6 Stückliste je Daughter Card

| Pos | Bauteil | Menge | Anmerkung |
|---|---|---|---|
| 1 | ATtiny1616-SNR, SOIC-20 | 1 | |
| 2 | TP8485E-SR, SOIC-8 | 1 | RS-485, 3–5,5 V, Full-Fail-Safe-Empfänger, /RE fest auf GND |
| 3 | BSS84 (P-Kanal) oder MMBT3904 (NPN) | 1 | Triac-Ansteuerung, nur ein Zweig bestückt |
| 4 | — | — | entfällt gegenüber v0.2 (isolierter DC/DC nicht mehr nötig) |
| 5 | Buchsenleiste 2×5, 2,54 mm, gerade | 1 | zur Anzeige, board-to-board auf den Pfostenstecker der Anzeigenplatine (Referenz BKL 10120960); nicht kodiert, Verpolschutz siehe Schaltplan 4.1 |
| 6 | Wannenstecker 2×5, 2,54 mm | 2 | Bus ein / Bus aus |
| 7 | Schraubklemme 2-polig, 5 mm | 2 | 42 V~ ein / aus |
| 8 | Stiftleiste 1×3, 2,54 mm | 1 | UPDI |
| 9 | Lötjumper bzw. 0-Ω-Brücken | 2 | Polaritätswahl Triac-Steuereingang, siehe 4.4 |
| 10 | LED 3 mm + 1 kΩ | 1 | Status |
| 11 | Widerstand 10 kΩ | 3 | Pull-up Impulseingänge |
| 12 | Widerstand 1 kΩ | 3 | Serienwiderstand Impulseingänge |
| 13 | Widerstand 10 kΩ | 1 | Basis- bzw. Gatewiderstand Triac-Treiber |
| 14 | Widerstand 4,7 kΩ oder 1,2 kΩ | 1 | R_S, Wert nach Ergebnis von O-2 |
| 15 | Widerstand 120 Ω | 1 | Busabschluss, per Jumper zuschaltbar |
| 16 | Kondensator 10 nF | 3 | Eingangsfilter |
| 17 | Kondensator 100 nF | 3 | Abblockung |
| 18 | Kondensator 10 µF | 1 | Stützkondensator |
| 19 | BAT54S | 3 | Klemmdioden |

Platinenformat: 74 mm × 60 mm. Die Breite entspricht der Modulbreite ((1 × 75) − 1 mm), die Höhe bleibt unter der 100-mm-Grenze der günstigen Fertigungsstufen.

---

## 5. Bus und Protokoll

### 5.1 Physikalische Schicht

| Parameter | Wert |
|---|---|
| Norm | RS-485, half duplex, 2-Draht |
| Datenrate | 115200 Bd, 8N1 |
| Topologie | Linie, Daisy-Chain über Flachbandkabel |
| Abschluss | 120 Ω an beiden physikalischen Enden, per Jumper |
| Fail-Safe-Bias | 2 × 680 Ω, ausschließlich am Master |
| Treiber Modul | TP8485E-SR, betrieben mit 5 V |
| Treiber Master | TP8485E-SR, betrieben mit 3,3 V |
| Empfänger | dauerhaft aktiv, /RE fest auf GND |

Auf beiden Seiten kommt derselbe Baustein zum Einsatz. Der TP8485E arbeitet von 3 bis 5,5 V, wodurch sich Master und Modul denselben Bauteiltyp teilen, obwohl sie mit unterschiedlicher Logikspannung laufen. Der Mischbetrieb ist zulässig, die Differenzpegel sind kompatibel. Der Empfänger ist Full-Fail-Safe ausgelegt und liefert auch bei offenem, kurzgeschlossenem oder terminiertem aber unbetriebenem Bus einen definierten High-Pegel.

Der dauerhaft aktive Empfänger dient der Kollisionserkennung nach 4.5.3. Die Firmware muss das eigene Sendeecho im Normalbetrieb verwerfen.

### 5.2 Buskabel

10-poliges Flachbandkabel mit IDC-Pfostensteckern, konfektioniert in passender Länge.

| Ader | Signal |
|---|---|
| 1 | +5 V |
| 2 | GND |
| 3 | RS-485 A |
| 4 | GND |
| 5 | RS-485 B |
| 6 | GND |
| 7 | CHAIN |
| 8 | GND |
| 9 | +15 V Triac-Treiberspannung (nur falls O-2 dies ergibt) |
| 10 | GND |

Es gibt keine isolierte Domäne mehr. Ader 9 führt gegebenenfalls eine gewöhnliche, massebezogene 15-V-Schiene aus dem zentralen Netzteil. Ergibt O-2, dass Pin 9 der Anzeige mit 5 V ansteuerbar ist, bleibt Ader 9 unbelegt.

Die 42 V~ werden **nicht** über das Flachbandkabel geführt, sondern über separate zweipolige Schraubklemmen von Karte zu Karte durchgeschleift. Grund: Querschnitt und Schutz gegen Fehlstecken.

### 5.3 Rahmenformat

```
Offset  Länge  Feld
------  -----  ----------------------------------------------
  0       1    Präambel 0xAA
  1       1    Präambel 0x55
  2       1    LEN   = Anzahl Bytes ab Offset 3 inkl. CRC
  3       1    CMD
  4       1    ADDR  (0 = Broadcast, 1…250 = Modul)
  5      n     PAYLOAD
 5+n      2    CRC16/MODBUS über Offset 2 … 4+n, Little Endian
```

CRC16/MODBUS wurde gewählt, damit Standardwerkzeuge und Logic-Analyzer-Dekoder verwendet werden können.

### 5.4 Kommandos

| CMD | Name | Adressierung | Payload | Antwort |
|---|---|---|---|---|
| 0x01 | SET | einzeln | 1 B Zielblatt (1–40) | ACK |
| 0x02 | SET_ALL | Broadcast | n B, Index = Adresse − 1 | keine |
| 0x03 | GO | Broadcast | — | keine |
| 0x04 | STOP | beides | — | ACK |
| 0x10 | GET_STATUS | einzeln | — | 8 B, siehe 5.5 |
| 0x20 | HOME | beides | — | ACK |
| 0x30 | SET_CONFIG | einzeln | 4 B, siehe 6.3 | ACK |
| 0x31 | GET_CONFIG | einzeln | — | 4 B |
| 0x40 | IDENTIFY | einzeln | 1 B Dauer in s | ACK |
| 0x50 | ENUM_RESET | Broadcast | — | keine |
| 0x51 | ENUM_ASSIGN | Broadcast | 1 B neue Adresse | ACK nur von der Karte mit aktivem CHAIN_IN |
| 0x52 | ENUM_DONE | Broadcast | — | keine |
| 0x53 | GET_UID | einzeln | — | 10 B Seriennummer aus SIGROW |
| 0xF0 | PING | einzeln | — | ACK + Firmware-Version |

**Das zentrale Muster für Display-Updates** ist `SET_ALL` gefolgt von `GO`. Der Broadcast enthält die Zielwerte aller Module in einem Rahmen, jedes Modul entnimmt das Byte an der Stelle seiner eigenen Adresse und puffert es. Erst `GO` löst die Bewegung aus, sodass alle Module synchron starten.

Bei 10 Modulen umfasst `SET_ALL` 18 Byte, `GO` 8 Byte. Ein komplettes Update belegt den Bus damit für rund 2,3 ms.

### 5.5 Statusantwort

| Byte | Inhalt |
|---|---|
| 0 | Ist-Blatt (1–40, 0 = unbekannt) |
| 1 | Ziel-Blatt |
| 2 | Zustand: 0 Idle, 1 Homing, 2 Moving, 3 Fehler |
| 3 | Fehlercode, siehe 6.4 |
| 4 | erkannte Blattzahl |
| 5–6 | Zähler korrigierter Positionierversuche, 16 Bit |
| 7 | Firmware-Version |

### 5.6 Zeitverhalten

| Parameter | Wert |
|---|---|
| Antwortverzug Slave, min. | 200 µs nach Rahmenende |
| Antwortverzug Slave, max. | 3 ms |
| Timeout Master | 5 ms |
| Guard-Zeit nach DE-Abschaltung | 100 µs |
| Status-Polling | rundlaufend, ein Modul je 100 ms |
| Wiederholungen bei Timeout | 2, danach Modul als offline markiert |

---

## 6. Firmware Modulsteuerung

### 6.1 Zustandsautomat

```
        ┌──────┐
        │ INIT │  Konfiguration aus EEPROM, Adresse ermitteln
        └───┬──┘
            ▼
      ┌──────────┐   Leerbildimpuls
      │ HOMING   │──────────────────┐
      └───┬──────┘                  │
          │ Timeout 4 s             ▼
          ▼                    ┌────────┐
      ┌───────┐   GO / SET     │  IDLE  │
      │ ERROR │◀──────────────▶└───┬────┘
      └───────┘                    │
          ▲                        ▼
          │   Timeout          ┌────────┐
          └────────────────────│ MOVING │
                               └────────┘
```

### 6.2 Ablauf im Einzelnen

**HOMING.** Motor einschalten, auf Leerbildimpuls warten. Bei Empfang wird der Blattzähler auf den konfigurierten Blatt-Offset gesetzt. Der Zustand gilt danach als synchronisiert. Timeout 4 s, das entspricht rund 1,7 vollen Umdrehungen.

**Blattzahlerkennung.** Beim ersten Homing nach einem Kaltstart läuft der Motor bis zum zweiten Leerbildimpuls weiter und zählt die dazwischenliegenden Blattimpulse. Das Ergebnis wird gegen die konfigurierte Blattzahl geprüft und im Statusbyte gemeldet. Damit ist die Karte selbstkonfigurierend und für 40, 64 und 80 Blatt gleichermaßen verwendbar.

**MOVING.** Wegstrecke = (Ziel − Ist) mod Blattzahl. Der Fallblattsatz läuft ausschließlich vorwärts. Motor einschalten, Blattimpulse zählen, bei Erreichen des Ziels abzüglich des Abschaltvorhalts das Gate abschalten.

**Abschaltvorhalt.** Der Triac löscht erst beim nächsten Stromnulldurchgang, bis zu 10 ms nach Wegnahme des Gate-Signals. Dazu kommt mechanischer Nachlauf. Der Vorhalt wird als Parameter in Millisekunden geführt und einmalig empirisch ermittelt.

**Positionsspeicherung.** Nach jedem Stillstand wird die erreichte Position ins EEPROM geschrieben, damit die Anzeige nach einem Netzausfall nicht zwingend homen muss. Zum Schutz vor Verschleiß wird ein Ringpuffer über 16 EEPROM-Zellen verwendet.

### 6.3 Konfigurationsparameter (EEPROM)

| Byte | Parameter | Wertebereich | Vorgabe |
|---|---|---|---|
| 0 | Blattzahl | 40, 64, 80 | 40 |
| 1 | Blatt-Offset | 0–79 | ⚠️ zu ermitteln |
| 2 | Abschaltvorhalt in ms | 0–60 | ⚠️ zu ermitteln |
| 3 | Flags: Bit 0 Positionsspeicherung, Bit 1 Autohoming beim Start, Bit 2 Triac-Polarität | — | 0x03 |
| 4 | Zuletzt zugewiesene Busadresse | 0 = keine, 1–250 | 0 |
| 5 | T_enum in Sekunden | 1–60 | 10 |

### 6.4 Fehlerbehandlung

| Code | Bedeutung | Reaktion |
|---|---|---|
| 0x01 | Kein Blattimpuls innerhalb 200 ms bei laufendem Motor | Abbruch, bis zu 3 Wiederholungen, dann Fehler |
| 0x02 | Kein Leerbildimpuls innerhalb 4 s beim Homing | Motor aus, Fehler |
| 0x03 | Erkannte Blattzahl unplausibel | Motor aus, Fehler |
| 0x04 | Position verloren, nicht synchronisiert | Homing anfordern |
| 0x05 | Laufzeitüberwachung ausgelöst (> 4 s durchgehend) | Motor aus, Fehler |
| 0x06 | Adresskollision über Sendeecho erkannt | Adresse verwerfen, Zustand UNADDRESSED, Mechanik unberührt |

Die dreifache Wiederholung entspricht dem Verhalten der Originalsteuerung.

**Fail-Safe.** Ein hängender Controller darf den Motor nicht dauerhaft bestromen. Absicherung durch den internen Watchdog (Zeitbasis 1 s) sowie durch die Laufzeitüberwachung nach Code 0x05. Optional lässt sich ein retriggerbares Monoflop in Hardware ergänzen, das den Transistorschalter nach 4 s zwangsweise sperrt.

---

## 7. Zentralsteuerung ESP32-C3

### 7.1 Hardware

| Position | Auswahl |
|---|---|
| Modul | **ESP32-C3 Super Mini** (Aftermarket-Modul), steckbar in Buchsenleisten auf einem Trägerboard |
| Bustreiber | TP8485E-SR, betrieben mit 3,3 V (vom 3V3-Pin des Moduls), /RE fest auf GND |
| Busabschluss | 120 Ω fest, Fail-Safe-Bias 2 × 680 Ω (A→+3V3, B→GND) |
| CHAIN-Ausgang | GPIO über Pegelwandler 3,3 V → 5 V (74LVC1G17); bei Bedarf 0-Ω-Brücke |
| Versorgung | 5 V aus der Netzteilbaugruppe (Abschnitt 8.2); ESP32-C3 über Onboard-LDO |
| Triac-Treiberspannung (Ader 9) | Lötbrücke offen / +5 V / +15 V; Aufwärtswandler als unbestückter Steckplatz, siehe O-2 |

Das Trägerboard erzeugt alle Logikspannungen und Bussignale außer der 42 V~. Die
42 V~ (Ringkerntrafo 2 × 18 V in Reihe) werden **nicht** über das Master-Board
geführt, sondern direkt an die erste Daughter Card verdrahtet.

Begründung für den ESP32-C3 Super Mini statt des WROOM-32E: kleiner, natives
USB-C für Flashen und Konsole, ausreichend GPIO für RS-485 + CHAIN + Status-LED
bei fest verdrahtetem Aufbau, geringere Kosten. Der C3 hat nur zwei UARTs
(UART0 = USB-Konsole); RS-485 läuft auf UART1 über die GPIO-Matrix. Details:
`docs/schaltplan-master.md`, Symbolprüfung `docs/symbolpruefung-master.md`.

### 7.2 Softwarestack

Arduino-ESP32, bewusst ohne ESPHome, da bei zehn Modulen die Entity-Verwaltung sonst unübersichtlich wird.

| Aufgabe | Bibliothek |
|---|---|
| WLAN-Einrichtung | WiFiManager, Captive Portal beim Erststart |
| Web-UI | eingebauter `WebServer` (in T8 gewählt; siehe unten) |
| Konfiguration | ArduinoJson zum Parsen, Ablage in `Preferences`/NVS |
| MQTT | PubSubClient mit Home-Assistant-Auto-Discovery |
| Zeit | `configTzTime`; NTP-Server und Zeitzone in der Web-UI änderbar, Uhr auch manuell stellbar; NTP abschaltbar |
| Update | ArduinoOTA (abschaltbar) |
| mDNS | `<node>.local` (abschaltbar) |

In T8 wurden gegenüber der Erstfassung `ESPAsyncWebServer` durch den eingebauten
synchronen `WebServer` und `LittleFS` durch `Preferences` (NVS) ersetzt:
dependency-arm, ohne `AsyncTCP`, mit arduino-esp32 3.x ohne Patches lauffähig.
Für zehn Module und die einfache UI genügt der synchrone Server. Details in
`docs/toolchain.md` Abschnitt 4.

### 7.3 Funktionsumfang

| Funktion | Beschreibung |
|---|---|
| WLAN-Konfiguration | Access-Point mit Captive Portal beim Erststart, danach über die Web-UI änderbar |
| Modulverwaltung | Anzahl per Enumeration automatisch, in der UI überschreib- und sperrbar |
| Freitext | Eingabe über Web-UI, REST und MQTT; Umlaute und Kleinbuchstaben werden gemappt, unbekannte Zeichen auf Leerbild |
| Uhrzeit | NTP-gestützt (Server + Zeitzone konfigurierbar), Format `hh:mm` oder `hh:mm:ss`, Trennzeichen wählbar. Ohne erreichbaren NTP-Server auch manuell stellbar (freilaufend, keine gepufferte RTC). |
| Selbsttest | jedes Modul fährt eine volle Umdrehung, prüft die Zahl der Blattimpulse zwischen zwei Leerbildimpulsen und meldet Timing-Abweichungen |
| Statusabfrage | Ist-Zeichen, Zustand, Fehlerzähler je Modul in der UI und über REST |
| Diagnose | Modul-Detailtabelle (erkannte Blattzahl, FW-Version, verpasste Antworten), Ereignis-Log (Ringpuffer), Bus-CRC-/Timeout-Zähler in der Web-UI |
| Homing | einzeln oder für alle Module; einzeln auch Stop und Identify |
| Betriebsartenwahl | Text, Uhr, Leerbild, Aus |
| Schnittstellen | MQTT, REST-Schreib-API, OTA und mDNS einzeln abschaltbar (Einstellungen). Die Web-Oberfläche selbst nicht. |

### 7.4 Zeichenabbildung

- Kleinbuchstaben werden in Großbuchstaben gewandelt.
- `Ä` → `AE`, `Ö` → `OE`, `Ü` → `UE`, `ß` → `SS`. Reicht der Platz nicht, wird auf den Grundbuchstaben reduziert.
- Nicht darstellbare Zeichen werden auf Leerbild abgebildet.
- Ausrichtung wählbar: linksbündig, zentriert, rechtsbündig.

### 7.5 REST-Schnittstelle

| Methode | Pfad | Funktion |
|---|---|---|
| GET | `/api/status` | Anzeige- und Modulstatus als JSON (mode, Zielzeichen, je Modul Ist/Ziel/Zustand/Fehler/Korrekturen/erkannte Blattzahl/FW/verpasste Antworten, erkannte Modulzahl, Enumerationsstatus) |
| GET | `/api/system` | Uptime, freier/min. Heap, SSID/IP/RSSI/MAC, Uhrzeit + Quelle, NTP-Server/Zeitzone, MQTT-/OTA-/mDNS-Status, Bus-CRC-Fehler und -Timeouts, Firmware-Build |
| GET | `/api/log` | Ereignis-Ringpuffer (`?sev=info\|warn\|err`) |
| POST | `/api/log/clear` | Log leeren |
| POST | `/api/text` | `{"text":"HALLO"}` |
| POST | `/api/mode` | `{"mode":"clock_hm","sep":".","align":1}` |
| POST | `/api/home` | optional `{"addr":3}` |
| POST | `/api/selftest` | startet den Selbsttest |
| POST | `/api/module` | `{"addr":3,"action":"home\|stop\|identify"}` (addr 0 = Broadcast) |
| POST | `/api/enumerate` | Enumeration neu starten |
| POST | `/api/time` | `{"iso":"2026-09-01T14:07:00"}` — Uhr manuell stellen |
| GET | `/api/wifi/scan` | erreichbare WLANs (SSID, RSSI, verschlüsselt) |
| POST | `/api/wifi` | `{"ssid":"…","psk":"…"}` — Netz wechseln (Rückfall aufs alte Netz nach ~25 s) |
| POST | `/api/wifi/portal` | WiFiManager-Konfigurationsportal öffnen |
| POST | `/api/reboot` | Neustart |
| GET/POST | `/api/config` | vollständige Konfiguration lesen/schreiben: MQTT, NTP-Server, Zeitzone, feste IP, Ausrichtung, Trennzeichen, Modulzahl, hh:mm:ss-Timeout, sowie die Schalter MQTT / REST-Schreib-API / OTA / mDNS |

Die schreibenden Steuer-Endpunkte (`/api/text`, `/api/mode`, `/api/home`,
`/api/selftest`, `/api/module`, `/api/enumerate`) lassen sich über den Schalter
**REST-Schreib-API** in den Einstellungen sperren (`403`); Statusabfragen und die
Einstellungen bleiben dann weiter erreichbar. MQTT, OTA und mDNS sind einzeln
abschaltbar. Die Web-Oberfläche selbst ist nicht abschaltbar.

Die Weboberfläche ist eine einzelne, vom ESP32-C3 ausgelieferte Seite
(System-Schriften, kein CDN — im LAN ohne Internet nutzbar) mit den Ansichten
Übersicht (Split-Flap-Statusstreifen, Kacheln, Schnellaktionen), Module (Tabelle),
Log und Einstellungen.

### 7.6 MQTT und Home Assistant

Anbindung über MQTT mit Auto-Discovery, damit keine Custom Integration in Home Assistant installiert werden muss.

| Entity | Typ | Topic-Suffix |
|---|---|---|
| Anzeigetext | `text` | `text/set`, `text/state` |
| Betriebsart | `select` | `mode/set`, `mode/state` |
| Homing | `button` | `home/press` |
| Selbsttest | `button` | `selftest/press` |
| Zeichen je Modul | `sensor` | `module/<n>/char` |
| Sammelfehler | `binary_sensor` | `error/state` |
| Modul online | `binary_sensor` | `module/<n>/online` |

### 7.7 Hinweis zur Sekundenanzeige

Bei `hh:mm:ss` springt der Sekunden-Einer alle zehn Sekunden von `9` auf `0`. Der Weg beträgt (3 − 12) mod 40 = 31 Blätter, also 1,86 s. Das Modul absolviert damit eine volle Umdrehung je zehn Sekunden, entsprechend 8.640 Umdrehungen je Tag. Gegen die spezifizierte MTBF von 1,5 · 10⁶ Rotorumdrehungen entspricht das rund **173 Tagen Dauerbetrieb**.

Zum Vergleich: Der Minuten-Einer im Format `hh:mm` kommt auf 144 Umdrehungen je Tag, also knapp 28 Jahre.

Festlegung: `hh:mm` ist die Vorgabe. `hh:mm:ss` ist verfügbar, wird in der Web-UI mit einem Hinweis versehen und kehrt nach einer konfigurierbaren Zeit (Vorgabe 10 Minuten) automatisch auf `hh:mm` zurück.

---

## 8. Stromversorgung

### 8.1 Motorspannung 42 V~

**Zulässiger Bereich: 35,7 bis 46,2 V** (42 V +10 % / −15 %).

Da 42 V keine gängige Katalogspannung mehr ist, wird ein Standardtrafo mit zwei in Reihe geschalteten 18-V-Wicklungen eingesetzt.

| Sekundär | In Reihe | Bewertung |
|---|---|---|
| 2 × 15 V | 30 V | zu niedrig, −29 % |
| **2 × 18 V** | **36 V nominal** | **gewählt** |
| 2 × 24 V | 48 V | zu hoch, bereits nominal über der Grenze |

Die Nennspannung gilt bei Volllast. Bei der vorgesehenen Teillast und einem Ringkern mit 5–8 % Regelung stellen sich rund **38 bis 39 V** ein. Das entspricht −8 bis −10 % gegenüber 42 V und liegt damit innerhalb der Spezifikation. Da das Drehmoment eines permanenterregten Synchronmotors näherungsweise linear mit der Spannung skaliert, verbleiben rund 90 % des Nennmoments.

**Auslegung:** 10 Module à 100 mA ergeben 1 A, bei 36 V also 36 VA. Gewählt: Ringkerntransformator 230 V → 2 × 18 V, **50 bis 80 VA**.

**Anschluss:** Beide Wicklungen phasenrichtig in Reihe. Vor dem Anschluss der Anzeigen die Ausgangsspannung messen; bei falscher Polung ergeben sich 0 V statt 36 V.

**Reserve, falls das Moment nicht reicht:** Bei einem Ringkern lassen sich Zusatzwindungen durch den Kern legen. Zunächst 10 Windungen einziehen, Spannung messen, Volt je Windung bestimmen und die für die fehlenden 4 bis 6 V nötige Windungszahl ergänzen. Bei einem 50-VA-Ringkern sind das typischerweise 15 bis 30 Windungen.

**Primärseitig:** träge Sicherung, bei Ringkern zusätzlich NTC gegen den Einschaltstromstoß.

### 8.2 Logikversorgung 5 V

| Verbraucher | Strom |
|---|---|
| 20 Hall-Sensoren à 15 mA | 300 mA |
| 10 × ATtiny + Transceiver | ca. 100 mA |
| ESP32-C3 mit WLAN, Spitze | 350 mA |
| **Summe mit Reserve** | **2 A** |

Gewählt: Schaltnetzteil 5 V / 2 A. Verteilung über zwei Adern des Busbandkabels. Bei zehn Modulen und kurzer Kettenlänge sind keine lokalen Regler erforderlich.

### 8.3 Treiberspannung für den Triac-Eingang

Eine galvanisch getrennte Quelle ist nicht erforderlich, da Masse und Versorgung über den gesamten Aufbau gemeinsam sind.

Ergibt O-2 Deutung 1, wird eine massebezogene 15-V-Schiene mit mindestens 100 mA benötigt und über Ader 9 des Buskabels verteilt. Ergibt O-2 Deutung 2, entfällt sie ersatzlos und der Treiber arbeitet aus der vorhandenen 5-V-Schiene.

### 8.4 Potenzialtrennung der Motorspannung

Die Sekundärwicklung des 42-V~-Trafos bleibt **potenzialfrei**. Sie darf an keiner Stelle mit der Logikmasse verbunden werden, da der Gate-Kreis des Triacs auf MT1 bezogen ist und damit auf dem Wechselspannungspotenzial schwimmt. Eine Verbindung würde die Treiberstufe auf der Anzeigenplatine kurzschließen.

---

## 9. Nichtfunktionale Anforderungen

| Nr | Anforderung |
|---|---|
| NF-1 | Betriebstemperatur 0 bis +40 °C (Innenraum), Bauteile für −20 bis +60 °C ausgelegt |
| NF-2 | Dauerbetrieb ohne Neustart über mindestens 30 Tage |
| NF-3 | Ausfall des WLAN oder des MQTT-Brokers beeinträchtigt die Anzeige nicht; der zuletzt gesetzte Inhalt bleibt stehen |
| NF-4 | Ausfall einer einzelnen Daughter Card legt die übrigen Module nicht lahm |
| NF-5 | Alle 42-V~-führenden Teile berührsicher; Trafo als Sicherheitstransformator nach EN 61558 |
| NF-6 | Firmware-Update der Zentralsteuerung über OTA, der Modulsteuerungen über UPDI |
| NF-7 | Kein Modul darf durch einen Firmwarefehler dauerhaft bestromt bleiben, siehe 6.4 |
| NF-8 | Konfiguration überlebt Stromausfall |

---

## 10. Abnahmekriterien und Testplan

### 10.1 Stufenweise Inbetriebnahme eines Einzelmoduls

| Stufe | Aufbau | Prüfkriterium |
|---|---|---|
| 1 | Nur 5 V an Pin 5/6, Rotor von Hand drehen, Logic Analyzer an Pin 8 und 10 | Zwischen zwei Leerbildimpulsen liegen exakt 40 Blattimpulse; Ruhepegel und aktive Flanke dokumentiert |
| 2 | Steuereingang Pin 9 über 4,7 kΩ an 15 V bzw. über 1,2 kΩ an 5 V, **ohne** 42 V~ | Widerstand zwischen Pin 2 und Pin 4 wechselt von hochohmig auf niederohmig. Der Pegel, bei dem das zuverlässig gelingt, entscheidet O-2. |
| 3 | 42 V~ mit Glühlampe 40 W / 230 V in Reihe | Triac zündet und löscht wieder, erkennbar am Glimmen der Lampe |
| 4 | Voller Aufbau | Abschaltvorhalt so kalibriert, dass die Zielposition ohne Überlauf erreicht wird |

### 10.2 Abnahmekriterien Gesamtsystem

| Nr | Kriterium |
|---|---|
| A-1 | Nach Kaltstart ist jedes Modul innerhalb von 5 s synchronisiert |
| A-2 | Die automatische Blattzahlerkennung liefert bei allen Modulen den Wert 40 |
| A-3 | 100 zufällige Zielpositionen je Modul werden ohne Abweichung angefahren |
| A-4 | Ein vollständiges Display-Update aller Module ist nach höchstens 2,6 s abgeschlossen |
| A-5 | Netzunterbrechung während einer Bewegung führt beim Wiedereinschalten zur selbsttätigen Synchronisierung |
| A-6 | 24 Stunden Dauerbetrieb bei 115200 Bd ohne CRC-Fehler auf dem Bus |
| A-7 | Trennung des WLAN lässt den Anzeigeinhalt unverändert stehen |
| A-8 | Die Motorspannung liegt unter Volllast zwischen 35,7 und 46,2 V |
| A-9 | Ziehen und Wiedereinsetzen einer Daughter Card stellt nach der Enumeration den ursprünglichen Zustand her |
| A-10 | Unterbrechung der CHAIN-Leitung während der Enumeration führt dazu, dass alle betroffenen Karten nach T_enum wieder unter ihrer zuvor gespeicherten Adresse erreichbar sind |
| A-11 | Eine fabrikneue Karte allein am Bus ist unter der Serviceadresse 250 erreichbar |
| A-12 | Zwei Karten mit gleicher Adresse werden über die Echo-Auswertung erkannt und in der Web-UI gemeldet |
| A-13 | Der Verifikationslauf über GET_UID liefert für jede Adresse eine eindeutige Seriennummer |

---

## 11. Offene Punkte

| Nr | Punkt | Auswirkung | Klärung durch | Status |
|---|---|---|---|---|
| O-1 | Getrennte Potenzialbereiche zwischen Sensorik und Triac | — | — | ✅ **erledigt in v0.3.** Laut Originaldoku sind Pin 1 und 3 gemeinsame Masse, Pin 5 und 6 gemeinsame Versorgung. Es gibt keine getrennten Domänen. |
| O-2 | Erforderlicher Ansteuerpegel an Pin 9: 5 V oder 12–20 V | Bestückung von JP1/JP2 und Wert von R_S; entscheidet über Ader 9 des Buskabels und eine 15-V-Schiene im Netzteil | Teststufe 2 nach 10.1. Ergänzend Widerstands- und Diodenmessung Pin 9 gegen Masse: Ein Vorwiderstand um 4,7 kΩ deutet auf 15 V, um 1 kΩ auf 5 V. | offen |
| O-3 | Pinnummern für Triac-Eingang und 42 V~ | — | — | ✅ **erledigt in v0.3.** Pin 9 Triac, Pin 2 und Pin 4 die 42 V~. |
| O-4 | Bestätigung der Versorgungsspannung an Pin 5 und 6 | — | — | ✅ **erledigt in v0.3.** Beide Pins führen dieselbe Versorgung, +5…+6,6 V. |
| O-5 | Ausgangstyp der Hall-Sensoren („03F 936") | Dimensionierung der Pull-ups | Teststufe 1 nach 10.1 | weitgehend geklärt: Ruhepegel high, Impuls zieht nach Masse, also Open Collector. Offen bleibt nur der zulässige Senkenstrom. |
| O-6 | Blatt-Offset zwischen Leerbildimpuls und Blattindex | Firmware-Parameter | Teststufe 4 nach 10.1 | offen |
| O-7 | Funktion des Nullimpulses (Pin 7) | Keine, wird nicht ausgewertet | Optional: Mitschnitt über eine volle Umdrehung | offen, nicht blockierend |

---

## Anhang A — Blattbelegung (40-Blatt-Ausführung)

| Blatt | Zeichen | Blatt | Zeichen | Blatt | Zeichen | Blatt | Zeichen |
|---|---|---|---|---|---|---|---|
| 1 | Leerbild | 11 | 8 | 21 | I | 31 | S |
| 2 | Leerbild | 12 | 9 | 22 | J | 32 | T |
| 3 | 0 | 13 | A | 23 | K | 33 | U |
| 4 | 1 | 14 | B | 24 | L | 34 | V |
| 5 | 2 | 15 | C | 25 | M | 35 | W |
| 6 | 3 | 16 | D | 26 | N | 36 | X |
| 7 | 4 | 17 | E | 27 | O | 37 | Y |
| 8 | 5 | 18 | F | 28 | P | 38 | Z |
| 9 | 6 | 19 | G | 29 | Q | 39 | - |
| 10 | 7 | 20 | H | 30 | R | 40 | . |

Wegstrecke von Blatt a nach Blatt b: `(b − a) mod 40` Blätter zu je 60 ms. Längster Weg 39 Blätter, entspricht 2,34 s.

---

## Anhang B — Glossar

| Begriff | Bedeutung |
|---|---|
| Palettenmodul | Anzeigenmodul mit Blattsatz, Antrieb und Sensorik |
| Palettensteuerung (PST) | Originale Steuerelektronik je Modul, hier ersetzt |
| Anzeigersteuerung | Originale Zentralsteuerung, hier durch den ESP32-C3 ersetzt |
| Leerbild | Unbedrucktes Blatt, hier Blatt 1 und 2, zugleich Synchronisationspunkt |
| Leerbildimpuls | Hall-Impuls, ein Ereignis je voller Blattsatzumdrehung |
| Blattimpuls / Zählimpuls | Hall-Impuls, ein Ereignis je Blatt |
| Blatt-Offset | Versatz zwischen Leerbildimpuls und Blattindex |
| Abschaltvorhalt | Vorlaufzeit beim Abschalten des Gates zum Ausgleich von Triac-Löschverzug und mechanischem Nachlauf |
| Daughter Card | Eigenbau-Modulsteuerung, eine je Anzeigenmodul |
| Enumeration | Automatische Adressvergabe entlang der CHAIN-Leitung |
| T_enum | Wartezeit im Zustand ENUMERATING, nach deren Ablauf die Karte auf ihre gespeicherte Adresse oder die Serviceadresse zurückfällt |
| Serviceadresse | Feste Adresse 250 für fabrikneue Karten, nur im Einzelbetrieb eindeutig |

---

## Anhang C — Referenzdokumente

| Dokument | Inhalt |
|---|---|
| KroneRew.pdf, S. 184–193 | Funktionsprinzip, konstruktive Merkmale, Ansteuerung, technische Daten Palettenmodulreihe A |
| KroneRew_Fallblattanzeige.pdf | Schaltplan Palettensteuerung PST, 6412 2 100-00 |
| Zeichnung 6280 1 200-11 U | Funktionsdarstellung Palettenmodul |
| Zeichnung 6280 1 200-14 U | Abtastprinzip, Impulsrad und Hall-Sensoren |
| Leiterplatte 6281 3 160-00 K1/2 | Anzeigenplatine, Fotos vom 27.08.2026 |
| Datenblatt Teccor L201E3 | Sensitive Triacs 0,8 A bis 8 A |

---

## Anhang D — Änderungshistorie

| Version | Datum | Änderung |
|---|---|---|
| 0.1 | 27.08.2026 | Erstfassung. Architektur, Modul-CPU, Busprotokoll, Firmware-Konzept, Zentralsteuerung, Netzteil, Testplan. Offene Punkte O-1 bis O-7 markiert. |
| 0.3 | 27.08.2026 | Steckerbelegung aus der Originaldokumentation vollständig übernommen: gemeinsame Masse (Pin 1, 3), gemeinsame Versorgung (Pin 5, 6), 42 V~ an Pin 2 und 4, Triac-Eingang an Pin 9, Impulse an Pin 8 und 10 mit fallender auswertender Flanke. Getrennte Potenzialbereiche entfallen, damit auch Optokoppler und isolierter DC/DC-Wandler. Kapitel 4.4 neu gefasst mit Transistorschalter und wählbarer Treiberspannung. Abschnitt 8.4 zur Potenzialfreiheit der Motorspannung ergänzt. O-1, O-3 und O-4 abgeschlossen, O-2 neu formuliert. |
| 0.4 | 28.08.2026 | Kapitel 4.2: Reserve-Pins präzisiert. Nur PB5/PB4 liegen auf Testpads (TP1/TP2); PB0 und PC0…PC3 bleiben im Schaltplan v0.2 unbeschaltet. Auflösung des Widerspruchs zwischen Schaltplan 6.3 und 6.4, dokumentiert in `docs/pruefpunkte-t4.md`. Zur zweiten Prüfung durch den Betreiber offen. |
| 0.5 | 28.08.2026 | Kapitel 4.2: RS-485-Pins auf die tatsächliche USART0-Belegung des ATtiny1616 korrigiert (`docs/pruefpunkte-t7.md` P-3, vom Betreiber freigegeben). RXD an PB3 (Pin 8), TXD an PB2 (Pin 9), XDIR an PB0 (Pin 11). PB1 (USART0 XCK) wird Reserve. P-1 und P-2 aus `pruefpunkte-t4.md` als freigegeben vermerkt. |
| 0.6 | 28.08.2026 | T7: Blatt- und Leerbildimpuls werden über einen Flanken-Interrupt auf PORTA ausgewertet statt über TCB0 Input Capture (Kapitel 4.2). TCB0 stellt die 1-ms-Zeitbasis. Bei ≤ 17 Impulsen/s genügt der Software-Zeitstempel; das spart einen Zeitgeber. |
| 0.7 | 28.08.2026 | T8: Kapitel 7.2 — Softwarestack der Zentralsteuerung dependency-arm gefasst: eingebauter `WebServer` statt `ESPAsyncWebServer`, `Preferences` (NVS) statt `LittleFS`. Funktionsumfang 7.3–7.7 unverändert. |
| 0.8 | 31.08.2026 | Stückliste 4.6: J1 (zur Anzeige) ist eine **Buchsenleiste 2×5**, die board-to-board direkt auf den Pfostenstecker der Anzeigenplatine gesteckt wird — kein Flachbandkabel an dieser Stelle. J1 ist damit nicht mechanisch kodiert; der Verpolschutz (42 V~ an Pin 2/4) ist über Mechanik, Bestückungsdruck und die Durchgangsprüfung sicherzustellen, siehe Schaltplan 4.1 und `docs/pruefpunkte-j1-buchsenleiste.md`. Pinbelegung unverändert. |
| 0.9 | 01.09.2026 | Kapitel 7.1: Master-CPU auf **ESP32-C3 Super Mini** (steckbares Aftermarket-Modul) festgelegt. Trägerboard erzeugt 5 V (Eingang) → 3,3 V (Onboard-LDO), RS-485 auf UART1, CHAIN über Pegelwandler 74LVC1G17, Ader 9 als Lötbrücke offen/+5V/+15V mit unbestücktem Aufwärtswandler-Steckplatz. Die 42 V~ laufen nicht über das Master-Board. Schaltplan + geroutete PCB: `docs/schaltplan-master.md`, `hardware/master/`, `tools/gen_master_*.py`, `tools/route_master.py`. ERC 0/0, DRC 0/0. Offene Punkte M-1 (Modul-Pinbelegung), M-2 (Boost), M-3 (CHAIN-Pegel); Firmware-Portierung auf den C3 = Backlog T12. |
| 0.10 | 01.09.2026 | Kapitel 7.3/7.5: Weboberfläche der Zentralsteuerung überarbeitet (Dark-Theme, Ansichten Übersicht/Module/Log/Einstellungen). REST um `/api/system`, `/api/log`, `/api/module`, `/api/enumerate`, `/api/time`, `/api/wifi/scan`, `/api/wifi`, `/api/wifi/portal`, `/api/reboot` erweitert; `/api/config` deckt jetzt NTP-Server, Zeitzone, feste IP und die Schalter MQTT / REST-Schreib-API / OTA / mDNS ab. Neues hardwareunabhängiges Modul `lib/eventlog` (Ereignis-Ringpuffer, host-getestet). Busmaster zählt CRC-Fehler und Timeouts. |
| 0.11 | 01.09.2026 | Dokumentationspflege: Dokumentkopf auf die tatsächliche Version gebracht (war seit v0.8 nicht mitgezogen). Kapitel 2.2/7/8.2/Anhang B durchgängig „ESP32-C3" statt „ESP32". Kapitel 7.2/7.3 um die konfigurierbaren Zeit-/Schnittstellen-Einstellungen und die Diagnose-Ansicht ergänzt. Keine inhaltlichen Systemänderungen. |
