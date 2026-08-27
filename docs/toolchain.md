# Toolchain

Zielumgebung: Linux-VM, Debian oder Ubuntu.

## 1. Vorbemerkung zur KiCad-Automatisierung

Die offizielle IPC-API von KiCad hilft für dieses Projekt nicht. Sie ist in KiCad 9 und 10 nur im PCB-Editor implementiert, kommuniziert ausschließlich mit einer laufenden GUI-Instanz, und Headless-Betrieb über `kicad-cli` kam erst mit KiCad 11. Der Schaltplan-Editor ist dort noch gar nicht abgedeckt. Eine eigenständige Bibliothek zum Bearbeiten von KiCad-Dateien außerhalb des Client-Server-Modells ist seitens des Projekts nicht geplant.

Der gangbare Weg läuft daran vorbei:

- **`kicad-sch-api`** schreibt `.kicad_sch`-Dateien direkt, ohne laufendes KiCad, formatgetreu zur nativen Ausgabe. Die Formattreue ist wichtig, sonst erzeugt das erste Öffnen in der GUI einen großflächigen Diff und die Versionierung wird wertlos.
- **`kicad-cli`** läuft headless und liefert ERC, Netzlisten-Export und PDF-Ausgabe.

Damit ist die Schleife aus Generieren, Prüfen und Korrigieren geschlossen.

> ⚠️ Die Dokumentation von `kicad-sch-api` nennt KiCad 7 und 8. Ob KiCad 9 unterstützt wird, ist vor der Nutzung zu verifizieren. Rückfallebene ist `kicad-skip`, das die S-Expressions direkt manipuliert, oder eine parallele KiCad-8-Installation.

### Recherchestand 27.08.2026 (Backlog T2)

- `kicad-sch-api`, aktuelle Version **0.5.6** (PyPI, 19.11.2025, Python ≥ 3.10). README und Doku nennen weiterhin ausdrücklich nur „KiCAD 7/8"; eine KiCad-9-Zusage gibt es nicht. Das Format der `.kicad_sch` ist zwischen den Versionen weitgehend abwärtskompatibel, ein sauberer Lauf mit KiCad 9 ist damit wahrscheinlich, aber nicht zugesichert.
- Belastbar ist nur ein echter Schreib-/Lesetest. `tools/setup.sh` führt ihn am Ende automatisch aus: Es erzeugt mit `kicad-sch-api` eine minimale Schaltung und lässt `kicad-cli sch erc` (KiCad 9) sie einlesen. Meldet KiCad ein Formatproblem, setzt das Skript das Schaltplan-Backend auf `kicad-skip` und weist darauf hin.
- `kicad-skip` (psychogenic/kicad-skip) ist auf „KiCAD 7+" ausgelegt und wird von `setup.sh` immer mitinstalliert, damit der Umstieg keinen zweiten Lauf braucht.

## 2. Installation

```bash
# KiCad 9 (Ubuntu/Debian)
sudo add-apt-repository ppa:kicad/kicad-9.0-releases
sudo apt update && sudo apt install --install-recommends kicad
kicad-cli version                 # muss eine Version ausgeben

# Falls Flatpak statt PPA verwendet wird:
#   flatpak run --command=kicad-cli org.kicad.KiCad version

# Einzelne kicad-cli-Unterbefehle benötigen ein Display
sudo apt install xvfb             # Aufruf dann: xvfb-run -a kicad-cli …

# Python-Umgebung
python3 -m venv .venv && source .venv/bin/activate
pip install kicad-sch-api         # Alternative: kicad-skip
pip install platformio
```

## 3. Kommandos

```bash
# Schaltplanprüfung und Export
kicad-cli sch erc --format json -o erc.json hardware/daughtercard/daughtercard.kicad_sch
kicad-cli sch export netlist --format kicadsexpr -o daughtercard.net <sch>
kicad-cli sch export pdf -o docs/daughtercard.pdf <sch>

# Fertigungsdaten
kicad-cli pcb export gerbers -o gerber/ <pcb>

# Firmware
pio run  -d firmware/module            # ATtiny1616 kompilieren
pio run  -d firmware/master            # ESP32 kompilieren
pio test -e native -d firmware/module  # Protokolltests auf dem Host
```

## 4. PlatformIO-Ziele

| Ziel | Plattform | Board | Framework | Upload |
|---|---|---|---|---|
| Modul | `atmelmegaavr` | `ATtiny1616` | arduino (megaTinyCore) | `serialupdi` |
| Master | `espressif32` | `esp32dev` | arduino | `esptool`, später OTA |
| Tests | `native` | — | — | — |

Für SerialUPDI genügt ein FTDI-USB-Seriell-Adapter mit einem 4,7-kΩ-Widerstand zwischen TX und RX. Der Widerstand sitzt im Adapter, nicht auf der Daughter Card.

## 5. Was die Werkzeuge nicht leisten

ERC prüft, ob eine Schaltplandatei gültig ist, nicht ob sie richtig ist. Trägt das verwendete Symbol eine falsche Pinnummerierung, läuft ERC fehlerfrei durch und die Platine ist trotzdem unbrauchbar. Die Gegenprüfung der Symbolpins gegen das Datenblatt bleibt ein manueller Schritt, siehe T3 im Backlog.
