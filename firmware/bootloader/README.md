# firmware/bootloader

Residenter Bootloader der Modulsteuerung (ATtiny1616, bare metal).
Am Gerät verifiziert seit Firmware v1.9 (Werksflash, Bus-Update Ende-zu-Ende,
siehe [`docs/module-bootloader.md`](../../docs/module-bootloader.md)).
Änderungen ab `BL_VERSION` 2 (`src/board.h`) sind bisher nur auf dem Host
getestet; der Bootloader erreicht die Karten nur per UPDI-Werksflash.

```bash
pio run -e bootloader        # ~2,7 KiB, Boot-Sektion 3 KiB (Fuse BOOTEND = 0x0C)
```

Liegt im Flash ab 0x0000, die Anwendung ab 0x0C00 (`firmware/module` env
`attiny1616_boot`). Empfängt die Firmware über USART0/RS-485 mit derselben
Rahmenschicht wie die App (`lib/protocol`), sammelt die Seiten über
`lib/fwupdate` und schreibt sie per `NVMCTRL`.

Alle Entscheidungen (Startfenster, App-Gültigkeit, Annahme von Rahmen,
Wiederaufsetzen) trifft `lib/fwupdate/fwboot` — hardwareunabhängig und mit
`pio test -e native -d firmware/module` (Suite `test_fwupdate`) getestet.
`src/main.c` macht nur Hardware: USART0 gepollt, TCB0 als 1-ms-Zeitbasis,
NVMCTRL, EEPROM. Hardwarenahe Konstanten stehen ausschließlich in
`src/board.h`.

Verhalten (BL_VERSION 2):

- App startbar = erstes App-Wort ≠ `0xFFFF` **und** EEPROM-Byte 8 = `0xA5`
  (Bus-Update fertig) oder `0xFF` (Werksflash).
- Startbare App, kein `ENTER_BOOTLOADER`-Marker → Startfenster 2,5 s (echte
  Zeit über TCB0). `FW_BEGIN` an die eigene Adresse startet ein Update, jeder
  andere Rahmen an sie startet sofort die App, Broadcasts und fremde Adressen
  verlängern oder beenden das Fenster nicht.
- Sonst warten. Nach `FW_BEGIN` (Byte 8 = `0x00`) ist die App bis zum
  erfolgreichen `FW_END` nicht startbar — weder nach Busstille noch nach einem
  Neustart.
- Wiederaufsetzbar: Wiederholung des letzten `FW_DATA` wird bestätigt,
  `FW_BEGIN` startet jederzeit neu, Leerlauf beendet die Übertragung nicht.
- Nur Unicast an die eigene Adresse (ungültige EEPROM-Adresse → 250) mit
  passender Nutzlastlänge; Antwort frühestens 200 µs nach Rahmenende.
- `GET_VERSION`: `[1 · 0xFF · 0xFF · flags · BL_VERSION]`, `APP_VALID` nach
  aktuellem Stand.

Der Bootloader konfiguriert den Triac-Treiberpin (PA7) nie als Ausgang (setzt
nur dessen Ausgangslatch auf low) → der Motor kann hier nicht bestromt werden;
eigener Watchdog als Hänge-Schutz.

Erstflash + Fuse weiterhin über UPDI/J6 (Werksflasher `updi.js` oder
`pio -t upload` + `pymcuprog`).
