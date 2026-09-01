# Vorgebaute Master-Firmware (ESP32-C3 Super Mini)

Erzeugt von `tools/build_master_firmware.py` aus `firmware/master/` (zuletzt gebaut nahe Commit `5212ed5`, 2026-09-01). Bei jeder Firmware-Aenderung neu ausfuehren.

| Datei | Zweck |
|---|---|
| `index.html` | Web-Flasher (ESP Web Tools). Wird per GitHub Actions als Page veroeffentlicht. |
| `krone-master-esp32c3.factory.bin` | Merged-Image fuer den **Erst-Flash ueber USB** (Offset 0x0) |
| `krone-master-esp32c3.ota.bin` | App-Image fuer das **OTA-Update aus der Web-UI** (Einstellungen > System > Firmware aktualisieren) |
| `manifest.json` | Manifest fuer [ESP Web Tools](https://esphome.github.io/esp-web-tools/) |

SHA-256 `factory.bin`: `c06790a48d4768c25023d50611ce35e39c5088b7234a62da139b3d2146e1fdda`  
SHA-256 `ota.bin`: `b3bbf3b76e3b0bf4b4f567984e99951fc3e0bbb8c901a2f7620861894b5d2765`

## Erst-Flash (USB)

- **Browser:** <https://tenofnine.github.io/SmartKroneSplitFlap/> (laedt immer diesen Verzeichnisstand). Chrome/Edge Desktop.
- Alternativ [esptool-js](https://espressif.github.io/esptool-js/) — `factory.bin` an Offset `0x0`. Der C3 Super Mini geht ueber die USB-C-Buchse selbsttaetig in den Download-Modus (kein BOOT-Taster).
- Kommandozeile:

  ```
  esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 krone-master-esp32c3.factory.bin
  ```

## Spaetere Updates (OTA)

- **Web-UI:** *Einstellungen > System > Firmware aktualisieren* -> `krone-master-esp32c3.ota.bin` hochladen. Kein Toolchain, jeder Browser. Bei Fehler bleibt die alte Firmware aktiv. Einstellungen (NVS) bleiben erhalten.
- **PlatformIO:** `pio run -e esp32c3 -t upload -d firmware/master` mit `--upload-port krone_anzeige.local` (ArduinoOTA, im Netz aktiv).

Nach dem Boot: Access-Point `krone_anzeige` fuer die WLAN-Einrichtung, serielle Konsole auf USB-C (115200 Bd). Status-LED (GPIO6): schnelles Blinken = kein WLAN.

> `index.html` ist handgepflegt und wird von diesem Skript **nicht** ueberschrieben.
