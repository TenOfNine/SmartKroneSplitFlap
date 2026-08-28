# firmware/module

Firmware der Modulsteuerung (ATtiny1616, PlatformIO / megaTinyCore).

## Stand

| Teil | Status |
|---|---|
| `lib/protocol/` | **T6 fertig.** Rahmen, CRC16/MODBUS, Kommandotabelle. Hardwareunabhängig, host-getestet. |
| `lib/enumeration/` | **T6 fertig.** Enumerations-Zustandsautomat inkl. Rückfallverhalten und Kollisionserkennung. |
| `src/main.c` | Platzhalter. Zustandsautomat, USART/RS-485, Impulsauswertung, Watchdog → **T7**. |

## Bibliotheken

Beide hardwareunabhängig (kein Registerzugriff), Bezug jeweils
`docs/spezifikation.md`:

- **`protocol`** (Abschnitt 5.3–5.5) — `proto_encode`, Streaming-Parser
  `proto_parser_feed`, `proto_crc16`, Kommandotabelle `proto_cmd_lookup` /
  `proto_cmd_is_valid`.
- **`enumeration`** (Abschnitt 4.5) — `enum_fsm_*`. Der Automat kennt weder EEPROM
  noch CHAIN-Leitung; der Aufrufer speist Ereignisse ein (`on_frame`, `on_tick`,
  `on_echo_mismatch`) und wertet die Ausgabeflags aus (`want_ack`,
  `want_eeprom_write`).

## Tests

```bash
source ../../.venv/bin/activate
pio test -e native
```

`test/test_crc`, `test/test_frame`, `test/test_command`, `test/test_enumeration`
(Unity). Der Enumerationstest deckt die vom Backlog T6 geforderten Fälle ab:
Kollisionserkennung, Rückfall auf die EEPROM-Adresse, Rückfall auf die
Serviceadresse 250.

## Firmware bauen (ab T7)

```bash
pio run  -e attiny1616
pio run  -e attiny1616 -t upload      # SerialUPDI, FTDI-Adapter mit 4,7 kΩ TX–RX
```
