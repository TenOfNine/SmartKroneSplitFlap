# firmware/bootloader

Residenter Bootloader der Modulsteuerung (ATtiny1616, bare metal).
**Experimentell — am Gerät noch nicht verifiziert.** Siehe
[`docs/module-bootloader.md`](../../docs/module-bootloader.md).

```bash
pio run -e bootloader        # ~2,5 KiB, Boot-Sektion 3 KiB (Fuse BOOTEND = 0x0C)
```

Liegt im Flash ab 0x0000, die Anwendung ab 0x0C00 (`firmware/module` env
`attiny1616_boot`). Empfängt die Firmware über USART0/RS-485 mit derselben
Rahmenschicht wie die App (`lib/protocol`), sammelt die Seiten über
`lib/fwupdate` und schreibt sie per `NVMCTRL`.

Der Bootloader konfiguriert den Triac-Treiberpin (PA7) nie als Ausgang → der
Motor kann hier nicht bestromt werden; eigener Watchdog als Hänge-Schutz.

Erstflash + Fuse weiterhin über UPDI/J6 (Werksflasher `updi.js` oder
`pio -t upload` + `pymcuprog`).
