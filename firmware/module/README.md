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
pio run  -e attiny1616               # App @ 0x0000, ~5,6 KB (Grenze 8 KB) -- der Referenzweg
pio run  -e attiny1616 -t upload     # SerialUPDI, FTDI-Adapter mit 4,7 kΩ TX–RX
pio run  -e attiny1616_boot          # App @ 0x0C00, hinter dem Bootloader (experimentell)
python tools/build_module_firmware.py   # alle Images + signierte .mota -> prebuilt/
```

Voraussetzung am Baustein: OSCCFG-Fuse auf 20 MHz (PlatformIO-Board-Vorgabe).

**Browser-Werksflasher (experimentell).** <https://tenofnine.github.io/SmartKroneSplitFlap/>,
Tab *Daughter Card*: schreibt **Bootloader + Anwendung** (`firmware/bootloader` +
env `attiny1616_boot`) und setzt die Fuse `BOOTEND = 0x0C`. Ein Browser-Port von
SerialUPDI (`firmware/master/prebuilt/updi.js`, Web Serial API). Danach lässt sich
die Karte über den Bus aus der Master-Web-UI aktualisieren
([`docs/module-bootloader.md`](../../docs/module-bootloader.md)).
**Am Gerät noch nicht verifiziert**; `pio run -e attiny1616 -t upload` (ohne
Bootloader) bleibt der abgesicherte Weg. Verkabelung siehe Projekt-README.

`CMD_GET_VERSION` (0x54) und `CMD_ENTER_BOOTLOADER` (0x55) beantwortet die App
in beiden Ausprägungen; die eigentliche Bus-Programmierung (0x56–0x58) macht der
Bootloader. `lib/fwupdate` ist der host-getestete Seiten-Sammler.
