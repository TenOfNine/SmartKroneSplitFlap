# Firmware-Signatur (OTA der Zentralsteuerung, Modul-Firmware über den Bus)

Signiert werden zwei Container:

| Container | Inhalt | Weg aufs Gerät |
|---|---|---|
| `krone-master-esp32c3.kota` | App-Image der Zentralsteuerung (`firmware.bin`) | Web-UI *Einstellungen › Firmware aktualisieren* (`POST /api/update`) oder *Von GitHub aktualisieren* (`POST /api/update/github`) |
| `krone-daughtercard-attiny1616.mota` | App der Daughter Card hinter dem Bootloader (`attiny1616_boot`, Rohbinär) | in die Master-Firmware eingebettet (`firmware/master/src/module_fw.h`), von dort über den Bus verteilt (`docs/module-bootloader.md`) |

Über den offenen Netzwerkpfad kann damit keine fremde oder manipulierte Firmware
eingespielt werden. Der USB-Erst-Flash (`factory.bin`) und der Browser-Werksflash
der Karten (UPDI) sind davon unberührt; sie setzen physischen Zugang voraus.

## Verfahren

- **ECDSA P-256** (secp256r1), Signatur über SHA-256.
- Der **öffentliche** Schlüssel ist als 65-Byte-Array in die Firmware kompiliert:
  `firmware/master/lib/otaverify/ota_pubkey.h` (generiert, committet).
- Der **private** Schlüssel liegt **nie im Repo**. Standardpfad
  `~/.config/krone/ota-signing.pem` (chmod 600) oder Umgebungsvariable
  `KRONE_OTA_KEY` (Pfad **oder** PEM-Inhalt).
- Prüfung auf dem Gerät: mbedTLS (`src/ota_sign.cpp`). Schlägt sie fehl, wird
  `Update.abort()` gerufen und die laufende Firmware bleibt aktiv.

## Container-Aufbau (`.kota` und `.mota`)

| Offset | Länge | Inhalt |
|---|---|---|
| 0 | 4 | Magic `"KRN1"` |
| 4 | 1 | Formatversion (= 1) |
| 5 | 1 | Ziel: 1 = Zentralsteuerung, 2 = Modul (0 = unbekannt, Container bis 1.18) |
| 6 | 1 | Version MAJOR (bis 1.18: 0) |
| 7 | 1 | Version MINOR (bis 1.18: 0) |
| 8 | 4 | `img_len` (uint32 LE) |
| 12 | 32 | SHA-256 des Images |
| 44 | 64 | ECDSA-P-256-Signatur (`r‖s`) über Byte 0…43 |
| 108 | `img_len` | Image |

