/*
 * SerialUPDI im Browser -- schreibt die Modul-Firmware (ATtiny1616) ueber einen
 * USB-Seriell-Adapter (Web Serial API).  Werksflash am Geraet verifiziert
 * (10.09.2026: Chip-Erase, Geraete-ID, BOOTEND, Seitenschreiben, Verify).
 * Seit Firmware 1.19 (#41, #48): Geraete-ID vor dem Chip-Erase, App-Marker im
 * EEPROM erst nach erfolgreichem Verify gueltig, OSCCFG/WDTCFG geprueft,
 * UPDI am Ende aus -- diese Schritte sind am Geraet noch nicht bestaetigt.
 * Ausweichweg bleibt `pio run -e attiny1616 -t upload`.
 *
 * Portiert nach den Referenzimplementierungen pymcuprog (Microchip) und
 * SerialUPDI/prog.py (megaTinyCore).  Nur tinyAVR-1-Serie (NVMCTRL v0,
 * Flash-Page 64 B, Flash im UPDI-Adressraum ab 0x8000).
 *
 * Verkabelung:  Adapter-TXD --[4,7 kOhm]--+-- J6.2 (UPDI)
 *               Adapter-RXD ---------------+
 *               Adapter-GND ----------------- J6.1 (GND)
 *               Adapter-5V  ----------------- J6.3 (+5V, nur falls noetig)
 * Wegen der TX/RX-Bruecke liest der Adapter jedes gesendete Byte als Echo
 * zurueck; das wird hier verworfen, bevor die Antwort gelesen wird.
 */
