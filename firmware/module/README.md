# firmware/module

Firmware der Modulsteuerung (ATtiny1616, PlatformIO, **bare metal** / avr-libc).

## Aufbau

| Teil | Inhalt | Bezug |
|---|---|---|
| `lib/protocol/` | Rahmen, CRC16/MODBUS, Kommandotabelle | Spez. 5.3–5.5 |
| `lib/enumeration/` | Enumerations-Automat, Rückfall, Kollisionserkennung | Spez. 4.5 |
| `lib/motion/` | Bewegungs-Zustandsautomat (HOMING/IDLE/MOVING/ERROR) | Spez. 6 |
| `lib/config/` | EEPROM-Konfiguration, Vorgaben, Bereichsprüfung | Spez. 6.3 |
| `src/board.h` | **einzige** Datei mit hardwarenahen Konstanten (Pins, Takt, EEPROM-Layout) |
| `src/main.c` | Peripherie-Setup, ISRs, Kommando-Dispatch, Hauptschleife |

Die vier Bibliotheken sind hardwareunabhängig (kein Registerzugriff) und auf dem
Host getestet. `src/` verdrahtet sie mit USART0 (RS-485-Modus, XDIR treibt DE),
TCB0 (1-ms-Zeitbasis), PORTA-Flankeninterrupts (Impulse) und dem Watchdog.

## Tests (Host)

```bash
source ../../.venv/bin/activate
pio test -e native
```

`test_crc`, `test_frame`, `test_command`, `test_enumeration`, `test_motion`,
`test_config` (Unity). Der Enumerationstest deckt die vom Backlog T6 geforderten
Fälle ab: Kollisionserkennung, Rückfall EEPROM-Adresse, Rückfall Serviceadresse 250.

## Firmware bauen und flashen

```bash
pio run  -e attiny1616               # ~5,3 KB Flash (Grenze 8 KB)
pio run  -e attiny1616 -t upload     # SerialUPDI, FTDI-Adapter mit 4,7 kΩ TX–RX
```

Voraussetzung am Baustein: OSCCFG-Fuse auf 20 MHz (PlatformIO-Board-Vorgabe).
