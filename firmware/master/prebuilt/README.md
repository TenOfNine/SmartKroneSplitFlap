# Vorgebaute Master-Firmware (ESP32-C3 Super Mini)

Erzeugt von `tools/build_master_firmware.py` aus `firmware/master/` (zuletzt gebaut nahe Commit `e2fccd8`, 2026-09-01). Bei jeder Firmware-Aenderung neu ausfuehren.

| Datei | Zweck |
|---|---|
| `index.html` | Web-Flasher (ESP Web Tools). Wird per GitHub Actions als Page veroeffentlicht. |
| `krone-master-esp32c3.factory.bin` | Merged-Image fuer den **Erst-Flash ueber USB** (Offset 0x0) |
| `krone-master-esp32c3.ota.bin` | App-Image fuer das **OTA-Update aus der Web-UI** (Einstellungen > System > Firmware aktualisieren) |
| `manifest.json` | Manifest fuer [ESP Web Tools](https://esphome.github.io/esp-web-tools/) |

SHA-256 `factory.bin`: `95e99d5387ba1fa2f81f7a04fdaa8fc9366774845f048c47f6882dd92d9a53ad`  
SHA-256 `ota.bin`: `d4ed62b3a7eda63078283fb84a940413b6cad32fbf832294cbbdf2784ad3c467`

## Erst-Flash (USB)

- **Browser:** <https://tenofnine.github.io/SmartKroneSplitFlap/> (laedt immer diesen Verzeichnisstand). Chrome/Edge Desktop.
- Alternativ [esptool-js](https://espressif.github.io/esptool-js/) — `factory.bin` an Offset `0x0`. Der C3 Super Mini geht ueber die USB-C-Buchse selbsttaetig in den Download-Modus (kein BOOT-Taster).
- Kommandozeile:

  ```
  esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 krone-master-esp32c3.factory.bin
  ```

## Spaetere Updates (OTA)

*Einstellungen > System > Firmware aktualisieren* -> `krone-master-esp32c3.ota.bin` hochladen (nicht die `.factory.bin`). Kein Toolchain, jeder Browser. Bei Fehler bleibt die alte Firmware aktiv, die Einstellungen (NVS) bleiben erhalten. In den *Schnittstellen* abschaltbar; ein Netzwerk-OTA (ArduinoOTA) gibt es bewusst nicht.

Nach dem Boot: Access-Point `krone_anzeige` fuer die WLAN-Einrichtung, serielle Konsole auf USB-C (115200 Bd). Status-LED (GPIO6): schnelles Blinken = kein WLAN.

> `index.html` ist handgepflegt und wird von diesem Skript **nicht** ueberschrieben.