(function (global) {
  "use strict";

  // ---- UPDI-Konstanten -------------------------------------------------
  const SYNCH = 0x55;
  const ACK = 0x40;

  // CS-Register (LDCS/STCS)
  const CS_STATUSA = 0x00;
  const CS_CTRLA = 0x02;
  const CS_CTRLB = 0x03;
  const CS_ASI_KEY_STATUS = 0x07;
  const CS_ASI_RESET_REQ = 0x08;
  const CS_ASI_SYS_STATUS = 0x0b;

  const CTRLB_CCDETDIS = 1 << 3; // 0x08 -- Kollisionserkennung aus (1-Draht)
  const CTRLB_UPDIDIS = 1 << 2;  // 0x04 -- UPDI aus (wie pymcuprog leave_progmode)
  const CTRLA_IBDLY = 1 << 7;    // 0x80 -- Inter-Byte-Delay
  // CTRLA-Werte fuer den Blockschreibvorgang: GTVAL = 6 (Guard-Zeit 2 Takte)
  // plus/ohne RSD (Response Signature Disable, Bit 3). Wie SerialUPDI.
  const RSD_ON = 0x0e;
  const RSD_OFF = 0x06;

  const KEY_STATUS_CHIPERASE = 1 << 3;
  const KEY_STATUS_NVMPROG = 1 << 4;

  const SYS_STATUS_LOCKSTATUS = 1 << 0;
  const SYS_STATUS_NVMPROG = 1 << 3;

  const RESET_REQ = 0x59;
  const RESET_RUN = 0x00;

  // Schluessel: 8 ASCII-Zeichen, ueber die Leitung LSB zuerst (String rueckwaerts)
  const KEY_NVMPROG = strKeyLE("NVMProg ");
  const KEY_CHIPERASE = strKeyLE("NVMErase");

  // NVMCTRL (tinyAVR-1)
  const NVMCTRL_BASE = 0x1000;
  const NVMCTRL_CTRLA = NVMCTRL_BASE + 0x00;
  const NVMCTRL_STATUS = NVMCTRL_BASE + 0x02;
  const NVMCTRL_DATAL = NVMCTRL_BASE + 0x06;
  const NVMCTRL_ADDRL = NVMCTRL_BASE + 0x08;
  const NVM_CMD_WP = 0x01;   // write page
  const NVM_CMD_ERWP = 0x03; // erase+write page
  const NVM_CMD_PBC = 0x04;  // page buffer clear
  const NVM_CMD_CHER = 0x05; // chip erase
  const NVM_CMD_WFU = 0x07;  // write fuse
  const NVM_STATUS_BUSY = 0x03; // FBUSY | EEBUSY
  const NVM_STATUS_WRERROR = 0x04;

  const SIGROW_DEVICEID = 0x1100;
  // Fuse-Adressen und Vorgaben aus iotn1616.h (avr-libc der PlatformIO-Toolchain)
  const FUSE_BASE = 0x1280;
  const FUSE_WDTCFG = FUSE_BASE + 0x00;    // Fuse 0, Vorgabe 0x00 (WDT per Software)
  const FUSE_BODCFG = FUSE_BASE + 0x01;    // Fuse 1
  const FUSE_OSCCFG = FUSE_BASE + 0x02;    // Fuse 2, Vorgabe 0x02 (FREQSEL 20 MHz)
  const FUSE_SYSCFG0 = FUSE_BASE + 0x05;   // Fuse 5, Vorgabe 0xC4 (CRC aus, UPDI-Pin, EESAVE 0)
  const FUSE_BOOTEND = FUSE_BASE + 0x08;   // Fuse 8
  const WDTCFG_VALUE = 0x00;               // Firmware stellt den WDT selbst (board.h)
  const OSCCFG_FREQSEL_MASK = 0x03;
  const OSCCFG_FREQSEL_20MHZ = 0x02;       // F_CPU 20 MHz ohne Vorteiler (board.h)
  const SYSCFG0_EESAVE = 0x01;
  const SYSCFG0_RSTPINCFG_MASK = 0x0c;
  const SYSCFG0_RSTPINCFG_UPDI = 0x04;
  const BOOTEND_VALUE = 0x0c;              // 0x0C * 256 = App ab 0x0C00

  // BODCFG-Soll-Wert, siehe ATtiny1614/1616/1617-Datenblatt (Microchip
  // DS40002204A) Kapitel 17.5.1 "Control A" (ACTIVE/SLEEP) und 17.5.2
  // "Control B" (LVL) -- die drei Felder liegen im FUSE-Byte an ANDEREN
  // Bitpositionen als in den gleichnamigen Laufzeitregistern:
  //   Bits 1:0  SLEEP    01 = Enabled (kontinuierlich)
  //   Bits 3:2  ACTIVE   01 = Enabled (kontinuierlich)
  //   Bit  4    SAMPFREQ irrelevant, da nicht Sampled-Modus
  //   Bits 7:5  LVL      111 = BODLEVEL7 = 4,2 V (hoechste verfuegbare
  //                      Stufe; das Datenblatt (Seite 2, "Speed Grades")
  //                      nennt fuer 20 MHz 4,5-5,5 V -- 4,2 V ist die
  //                      naechstliegende Stufe darunter, mehr bietet der
  //                      Chip nicht (nur 1,8/2,6/4,2 V moeglich)).
  // Kein Fuse fuer Vorgänger-Firmware noetig: BOD ist reine Absicherung
  // gegen einen unsauberen Stromauf-/-abfall, kein Verhaltensunterschied
  // im Normalbetrieb bei stabiler 5-V-Versorgung.
  const BODCFG_VALUE = (0x07 << 5) | (0x01 << 2) | (0x01 << 0); // 0xE5
  const FLASH_BASE = 0x8000;
  const FLASH_PAGE = 64;
  const FLASH_SIZE = 0x4000; // 16 KiB
  // EEPROM-Basisadresse im UPDI-Datenraum (tinyAVR-0/1/2 einheitlich, wie
  // SIGROW/FUSES oben). EE_APP_VALID = firmware/bootloader/src/main.c
  // (dort dasselbe Byte 8: 0x00 = Update laeuft, 0xFF/0xA5 = App gueltig).
  const EEPROM_BASE = 0x1400;
  const EE_APP_VALID = 8;
  const EE_APP_INVALID = 0x00;   // Bootloader bleibt im Update-Wartemodus
  const EE_APP_OK = 0xff;

  const ATTINY1616_ID = [0x1e, 0x94, 0x21]; // laut pymcuprog DFP + avrdude.conf

  // Ziel-Baudrate.  Falls die Init scheitert, ist das der erste Knopf zum Drehen.
  const BAUD = 115200;
  const BREAK_BAUD = 300;

  function strKeyLE(s) {
    const out = new Uint8Array(8);
    for (let i = 0; i < 8; i++) out[i] = s.charCodeAt(7 - i);
    return out;
  }
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const hex2 = (n) => n.toString(16).padStart(2, "0");

  // ---- Web-Serial-Transport -----------------------------------------
  class Transport {
    constructor(port) {
      this.port = port;
      this.reader = null;
      this.buf = new Uint8Array(0);
    }

    async open(baud) {
      await this.port.open({
        baudRate: baud,
        dataBits: 8,
        parity: "even",
        stopBits: 2,
        flowControl: "none",
        bufferSize: 4096,
      });
      this.reader = this.port.readable.getReader();
      this.buf = new Uint8Array(0);
      this._pump();
    }

    async _pump() {
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          if (value && value.length) {
            const merged = new Uint8Array(this.buf.length + value.length);
            merged.set(this.buf);
            merged.set(value, this.buf.length);
            this.buf = merged;
          }
        }
      } catch (_) {
        /* Port geschlossen */
      }
    }

    async close() {
      try {
        if (this.reader) {
          await this.reader.cancel().catch(() => {});
          this.reader.releaseLock();
          this.reader = null;
        }
      } catch (_) {}
      try {
        await this.port.close();
      } catch (_) {}
    }

    async reopen(baud) {
      await this.close();
      await sleep(30);
      await this.open(baud);
    }

    async write(bytes) {
      const w = this.port.writable.getWriter();
      try {
        await w.write(bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes));
      } finally {
        w.releaseLock();
      }
    }

    /* n Bytes lesen, Timeout in ms */
    async read(n, timeout = 500) {
      const deadline = Date.now() + timeout;
      while (this.buf.length < n) {
        if (Date.now() > deadline) {
          throw new Error(
            `Timeout: ${this.buf.length}/${n} B (erhalten: ${[...this.buf].map(hex2).join(" ")})`
          );
        }
        await sleep(2);
      }
      const out = this.buf.slice(0, n);
      this.buf = this.buf.slice(n);
      return out;
    }

    flush() {
      this.buf = new Uint8Array(0);
    }

    /* UPDI-BREAK: bei 300 Bd zwei Nullbytes -> Leitung lange genug low.
     * Erst das Echo beider Bytes abwarten (je ~33 ms), sonst schneidet das
     * Umschalten auf die Arbeitsbaudrate den BREAK ab (#41). */
    async sendBreak() {
      await this.reopen(BREAK_BAUD);
      await this.write(new Uint8Array([0x00, 0x00]));
      try {
        await this.read(2, 400);
      } catch (_) {
        await sleep(80);   // Adapter ohne Echo bei 300 Bd: Mindestdauer abwarten
      }
      await this.reopen(BAUD);
      await sleep(2);
      this.flush();
    }
  }

  // ---- UPDI-Protokoll ----------------------------------------------
  class Updi {
    constructor(transport) {
      this.t = transport;
    }

    /* Instruktion senden, Echo verwerfen, danach `respLen` Antwortbytes lesen */
    async _cmd(bytes, respLen, timeout = 500) {
      this.t.flush();
      const frame = new Uint8Array(bytes);
      await this.t.write(frame);
      await this.t.read(frame.length, timeout); // Echo (TX/RX gebrueckt)
      return respLen ? this.t.read(respLen, timeout) : new Uint8Array(0);
    }

    async ldcs(reg) {
      const r = await this._cmd([SYNCH, 0x80 | (reg & 0x0f)], 1);
      return r[0];
    }
    async stcs(reg, val) {
      await this._cmd([SYNCH, 0xc0 | (reg & 0x0f), val & 0xff], 0);
    }

    /* LDS: direkte 16-bit-Adresse, 1 Byte lesen */
    async lds8(addr) {
      const r = await this._cmd(
        [SYNCH, 0x00 | (1 << 2) | 0, addr & 0xff, (addr >> 8) & 0xff],
        1
      );
      return r[0];
    }
    /* STS: direkte 16-bit-Adresse, 1 Byte schreiben */
    async sts8(addr, val) {
      // Adressphase -> ACK, dann Datenphase -> ACK
      this.t.flush();
      const a = new Uint8Array([SYNCH, 0x40 | (1 << 2) | 0, addr & 0xff, (addr >> 8) & 0xff]);
      await this.t.write(a);
      await this.t.read(a.length);
      const ack1 = await this.t.read(1);
      if (ack1[0] !== ACK) throw new Error(`STS Adress-NACK 0x${hex2(ack1[0])}`);
      const d = new Uint8Array([val & 0xff]);
      await this.t.write(d);
      await this.t.read(d.length);
      const ack2 = await this.t.read(1);
      if (ack2[0] !== ACK) throw new Error(`STS Daten-NACK 0x${hex2(ack2[0])}`);
    }

    /* Pointer auf 16-bit-Adresse setzen (ptr-mode 2, word) */
    async setPtr(addr) {
      this.t.flush();
      const f = new Uint8Array([SYNCH, 0x60 | (2 << 2) | 1, addr & 0xff, (addr >> 8) & 0xff]);
      await this.t.write(f);
      await this.t.read(f.length);
      const ack = await this.t.read(1);
      if (ack[0] !== ACK) throw new Error(`setPtr NACK 0x${hex2(ack[0])}`);
    }

    /* LD *ptr++ (byte), n Bytes am Stueck via REPEAT */
    async ldPtrInc(n) {
      const out = new Uint8Array(n);
      if (n > 1) await this._cmd([SYNCH, 0xa0 | 0, n - 1], 0); // REPEAT n-1
      this.t.flush();
      await this.t.write(new Uint8Array([SYNCH, 0x20 | (1 << 2) | 0])); // LD *ptr++
      await this.t.read(2); // Echo
      const data = await this.t.read(n, 2000);
      out.set(data);
      return out;
    }

    /* Einen ganzen Seitenpuffer (data.length gerade) am Stueck schreiben.
     *
     * ST liefert im Normalbetrieb nach JEDEM Byte ein ACK -- das kollidiert auf
     * der Ein-Draht-Leitung mit den folgenden Datenbytes. Deshalb wie SerialUPDI
     * (megaTinyCore): CTRLA.RSD = 1 (Response Signature Disable), dann
     * REPEAT + ST-Wort + alle Datenwoerter + STCS(CTRLA, RSD=0) in einem Zug,
     * anschliessend nur das Echo lesen (keine ACKs). */
    async stPtrIncBlock(data) {
      const words = data.length >> 1;
      await this.stcs(CS_CTRLA, RSD_ON);                      // RSD an
      await this._cmd([SYNCH, 0xa0 | 0, words - 1], 0);       // REPEAT words-1
      this.t.flush();
      const frame = new Uint8Array(2 + data.length + 3);
      frame[0] = SYNCH;
      frame[1] = 0x60 | (1 << 2) | 1;                         // ST *ptr++ (Wort)
      frame.set(data, 2);
      const tail = 2 + data.length;
      frame[tail] = SYNCH;
      frame[tail + 1] = 0xc0 | CS_CTRLA;                      // STCS CTRLA
      frame[tail + 2] = RSD_OFF;                              // RSD aus
      await this.t.write(frame);
      await this.t.read(frame.length, 3000);                  // nur Echo, keine ACKs
    }

    /* 64-bit-Schluessel senden */
    async key(key8) {
      this.t.flush();
      const f = new Uint8Array(2 + 8);
      f[0] = SYNCH;
      f[1] = 0xe0 | 0x00; // KEY, 64 bit
      f.set(key8, 2);
      await this.t.write(f);
      await this.t.read(f.length);
    }

    async initLink() {
      await this.t.sendBreak();
      await this.stcs(CS_CTRLB, CTRLB_CCDETDIS);
      await this.stcs(CS_CTRLA, CTRLA_IBDLY);
      const sa = await this.ldcs(CS_STATUSA);
      if (sa === 0x00 || sa === 0xff) {
        throw new Error(`keine UPDI-Antwort (STATUSA=0x${hex2(sa)}). Verkabelung/Adapter prüfen.`);
      }
      return sa;
    }
  }

  // ---- NVM-Ablauf --------------------------------------------------
  class NvmProgrammer {
    constructor(updi, log) {
      this.u = updi;
      this.log = log || (() => {});
    }

    async _waitSysBit(mask, want, timeout = 2000) {
      const deadline = Date.now() + timeout;
      for (;;) {
        const s = await this.u.ldcs(CS_ASI_SYS_STATUS);
        if (want ? s & mask : !(s & mask)) return s;
        if (Date.now() > deadline) throw new Error(`ASI_SYS_STATUS Timeout (0x${hex2(s)})`);
        await sleep(10);
      }
    }

    async _waitNvmReady(timeout = 2000) {
      const deadline = Date.now() + timeout;
      for (;;) {
        const st = await this.u.lds8(NVMCTRL_STATUS);
        if (st & NVM_STATUS_WRERROR) throw new Error("NVM WRERROR");
        if (!(st & NVM_STATUS_BUSY)) return;
        if (Date.now() > deadline) throw new Error("NVM busy Timeout");
        await sleep(2);
      }
    }

    async chipErase() {
      this.log("Chip-Erase …");
      await this.u.initLink();
      await this.u.key(KEY_CHIPERASE);
      const ks = await this.u.ldcs(CS_ASI_KEY_STATUS);
      if (!(ks & KEY_STATUS_CHIPERASE)) throw new Error(`Chip-Erase-Key abgelehnt (0x${hex2(ks)})`);
      await this.u.stcs(CS_ASI_RESET_REQ, RESET_REQ);
      await this.u.stcs(CS_ASI_RESET_REQ, RESET_RUN);
      await this._waitSysBit(SYS_STATUS_LOCKSTATUS, false, 3000);
      await sleep(20);
    }

    async enterProgmode() {
      this.log("NVM-Programmiermodus …");
      await this.u.initLink();
      await this.u.key(KEY_NVMPROG);
      const ks = await this.u.ldcs(CS_ASI_KEY_STATUS);
      if (!(ks & KEY_STATUS_NVMPROG)) throw new Error(`NVMPROG-Key abgelehnt (0x${hex2(ks)})`);
      await this.u.stcs(CS_ASI_RESET_REQ, RESET_REQ);
      await this.u.stcs(CS_ASI_RESET_REQ, RESET_RUN);
      await this._waitSysBit(SYS_STATUS_NVMPROG, true, 3000);
    }

    async readDeviceId() {
      const id = [
        await this.u.lds8(SIGROW_DEVICEID + 0),
        await this.u.lds8(SIGROW_DEVICEID + 1),
        await this.u.lds8(SIGROW_DEVICEID + 2),
      ];
      return id;
    }

    async writeFlash(image, onProgress) {
      const pages = Math.ceil(image.length / FLASH_PAGE);
      for (let p = 0; p < pages; p++) {
        const off = p * FLASH_PAGE;
        const chunk = image.subarray(off, off + FLASH_PAGE);
        // leere Seiten (nach Chip-Erase alles 0xFF) ueberspringen
        let allFF = true;
        for (let i = 0; i < chunk.length; i++) if (chunk[i] !== 0xff) { allFF = false; break; }
        if (!allFF) {
          const page = new Uint8Array(FLASH_PAGE).fill(0xff);
          page.set(chunk);
          await this.u.sts8(NVMCTRL_CTRLA, NVM_CMD_PBC);
          await this._waitNvmReady();
          await this.u.setPtr(FLASH_BASE + off);
          await this.u.stPtrIncBlock(page);
          await this.u.sts8(NVMCTRL_CTRLA, NVM_CMD_WP);
          await this._waitNvmReady();
        }
        if (onProgress) onProgress((p + 1) / pages);
      }
    }

    async verifyFlash(image) {
      const total = image.length;
      let addr = 0;
      await this.u.setPtr(FLASH_BASE);
      while (addr < total) {
        const n = Math.min(256, total - addr);
        const got = await this.u.ldPtrInc(n);
        for (let i = 0; i < n; i++) {
          if (got[i] !== image[addr + i]) {
            throw new Error(
              `Verify-Fehler @0x${(addr + i).toString(16)}: 0x${hex2(image[addr + i])} != 0x${hex2(got[i])}`
            );
          }
        }
        addr += n;
      }
    }

    /* Eine Fuse schreiben (NVM v0: ADDR + DATA + WFU).  addr = volle Fuse-Adresse
     * im Datenraum, z. B. 0x1288 fuer BOOTEND. */
    async writeFuse(addr, value) {
      await this._waitNvmReady();
      await this.u.sts8(NVMCTRL_ADDRL, addr & 0xff);
      await this.u.sts8(NVMCTRL_ADDRL + 1, (addr >> 8) & 0xff);
      await this.u.sts8(NVMCTRL_DATAL, value & 0xff);
      await this.u.sts8(NVMCTRL_CTRLA, NVM_CMD_WFU);
      await sleep(2);
      await this._waitNvmReady();
    }
    async readFuse(addr) {
      return this.u.lds8(addr);
    }

    /* Ein EEPROM-Byte schreiben (NVM v0 wie pymcuprog write_eeprom: Seiten-
     * puffer leeren, Byte laden, Erase+Write; beim EEPROM betrifft das nur die
     * geladenen Bytes) und zuruecklesen. */
    async writeEepromByte(offset, value) {
      await this._waitNvmReady();
      await this.u.sts8(NVMCTRL_CTRLA, NVM_CMD_PBC);
      await this._waitNvmReady();
      await this.u.sts8(EEPROM_BASE + offset, value & 0xff);
      await this.u.sts8(NVMCTRL_CTRLA, NVM_CMD_ERWP);
      await this._waitNvmReady();
      const got = await this.u.lds8(EEPROM_BASE + offset);
      if (got !== (value & 0xff)) {
        throw new Error(`EEPROM[${offset}] = 0x${hex2(got)} statt 0x${hex2(value)}`);
      }
    }

    async done() {
      // Reset -> Anwendung startet; danach UPDI abschalten (#41)
      await this.u.stcs(CS_ASI_RESET_REQ, RESET_REQ);
      await this.u.stcs(CS_ASI_RESET_REQ, RESET_RUN);
      await this.u.stcs(CS_CTRLB, CTRLB_UPDIDIS | CTRLB_CCDETDIS);
    }
  }

  // ---- Intel-HEX --------------------------------------------------
  function parseIntelHex(text) {
    let maxAddr = 0;
    let base = 0;
    const bytes = new Map();
    const lines = text.split(/\r?\n/);
    for (const line of lines) {
      if (!line || line[0] !== ":") continue;
      const len = parseInt(line.substr(1, 2), 16);
      const addr = parseInt(line.substr(3, 4), 16);
      const type = parseInt(line.substr(7, 2), 16);
      let sum = 0;
      for (let i = 1; i < 9 + len * 2 + 2; i += 2) sum = (sum + parseInt(line.substr(i, 2), 16)) & 0xff;
      if (sum !== 0) throw new Error("Intel-HEX Prüfsummenfehler");
      if (type === 0x00) {
        for (let i = 0; i < len; i++) {
          const b = parseInt(line.substr(9 + i * 2, 2), 16);
          const a = base + addr + i;
          bytes.set(a, b);
          if (a + 1 > maxAddr) maxAddr = a + 1;
        }
      } else if (type === 0x01) {
        break;
      } else if (type === 0x02) {
        base = parseInt(line.substr(9, 4), 16) << 4;
      } else if (type === 0x04) {
        base = parseInt(line.substr(9, 4), 16) << 16;
      }
      // Typ 03/05 (Startadresse) ignorieren
    }
    if (maxAddr === 0) throw new Error("Intel-HEX enthält keine Daten");
    if (maxAddr > FLASH_SIZE) throw new Error(`Image zu groß (${maxAddr} B > ${FLASH_SIZE} B)`);
    const out = new Uint8Array(maxAddr).fill(0xff);
    for (const [a, b] of bytes) out[a] = b;
    return out;
  }

  /* Zwei geparste Images ueberlagern: b (Anwendung) hat Vorrang, wo != 0xFF. */
  function mergeImages(a, b) {
    const n = Math.max(a.length, b.length);
    const out = new Uint8Array(n).fill(0xff);
    for (let i = 0; i < n; i++) {
      const bv = i < b.length ? b[i] : 0xff;
      out[i] = bv !== 0xff ? bv : (i < a.length ? a[i] : 0xff);
    }
    return out;
  }

  // ---- Orchestrierung -------------------------------------------
  /*
   * opts.mode:
   *   "app"     -> nur die Anwendung schreiben (opts.hex, App @ 0x0000)
   *   "factory" -> Bootloader (opts.bootloaderHex) + Anwendung (opts.appHex,
   *                @ 0x0C00) schreiben und die Fuse BOOTEND = 0x0C setzen
   */
  async function flashDaughterCard(port, opts) {
    opts = opts || {};
    const log = opts.onLog || (() => {});
    const progress = opts.onProgress || (() => {});
    const factory = opts.mode === "factory";

    let image;
    if (factory) {
      image = mergeImages(parseIntelHex(opts.bootloaderHex), parseIntelHex(opts.appHex));
      log(`Werksflash: Bootloader + App, ${image.length} B`);
    } else {
      image = parseIntelHex(opts.hex);
      log(`Anwendung: ${image.length} B`);
    }

    const t = new Transport(port);
    const u = new Updi(t);
    const nvm = new NvmProgrammer(u, log);

    await t.open(BAUD);
    const checkId = async () => {
      const id = await nvm.readDeviceId();
      log(`Geräte-ID: ${id.map(hex2).join(" ")}`);
      if (id[0] !== ATTINY1616_ID[0] || id[1] !== ATTINY1616_ID[1] || id[2] !== ATTINY1616_ID[2]) {
        throw new Error(
          `kein ATtiny1616 (erwartet ${ATTINY1616_ID.map(hex2).join(" ")}). Abbruch, nichts gelöscht.`
        );
      }
    };
    let flashing = false;
    try {
      const sa = await u.initLink();
      log(`UPDI verbunden (STATUSA=0x${hex2(sa)}).`);

      // Geraete-ID vor dem Chip-Erase pruefen (#41): ein falscher Chip wird
      // nicht geloescht. Nur ein gesperrter Chip (LOCKBIT) gibt die ID erst
      // nach dem Erase preis -- das setzt diese Firmware nie.
      const locked = ((await u.ldcs(CS_ASI_SYS_STATUS)) & SYS_STATUS_LOCKSTATUS) !== 0;
      if (locked) {
        log("Chip gesperrt -- ID-Prüfung erst nach dem Chip-Erase möglich.");
      } else {
        await nvm.enterProgmode();
        await checkId();
      }

      await nvm.chipErase();
      await nvm.enterProgmode();
      if (locked) await checkId();

      // Diagnose (#48): EEPROM-Byte 8 ("App gueltig"-Marker des Bootloaders)
      // direkt nach dem Chip-Erase. Mit EESAVE = 1 bleibt das EEPROM stehen;
      // ein alter Marker 0x00 aus einem abgebrochenen Bus-Update liesse den
      // Bootloader trotz gruenem Verify im Update-Wartemodus haengen.
      const eeAfterErase = await u.lds8(EEPROM_BASE + EE_APP_VALID);
      log(`EEPROM[8] nach Chip-Erase: 0x${hex2(eeAfterErase)}` +
          (eeAfterErase === 0xff ? " (geloescht)" : " (nicht geloescht, EESAVE?)"));

      if (factory) {
        // Bis zum erfolgreichen Verify gilt die App als ungueltig: ein Abbruch
        // laesst den Bootloader warten, statt ein halbes Abbild zu starten (#41).
        await nvm.writeEepromByte(EE_APP_VALID, EE_APP_INVALID);
        flashing = true;

        const cur = await nvm.readFuse(FUSE_BOOTEND);
        log(`BOOTEND ist 0x${hex2(cur)} -> setze 0x${hex2(BOOTEND_VALUE)} …`);
        await nvm.writeFuse(FUSE_BOOTEND, BOOTEND_VALUE);
        const chk = await nvm.readFuse(FUSE_BOOTEND);
        if (chk !== BOOTEND_VALUE) throw new Error(`BOOTEND-Fuse nicht gesetzt (0x${hex2(chk)}).`);

        // Brown-out-Detection: bisher nirgends konfiguriert (Werksvorgabe
        // 0x00 = komplett aus). Setzt LVL=BODLEVEL7 (4,2 V, hoechste
        // verfuegbare Stufe) + ACTIVE/SLEEP=Enabled (kontinuierlich) --
        // siehe Herleitung bei BODCFG_VALUE oben.
        const curBod = await u.lds8(FUSE_BODCFG);
        log(`BODCFG ist 0x${hex2(curBod)} -> setze 0x${hex2(BODCFG_VALUE)} …`);
        await nvm.writeFuse(FUSE_BODCFG, BODCFG_VALUE);
        const chkBod = await u.lds8(FUSE_BODCFG);
        if (chkBod !== BODCFG_VALUE) throw new Error(`BODCFG-Fuse nicht gesetzt (0x${hex2(chkBod)}).`);

        // Takt und Watchdog auf die Vorgaben, von denen die Firmware ausgeht
        // (#41): 20 MHz ohne Vorteiler (sonst stimmt keine Baudrate), WDT
        // nicht per Fuse (sonst gesperrt und mit fremder Periode).
        const osc = await nvm.readFuse(FUSE_OSCCFG);
        if ((osc & OSCCFG_FREQSEL_MASK) !== OSCCFG_FREQSEL_20MHZ) {
          const want = (osc & ~OSCCFG_FREQSEL_MASK) | OSCCFG_FREQSEL_20MHZ;
          log(`OSCCFG ist 0x${hex2(osc)} (nicht 20 MHz) -> setze 0x${hex2(want)} …`);
          await nvm.writeFuse(FUSE_OSCCFG, want);
          if ((await nvm.readFuse(FUSE_OSCCFG)) !== want) throw new Error("OSCCFG-Fuse nicht gesetzt.");
        } else {
          log(`OSCCFG 0x${hex2(osc)} (20 MHz) ok.`);
        }
        const wdt = await nvm.readFuse(FUSE_WDTCFG);
        if (wdt !== WDTCFG_VALUE) {
          log(`WDTCFG ist 0x${hex2(wdt)} -> setze 0x${hex2(WDTCFG_VALUE)} …`);
          await nvm.writeFuse(FUSE_WDTCFG, WDTCFG_VALUE);
          if ((await nvm.readFuse(FUSE_WDTCFG)) !== WDTCFG_VALUE) throw new Error("WDTCFG-Fuse nicht gesetzt.");
        } else {
          log("WDTCFG 0x00 ok.");
        }
        // SYSCFG0 nur lesen: RSTPINCFG weg von UPDI waere die einzige echte
        // Aussperrfalle, EESAVE erklaert einen stehengebliebenen EEPROM-Marker.
        const sys0 = await nvm.readFuse(FUSE_SYSCFG0);
        log(`SYSCFG0 0x${hex2(sys0)}: Reset-Pin ${(sys0 & SYSCFG0_RSTPINCFG_MASK) === SYSCFG0_RSTPINCFG_UPDI ? "UPDI" : "(!) nicht UPDI"}, ` +
            `EESAVE ${(sys0 & SYSCFG0_EESAVE) ? "an (EEPROM bleibt beim Erase stehen)" : "aus"}.`);
      }

      log("Schreibe Flash …");
      await nvm.writeFlash(image, progress);
      log("Verifiziere …");
      await nvm.verifyFlash(image);

      if (factory) {
        // Erst jetzt gilt die App als gueltig (Bootloader: 0xFF/0xA5 startbar).
        await nvm.writeEepromByte(EE_APP_VALID, EE_APP_OK);
        flashing = false;
      }
      const eeFinal = await u.lds8(EEPROM_BASE + EE_APP_VALID);
      log(`EEPROM[8] vor dem Neustart: 0x${hex2(eeFinal)}` +
          (eeFinal !== 0x00 ? " (App gilt als gueltig)" : " (!) App gilt als UNGUELTIG -- Karte bleibt im Bootloader"));

      await nvm.done();
      log("Fertig. Die Karte startet neu, UPDI ist aus.");
    } catch (e) {
      if (flashing) {
        // Kein Reset in ein unvollstaendiges Abbild. Nach dem Aus-/Einschalten
        // wartet ein vollstaendig geschriebener Bootloader wegen des Markers.
        log("Abgebrochen: Karte bleibt angehalten (UPDI-Programmiermodus), App als ungueltig markiert. Erneut flashen.");
      }
      throw e;
    } finally {
      await t.close();
    }
  }

  global.KroneUpdi = { flashDaughterCard, parseIntelHex };
})(window);
