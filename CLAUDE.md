# Projekt: Steuerung KRONE REW Fallblattanzeige

## Kontext
Ersatz der Original-Steuerelektronik einer mechanischen Fallblattanzeige von 1990.
Zehn Anzeigenmodule mit je 40 Blättern. Die Mechanik und die Anzeigenplatinen
(KRONE 6281 3 160-00) bleiben unverändert und werden weiterverwendet.

Architektur:
- Je Anzeigenmodul eine Daughter Card mit ATtiny1616
- RS-485 half duplex (TP8485E), 115200 Bd, Daisy-Chain über Flachbandkabel
- Automatische Adressvergabe über eine separate CHAIN-Leitung
- ESP32 als Master mit Web-UI, REST, MQTT und NTP-Uhr

## Verbindliche Quellen
- docs/spezifikation.md — Systemspezifikation
- docs/schaltplan-daughtercard.md — Netzliste und Stückliste
Bei Widerspruch gilt die Netzliste.

## Sprache
- Dokumentation, Commit-Messages und Kommentare: Deutsch
- Bezeichner im Quellcode: Englisch

## Harte Regeln
1. Keine Bauteilnummern, Pinbelegungen oder LCSC-Nummern erfinden. Was nicht
   belegbar ist, wird mit einem Hinweis markiert und nachgefragt.
2. Das Verzeichnis reference/ enthält urheberrechtlich geschützte Scans der
   KRONE-Dokumentation. Es steht in .gitignore und wird niemals committet.
   In Dokumenten wird auf Zeichnungsnummern verwiesen, nicht aus den Scans zitiert.
3. Die Punkte O-2, O-5 und O-6 sind noch nicht gemessen. Alles, was davon
   abhaengt, wird als konfigurierbarer Parameter ausgefuehrt, niemals als
   fest einkompilierter Wert.
4. Sicherheitsrelevant und nicht verhandelbar:
   - Der Motor darf bei haengender Firmware nicht dauerhaft bestromt bleiben.
     Watchdog plus Laufzeitueberwachung sind Pflicht, siehe Fehlercode 0x05.
   - Die 42-V-Wechselspannung ist potenzialfrei und wird nirgends mit der
     Logikmasse verbunden.
5. Ein erfolgreicher ERC-Lauf beweist nicht, dass eine Pinbelegung stimmt.
   Symbolpins werden gegen das Datenblatt geprueft und das Ergebnis als
   Tabelle zur menschlichen Bestaetigung vorgelegt.

## Arbeitsweise
- Kleine Commits mit aussagekraeftigen Nachrichten.
- Alle hardwarenahen Konstanten in genau einer Headerdatei je Firmware.
- Die Protokollschicht wird hardwareunabhaengig implementiert und ist auf dem
  Host testbar (pio test -e native). Kein direkter Registerzugriff darin.
- Vor jeder Aenderung an docs/spezifikation.md die Aenderungshistorie fortschreiben.

## Was NICHT zu tun ist
- Keine Aenderung der Steckerbelegung an J1 ohne Ruecksprache. Ein
  spiegelverkehrter Stecker legt 42 V auf die 5-V-Schiene.
- Keine Umstellung des Busprotokolls auf Modbus oder ein anderes Verfahren.
  Das war eine bewusste Entscheidung.
- Keine Sekundenanzeige als Vorgabe. Begruendung in Abschnitt 7.7 der
  Spezifikation (Modulverschleiss).
