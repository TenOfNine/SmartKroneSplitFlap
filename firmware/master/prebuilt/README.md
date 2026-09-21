# Vorgebaute Master-Firmware (ESP32-C3 Super Mini)

Erzeugt von `tools/build_master_firmware.py` aus `firmware/master/` (zuletzt gebaut nahe Commit `d73b2dd`, 2026-09-21). Bei jeder Firmware-Aenderung neu ausfuehren.

| Datei | Zweck |
|---|---|
| `index.html` | Web-Flasher (ESP Web Tools). Wird per GitHub Actions als Page veroeffentlicht. |
| `krone-master-esp32c3.factory.bin` | Merged-Image fuer den **Erst-Flash ueber USB** (Offset 0x0) |
| `krone-master-esp32c3.kota` | **signierter** App-Container fuer das **OTA-Update aus der Web-UI** (Einstellungen > Firmware aktualisieren). Header mit SHA-256 + ECDSA-P-256-Signatur; das Modul lehnt fremde/manipulierte Dateien ab. Siehe `docs/firmware-signing.md`. |
| `manifest.json` | Manifest fuer [ESP Web Tools](https://esphome.github.io/esp-web-tools/) |

SHA-256 `factory.bin`: `79122552f11cb44ac35b5e9bd6aa6db95fae597eca6f784bc38fa91d9cb98dba`  
SHA-256 `kota`: `f4eff42c17305ab20851319b55e827489cee8580a0da9724f6160d6f849b84be`

## Erst-Flash (USB)

- **Browser:** <https://tenofnine.github.io/SmartKroneSplitFlap/> (laedt immer diesen Verzeichnisstand). Chrome/Edge Desktop.
- Alternativ [esptool-js](https://espressif.github.io/esptool-js/) — `factory.bin` an Offset `0x0`. Der C3 Super Mini geht ueber die USB-C-Buchse selbsttaetig in den Download-Modus (kein BOOT-Taster).
- Kommandozeile:

  ```
  esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 krone-master-esp32c3.factory.bin
  ```

## Spaetere Updates (OTA)

*Einstellungen > Firmware aktualisieren* -> `krone-master-esp32c3.kota` hochladen (nicht die `.factory.bin`). Das Modul prueft Signatur und Pruefsumme, schreibt in die zweite App-Partition und startet neu; fremde oder beschaedigte Dateien werden abgelehnt, die laufende Firmware bleibt aktiv. Kein Toolchain, jeder Browser. In den *Schnittstellen* abschaltbar; ein Netzwerk-OTA (ArduinoOTA) gibt es bewusst nicht.

Nach dem Boot: Access-Point `krone_anzeige` fuer die WLAN-Einrichtung, serielle Konsole auf USB-C (115200 Bd). Status-LED (GPIO6): schnelles Blinken = kein WLAN.

> `index.html` ist handgepflegt und wird von diesem Skript **nicht** ueberschrieben.
