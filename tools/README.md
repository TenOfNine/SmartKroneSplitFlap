# tools

| Werkzeug | Zweck |
|---|---|
| `setup.sh` | Toolchain der Entwicklungs-VM einrichten (KiCad 9, Python-venv, PlatformIO). Siehe `docs/toolchain.md`. |
| `build_krone_symbols.py` | Projekt-Symbolbibliothek `hardware/daughtercard/symbols/krone.kicad_sym` erzeugen. `--check` prüft für die CI, ob sie aktuell ist. |
| `gen_daughtercard_sch.py` | Schaltplan, PDF, PNG-Vorschau und PCB-Netzliste aus der Netzliste (`docs/schaltplan-daughtercard.md` Kap. 6) erzeugen. `--check-only`, `--erc`, `--pdf`, `--png`, `--netlist`. |
| `busctl.py` | Kommandozeilenwerkzeug für den KRONE-REW-Bus (T9). |
| `test_busctl.py` | stdlib-`unittest` für `busctl.py`. |

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
