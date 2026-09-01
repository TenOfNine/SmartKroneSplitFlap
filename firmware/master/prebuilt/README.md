# Vorgebaute Master-Firmware (ESP32-C3 Super Mini)

Erzeugt von `tools/build_master_firmware.py` aus `firmware/master/` (zuletzt gebaut nahe Commit `3f03c57`, 2026-09-01). Bei jeder Firmware-Aenderung neu ausfuehren.

| Datei | Zweck |
|---|---|
| `krone-master-esp32c3.factory.bin` | Merged-Image, im Webflasher an **Offset 0x0** flashen (mit „Erase before flash") |
| `manifest.json` | Manifest fuer [ESP Web Tools](https://esphome.github.io/esp-web-tools/) |

SHA-256 (`factory.bin`): `9da87cef96d82a68ecdde6198b4db9f4f0549719fe2005ac0c4ea811d335a818`

## Flashen

- Webflasher: [esptool-js](https://espressif.github.io/esptool-js/) — Datei an Offset `0x0`, „Erase" aktivieren. Der C3 Super Mini geht ueber die USB-C-Buchse selbsttaetig in den Download-Modus (kein BOOT-Taster).
- Kommandozeile:

  ```
  esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 krone-master-esp32c3.factory.bin
  ```

Nach dem Boot: Access-Point `krone_anzeige` fuer die WLAN-Einrichtung, serielle Konsole auf USB-C (115200 Bd). Status-LED (GPIO6): schnelles Blinken = kein WLAN.
