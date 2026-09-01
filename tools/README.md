# tools

Python-Skripte laufen in der venv (`source .venv/bin/activate`) — **außer** die
mit `pcbnew`, die brauchen das System-Python (`/usr/bin/python3`).

## Einrichtung

| Werkzeug | Zweck |
|---|---|
| `setup.sh` | Toolchain der Entwicklungs-VM (KiCad 9, venv, PlatformIO, Bibliothekstabellen). `--kicad-config` nur die KiCad-Config. Siehe `docs/toolchain.md`. |
| `setup_freerouting.sh` | FreeRouting 2.3.0 + Temurin-JRE 25 nach `tools/vendor/` laden (gitignored). |

## Daughter Card (KiCad)

| Werkzeug | Zweck |
|---|---|
| `build_krone_symbols.py` | Symbolbibliothek `hardware/daughtercard/symbols/krone.kicad_sym`. `--check` für die CI. |
| `gen_daughtercard_sch.py` | Schaltplan + PDF + PNG + PCB-Netzliste aus `docs/schaltplan-daughtercard.md` Kap. 6. `--check-only`, `--erc`, `--pdf`, `--png`, `--netlist`. |
| `gen_daughtercard_pcb.py` | Erstplatzierung der `.kicad_pcb` (**System-Python**, `pcbnew`). `--png`, `--drc`, `--jlc`. Verweigert Neuaufbau nach dem Routing nicht — dann UUID-Remapping statt Neu-Erzeugen. |
| `route_daughtercard.py` | FreeRouting + `finish_routes.py` + Masseflächen (**System-Python**). `--dry-run`, `--no-zones`. |
| `finish_routes.py` | A*-Rastersuch-Router für die 1–2 Verbindungen, die FreeRouting offen lässt. Von `route_*` importiert, braucht `python3-numpy`. |
| `add_silk_marks.py` | Maker-Kennzeichnung + schwarz/weiß-Lagenaufbau (**System-Python**). `--board <pfad>` auch für den Master. |
| `gen_manufacturing.py` | Fertigungspaket → `hardware/daughtercard/manufacturing/` (committet). `--no-gerber`. |

## Master / Zentralsteuerung (KiCad)

| Werkzeug | Zweck |
|---|---|
| `build_krone_master_symbols.py` | `hardware/master/symbols/krone_master.kicad_sym` (inkl. handgebautem `ESP32-C3-SuperMini`-Symbol). `--check` für die CI. |
| `gen_master_sch.py` | Schaltplan aus `docs/schaltplan-master.md` Kap. 6. Flags wie beim Daughter-Card-Pendant. |
| `gen_master_pcb.py` | Erstplatzierung `hardware/master/master.kicad_pcb` (**System-Python**). `--png`, `--drc`, `--jlc`, `--render` (3D), `--force`. |
| `route_master.py` | wie `route_daughtercard.py` für die Master-Platine, ruft `add_silk_marks.py` selbst auf. |
| `gen_master_manufacturing.py` | Fertigungspaket → `hardware/master/manufacturing/`. |

## Firmware

| Werkzeug | Zweck |
|---|---|
| `check_flash.py` | Flash-Verbrauch einer ELF gegen eine Obergrenze prüfen (CI, ATtiny < 8 KB). |
| `build_master_firmware.py` | `pio run -e esp32c3` + `esptool merge_bin` → `firmware/master/prebuilt/` (Merged-`.bin` + Manifest für den Web-Flasher). |

## Bus

| Werkzeug | Zweck |
|---|---|
| `busctl.py` | Kommandozeilenwerkzeug für den KRONE-REW-Bus (T9). |
| `test_busctl.py` | stdlib-`unittest` für `busctl.py` (13 Tests). |

## busctl.py

Für die stufenweise Inbetriebnahme nach `docs/spezifikation.md` Kapitel 10.

```bash
source ../.venv/bin/activate

# gegen echte Hardware (USB-RS485-Adapter):
python busctl.py --port /dev/ttyUSB0 enum
python busctl.py --port /dev/ttyUSB0 status 3
python busctl.py --port /dev/ttyUSB0 show 13 3 40 1 1     # SET_ALL + GO
python busctl.py --port /dev/ttyUSB0 sniff 10             # Rohrahmen mitschneiden

# ohne Hardware:
python busctl.py selftest              # Rahmen-/CRC-Logik
python busctl.py --sim 3 enum          # 3 simulierte Module
python busctl.py --sim 3 -v status 2   # -v zeigt die Rohrahmen
python test_busctl.py                  # 13 Tests
```

Unterkommandos: `enum`, `status`, `uid`, `ping`, `set`, `show`, `home`, `stop`,
`config`, `sniff`, `selftest`.

Die Rahmen-/CRC-Logik ist ein eigenständiger Python-Nachbau von
`firmware/module/lib/protocol`. `selftest` prüft sie gegen dieselben Goldwerte
wie die C-Tests (CRC von `"123456789"` = `0x4B37`, Rahmenkopf `AA 55 05 01 03 28`).
