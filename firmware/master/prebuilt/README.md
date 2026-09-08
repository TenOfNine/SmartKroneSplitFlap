# Vorgebaute Master-Firmware (ESP32-C3 Super Mini)

Erzeugt von `tools/build_master_firmware.py` aus `firmware/master/` (zuletzt gebaut nahe Commit `4607213`, 2026-09-08). Bei jeder Firmware-Aenderung neu ausfuehren.

| Datei | Zweck |
|---|---|
| `index.html` | Web-Flasher (ESP Web Tools). Wird per GitHub Actions als Page veroeffentlicht. |
| `krone-master-esp32c3.factory.bin` | Merged-Image fuer den **Erst-Flash ueber USB** (Offset 0x0) |
| `krone-master-esp32c3.ota.bin` | App-Image fuer das **OTA-Update aus der Web-UI** (Einstellungen > System > Firmware aktualisieren) |
| `manifest.json` | Manifest fuer [ESP Web Tools](https://esphome.github.io/esp-web-tools/) |

SHA-256 `factory.bin`: `58df1fc513181dbbd39420bb0abe79039046546cd5cc730ae36c102478460f88`  
SHA-256 `ota.bin`: `d4b6e2c347ffac3c19a68e5bcd33b82daad82542897b276609f4c72f283877bc`

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