Byte 5…7 waren bis Firmware 1.18 reserviert (0). Sie liegen im signierten Teil.
Ältere Zentralsteuerungen ignorieren sie; die Formatversion bleibt deshalb 1,
damit auch ein 1.18-Master einen neuen Container annimmt (#38).

## Prüfungen

**Zentralsteuerung, `.kota` (Upload und GitHub):**

1. `lib/otaverify` (hostgetestet): Magic, Formatversion, Länge.
2. Signatur über Byte 0…43 gegen den einkompilierten Schlüssel.
3. Ziel und Version (`otaverify_check_target`, Anti-Rollback):
   - Ziel ≠ Zentralsteuerung → immer abgelehnt.
   - Version kleiner als die laufende, oder Container ohne Angaben (bis 1.18) →
     abgelehnt, außer beim Browser-Upload mit dem Schalter *Älteren Stand
     erlauben* (`POST /api/update?downgrade=1`, Warnung im Ereignislog).
   - Dieselbe Version ist erlaubt (Neuinstallation).
4. Das Image wird gestreamt in die inaktive App-Partition geschrieben, der
   mitgerechnete SHA-256 am Ende gegen den Header geprüft — erst dann
   `Update.end()`.

Der GitHub-Abruf (`raw.githubusercontent.com`) prüft zusätzlich das
TLS-Zertifikat gegen die eingebetteten Wurzel-CAs (`src/ca_bundle.h`, erzeugt von
`tools/gen_ca_bundle.py` aus dem Mozilla-Bestand des Python-Pakets `certifi`) und
geht nie auf einen älteren Stand. Wechselt GitHub zu einer CA außerhalb der
Auswahl, scheitert der Abruf sichtbar; der Browser-Upload bleibt. Dann
`gen_ca_bundle.py` erweitern.

**Eingebettete `.mota` (beim Start der Zentralsteuerung):** Signatur, Ziel = Modul,
Version = `MODULE_FW_VER` (= `APP_VERSION_*` aus `firmware/module/src/board.h`),
Länge und SHA-256 des Images. Erst danach bietet die Web-UI die Verteilung an.

## Einmalige Einrichtung

```bash
python tools/ota_keys.py init          # legt ota-signing.pem an + schreibt ota_pubkey.h
git add firmware/master/lib/otaverify/ota_pubkey.h
```

Danach `~/.config/krone/ota-signing.pem` **sichern** (Passwortmanager /
Offline-Kopie). Geht er verloren, ist kein OTA mehr möglich, bis mit
`init --force` ein neues Paar erzeugt, `ota_pubkey.h` neu committet und die
Firmware per **USB** neu geflasht ist.

## Signieren und Release

Signiert wird **lokal** beim Erzeugen der Prebuilts; die CI braucht keinen
Schlüssel und kein Secret. Sie prüft die committeten Container gegen den
öffentlichen Schlüssel (`tools/ota_keys.py verify`) und dass das Image in der
`.kota` bytegleich aus der Quelle entsteht (`firmware/master/scripts/reproducible.py`).

```bash
python tools/build_module_firmware.py    # Modul-Hex + .mota (Ziel Modul, Version aus board.h)
python tools/build_master_firmware.py    # bettet die .mota ein, factory.bin + .kota (Ziel Master, Version aus main.cpp)
python tools/make_release.py             # prüft Versionsgleichstand, legt das Tag an
```

Ohne Schlüssel überspringen beide Skripte den Container mit Warnung und
**entfernen** einen alten, damit keine zur Quelle unpassende Datei liegen bleibt.
`build_master_firmware.py` bettet eine `.mota` nur ein, wenn ihr Header genau die
Modulversion aus `board.h` trägt; sonst einen leeren Blob (die Web-UI meldet dann
„keine gebündelte Modul-Firmware“).

## Manuell signieren / prüfen

```bash
python tools/ota_keys.py sign firmware/master/.pio/build/esp32c3/firmware.bin out.kota --target master --version 1.19
python tools/ota_keys.py sign --raw app.bin out.mota --target module --version 1.19
python tools/ota_keys.py verify out.kota      # zeigt Ziel und Version
```

## Schlüssel wechseln

1. `python tools/ota_keys.py init --force`
2. `git add firmware/master/lib/otaverify/ota_pubkey.h` und committen
3. Prebuilts neu erzeugen, Firmware **per USB flashen** (das Gerät kennt den neuen
   Schlüssel noch nicht — ein OTA mit dem neuen Container würde abgelehnt)

## Bekannte Grenzen

- Zugangsdaten (WLAN-Schlüssel, MQTT- und Admin-Passwort) liegen im NVS im
  Klartext. Abhilfe wäre die Flash-Verschlüsselung des ESP32-C3; sie brennt
  eFuses (unumkehrbar) und ist deshalb nicht eingeschaltet. Über HTTP gibt die
  Sicherung (`/api/backup`) diese Werte nur heraus, wenn ein Admin-Passwort
  gesetzt ist (angemeldete Verbindung).
- Basic-Auth läuft über HTTP; der Zugriff ist auf private Netze bzw. das eigene
  Subnetz begrenzt (*Einstellungen › Zugriffsschutz*).
