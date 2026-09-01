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

### Verifikation 28.08.2026 (Backlog T2, abgeschlossen)

- `bash tools/setup.sh` auf Ubuntu 24.04 durchgelaufen: **KiCad 9.0.9** (PPA `9.0.9~ubuntu24.04.1`), `kicad-sch-api` 0.5.6, `kicad-skip` 0.2.5, PlatformIO 6.1.19.
- Der Roundtrip-Test ist **erfolgreich**: `kicad-sch-api` erzeugt natives KiCad-9-Format (`(version 20250114)`, `(generator "eeschema")`, `generator_version "9.0"`), `kicad-cli sch erc` und `sch export pdf` (jeweils über `xvfb-run -a`) lesen die Datei fehlerfrei. **Backend für T4 ist damit `kicad-sch-api`**, nicht der Rückfall `kicad-skip`.
- Korrektur an `setup.sh`: Die Roundtrip-Probe rief `sch.components.add_component()` / `add_symbol()` auf. Die Methode heißt in `kicad-sch-api` 0.5.x `sch.components.add(lib_id, …)`. Ohne die Korrektur fiel das Skript fälschlich auf `kicad-skip` zurück und brach mit Exit 1 ab.
- `kicad-cli`-Unterbefehle mit Qt-Anteil (`sch erc`, `sch export *`, `pcb export *`) brauchen ein Display-Target: `xvfb-run -a kicad-cli …` oder `export QT_QPA_PLATFORM=offscreen`. Eine interaktive Desktop-Session ist nicht nötig.

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

### Bibliothekstabellen headless

`kicad-cli sch erc` und `sch export netlist` loesen Symbole ueber die globale
`sym-lib-table` (`~/.config/kicad/9.0/sym-lib-table`) auf. Diese Datei legt sonst
nur die GUI beim ersten Start an. Fehlt sie, meldet ERC fuer jedes Bauteil
`configuration does not include the symbol library '<lib>'` und findet die Pins
nicht. `tools/setup.sh` kopiert daher `sym-lib-table` und `fp-lib-table` aus
`/usr/share/kicad/template/` und traegt die `KICAD9_*_DIR`-Pfade in
`kicad_common.json` ein. Die Projekt-Bibliothek `krone` liegt zusaetzlich in
`hardware/daughtercard/sym-lib-table` (projektlokal, wird von KiCad relativ zur
`.kicad_sch` gefunden).

## 3. Kommandos

```bash
# Schaltplanprüfung und Export  (erc/export brauchen ein Display -> xvfb-run)
xvfb-run -a kicad-cli sch erc --format json -o erc.json hardware/daughtercard/daughtercard.kicad_sch
xvfb-run -a kicad-cli sch export netlist --format kicadsexpr -o daughtercard.net <sch>
xvfb-run -a kicad-cli sch export pdf -o docs/daughtercard.pdf <sch>

# Fertigungsdaten
kicad-cli pcb export gerbers -o gerber/ <pcb>

# Fertigungspaket (Gerber + BOM + CPL) -> hardware/daughtercard/manufacturing/
/usr/bin/python3 tools/gen_manufacturing.py            # committetes Deliverable
/usr/bin/python3 tools/gen_daughtercard_pcb.py --jlc   # nur BOM/CPL nach jlc/ (Wegwerf, .gitignore)

# Zentralsteuerung (Master, ESP32-C3 Super Mini) -- selber Ablauf wie Daughter Card
python tools/build_krone_master_symbols.py             # Projektbibliothek krone_master.kicad_sym
python tools/gen_master_sch.py --erc --pdf --png       # Schaltplan, ERC 0/0
/usr/bin/python3 tools/gen_master_pcb.py --png --drc   # Vorplatzierung (verweigert Neuaufbau bei vorhandener Verdrahtung; --force)
/usr/bin/python3 tools/route_master.py                 # FreeRouting + Flaechen + Silk-Marks, DRC 0/0
/usr/bin/python3 tools/gen_master_manufacturing.py     # Fertigungspaket -> hardware/master/manufacturing/

# Firmware
pio run  -e attiny1616 -d firmware/module   # ATtiny1616 kompilieren
pio run  -e esp32c3    -d firmware/master   # ESP32-C3 Super Mini kompilieren
pio test -e native     -d firmware/module   # Protokolltests auf dem Host
python tools/build_master_firmware.py       # + Merged-.bin -> firmware/master/prebuilt/ (Webflasher)
```

## 4. PlatformIO-Ziele

| Ziel | Plattform | Board | Framework | Upload |
|---|---|---|---|---|
| Modul | `atmelmegaavr` | `ATtiny1616` | keins (bare metal, avr-libc) | `serialupdi` |
| Master | `espressif32` | `esp32-c3-devkitm-1` | arduino | `esptool` (USB-C), später OTA |
| Tests | `native` | — | unity | — |

`pio test -e native` läuft in `firmware/module/` (6 Suiten) und `firmware/master/`
(6 Suiten) getrennt.

> In T7 fiel die Wahl auf **bare metal** statt megaTinyCore: knapper Flash-Bedarf
> (rund 5,3 KB gegen 8 KB Grenze), volle Kontrolle über USART0-RS485, TCB0 und
> Watchdog, und der ganze Firmware-Code bleibt reines C wie `lib/protocol` und
> `lib/enumeration`. Die Plattformpakete (`toolchain-atmelavr`, Device-Header)
> kommen weiterhin über `atmelmegaavr`.

