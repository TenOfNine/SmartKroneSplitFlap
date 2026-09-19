# Vorgebaute Modul-Firmware (ATtiny1616)

Erzeugt von `tools/build_module_firmware.py` (zuletzt nahe Commit `892c67f`, 2026-09-19). Bei jeder Firmware-Aenderung neu ausfuehren.

| Datei | Zweck |
|---|---|
| `krone-daughtercard-attiny1616.hex` | App @ 0x0000, **ohne** Bootloader. `pio run -e attiny1616 -t upload` bzw. `pymcuprog`. Der abgesicherte Weg. |
| `krone-daughtercard-bootloader.hex` | residenter Bootloader @ 0x0000 (Werksflash). |
| `krone-daughtercard-attiny1616-boot.hex` | App @ 0x0C00, laeuft hinter dem Bootloader (Werksflash). |
| `krone-daughtercard-attiny1616.mota` | signierter Container der `-boot`-App fuer die Firmware-Verteilung ueber den Bus (der Master bettet sie ein). |

SHA-256 (`krone-daughtercard-attiny1616.hex`): `514b55bb8b7a71098fb71e7cc3c65a7514c0430bd7b4e28ea5a3ade1fd50d12e`

## Flashen

- **Toolchain (sicher):** `pio run -e attiny1616 -t upload -d firmware/module`.
- **Browser-Werksflash (experimentell):** <https://tenofnine.github.io/SmartKroneSplitFlap/>, Tab *Daughter Card*. Schreibt Bootloader + `-boot`-App und setzt die Fuse `BOOTEND = 0x0C`. USB-Seriell-Adapter (5 V) mit 4,7-kOhm-Bruecke TX--RX an J6 (TX/RX -> Pin 2 UPDI, GND -> Pin 1, +5 V -> Pin 3).
- **Ueber den Bus:** ist ein Bootloader geflasht, aktualisiert der Master die Karten aus seiner Web-UI (*Einstellungen > Modul-Firmware*). Siehe `docs/module-bootloader.md`.

Der Flasher prueft die Geraete-ID (ATtiny1616 = `1E 94 21`). `krone-daughtercard-attiny1616.mota` vorhanden.
