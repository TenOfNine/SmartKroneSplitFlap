# Vorgebaute Modul-Firmware (ATtiny1616)

Erzeugt von `tools/build_module_firmware.py` aus `firmware/module/` (zuletzt gebaut nahe Commit `76631e8`, 2026-09-09). Bei jeder Firmware-Aenderung neu ausfuehren.

| Datei | Zweck |
|---|---|
| `krone-daughtercard-attiny1616.hex` | Intel-HEX der Modul-Firmware. Wird vom Browser-UPDI-Flasher (Tab **Daughter Card** auf der GitHub Page) geschrieben. |

SHA-256: `362ef32d50c8ede7ef8548297dfc4e6871d616de9cbe584539cc134fbc840dff`

## Flashen

- **Browser (experimentell):** <https://tenofnine.github.io/SmartKroneSplitFlap/> , Tab *Daughter Card*. USB-Seriell-Adapter (5 V) mit 4,7-kOhm-Bruecke zwischen TX und RX an J6: TX/RX -> Pin 2 (UPDI), GND -> Pin 1, +5 V -> Pin 3 (nur wenn die Karte sonst keine 5 V hat). Chrome/Edge Desktop.
- **Kommandozeile:**

  ```
  pio run -e attiny1616 -t upload -d firmware/module
  ```

  bzw. direkt `pymcuprog write -d attiny1616 -t uart -u <port> -f krone-daughtercard-attiny1616.hex --erase --verify`.

Der Flasher prueft vor dem Schreiben die Geraete-ID (ATtiny1616 = `1E 94 22`) und bricht bei Abweichung ab. Fuses (OSCCFG 20 MHz) werden nicht angefasst.
