# Firmware-Signatur (Browser-OTA der Zentralsteuerung)

Das OTA-Update über die Web-UI (`POST /api/update`, *Einstellungen › Firmware
aktualisieren*) nimmt **nur signierte** Container `krone-master-esp32c3.kota` an.
Damit kann über den offenen Netzwerkpfad keine fremde oder manipulierte Firmware
eingespielt werden. Der USB-Erst-Flash (`factory.bin`) ist davon unberührt.

## Verfahren

- **ECDSA P-256** (secp256r1), Signatur über SHA-256.
- Der **öffentliche** Schlüssel ist als 65-Byte-Array in die Firmware kompiliert:
  `firmware/master/lib/otaverify/ota_pubkey.h` (generiert, committet).
- Der **private** Schlüssel liegt **nie im Repo**. Standardpfad
  `~/.config/krone/ota-signing.pem` (chmod 600) oder Umgebungsvariable
  `KRONE_OTA_KEY` (Pfad **oder** PEM-Inhalt, z. B. als CI-Secret).
- Prüfung auf dem Gerät: mbedTLS (`src/ota_sign.cpp`). Schlägt sie fehl, wird
  `Update.abort()` gerufen und die laufende Firmware bleibt aktiv.

## Container `.kota`

| Offset | Länge | Inhalt |
|---|---|---|
| 0 | 4 | Magic `"KRN1"` |
| 4 | 1 | Formatversion (= 1) |
| 5 | 3 | reserviert |
| 8 | 4 | `img_len` (uint32 LE) |
| 12 | 32 | SHA-256 des App-Images |
| 44 | 64 | ECDSA-P-256-Signatur (`r‖s`) über Byte 0…43 |
| 108 | `img_len` | App-Image (`firmware.bin`) |

Der Parser (`lib/otaverify`, hostgetestet) prüft Magic, Version und Länge; die
Signatur bindet den Image-Hash. Beim Upload streamt das Modul das Image in die
inaktive App-Partition und vergleicht am Ende den mitgerechneten SHA-256 gegen
den Header — erst dann `Update.end()`.

## Einmalige Einrichtung

```bash
python tools/ota_keys.py init          # legt ota-signing.pem an + schreibt ota_pubkey.h
git add firmware/master/lib/otaverify/ota_pubkey.h
```

Danach:

- `~/.config/krone/ota-signing.pem` **sichern** (Passwortmanager / Offline-Kopie).
  Geht er verloren, ist kein Browser-OTA mehr möglich, bis mit `init --force` ein
  neues Paar erzeugt, `ota_pubkey.h` neu committet und die Firmware per **USB**
  neu geflasht ist.
- Für Signatur in GitHub Actions: Inhalt der PEM als Repo-Secret `OTA_SIGNING_KEY`
  hinterlegen (`Settings → Secrets and variables → Actions`).

## Release bauen

```bash
pio run -e esp32c3 -d firmware/master
python tools/build_master_firmware.py           # signiert .kota automatisch, wenn ein Schluessel da ist
git add firmware/master/prebuilt/
```

`build_master_firmware.py` ruft `tools/ota_keys.py sign` auf. Ohne Schlüssel wird
die `.kota` übersprungen (mit Warnung) und nur die `factory.bin` erzeugt.

## Schlüssel wechseln

1. `python tools/ota_keys.py init --force`
2. `git add firmware/master/lib/otaverify/ota_pubkey.h` und committen
3. Firmware neu bauen **und per USB flashen** (das alte Gerät kennt den neuen
   Schlüssel noch nicht — ein OTA mit der neuen `.kota` würde abgelehnt)
4. neues Secret `OTA_SIGNING_KEY` setzen

## Manuell signieren

```bash
python tools/ota_keys.py sign firmware/master/.pio/build/esp32c3/firmware.bin out.kota
```