> In T8 wurden gegenüber Spezifikation 7.2 dependency-arme Bausteine gewählt:
> der **eingebaute `WebServer`** statt `ESPAsyncWebServer` (kein `AsyncTCP`,
> läuft mit arduino-esp32 3.x ohne Patches) und **`Preferences`** (NVS) statt
> `LittleFS` für die Konfiguration. `WiFiManager` (Captive Portal), `PubSubClient`
> (MQTT) und `ArduinoJson` bleiben.
> `lib_extra_dirs = ../module/lib` teilt `lib/protocol` mit der Modul-Firmware.

> In T11/T12 auf den **ESP32-C3 Super Mini** (`board = esp32-c3-devkitm-1`)
> portiert (`hardware/master`). Der C3 hat nur UART0 (USB-Konsole) und UART1 →
> RS-485 auf **UART1** im Hardware-Halbduplexmodus (`UART_MODE_RS485_HALF_DUPLEX`).
> Pin-/UART-Belegung kommt aus `platformio.ini` (`build_flags`, Vorgaben =
> `src/main.cpp`): RX GPIO4, TX GPIO3, DE GPIO10, CHAIN GPIO5 (über den
> nicht invertierenden Pegelwandler 74LVC1G17), Status-LED GPIO6.
> `-DARDUINO_USB_CDC_ON_BOOT=1` legt `Serial` auf die USB-C-Buchse.
> `pio run -e esp32c3`: ~936 KB Flash.

Für SerialUPDI genügt ein FTDI-USB-Seriell-Adapter mit einem 4,7-kΩ-Widerstand zwischen TX und RX. Der Widerstand sitzt im Adapter, nicht auf der Daughter Card.

## 5. CI

`.github/workflows/ci.yml` läuft bei jedem Push auf `main` und bei Pull Requests:

| Job | Prüfung |
|---|---|
| `host-tests` | `pio test -e native` in `firmware/module` (62) und `firmware/master` (40), `python tools/test_busctl.py` (13) |
| `firmware` | `pio run -e attiny1616` + `tools/check_flash.py … 8192`, `pio run -e esp32c3` |
| `hardware` | KiCad 9, `build_krone_symbols.py --check` + `build_krone_master_symbols.py --check` (informativ), `gen_daughtercard_sch.py --check-only` + `gen_master_sch.py --check-only`, `kicad-cli sch erc` für beide Schaltpläne (0 Fehler / 0 Warnungen) |
| `release` | nur bei Tag `v*`: Schaltplan-PDF, Gerber der Daughter Card |

`~/.platformio` wird zwischen Läufen gecacht. Der `hardware`-Job installiert KiCad
aus dem PPA (`--no-install-recommends`, ohne 3D-Modelle).

## 6. Autorouting (FreeRouting)

```bash
bash tools/setup_freerouting.sh          # laedt FreeRouting 2.3.0 + JRE 25 nach tools/vendor/ (gitignored)
/usr/bin/python3 tools/route_daughtercard.py            # DSN -> FreeRouting -> SES -> .kicad_pcb
/usr/bin/python3 tools/route_daughtercard.py --dry-run  # nur .dsn/.ses erzeugen, kein Import
```

`route_daughtercard.py` macht in einem Durchlauf:

1. Netzklassen aus `gen_daughtercard_pcb.py` in die `.kicad_pro` schreiben.
2. Specctra-`.dsn` über `pcbnew` exportieren, FreeRouting 2.3.0 headless laufen
   lassen (braucht **Java 25** — das Setup-Skript holt eine Temurin-JRE), `.ses`
   zurueck importieren.
3. `tools/finish_routes.py` — die wenigen Verbindungen, die FreeRouting offen
   laesst (meist 0–2, aus `kicad-cli pcb drc --format json`), mit einem
   Rastersuch-Router (A*, 0,25 mm, 2 Lagen) schliessen.
4. FreeRoutings GND-Bahnen verwerfen, Masseflaechen F.Cu + B.Cu anlegen,
   Stitching-Raster (5 mm) + je ein Via an jedem GND-Pad, fuellen,
   freistehende Vias entfernen.
5. Netzklassen erneut schreiben (SaveBoard setzt sie zurueck), DRC.

Danach `tools/add_silk_marks.py`: Maker-Kennzeichnung (GitHub-Marke +
Repo-Owner) auf die Rueckseiten-Silkscreen, Lagenaufbau auf schwarze Maske /
weissen Druck. Mit `--board <pfad>` auch fuer den Master; `tools/route_master.py`
ruft es selbst am Ende auf (Silk-Texte + Stackup ueberleben den SES-Import nicht).

Der Router-Lauf gehoert an den Schluss, wenn die Bauteilpositionen feststehen.

**Master:** `tools/route_master.py` macht denselben Ablauf fuer
`hardware/master/master.kicad_pcb` (Netzklassen aus `gen_master_pcb.py`: alle
Signale 0,5 mm, Versorgung 0,8 mm, kein AC) und erzeugt am Ende `docs/pcb-master.png`.
`gen_master_pcb.py --png` wuerde die Platine neu aufbauen und die Verdrahtung
verwerfen -- es verweigert das, solange Bahnen vorhanden sind (`--force` erzwingt).

**KiCad-GUI-Plugin** (fuer die eigene Maschine): Plugin and Content Manager
(Strg+M) → „Freerouting" → Installieren; ebenfalls Java 25 noetig.

## 7. Was die Werkzeuge nicht leisten

ERC prüft, ob eine Schaltplandatei gültig ist, nicht ob sie richtig ist. Trägt das verwendete Symbol eine falsche Pinnummerierung, läuft ERC fehlerfrei durch und die Platine ist trotzdem unbrauchbar. Die Gegenprüfung der Symbolpins gegen das Datenblatt bleibt ein manueller Schritt, siehe T3 im Backlog.
