/*
 * SerialUPDI im Browser -- schreibt die Modul-Firmware (ATtiny1616) ueber einen
 * USB-Seriell-Adapter (Web Serial API).  EXPERIMENTELL, am Geraet noch nicht
 * verifiziert.  Der abgesicherte Weg bleibt `pio run -e attiny1616 -t upload`.
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
  const CTRLA_IBDLY = 1 << 7;    // 0x80 -- Inter-Byte-Delay

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
  const NVM_CMD_WP = 0x01;   // write page
  const NVM_CMD_ERWP = 0x03; // erase+write page
  const NVM_CMD_PBC = 0x04;  // page buffer clear
  const NVM_CMD_CHER = 0x05; // chip erase
  const NVM_STATUS_BUSY = 0x03; // FBUSY | EEBUSY
  const NVM_STATUS_WRERROR = 0x04;

  const SIGROW_DEVICEID = 0x1100;
  const FLASH_BASE = 0x8000;
  const FLASH_PAGE = 64;
  const FLASH_SIZE = 0x4000; // 16 KiB

  const ATTINY1616_ID = [0x1e, 0x94, 0x22];

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

    /* UPDI-BREAK: bei 300 Bd zwei Nullbytes -> Leitung lange genug low */
    async sendBreak() {
      await this.reopen(BREAK_BAUD);
      await this.write(new Uint8Array([0x00, 0x00]));
      await sleep(60);
      await this.reopen(BAUD);
      await sleep(2);
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

    /* ST *ptr++ (byte) fuer einen ganzen Block; jede ST liefert ein ACK */
    async stPtrIncBlock(data) {
      const n = data.length;
      if (n > 1) await this._cmd([SYNCH, 0xa0 | 0, n - 1], 0); // REPEAT n-1
      this.t.flush();
      // ST-Opcode + alle Datenbytes zusammen senden
      const frame = new Uint8Array(2 + n);
      frame[0] = SYNCH;
      frame[1] = 0x60 | (1 << 2) | 0; // ST *ptr++ byte
      frame.set(data, 2);
      await this.t.write(frame);
      await this.t.read(frame.length, 3000); // Echo
      const acks = await this.t.read(n, 3000);
      for (let i = 0; i < n; i++) {
        if (acks[i] !== ACK) throw new Error(`ST-Block NACK @${i} 0x${hex2(acks[i])}`);
      }
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

    async done() {
      // Reset -> Anwendung startet
      await this.u.stcs(CS_ASI_RESET_REQ, RESET_REQ);
      await this.u.stcs(CS_ASI_RESET_REQ, RESET_RUN);
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

  // ---- Orchestrierung -------------------------------------------
  async function flashDaughterCard(port, hexText, opts) {
    opts = opts || {};
    const log = opts.onLog || (() => {});
    const progress = opts.onProgress || (() => {});

    const image = parseIntelHex(hexText);
    log(`Firmware: ${image.length} B, ${Math.ceil(image.length / FLASH_PAGE)} Seiten`);

    const t = new Transport(port);
    const u = new Updi(t);
    const nvm = new NvmProgrammer(u, log);

    await t.open(BAUD);
    try {
      const sa = await u.initLink();
      log(`UPDI verbunden (STATUSA=0x${hex2(sa)}).`);

      await nvm.chipErase();
      await nvm.enterProgmode();

      const id = await nvm.readDeviceId();
      log(`Geräte-ID: ${id.map(hex2).join(" ")}`);
      if (id[0] !== ATTINY1616_ID[0] || id[1] !== ATTINY1616_ID[1] || id[2] !== ATTINY1616_ID[2]) {
        throw new Error(
          `kein ATtiny1616 (erwartet ${ATTINY1616_ID.map(hex2).join(" ")}). Abbruch.`
        );
      }

      log("Schreibe Flash …");
      await nvm.writeFlash(image, progress);
      log("Verifiziere …");
      await nvm.verifyFlash(image);
      await nvm.done();
      log("Fertig. Die Karte startet neu.");
    } finally {
      await t.close();
    }
  }

  global.KroneUpdi = { flashDaughterCard, parseIntelHex };
})(window);
