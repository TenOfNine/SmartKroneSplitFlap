# KRONE REW Fallblattanzeige — Ersatzsteuerung

Eigenbau-Steuerung für eine mechanische Fallblattanzeige der KRONE AG aus dem Jahr 1990. Die Original-Elektronik (Anzeigersteuerung und Palettensteuerungen PST mit HMCS44C) wird ersetzt. Mechanik und Anzeigenplatinen bleiben unverändert.

## Aufbau

```
ESP32 (Master, WLAN/REST/MQTT)
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

Konzeptphase abgeschlossen, Schaltplan als Netzliste vollständig, Layout und Firmware stehen aus. Drei Messungen an der Anzeigenplatine sind noch offen (O-2, O-5, O-6), siehe `docs/backlog.md`. Alles, was davon abhängt, ist als Parameter ausgeführt und blockiert die Fertigung nicht.

## Einstieg

```bash
bash tools/setup.sh          # Toolchain installieren, siehe docs/toolchain.md
cat docs/backlog.md          # Aufgabenreihenfolge
```

## Hinweis zu den Originalunterlagen

Die technische Dokumentation der KRONE AG ist urheberrechtlich geschützt und trägt einen ausdrücklichen Vervielfältigungsvorbehalt. Sie gehört nach `reference/` und ist über `.gitignore` von der Versionierung ausgenommen. In den Dokumenten dieses Repositories wird auf Zeichnungsnummern verwiesen, nicht aus den Unterlagen zitiert.

Alle eigenen Inhalte — Spezifikation, Schaltplan, Firmware, Messprotokolle, Fotos der eigenen Hardware — sind davon nicht betroffen.
