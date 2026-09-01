# KRONE REW Fallblattanzeige — Ersatzsteuerung

![CI](https://github.com/TenOfNine/SmartKroneSplitFlap/actions/workflows/ci.yml/badge.svg)

Eigenbau-Steuerung für eine mechanische Fallblattanzeige der KRONE AG aus dem Jahr 1990. Die Original-Elektronik (Anzeigersteuerung und Palettensteuerungen PST mit HMCS44C) wird ersetzt. Mechanik und Anzeigenplatinen bleiben unverändert.

## Aufbau

```
ESP32-C3 (Master, WLAN/REST/MQTT)
  └─ RS-485 half duplex + CHAIN
       ├─ Daughter Card 1 (ATtiny1616) ── Anzeigenmodul 1
       ├─ Daughter Card 2 ────────────── Anzeigenmodul 2
       └─ …                                        bis 10
```

Jedes Anzeigenmodul erhält eine eigene kleine Steuerplatine, die die Hall-Impulse zählt, den Triac ansteuert und die aktuelle Blattposition kennt. Die Adressvergabe erfolgt automatisch entlang einer CHAIN-Leitung, sodass die Adresse der physischen Position entspricht und ein Kartentausch keine Einstellung erfordert.

## Eckdaten

| Größe | Wert |
|---|---|
| Anzeigenmodule | 10, Palettenmodulreihe A, Modulgröße 1 |
| Blätter je Modul | 40 |
| Zeichensatz | Leerbild, 0–9, A–Z, `-`, `.` |
| Zeit je Blatt | 60 ms, volle Umdrehung 2,40 s |
| Motor | Berger RSM 42/12, 42 V~, 250 U/min |
| Triac | Teccor L201E3, sensitives Gate |
| Modul-CPU | ATtiny1616 |
| Bus | RS-485, 115200 Bd, TP8485E |
| Master | ESP32 |

## Verzeichnisse

| Pfad | Inhalt |
|---|---|
| `docs/` | Spezifikation, Schaltplan, Backlog, Toolchain, Messprotokolle |
| `hardware/daughtercard/` | KiCad-Projekt der Modulsteuerung |
| `hardware/master/` | KiCad-Projekt der Zentralsteuerung |
| `firmware/module/` | ATtiny1616, PlatformIO |
| `firmware/master/` | ESP32, PlatformIO |
| `tools/` | Setup-Skript, Schaltplangenerator, Bus-Testwerkzeug |
| `reference/` | **Nicht versioniert.** Ablage für die Original-Herstellerunterlagen. |

## Stand

| Bereich | Stand |
|---|---|
| Spezifikation, Schaltplan (Netzliste) | vollständig, `docs/` |
| Schaltplan `.kicad_sch` + Footprints + PCB-Netzliste | generiert, ERC 0/0 (Daughter Card + Master) |
| PCB-Layout `.kicad_pcb` | geroutet, DRC 0 Fehler; Daughter Card bei JLCPCB bestellt, Master als Planungsstand |
| Modul-Firmware (ATtiny1616) | fertig, `firmware/module/` |
| Master-Firmware (ESP32-C3 Super Mini) | fertig, `firmware/master/` — Web-Flasher siehe unten |
| Bus-Werkzeug | `tools/busctl.py` |
| CI | `.github/workflows/ci.yml` |
| Offene Messungen O-2, O-5, O-6 | parametrisiert, blockieren die Fertigung nicht |

Der Projektstand für den Einstieg in eine neue Arbeitssitzung steht in
`docs/projektstand.md`.

## Einstieg

```bash
bash tools/setup.sh                              # Toolchain, siehe docs/toolchain.md
source .venv/bin/activate

pio test -e native -d firmware/module            # 62 Tests
pio test -e native -d firmware/master            # 34 Tests
python tools/test_busctl.py                      # 13 Tests
python tools/gen_daughtercard_sch.py --erc --pdf --png
```

## Firmware flashen

**Master (ESP32-C3 Super Mini).** Am einfachsten über den Browser-Flasher:
**<https://tenofnine.github.io/SmartKroneSplitFlap/>**

Nötig: Chrome oder Edge auf dem Desktop, ein USB-C-Kabel **mit Datenadern** an
die Buchse des Moduls. „Modul verbinden & flashen" drücken — der C3 geht selbst
in den Download-Modus, kein BOOT-Taster. Nach dem Flashen öffnet die Karte den
Access-Point `krone_anzeige` für die WLAN-Zugangsdaten. Das fertige Image liegt
committet unter `firmware/master/prebuilt/`; neu bauen mit
`python tools/build_master_firmware.py` oder direkt
`pio run -e esp32c3 -t upload -d firmware/master`.

**Daughter Card (ATtiny1616).** Über die UPDI-Stiftleiste J6. Nötig: ein
USB-Seriell-Adapter (FTDI o. ä.) mit einem 4,7-kΩ-Widerstand zwischen dessen TX
und RX; TX/RX gemeinsam an J6 Pin 2 (UPDI), GND an Pin 1, +5 V an Pin 3. Dann
`pio run -e attiny1616 -t upload -d firmware/module` (Upload-Protokoll
`serialupdi`; Adapter-Port ggf. per `--upload-port`).

## Hinweis zu den Originalunterlagen

Die technische Dokumentation der KRONE AG ist urheberrechtlich geschützt und trägt einen ausdrücklichen Vervielfältigungsvorbehalt. Sie gehört nach `reference/` und ist über `.gitignore` von der Versionierung ausgenommen. In den Dokumenten dieses Repositories wird auf Zeichnungsnummern verwiesen, nicht aus den Unterlagen zitiert.

Alle eigenen Inhalte — Spezifikation, Schaltplan, Firmware, Messprotokolle, Fotos der eigenen Hardware — sind davon nicht betroffen.
