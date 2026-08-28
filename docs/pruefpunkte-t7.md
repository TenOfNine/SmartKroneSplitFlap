# Prüfpunkte T7 — Freigabe durch den Betreiber

| Feld | Wert |
|---|---|
| Zweck | Beim Beginn von T7 (Modul-Firmware) ist ein Widerspruch zwischen der Pinbelegung in `spezifikation.md` 4.2 / `schaltplan-daughtercard.md` 6.3 und der tatsächlichen USART0-Verdrahtung des ATtiny1616 aufgefallen. |
| Bezug | CLAUDE.md Regel 5 (Pinbelegung gegen Datenblatt, menschliche Freigabe) · Backlog T7 |
| Status | **offen** — blockiert die USART-Anbindung in T7 |
| Datum | 28.08.2026 |

---

## P-3 — RS-485-Anschluss an falschen ATtiny1616-Pins

### Befund

Die Spezifikation 4.2 legt fest:

| Signal | Spec 4.2 / Schaltplan 6.3 | ATtiny1616 USART0 (MUX-Standard) |
|---|---|---|
| TXD → DI  | PB2 (Pin 9)  | **PB2** ✅ |
| RXD ← RO  | PB1 (Pin 10) | PB1 ist **XCK**, nicht RXD ❌ |
| XDIR → DE | PB3 (Pin 8)  | PB3 ist **RXD**, nicht XDIR ❌ |

Belegt durch `framework-arduino-megaavr-megatinycore` 2.6.11,
`variants/txy6/pins_arduino.h`:

```
#define HWSERIAL0_MUX_DEFAULT   (0)
#define PIN_HWSERIAL0_TX        (PIN_PB2)
#define PIN_HWSERIAL0_RX        (PIN_PB3)
#define PIN_HWSERIAL0_XCK       (PIN_PB1)
#define PIN_HWSERIAL0_XDIR      (PIN_PB0)
```

Die alternative MUX-Position (PA1/PA2/PA3/PA4) scheidet aus — dort liegen bereits
CHAIN_IN, CHAIN_OUT, Status-LED und der Blattimpuls.

**Folge, wenn nicht korrigiert:** Die USART kann RO und DE nicht auf PB1/PB3
bedienen. Die im Schaltplan 5.2 und in der Spez. 4.2 beschriebene byte-genaue
DE-Steuerung über die Hardware-XDIR-Funktion funktioniert nicht; RXD landet auf
einem Pin ohne UART-Funktion.

### Empfohlene Korrektur — USART0 auf der Standard-MUX-Position

| Pin | Port | Bisher | Neu |
|---|---|---|---|
| 8  | PB3 | DE   | **RO**  (USART0 RXD) |
| 9  | PB2 | DI   | DI (unverändert, USART0 TXD) |
| 10 | PB1 | RO   | **frei / Reserve** (USART0 XCK, im Async-Betrieb ungenutzt) |
| 11 | PB0 | offen (Reserve) | **DE** (USART0 XDIR) |

Damit übernimmt die Hardware-XDIR-Funktion von USART0 die DE-Steuerung wie in
Spez. 4.2 vorgesehen (`USART0.CTRLA`, RS485-Modus). PB1 wird zum neuen
unbeschalteten Reserve-Pin, symmetrisch zur bisherigen Behandlung von PB0.

### Betroffene Stellen (bei Freigabe alle in einem Commit)

- `tools/gen_daughtercard_sch.py` — Netze `RO`, `DE`; `NO_CONNECT_PINS`
  (PB0 raus, PB1 rein). Schaltplan neu erzeugen, ERC muss 0/0 bleiben.
- `docs/schaltplan-daughtercard.md` 5.2, 6.2, 6.3, Änderungshistorie → v0.3
- `docs/spezifikation.md` 4.2, Anhang D → v0.5
- `docs/symbolpruefung.md` — NL-Spalte für Pin 8/10/11 (die Datenblattprüfung
  VDD/GND/Portnamen bleibt unberührt)
- `docs/layout-daughtercard.md` — DE liegt jetzt an PB0 (Pin 11) statt PB3

### Freigabe

| Ergebnis | Bestätigt von | Datum |
|---|---|---|
| _(offen)_ | | |

---

## Was T7 in der Zwischenzeit nicht blockiert

Der Motion-Zustandsautomat (HOMING/IDLE/MOVING/ERROR), die Impulsauswertung
(PA4–PA6), die Triac-Ausgabe (PA7), CHAIN (PA1/PA2), Status-LED (PA3), Watchdog
und die EEPROM-Parameter sind von P-3 nicht betroffen. Betroffen ist allein die
USART-/RS-485-Anbindung.
