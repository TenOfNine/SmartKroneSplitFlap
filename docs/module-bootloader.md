# Modul-Bootloader und Firmware-Verteilung über den Bus

> **Status: experimentell.** Der **Werksflash über den Browser** (Bootloader +
> App + `BOOTEND`-Fuse, Bench-Punkte 1+2) ist **am Gerät verifiziert**
> (10.09.2026). Die **Firmware-Verteilung über den Bus** (Punkte 3–8) ist noch
> **nicht verifiziert** — dafür wird die Master-Hardware gebraucht (bestellt,
> noch nicht aufgebaut). `pio run -e attiny1616 -t upload` (App @ 0x0000, ohne
> Bootloader) bleibt bis dahin der abgesicherte Weg für Firmware-Änderungen.

## Ziel

Die Master-Steuerung spielt den Daughter Cards die Firmware **über den vorhandenen
RS-485-Bus** ein — kein PC, kein UPDI-Adapter, einzeln adressierbar, ausfallsicher.

## Flash-Aufteilung (ATtiny1616, 16 KiB)

```
  0x0000 ┌─────────────────────┐
         │  Bootloader         │  Fuse BOOTEND = 0x0C  (0x0C * 256 = 0x0C00)
  0x0C00 ├─────────────────────┤
         │  Anwendung          │  gebaut mit env attiny1616_boot
         │  (Vektortabelle,    │  (-Wl,--section-start=.text=0x0C00, -DHAS_BOOTLOADER)
         │   IVSEL = 0)        │
  0x2400 ├─────────────────────┤
         │  frei               │
  0x4000 └─────────────────────┘
```

- Der Bootloader belegt ~2,5 KiB, die Boot-Sektion 3 KiB. Die App belegt ~5,6 KiB.
- **`BOOTEND` sperrt den Baustein nicht.** UPDI hat weiter vollen Zugriff und kann
  den Fuse jederzeit zurücksetzen. `LOCKBIT` wird nicht gesetzt; wäre es gesetzt,
  entsperrt ein UPDI-Chip-Erase wieder. Die einzige echte Aussperr-Falle wäre
  `SYSCFG0.RSTPINCFG` weg von UPDI — das ändert weder der Bootloader noch der
  Werksflash.
- Interruptvektoren: tinyAVR-1 hat `CPUINT.CTRLA.IVSEL`. Nach Reset = 0 (Vektoren
  in der App-Sektion). Der Bootloader nutzt **keine Interrupts**; er setzt vor dem
  Sprung `CPUINT.CTRLA = 0`, die App setzt es zusätzlich selbst (`#ifdef HAS_BOOTLOADER`).

## Ablauf

**Reset → Bootloader.** Prüft `GPIOR0` (Marker `0xB7`, von `CMD_ENTER_BOOTLOADER`)
und `app_valid()` (erstes App-Wort ≠ `0xFFFF` **und** EEPROM-Byte 8 ≠ `0x00`).

- gültige App, kein Marker → ~2,5 s auf `CMD_FW_BEGIN` lauschen, sonst App starten.
- Marker gesetzt **oder** keine gültige App → im Bootloader bleiben und warten.

**Update-Sitzung** (Master-getrieben, bestehende Rahmenschicht `lib/protocol`):

| Kommando | Payload | Antwort |
|---|---|---|
| `CMD_ENTER_BOOTLOADER` (0x55) | — | — (Modul: `GPIOR0=0xB7`, SW-Reset) |
| `CMD_FW_BEGIN` (0x56) | `len16 · crc16` (CRC16/MODBUS über das Image) | `[0x01]` ok / `[0x00]` nak |
| `CMD_FW_DATA` (0x57) | `off16 · bis 30 Byte` | `[0x01]` / `[0x00]` |
| `CMD_FW_END` (0x58) | — | `[0x01]` ok / `[Fehlercode]` |
| `CMD_GET_VERSION` (0x54) | — | `[1 · maj · min · flags · bl_ver]` |

Der Bootloader markiert die App bei `FW_BEGIN` als ungültig (EEPROM-Byte 8 = 0x00),
schreibt die Seiten per `NVMCTRL` (Erase+Write) ab 0x0C00, prüft bei `FW_END`
Länge und CRC, setzt dann Byte 8 = `0xA5` und startet per SW-Reset neu.

**Fehler / Stromausfall:** die App bleibt ungültig → beim nächsten Reset bleibt der
Bootloader im Update-Modus, der Master wiederholt. Der Bootloader selbst kann von
App-Code nicht überschrieben werden (Hardware-Schutz der Boot-Sektion).

## Motorsicherheit

Der Bootloader konfiguriert **PA7 (Triac-Treiber) nie als Ausgang** → der
Transistor bleibt gesperrt, der Motor kann im Bootloader nicht bestromt werden,
egal welcher Fehler auftritt. Der Bootloader hat einen eigenen Watchdog (~1 s).
Die App-seitige Laufzeitüberwachung (Fehlercode 0x05) und der App-Watchdog bleiben
unverändert.

