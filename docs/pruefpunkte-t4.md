# Prüfpunkte T4 — zweite Gegenprüfung durch den Betreiber

| Feld | Wert |
|---|---|
| Zweck | In T4 wurden zwei Widersprüche in `schaltplan-daughtercard.md` Kapitel 6 aufgelöst, um die Netzliste in einen ERC-fähigen KiCad-Schaltplan zu überführen. |
| Bezug | CLAUDE.md Regel 1 (nichts erfinden) und Regel 5 (Pinbelegung gegen Datenblatt, menschliche Freigabe) · Backlog T4 |
| Status | **P-1 und P-2 freigegeben** (Betreiber, im Chat, 28.08.2026). Bleibt als Prüfprotokoll erhalten. |
| Datum | 28.08.2026 |

> P-1 und P-2 wurden am 28.08.2026 vom Betreiber im Chat bestätigt („das sollte so
> passen"). Sollte sich bei einer späteren Detailprüfung doch etwas ändern:
> Netzliste in `tools/gen_daughtercard_sch.py` anpassen, Schaltplan mit
> `python tools/gen_daughtercard_sch.py --erc --pdf --png` neu erzeugen.

---

## P-1 — Testpad-Zuordnung TP3, TP4, TP5

### Widerspruch

| Quelle | Aussage |
|---|---|
| `schaltplan-daughtercard.md` 6.4 (Testpad-Tabelle) | TP3 = PULSE_NULL_RAW, TP4 = TRIAC_CTRL, TP5 = CHAIN_IN |
| `schaltplan-daughtercard.md` 6.3 (U1-Pinbelegung) | Pin 11 PB0 → „TP3 (Reserve)", Pin 12 PC0 → „TP4 (Reserve)", Pin 13 PC1 → „TP5 (Reserve)" |
| `spezifikation.md` 4.2 (vor v0.4) | „PB5, PB4, PC0…PC3 — Reserve, herausgeführt auf Testpads" (PB0 nicht genannt) |

TP1 und TP2 sind in allen drei Quellen gleich (PB5 an Pin 6, PB4 an Pin 7).

### Getroffene Entscheidung

**6.4 gilt.** Begründung: 6.4 ist die einzige in sich vollständige und zweckbeschriebene
Tabelle (alle sieben Testpads mit Diagnosezweck), 6.3 nennt nur „(Reserve)" ohne Zweck und
widerspricht sich selbst (Pin 14/15 „offen", obwohl die Spezifikation PC2/PC3 auf Testpads
sehen will).

Daraus folgt:

- TP1 → U1.6 (PB5), TP2 → U1.7 (PB4)
- TP3 → Netz PULSE_NULL_RAW (an J1.7)
- TP4 → Netz TRIAC_CTRL (an J1.9)
- TP5 → Netz CHAIN_IN (an U1.17 / PA1)
- TP6 → Netz VSENS, TP7 → Netz GND (unverändert)
- **Reserve-GPIOs PB0 (Pin 11), PC0 (12), PC1 (13), PC2 (14), PC3 (15) bleiben unbeschaltet.**
  Kein Testpad, kein herausgeführtes Pad. Im Schaltplan tragen sie ein No-Connect-Flag.

### Was zu prüfen ist

1. Ist es akzeptabel, **gar keinen** Testzugriff auf die fünf Reserve-GPIOs zu haben?
   Alternative: TP3–TP5 bleiben auf PB0/PC0/PC1 (6.3), und PULSE_NULL_RAW / TRIAC_CTRL /
   CHAIN_IN bekommen kein Testpad (oder es gibt TP8–TP10).
2. Falls die Reserve-GPIOs erreichbar sein sollen: als Lötpad, Stiftleiste oder Via-Reihe?
   Das ist eine Layout-Vorgabe für T5.

### Freigabe

| Ergebnis | Bestätigt von | Datum |
|---|---|---|
| 6.4 gilt, Reserve-GPIOs unbeschaltet — **freigegeben** | Betreiber (phi.hoffmann@hotmail.de), im Chat | 28.08.2026 |

---

## P-2 — Klemmdioden D1–D3 (BAT54S), Pinbelegung

### Widerspruch / Fehler

Drei Punkte gleichzeitig:

1. **Prosa gegen Netzliste.** 6.2 (vor v0.2) sagt „Anode (Pin 1) an +5V". In der
   +5V-Netzliste 6.1 fehlten `D1.1 / D2.1 / D3.1` jedoch vollständig.
2. **Pin-Nummern gegen KiCad-Symbol.** Die Prosa nannte Pin 3 „Kathode" und Pin 2
   „Mittelabgriff". Das offizielle Symbol `Diode:BAT54S` (kicad-symbols, Stand 26.08.2026)
   hat es umgekehrt: **Pin 1 = A, Pin 2 = K, Pin 3 = COM** (Mittelabgriff).
3. **Elektrisch unbrauchbar.** Nimmt man 6.2 wörtlich mit dem echten BAT54S-Pinout
   (Signal an Pin 2 = K, GND an Pin 3 = COM, +5V an Pin 1 = A), liegt die Diode A→COM
   direkt zwischen +5V und GND **in Durchlassrichtung** — ein Schottky-Kurzschluss der
   Versorgung. ERC erkennt das nicht (Regel 5).

### Getroffene Entscheidung

Zuordnung auf eine funktionsfähige Rail-Clamp-Topologie mit dem realen BAT54S-Pinout
umgestellt:

| BAT54S-Pin | Symbol-Name | Neu verbunden mit |
|---|---|---|
| 1 | A (Anode) | **GND** |
| 2 | K (Kathode) | **+5V** |
| 3 | COM (Mittelabgriff) | **gefiltertes Signal** (PULSE_BLATT / PULSE_LEER / PULSE_NULL) |

Wirkung: Signalspitze über +5 V + U_F → Diode COM→K leitet nach +5V ab.
Signalspitze unter GND − U_F → Diode A→COM leitet nach GND ab. Das ist der
Standard-Klemmschutz mit einem seriellen Doppel-Schottky.

Netzliste 6.1 / 6.2 in v0.2 entsprechend geändert:
`+5V` erhält `D1.2, D2.2, D3.2`; `GND` erhält `D1.1, D2.1, D3.1`;
`PULSE_BLATT/LEER/NULL` erhalten `D1.3 / D2.3 / D3.3` statt vorher `D*.2`.

### Was zu prüfen ist

1. Ist der Baustein wirklich **BAT54S** (seriell, Mittelabgriff herausgeführt) und nicht
   BAT54**C** (gemeinsame Kathode) oder BAT54**A** (gemeinsame Anode)? Die Stückliste 3.1
   sagt „BAT54S". Bei C oder A wäre die Topologie anders.
2. Stimmt die Polarität für die Anwendung? Ruhepegel ist high (Pull-up nach +5V), der
   Hall-Sensor zieht im Impuls nach Masse. Die Klemmung soll nur Überschwinger über +5V
   und unter GND abfangen, im normalen Hub 0…5 V sperren beide Dioden. Das ist mit der
   neuen Zuordnung gegeben — bitte gegenzeichnen.
3. Footprint: In T5 ist der SOT-23-Footprint mit der Pin-Reihenfolge des KiCad-Symbols
   (1-2-3) gegen das reale Bauteil zu prüfen.

### Freigabe

| Ergebnis | Bestätigt von | Datum |
|---|---|---|
| BAT54S-Zuordnung A→GND, K→+5V, COM→Signal — **freigegeben** | Betreiber (phi.hoffmann@hotmail.de), im Chat | 28.08.2026 |

---

## Sonstiges, in T4 nebenbei geändert (unkritisch)

- Netz `LED_K` neu benannt: Knoten zwischen R15 und LED-Anode D4.2. Die LED-Kathode
  D4.1 liegt auf GND. `Device:LED`: Pin 1 = K, Pin 2 = A. Vorher stand in 6.1 nur
  „R15.2 (über D4)" ohne eigenen Netznamen.
- Testpad-Netze `TP_PB5`, `TP_PB4` und die Zuweisungen `TP3.1 … TP7.1` in die Netzliste
  aufgenommen, damit die Testpunkte im Schaltplan verdrahtet sind.
