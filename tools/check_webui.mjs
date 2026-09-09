/*
 * Prueft die Web-UI der Zentralsteuerung (INDEX_HTML in firmware/master/src/main.cpp):
 *
 *   1. Syntax des Inline-<script> (vm.Script -- keine Abhaengigkeit)
 *   2. Optionaler Laufzeit-Smoke-Test mit jsdom: Seite laden, /api/* mocken,
 *      pruefen dass kein Fehler fliegt und die Navigation reagiert.
 *
 *   node tools/check_webui.mjs
 *
 * Fuer (2) muss jsdom installiert sein (CI: npm i jsdom@24). Fehlt es, wird nur
 * (1) geprueft und mit Hinweis weitergemacht.
 */
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import vm from "node:vm";

const repo = join(dirname(fileURLToPath(import.meta.url)), "..");
const src = readFileSync(join(repo, "firmware/master/src/main.cpp"), "utf8");

const m = src.match(/R"HTML\(([\s\S]*?)\)HTML"/);
if (!m) {
  console.error("INDEX_HTML (R\"HTML(...)HTML\") nicht gefunden.");
  process.exit(1);
}
const html = m[1];

// ---- 1) Syntax jedes Inline-<script> ---------------------------------
const scripts = [...html.matchAll(/<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/g)].map((x) => x[1]);
if (scripts.length === 0) {
  console.error("Kein Inline-<script> gefunden.");
  process.exit(1);
}
for (const [i, code] of scripts.entries()) {
  try {
    new vm.Script(code, { filename: `INDEX_HTML script #${i + 1}` });
  } catch (e) {
    console.error(`SYNTAXFEHLER im Inline-Script #${i + 1}: ${e.message}`);
    // Zeile zeigen
    const mm = String(e.stack).match(/INDEX_HTML script #\d+:(\d+)/);
    if (mm) {
      const ln = +mm[1];
      code.split("\n").slice(Math.max(0, ln - 2), ln + 1).forEach((l, k) =>
        console.error(`${Math.max(1, ln - 1) + k}  ${l}`)
      );
    }
    process.exit(1);
  }
}
console.log(`Syntax OK (${scripts.length} Inline-Script${scripts.length === 1 ? "" : "s"}).`);

// ---- 2) Laufzeit-Smoke mit jsdom ------------------------------------
let JSDOM;
for (const spec of ["jsdom", process.env.NODE_PATH ? join(process.env.NODE_PATH, "jsdom/lib/api.js") : null].filter(Boolean)) {
  try {
    ({ JSDOM } = await import(spec));
    break;
  } catch { /* naechsten Pfad versuchen */ }
}
if (!JSDOM) {
  console.log("jsdom nicht installiert -- Laufzeit-Smoke uebersprungen (npm i jsdom@24).");
  process.exit(0);
}

const API = {
  "/api/config": { module_count: 0, mqtt_port: 1883, sep: ".", net_scope: 1, admin_user: "admin", tz: "CET-1CEST,M3.5.0,M10.5.0/3" },
  "/api/system": { uptime_s: 1, heap_free: 2e5, heap_total: 3e5, detected: 0, field_width: 0, auto_modules: true, enum_busy: true, ssid: "x", ip: "10.0.0.2" },
  "/api/status": { mode: "clock_hm", modules: [], detected: 0, enum_busy: true, time_valid: false },
  "/api/log": { seq: 0, entries: [] },
  "/api/module/firmware": { ok: false, ver: 0, modules: [] },
  "/api/module/update/status": { busy: false, results: [] },
};

// externe <script src> entfernen, updi.js-Global stubben
const page = html
  .replace(/<script[^>]*\bsrc=[^>]*><\/script>/g, "")
  .replace("</head>", `<script>
    window.KroneUpdi = { parseIntelHex: () => new Uint8Array(1), flashDaughterCard: async () => {} };
    customElements.define('esp-web-install-button', class extends HTMLElement {});
  </script></head>`);

const errors = [];
const { VirtualConsole } = await import(process.env.NODE_PATH
  ? join(process.env.NODE_PATH, "jsdom/lib/api.js") : "jsdom");
const vc = new VirtualConsole();               // jsdom-Rauschen (scrollTo …) schlucken
const dom = new JSDOM(page, {
  runScripts: "dangerously",
  url: "https://localhost/",
  virtualConsole: vc,
  beforeParse(win) {
    win.fetch = async (u, o) => {
      const path = String(u).replace(/^\.?/, "").split("?")[0];
      const body = (o && o.method === "POST") ? { ok: true } : (API[path] ?? {});
      return { ok: true, status: 200, json: async () => body, text: async () => JSON.stringify(body) };
    };
    win.addEventListener("error", (e) => errors.push(e.error?.stack || e.message));
    win.addEventListener("unhandledrejection", (e) => errors.push("unhandledrejection: " + (e.reason?.stack || e.reason)));
  },
});

await new Promise((r) => setTimeout(r, 400));

const doc = dom.window.document;
if (errors.length) {
  console.error("LAUFZEITFEHLER beim Laden:\n" + errors.join("\n---\n"));
  process.exit(1);
}

// Navigation muss reagieren
const navBtns = [...doc.querySelectorAll("#nav button")];
if (navBtns.length < 3) {
  console.error(`Navigation nicht aufgebaut (${navBtns.length} Buttons).`);
  process.exit(1);
}
const setBtn = navBtns.find((b) => b.dataset.v === "set");
setBtn.dispatchEvent(new dom.window.MouseEvent("click", { bubbles: true }));
await new Promise((r) => setTimeout(r, 200));
if (!doc.querySelector('.view[data-v=set]').classList.contains("on")) {
  console.error("Klick auf 'Einstellungen' hat die Ansicht nicht umgeschaltet.");
  process.exit(1);
}
if (errors.length) {
  console.error("LAUFZEITFEHLER nach Interaktion:\n" + errors.join("\n---\n"));
  process.exit(1);
}

console.log("Laufzeit-Smoke OK (Navigation reagiert, keine Fehler).");
dom.window.close();
process.exit(0);