## Bausteine

| Ort | Inhalt |
|---|---|
| `firmware/bootloader/` | der Bootloader (`pio run -e bootloader`) |
| `firmware/module` env `attiny1616_boot` | App @ 0x0C00, `CMD_GET_VERSION`/`CMD_ENTER_BOOTLOADER` |
| `firmware/module/lib/fwupdate/` | empfangsseitiger Seiten-Sammler (host-getestet) |
| `firmware/master/lib/moduleupdate/` | sendeseitige Warteschlange + Zustandsautomat (host-getestet) |
| `firmware/master/src/module_fw.h` | signierter `.mota`-Container der App, in die Master-Firmware eingebettet (Option A) |
| `tools/build_module_firmware.py` | baut alle drei Images + signiert die `.mota` |

Der Master prüft die eingebettete `.mota` beim Start gegen `ota_pubkey.h`
(dieselbe ECDSA-Kette wie das Master-OTA). Der Bootloader prüft nur die CRC —
Vertrauensanker ist der Master.

## Master-Web-UI

*Einstellungen › Modul-Firmware*: Tabelle mit installierter vs. gebündelter
Version je Adresse, Status (aktuell / Update verfügbar / kein Bootloader /
offline), Buttons **„Alle aktualisieren"** und **„Veraltete aktualisieren"**.
Läuft ein Update, ruht der Statusverkehr auf dem Bus; die Module werden
nacheinander abgearbeitet, das jeweils betroffene ist einige Sekunden eingefroren.

REST: `GET /api/module/firmware`, `POST /api/module/update` (`{"all":true}` oder
`{"addr":[2,3]}`), `GET /api/module/update/status`.

## Bench-Test-Checkliste (vor dem ersten Einsatz)

1. ✅ **Bootloader isoliert.** `pio run -e bootloader`, per UPDI @ 0x0000 flashen,
   Fuse `BOOTEND = 0x0C` setzen (`pymcuprog write -m fuses -o 8 --values 0x0C`
   oder der Werksflasher). Ohne App: bleibt der Bootloader im Warte-Loop, WDT
   löst nicht aus (kein Dauerreset).
   **Verifiziert 10.09.2026** über den Browser-Werksflasher (schreibt
   Bootloader + Fuse in einem Zug): Chip-Erase, Geräte-ID, Fuse- und
   Seitenschreiben liefen sauber, kein Dauerreset.
2. ✅ **Sprung in die App.** `attiny1616_boot`-App @ 0x0C00 dazuflashen. Reset →
   Bootloader übergibt an die App (Interruptvektoren korrekt, IVSEL = 0) —
   **verifiziert** (Status-LED blinkt/leuchtet wie von der App vorgegeben, der
   1-ms-Timer-Interrupt läuft also). RS-485-Kommunikation + Motorsteuerung im
   Zusammenspiel mit dem Master noch offen (Punkte 3+, Master-Hardware
   bestellt, noch nicht aufgebaut).
3. **`CMD_GET_VERSION`.** Master fragt Version ab, bekommt `1 · 1 · 0 · 0x03 · x`
   (App gültig + Bootloader vorhanden).
4. **`CMD_ENTER_BOOTLOADER`.** Modul startet in den Bootloader und bleibt dort.
5. **Update über den Bus.** `POST /api/module/update` für eine Karte. Verlauf im
   Log; nach `FW_END` startet die Karte neu, `GET_VERSION` bestätigt die Version.
6. **Abbruch mitten im Transfer** (Karte kurz stromlos): App bleibt ungültig,
   Bootloader wartet, Master-Wiederholung bringt sie zurück.
7. **„Alle aktualisieren"** mit mehreren Karten: nacheinander, andere Module
   bleiben erreichbar.
8. **CRC-Fehlerpfad**: eine bewusst verfälschte `.mota` → Bootloader lehnt bei
   `FW_END` ab, App bleibt ungültig.
9. Bus-Signalintegrität + Dauer bei zehn Karten am realen Kabelbaum messen.

## Werksflash über den Browser

`firmware/master/prebuilt/updi.js`, Tab *Daughter Card*: Chip-Erase → Bootloader
+ `-boot`-App → `BOOTEND = 0x0C`. Geräte-ID-Prüfung (`1E 94 21`). Der Fuse-Write
folgt der `pymcuprog`-v0-Sequenz (ADDR + DATA + `WFU`).

Der Seitenpuffer wird wie in SerialUPDI (megaTinyCore) mit **`CTRLA.RSD = 1`**
(Response Signature Disable) beschrieben: ohne RSD schickt das Modul nach jedem
ST-Byte ein ACK, das auf der Ein-Draht-Leitung mit den folgenden Datenbytes
kollidiert. Mit RSD gehen `REPEAT` + `ST` (Wort) + 64 Datenbytes +
`STCS(CTRLA, RSD=0)` in einem Transfer raus, danach nur das Echo lesen.
