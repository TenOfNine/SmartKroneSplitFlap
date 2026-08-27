# Projekt: Steuerung KRONE REW Fallblattanzeige

## Kontext

Ersatz der Original-Steuerelektronik einer mechanischen Fallblattanzeige der KRONE AG, Baujahr 1990. Zehn Anzeigenmodule der Palettenmodulreihe A mit je 40 Blättern. Die Mechanik und die Anzeigenplatinen (KRONE 6281 3 160-00) bleiben unverändert und werden weiterverwendet.

Architektur:

- Je Anzeigenmodul eine Daughter Card mit ATtiny1616
- RS-485 half duplex (TP8485E), 115200 Bd, Daisy-Chain über Flachbandkabel
- Automatische Adressvergabe über eine separate CHAIN-Leitung
- ESP32 als Master mit Web-UI, REST, MQTT und NTP-Uhr

## Verbindliche Quellen

- `docs/spezifikation.md` — Systemarchitektur, Busprotokoll, Firmware-Konzept, Netzteil, Testplan
- `docs/schaltplan-daughtercard.md` — vollständige Netzliste, Stückliste, Layout-Vorgaben
- `docs/backlog.md` — Aufgabenreihenfolge mit Abnahmekriterien
- `docs/toolchain.md` — Werkzeugkette und Kommandos

Bei Widerspruch zwischen Spezifikation und Schaltplan gilt die **Netzliste** in `docs/schaltplan-daughtercard.md`.

## Sprache

- Dokumentation, Commit-Messages und Kommentare: Deutsch
- Bezeichner im Quellcode: Englisch

## Harte Regeln

1. **Keine Bauteilnummern, Pinbelegungen oder LCSC-Nummern erfinden.** Was nicht belegbar ist, wird mit einem Hinweis markiert und nachgefragt.
2. Das Verzeichnis `reference/` enthält urheberrechtlich geschützte Scans der KRONE-Dokumentation. Es steht in `.gitignore` und wird **niemals** committet. In Dokumenten wird auf Zeichnungsnummern verwiesen, nicht aus den Scans zitiert.
3. Die Punkte O-2, O-5 und O-6 sind noch nicht gemessen. Alles, was davon abhängt, wird als konfigurierbarer Parameter ausgeführt, niemals als fest einkompilierter Wert.
4. Sicherheitsrelevant und nicht verhandelbar:
   - Der Motor darf bei hängender Firmware nicht dauerhaft bestromt bleiben. Watchdog und Laufzeitüberwachung sind Pflicht, siehe Fehlercode 0x05.
   - Die 42-V-Wechselspannung ist potenzialfrei und wird nirgends mit der Logikmasse verbunden.
5. Ein erfolgreicher ERC-Lauf beweist nicht, dass eine Pinbelegung stimmt. Symbolpins werden gegen das Datenblatt geprüft und das Ergebnis als Tabelle zur menschlichen Bestätigung vorgelegt.

## Arbeitsweise

- Kleine Commits mit aussagekräftigen Nachrichten.
- Alle hardwarenahen Konstanten in genau einer Headerdatei je Firmware.
- Die Protokollschicht wird hardwareunabhängig implementiert und ist auf dem Host testbar (`pio test -e native`). Kein direkter Registerzugriff darin.
- Vor jeder Änderung an `docs/spezifikation.md` die Änderungshistorie im Anhang fortschreiben.
- Offene Punkte werden als GitHub Issue geführt, nicht nur als Tabellenzeile.

## Was nicht zu tun ist

- Keine Änderung der Steckerbelegung an J1 ohne Rücksprache. Ein spiegelverkehrter Stecker legt 42 V auf die 5-V-Schiene und zerstört beide ICs.
- Keine Umstellung des Busprotokolls auf Modbus oder ein anderes Verfahren. Das war eine bewusste Entscheidung, begründet in der Spezifikation.
- Keine Sekundenanzeige als Vorgabe. Begründung in Abschnitt 7.7 der Spezifikation (Modulverschleiß, rund 173 Tage bis zur spezifizierten MTBF).
- Keine Wiedereinführung des DIP-Schalters zur Adressierung. Ebenfalls bewusst entfallen.
