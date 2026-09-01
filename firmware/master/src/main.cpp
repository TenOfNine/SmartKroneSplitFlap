/*
 * Zentralsteuerung der KRONE-REW-Fallblattanzeige (ESP32-C3 Super Mini).
 *
 * Bindet die hardwareunabhaengigen, host-getesteten Bibliotheken an die
 * ESP32-Peripherie:
 *   lib/protocol     Rahmen/CRC (mit der Modul-Firmware geteilt)
 *   lib/busmaster    Master-Protokollseite, Enumeration, Modul-Statustabelle
 *   lib/charmap      Zeichenabbildung Text -> Fallblatt
 *   lib/clocktext    Uhrzeit -> Text
 *   lib/masterapp    Betriebsarten, Auto-Rueckfall Sekundenanzeige, Status-JSON
 *   lib/hadiscovery  Home-Assistant-MQTT-Auto-Discovery
 *   lib/eventlog     Ereignis-Ringpuffer fuer den Log-Tab der Web-UI
 *
 * Abweichungen von Spezifikation 7.2 (dependency-arm, siehe docs/toolchain.md):
 *   Web-UI/REST  -> eingebauter WebServer statt ESPAsyncWebServer
 *   Konfig       -> Preferences (NVS) statt LittleFS
 *   Captive Portal-> WiFiManager (unveraendert)
 *
 * Pin-/UART-Belegung: platformio.ini (build_flags) / docs/schaltplan-master.md.
 * Bezug: docs/spezifikation.md Kapitel 7.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>

#include "driver/uart.h"
#include "esp_freertos_hooks.h"

extern "C" {
#include "busmaster.h"
#include "charmap.h"
#include "eventlog.h"
#include "hadiscovery.h"
#include "masterapp.h"
#include "protocol.h"
}

/* --- Hardware --------------------------------------------------------- */
#ifndef RS485_UART_NUM
#  define RS485_UART_NUM 1
#endif
#ifndef PIN_RS485_RX
#  define PIN_RS485_RX 4
#endif
#ifndef PIN_RS485_TX
#  define PIN_RS485_TX 3
#endif
#ifndef PIN_RS485_DE
#  define PIN_RS485_DE 10
#endif
#ifndef PIN_CHAIN
#  define PIN_CHAIN 5
#endif
#ifndef PIN_STATUS_LED
#  define PIN_STATUS_LED 6
#endif

static const uart_port_t  RS485_UART   = static_cast<uart_port_t>(RS485_UART_NUM);
static constexpr int      RS485_RX_PIN = PIN_RS485_RX;
static constexpr int      RS485_TX_PIN = PIN_RS485_TX;
static constexpr int      RS485_DE_PIN = PIN_RS485_DE;
static constexpr int      CHAIN_PIN    = PIN_CHAIN;
static constexpr int      STATUS_LED   = PIN_STATUS_LED;
static constexpr uint32_t BUS_BAUD     = 115200;

static const char FW_BUILD[] = __DATE__ " " __TIME__;

/* --- Zustand -------------------------------------------------------- */

static Preferences   prefs;
static WebServer     web(80);
static WiFiClient    net;
static PubSubClient  mqtt(net);

static busmaster_t   g_bus;
static masterapp_t   g_app;
static evlog_t       g_log;

struct Settings {
    char     mqtt_host[64]  = "";
    uint16_t mqtt_port      = 1883;
    char     mqtt_user[32]  = "";
    char     mqtt_pass[32]  = "";
    char     base_topic[48] = "krone/anzeige";
    char     node_id[32]    = "krone_anzeige";
    uint8_t  module_count   = 10;
    uint32_t hms_timeout_s  = 600;

    char     ntp_server[48] = "pool.ntp.org";
    char     tz[48]         = "CET-1CEST,M3.5.0,M10.5.0/3";
    bool     ntp_enabled    = true;

    char     sep            = '.';
    uint8_t  align          = CHARMAP_ALIGN_CENTER;

    bool     use_static     = false;
    char     ip[16]   = "";
    char     mask[16] = "";
    char     gw[16]   = "";
    char     dns[16]  = "";

    bool     mqtt_enabled   = true;
    bool     api_write      = true;
    bool     ota_enabled    = true;
    bool     mdns_enabled   = true;
} cfg;

static uint32_t last_poll_ms;
static uint8_t  poll_addr = 1;
static uint32_t last_time_ms;
static uint32_t last_mqtt_try;

/* --- grobe CPU-Last ueber den FreeRTOS-Idle-Hook -------------------- */
static volatile uint32_t g_idle_ticks = 0;
static uint32_t g_idle_last = 0;
static uint32_t g_idle_peak = 1;
static uint32_t g_idle_sample_ms = 0;
static uint8_t  g_cpu_load = 0;

static bool idle_hook()
{
    g_idle_ticks++;
    return true;
}

static void cpu_load_tick(uint32_t now)
{
    if (now - g_idle_sample_ms < 1000u) {
        return;
    }
    g_idle_sample_ms = now;
    const uint32_t t = g_idle_ticks;
    const uint32_t d = t - g_idle_last;   /* Wrap-around ist unkritisch */
    g_idle_last = t;
    if (d > g_idle_peak) {
        g_idle_peak = d;                  /* Selbstkalibrierung: Maximum = ~100 % frei */
    }
    g_cpu_load = (g_idle_peak > d) ? (uint8_t)(100u * (g_idle_peak - d) / g_idle_peak) : 0u;
}

/* WLAN-Wechsel mit Rueckfall auf das alte Netz */
static bool     wifi_switching = false;
static uint32_t wifi_switch_ms = 0;
static String   wifi_old_ssid, wifi_old_psk;

/* Portal-Anforderung aus der UI (im loop bearbeitet, nicht im Handler) */
static bool     want_portal = false;
static bool     want_reboot = false;
static uint32_t reboot_at = 0;


/* ================================================================== */
/* Bus-Transport                                                       */
/* ================================================================== */

static void bus_tx(void *, const uint8_t *data, size_t len)
{
    uart_write_bytes(RS485_UART, reinterpret_cast<const char *>(data), len);
    uart_wait_tx_done(RS485_UART, pdMS_TO_TICKS(20));
    uart_flush_input(RS485_UART);
}

static void bus_begin()
{
    uart_config_t uc = {};
    uc.baud_rate = BUS_BAUD;
    uc.data_bits = UART_DATA_8_BITS;
    uc.parity    = UART_PARITY_DISABLE;
    uc.stop_bits = UART_STOP_BITS_1;
    uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_driver_install(RS485_UART, 512, 0, 0, nullptr, 0);
    uart_param_config(RS485_UART, &uc);
    uart_set_pin(RS485_UART, RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN,
                 UART_PIN_NO_CHANGE);
    uart_set_mode(RS485_UART, UART_MODE_RS485_HALF_DUPLEX);

    pinMode(CHAIN_PIN, OUTPUT);
    digitalWrite(CHAIN_PIN, LOW);
    if (STATUS_LED >= 0) {
        pinMode(STATUS_LED, OUTPUT);
        digitalWrite(STATUS_LED, LOW);
    }
}

static void bus_pump(uint32_t now)
{
    uint8_t b;
    while (uart_read_bytes(RS485_UART, &b, 1, 0) == 1) {
        busmaster_on_rx_byte(&g_bus, b, now);
    }
    /* CHAIN ist high-aktiv; der 74LVC1G17 hebt 3,3 V -> 5 V nicht invertierend. */
    digitalWrite(CHAIN_PIN, g_bus.chain_active ? HIGH : LOW);
}

/* --- Status-LED (D1 an GPIO6) ------------------------------ */

static void status_led_tick(uint32_t now)
{
    if (STATUS_LED < 0) {
        return;
    }
    const uint8_t count = g_bus.module_count ? g_bus.module_count : cfg.module_count;
    bool trouble = (WiFi.status() != WL_CONNECTED);
    for (uint8_t i = 0; i < count && !trouble; ++i) {
        if (!g_bus.mod[i].online || g_bus.mod[i].fehler != 0) {
            trouble = true;
        }
    }
    bool on;
    if (WiFi.status() != WL_CONNECTED) {
        on = (now / 125) & 1;
    } else if (trouble) {
        on = (now / 500) & 1;
    } else {
        on = true;
    }
    digitalWrite(STATUS_LED, on ? HIGH : LOW);
}

/* ================================================================== */
/* Einstellungen (NVS)                                                 */
/* ================================================================== */

static void settings_load()
{
    prefs.begin("krone", true);
    prefs.getString("mqtt_host", cfg.mqtt_host, sizeof(cfg.mqtt_host));
    cfg.mqtt_port = prefs.getUShort("mqtt_port", cfg.mqtt_port);
    prefs.getString("mqtt_user", cfg.mqtt_user, sizeof(cfg.mqtt_user));
    prefs.getString("mqtt_pass", cfg.mqtt_pass, sizeof(cfg.mqtt_pass));
    prefs.getString("base_topic", cfg.base_topic, sizeof(cfg.base_topic));
    prefs.getString("node_id", cfg.node_id, sizeof(cfg.node_id));
    cfg.module_count  = prefs.getUChar("modules", cfg.module_count);
    cfg.hms_timeout_s = prefs.getULong("hms_to", cfg.hms_timeout_s);

    prefs.getString("ntp_server", cfg.ntp_server, sizeof(cfg.ntp_server));
    prefs.getString("tz", cfg.tz, sizeof(cfg.tz));
    cfg.ntp_enabled = prefs.getBool("ntp_en", cfg.ntp_enabled);

    { char s[2]; s[0] = cfg.sep; s[1] = 0;
      prefs.getString("sep", s, sizeof(s)); cfg.sep = s[0] ? s[0] : '.'; }
    cfg.align = prefs.getUChar("align", cfg.align);

    cfg.use_static = prefs.getBool("ip_static", cfg.use_static);
    prefs.getString("ip", cfg.ip, sizeof(cfg.ip));
    prefs.getString("mask", cfg.mask, sizeof(cfg.mask));
    prefs.getString("gw", cfg.gw, sizeof(cfg.gw));
    prefs.getString("dns", cfg.dns, sizeof(cfg.dns));

    cfg.mqtt_enabled = prefs.getBool("mqtt_en", cfg.mqtt_enabled);
    cfg.api_write    = prefs.getBool("api_write", cfg.api_write);
    cfg.ota_enabled  = prefs.getBool("ota_en", cfg.ota_enabled);
    cfg.mdns_enabled = prefs.getBool("mdns_en", cfg.mdns_enabled);
    prefs.end();
}

static void settings_save()
{
    prefs.begin("krone", false);
    prefs.putString("mqtt_host", cfg.mqtt_host);
    prefs.putUShort("mqtt_port", cfg.mqtt_port);
    prefs.putString("mqtt_user", cfg.mqtt_user);
    prefs.putString("mqtt_pass", cfg.mqtt_pass);
    prefs.putString("base_topic", cfg.base_topic);
    prefs.putString("node_id", cfg.node_id);
    prefs.putUChar("modules", cfg.module_count);
    prefs.putULong("hms_to", cfg.hms_timeout_s);
    prefs.putString("ntp_server", cfg.ntp_server);
    prefs.putString("tz", cfg.tz);
    prefs.putBool("ntp_en", cfg.ntp_enabled);
    { char s[2] = { cfg.sep, 0 }; prefs.putString("sep", s); }
    prefs.putUChar("align", cfg.align);
    prefs.putBool("ip_static", cfg.use_static);
    prefs.putString("ip", cfg.ip);
    prefs.putString("mask", cfg.mask);
    prefs.putString("gw", cfg.gw);
    prefs.putString("dns", cfg.dns);
    prefs.putBool("mqtt_en", cfg.mqtt_enabled);
    prefs.putBool("api_write", cfg.api_write);
    prefs.putBool("ota_en", cfg.ota_enabled);
    prefs.putBool("mdns_en", cfg.mdns_enabled);
    prefs.end();
}

static void apply_time_config()
{
    if (cfg.ntp_enabled && strlen(cfg.ntp_server) > 0) {
        configTzTime(cfg.tz, cfg.ntp_server, "time.nist.gov");
    } else {
        setenv("TZ", cfg.tz, 1);
        tzset();
    }
}

static void apply_static_ip()
{
    if (!cfg.use_static) {
        return;
    }
    IPAddress ip, gw, mask, dns;
    if (ip.fromString(cfg.ip) && gw.fromString(cfg.gw) && mask.fromString(cfg.mask)) {
        dns.fromString(strlen(cfg.dns) ? cfg.dns : cfg.gw);
        WiFi.config(ip, gw, mask, dns);
    }
}

/* ================================================================== */
/* Betriebsart / Ausrichtung aus/zu String                            */
/* ================================================================== */

static app_mode_t mode_from_string(const char *s)
{
    if (!strcmp(s, "clock_hm"))  return APP_MODE_CLOCK_HM;
    if (!strcmp(s, "clock_hms")) return APP_MODE_CLOCK_HMS;
    if (!strcmp(s, "blank"))     return APP_MODE_BLANK;
    if (!strcmp(s, "off"))       return APP_MODE_OFF;
    return APP_MODE_TEXT;
}
static charmap_align_t align_from(uint8_t a)
{
    return a == 0 ? CHARMAP_ALIGN_LEFT
         : a == 2 ? CHARMAP_ALIGN_RIGHT
                  : CHARMAP_ALIGN_CENTER;
}

/* ================================================================== */
/* Web-UI (eine PROGMEM-Seite, System-Fonts, kein CDN)                 */
/* ================================================================== */

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html><html lang=de><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>KRONE REW Zentralsteuerung</title><style>
:root{--bg:#0b0c0e;--panel:#131519;--p2:#1a1d22;--p3:#22262c;--line:#262a31;--ls:#1e2127;
--ink:#e8e6e1;--dim:#8b8f98;--faint:#5b606a;--amber:#f2b03d;--amberd:#b98428;--ai:#17120a;
--ok:#56b877;--warn:#e0a63a;--err:#e5675c;--cool:#6ea8d8;--r:10px;--rs:6px;
--sans:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
--mono:ui-monospace,"SF Mono","Cascadia Mono","Roboto Mono",Menlo,Consolas,monospace}
*{box-sizing:border-box}html,body{height:100%}
body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.55 var(--sans);-webkit-font-smoothing:antialiased}
h1,h2,h3{margin:0;font-weight:600;letter-spacing:-.01em}button,input,select{font:inherit;color:inherit}
a{color:var(--cool)}::selection{background:var(--amber);color:var(--ai)}
.lbl{font:600 10.5px/1 var(--mono);letter-spacing:.14em;text-transform:uppercase;color:var(--faint)}
.app{display:grid;grid-template-columns:210px 1fr;min-height:100%}
.side{background:var(--panel);border-right:1px solid var(--line);display:flex;flex-direction:column;padding:18px 12px;gap:4px;position:sticky;top:0;height:100vh}
.brand{padding:6px 10px 16px}.brand .k{font:700 15px/1.2 var(--sans);letter-spacing:.02em}.brand .s{font:11px/1.3 var(--mono);color:var(--faint)}
.nav{display:flex;flex-direction:column;gap:2px;margin-top:6px}
.nav button{display:flex;align-items:center;gap:10px;width:100%;background:0;border:0;cursor:pointer;text-align:left;padding:9px 10px;border-radius:var(--rs);color:var(--dim);font-weight:500}
.nav button:hover{background:var(--p2);color:var(--ink)}.nav button.on{background:var(--p3);color:var(--ink)}
.nav b{width:6px;height:6px;border-radius:2px;background:transparent;flex:none}.nav button.on b{background:var(--amber)}
.side .foot{margin-top:auto;padding:10px;font:11px/1.6 var(--mono);color:var(--faint)}
.dot{width:7px;height:7px;border-radius:50%;display:inline-block;vertical-align:middle;background:var(--faint)}
.dot.ok{background:var(--ok)}.dot.err{background:var(--err)}.dot.warn{background:var(--warn)}
.main{min-width:0;display:flex;flex-direction:column}
.topbar{display:flex;align-items:center;gap:18px;flex-wrap:wrap;padding:14px 24px;border-bottom:1px solid var(--line);background:linear-gradient(var(--panel),var(--bg));position:sticky;top:0;z-index:5}
.topbar h1{font-size:16px}.topbar .meta{display:flex;gap:16px;margin-left:auto;flex-wrap:wrap}
.topbar .meta div{font:12px/1.3 var(--mono);color:var(--dim)}.topbar .meta b{color:var(--ink);font-weight:600}
.view{padding:24px;display:none;max-width:1040px}.view.on{display:block}
.grid{display:grid;gap:14px}.c2{grid-template-columns:repeat(2,1fr)}.c3{grid-template-columns:repeat(3,1fr)}.c6{grid-template-columns:repeat(6,1fr)}
.card{background:var(--panel);border:1px solid var(--line);border-radius:var(--r);padding:16px}
.card>.lbl{display:block;margin-bottom:10px}
.tile .v{font:600 22px/1.1 var(--mono);margin-top:6px}.tile .sub{font:11px/1.3 var(--mono);color:var(--faint);margin-top:3px}
.tile .v.ok{color:var(--ok)}.tile .v.err{color:var(--err)}.tile .v.warn{color:var(--warn)}
.flaps{display:flex;gap:6px;flex-wrap:wrap}.flap{width:46px}
.flap .cell{position:relative;height:56px;border-radius:4px;background:#0f1113;border:1px solid #2a2e35;overflow:hidden;display:flex;align-items:center;justify-content:center}
.flap .cell::after{content:"";position:absolute;left:0;right:0;top:50%;height:2px;background:var(--bg);box-shadow:0 1px 0 rgba(0,0,0,.6)}
.flap .ch{font:700 24px/1 var(--mono);color:var(--amber);text-shadow:0 0 12px rgba(242,176,61,.35)}
.flap.mv .cell{border-color:var(--warn)}.flap.hm .cell{border-color:var(--warn);animation:pl 1.4s ease-in-out infinite}
.flap.er .cell{border-color:var(--err)}.flap.er .ch{color:var(--err);text-shadow:none}
.flap.of .cell{opacity:.4}.flap.of .ch{color:var(--faint);text-shadow:none}
.flap .tag{display:flex;justify-content:space-between;align-items:center;margin-top:5px;font:600 10px/1 var(--mono);color:var(--faint)}
.flap .tag i{width:6px;height:6px;border-radius:50%;background:var(--ok);display:block}
.flap.mv .tag i,.flap.hm .tag i{background:var(--warn)}.flap.er .tag i{background:var(--err)}.flap.of .tag i{background:var(--faint)}
@keyframes pl{0%,100%{opacity:1}50%{opacity:.55}}@media(prefers-reduced-motion){.flap.hm .cell{animation:none}}
.seg{display:inline-flex;background:var(--p2);border:1px solid var(--line);border-radius:var(--rs);padding:3px;gap:2px;flex-wrap:wrap}
.seg button{background:0;border:0;cursor:pointer;padding:7px 13px;border-radius:4px;color:var(--dim);font-weight:500}
.seg button:hover{color:var(--ink)}.seg button.on{background:var(--amber);color:var(--ai);font-weight:600}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.row.mt{margin-top:12px}
input[type=text],input[type=password],input[type=number],input[type=datetime-local],select{background:var(--p2);border:1px solid var(--line);border-radius:var(--rs);padding:9px 11px;width:100%;outline:0}
input:focus,select:focus{border-color:var(--amberd)}
.field{display:flex;flex-direction:column;gap:6px}.field .lbl{color:var(--dim)}
.btn{background:var(--p3);border:1px solid var(--line);border-radius:var(--rs);padding:9px 15px;cursor:pointer;font-weight:600;color:var(--ink)}
.btn:hover{border-color:var(--faint);background:var(--p2)}
.btn.primary{background:var(--amber);border-color:var(--amber);color:var(--ai)}.btn.primary:hover{background:#ffbe52}
.btn.ghost{background:0}.btn.danger{color:var(--err);border-color:#5a3630}.btn.danger:hover{background:rgba(229,103,92,.12)}
.btn.sm{padding:6px 10px;font-size:12.5px}
.pill{display:inline-flex;align-items:center;gap:6px;padding:3px 9px;border-radius:20px;font:600 11px/1.4 var(--mono);letter-spacing:.03em;text-transform:uppercase;background:var(--p3);color:var(--dim)}
.pill i{width:6px;height:6px;border-radius:50%;background:currentColor;display:block}
.pill.ok{color:var(--ok);background:rgba(86,184,119,.14)}.pill.warn{color:var(--warn);background:rgba(224,166,58,.14)}
.pill.err{color:var(--err);background:rgba(229,103,92,.14)}.pill.mute{color:var(--faint)}
.tw{overflow-x:auto;border:1px solid var(--line);border-radius:var(--r)}
table{border-collapse:collapse;width:100%;font-size:13px}
thead th{position:sticky;top:0;background:var(--p2);text-align:left;padding:10px 12px;font:600 10.5px/1 var(--mono);letter-spacing:.1em;text-transform:uppercase;color:var(--faint);border-bottom:1px solid var(--line)}
tbody td{padding:11px 12px;border-bottom:1px solid var(--ls);font-variant-numeric:tabular-nums}
tbody tr:last-child td{border-bottom:0}tbody tr:hover{background:var(--p2)}
td.mono{font-family:var(--mono)}td .cm{font:700 14px/1 var(--mono);color:var(--amber)}
.ra{display:flex;gap:6px;opacity:0}tr:hover .ra{opacity:1}
.log{display:flex;flex-direction:column}
.log .e{display:grid;grid-template-columns:82px 74px 1fr;gap:12px;align-items:baseline;padding:9px 4px;border-bottom:1px solid var(--ls);font-size:13px}
.log .e:last-child{border-bottom:0}.log .e time{font:12px/1.4 var(--mono);color:var(--faint)}
.log .e .who{color:var(--faint);font-family:var(--mono);font-size:12px}
.wifi{display:flex;flex-direction:column;border:1px solid var(--line);border-radius:var(--r);overflow:hidden}
.wifi .n{display:flex;align-items:center;gap:12px;padding:11px 14px;cursor:pointer;border:0;border-bottom:1px solid var(--ls);background:0;width:100%;text-align:left;color:var(--ink)}
.wifi .n:last-child{border-bottom:0}.wifi .n:hover{background:var(--p2)}.wifi .n.sel{background:rgba(242,176,61,.1)}
.wifi .ssid{font-weight:600;flex:1}.wifi .bars{display:flex;gap:2px;align-items:flex-end;height:14px}
.wifi .bars i{width:3px;background:var(--faint);border-radius:1px}.wifi .bars i.on{background:var(--ok)}
.wifi .lock{color:var(--faint);font-size:12px}
.sect{border-top:1px solid var(--line);padding-top:18px;margin-top:18px}.sect:first-child{border-top:0;padding-top:0;margin-top:0}
.sect h3{font-size:13px;margin-bottom:12px;display:flex;align-items:center;gap:10px}
.kv{display:grid;grid-template-columns:150px 1fr;gap:8px 16px;font-size:13px}.kv dt{color:var(--dim)}.kv dd{margin:0;font-family:var(--mono)}
.hint{font-size:12px;color:var(--faint);margin-top:8px}.hint.warn{color:var(--warn)}
code{font:.88em var(--mono);background:var(--p2);border:1px solid var(--line);border-radius:5px;padding:.1em .4em}
.switch{position:relative;width:38px;height:22px;flex:none}
.switch input{position:absolute;inset:0;opacity:0;margin:0;cursor:pointer}
.switch .t{position:absolute;inset:0;background:var(--p3);border:1px solid var(--line);border-radius:20px;transition:.15s}
.switch .t::after{content:"";position:absolute;left:2px;top:2px;width:16px;height:16px;border-radius:50%;background:var(--dim);transition:.15s}
.switch input:checked+.t{background:rgba(86,184,119,.28);border-color:var(--ok)}
.switch input:checked+.t::after{left:18px;background:var(--ok)}
.trow{display:flex;align-items:flex-start;gap:14px;padding:12px 0;border-top:1px solid var(--ls)}
.trow:first-of-type{border-top:0}.trow .tx{flex:1;min-width:0}.trow .tx b{font-weight:600;display:block}.trow .tx span{font-size:12px;color:var(--faint)}
.collapse{margin-top:12px;padding-left:14px;border-left:2px solid var(--line)}.collapse[hidden]{display:none}
#toast{position:fixed;left:50%;bottom:26px;transform:translateX(-50%) translateY(20px);opacity:0;transition:.2s;background:var(--p3);border:1px solid var(--line);border-radius:8px;padding:10px 18px;font:13px var(--mono);pointer-events:none;z-index:20}
@media(max-width:820px){.app{grid-template-columns:1fr}.side{position:static;height:auto;flex-direction:row;align-items:center;overflow-x:auto}
.brand{padding:6px 8px}.nav{flex-direction:row}.side .foot{display:none}.c2,.c3{grid-template-columns:1fr}.c6{grid-template-columns:repeat(3,1fr)}}
</style></head><body><div class=app>
<aside class=side>
<div class=brand><div class=k>KRONE REW</div><div class=s>Zentralsteuerung</div></div>
<nav class=nav id=nav>
<button data-v=dash class=on><b></b>Übersicht</button>
<button data-v=mods><b></b>Module</button>
<button data-v=log><b></b>Log</button>
<button data-v=set><b></b>Einstellungen</button>
</nav>
<div class=foot id=foot><span class="dot"></span> …</div>
</aside>
<div class=main>
<div class=topbar><h1 id=crumb>Übersicht</h1>
<div class=meta id=meta></div></div>

<section class="view on" data-v=dash><div class=grid>
<div class=card><span class=lbl>Anzeige</span>
<div class=row><div class=seg id=modeseg>
<button data-m=text class=on>Text</button><button data-m=clock_hm>Uhr hh:mm</button>
<button data-m=clock_hms>hh:mm:ss</button><button data-m=blank>Leer</button><button data-m=off>Aus</button>
</div></div>
<div class="row mt" id=textrow>
<input type=text id=txt placeholder=Anzeigetext style=max-width:340px>
<div class=seg id=alignseg><button data-a=0>links</button><button data-a=1 class=on>zentriert</button><button data-a=2>rechts</button></div>
<button class="btn primary" id=sendbtn>Senden</button></div>
<div class="row mt"><span class=lbl style=align-self:center>Aktuell auf den Modulen</span></div>
<div class="flaps mt" id=flaps></div></div>
<div class="grid c6" id=tiles></div>
<div class=card><span class=lbl>Schnellaktionen</span><div class=row id=quick>
<button class=btn data-q=home>Homing alle</button>
<button class=btn data-q=selftest>Selbsttest</button>
<button class=btn data-q=stop>Stop alle</button>
<button class="btn ghost" data-q=wifi>WLAN wechseln →</button></div></div>
</div></section>

<section class=view data-v=mods>
<div class=row style="justify-content:space-between;margin-bottom:14px">
<h2 id=modsum style=margin:0>…</h2>
<button class="btn sm" id=enumbtn>Enumeration neu starten</button></div>
<div class=tw><table><thead><tr>
<th>Adr</th><th>Status</th><th>Ist</th><th>Ziel</th><th>Fehler</th><th>Korr.</th>
<th>Erk.&nbsp;Blatt</th><th>FW</th><th>Verpasst</th><th></th></tr></thead>
<tbody id=modtb></tbody></table></div>
<p class=hint>Ist/Ziel als Blattnummer und Zeichen. „Verpasst" = ausgebliebene Statusantworten in Folge (ab 3 gilt das Modul als offline).</p>
</section>

<section class=view data-v=log>
<div class=row style="justify-content:space-between;margin-bottom:14px">
<div class=seg id=logf><button data-f=info class=on>Alle</button><button data-f=warn>Warnungen</button><button data-f=err>Fehler</button></div>
<button class="btn sm ghost" id=logclear>Leeren</button></div>
<div class=card><div class=log id=loglist></div></div>
<p class=hint>Ringpuffer, 32 Einträge im RAM. Zeitstempel relativ zum Systemstart (keine gepufferte Uhr).</p>
</section>

<section class=view data-v=set><div class=card>
<div class=sect><h3><span class=dot id=wdot></span> WLAN</h3>
<dl class=kv id=wkv></dl>
<div class="row mt"><button class="btn sm" id=wscan>Netze suchen</button>
<button class="btn sm ghost" id=wportal>Konfigurationsportal öffnen</button></div>
<div id=wbox style="margin-top:12px;display:none"><div class=wifi id=wlist></div>
<div class="row mt"><input type=password id=wpsk placeholder="Passwort für ausgewähltes Netz" style=max-width:320px>
<button class="btn primary" id=wconn>Verbinden</button></div>
<p class=hint>Nach erfolgreicher Verbindung werden die Zugangsdaten dauerhaft gespeichert. Schlägt es fehl, kehrt die Karte nach ~25 s ins alte Netz zurück.</p></div></div>

<div class=sect><h3>IP-Adresse</h3>
<div class=trow><label class=switch><input type=checkbox id=cf_use_static><span class=t></span></label>
<div class=tx><b>Feste IP statt DHCP</b><span>Standard: Adresse vom Router beziehen.</span></div></div>
<div class=collapse id=ipf hidden><div class="grid c2">
<div class=field><span class=lbl>IP-Adresse</span><input type=text id=cf_ip></div>
<div class=field><span class=lbl>Subnetzmaske</span><input type=text id=cf_mask></div>
<div class=field><span class=lbl>Gateway</span><input type=text id=cf_gw></div>
<div class=field><span class=lbl>DNS</span><input type=text id=cf_dns></div></div></div>
<p class=hint id=iphint></p>
<div class="row mt"><button class="btn primary" data-save=ip>Speichern &amp; neu verbinden</button></div></div>

<div class=sect><h3>Zeit</h3>
<div class=trow><label class=switch><input type=checkbox id=cf_ntp_enabled checked><span class=t></span></label>
<div class=tx><b>Zeit über NTP beziehen</b><span>Aus = die Uhr wird ausschließlich manuell gestellt (freilaufend, ESP32-C3 ohne gepufferte RTC).</span></div></div>
<div class="grid c2" style=margin-top:12px>
<div class=field><span class=lbl>NTP-Server</span><input type=text id=cf_ntp_server></div>
<div class=field><span class=lbl>Zeitzone (POSIX TZ)</span><input type=text id=cf_tz></div></div>
<div class="row mt"><button class="btn primary" data-save=time>Speichern &amp; synchronisieren</button></div>
<div class="row" style=margin-top:16px><input type=datetime-local id=mtime style=max-width:240px>
<button class=btn id=setclock>Uhr manuell setzen</button></div>
<p class=hint id=timehint></p></div>

<div class=sect><h3><span class=dot id=mqdot></span> MQTT / Home Assistant</h3>
<div class=trow><label class=switch><input type=checkbox id=cf_mqtt_enabled checked><span class=t></span></label>
<div class=tx><b>MQTT aktiv</b><span>Anbindung an Home Assistant per Auto-Discovery.</span></div></div>
<div class=collapse id=mqf><div class="grid c2">
<div class=field><span class=lbl>Broker</span><input type=text id=cf_mqtt_host></div>
<div class=field><span class=lbl>Port</span><input type=number id=cf_mqtt_port></div>
<div class=field><span class=lbl>Benutzer</span><input type=text id=cf_mqtt_user></div>
<div class=field><span class=lbl>Passwort</span><input type=password id=cf_mqtt_pass></div>
<div class=field><span class=lbl>Basis-Topic</span><input type=text id=cf_base_topic></div></div>
<div class="row mt"><button class="btn primary" data-save=mqtt>Speichern</button></div></div></div>

<div class=sect><h3>Anzeige</h3><div class="grid c3">
<div class=field><span class=lbl>Module (Feldbreite)</span><input type=number id=cf_modules></div>
<div class=field><span class=lbl>Uhr-Trennzeichen</span><select id=cf_sep><option>.</option><option>:</option><option>-</option></select></div>
<div class=field><span class=lbl>hh:mm:ss Auto-Rückfall (min)</span><input type=number id=cf_hms></div></div>
<p class=hint>hh:mm:ss lässt ein Modul rund alle 10 s eine volle Umdrehung fahren (≈173 Tage bis zur MTBF) — daher der automatische Rückfall auf hh:mm.</p>
<div class="row mt"><button class="btn primary" data-save=disp>Speichern</button></div></div>

<div class=sect><h3>Schnittstellen</h3>
<div class=trow><label class=switch><input type=checkbox id=cf_api_write checked><span class=t></span></label>
<div class=tx><b>REST-Schreib-API</b><span><code>POST /api/text</code>, <code>/mode</code>, <code>/home</code>, <code>/module</code> … Aus = die Anzeige lässt sich nur über diese Oberfläche und MQTT steuern; GET-Status und Einstellungen bleiben erreichbar.</span></div></div>
<div class=trow><label class=switch><input type=checkbox id=cf_ota_enabled checked><span class=t></span></label>
<div class=tx><b>OTA-Update über die Web-UI</b><span>Erlaubt <code>POST /api/update</code> (Abschnitt „Firmware aktualisieren"). Aus = Updates nur per USB.</span></div></div>
<div class=trow><label class=switch><input type=checkbox id=cf_mdns_enabled checked><span class=t></span></label>
<div class=tx><b>mDNS / Bonjour</b><span>Erreichbarkeit unter <code>&lt;node&gt;.local</code>. (Neustart nötig)</span></div></div>
<p class="hint warn">Die Web-Oberfläche selbst lässt sich hier nicht abschalten.</p>
<div class="row mt"><button class="btn primary" data-save=iface>Speichern</button></div></div>

<div class=sect><h3>System</h3>
<div class=field style=max-width:320px><span class=lbl>Hostname / mDNS-Name</span><input type=text id=cf_node_id placeholder=krone_anzeige></div>
<p class=hint>Wird für mDNS (<code>&lt;name&gt;.local</code>), OTA und die MQTT-Client-ID verwendet. Nach dem Speichern neu starten.</p>
<div class="row mt"><button class="btn primary" data-save=host>Hostname speichern</button></div>
<dl class=kv id=syskv style=margin-top:18px></dl>
<div class="row mt"><button class=btn id=backup>Einstellungen sichern</button>
<label class=btn style=cursor:pointer>Wiederherstellen<input type=file id=restore accept="application/json,.json" hidden></label>
<button class="btn danger" id=reboot>Neu starten</button></div>
<p class=hint>Alle Einstellungen (auch WLAN &amp; MQTT) liegen im NVS und <b>überstehen OTA-Updates</b>. Nur beim Flashen per USB mit „Erase" gehen sie verloren — dann die Sicherung wieder einspielen.</p>
</div>

<div class=sect><h3>Firmware aktualisieren</h3>
<p class=hint>App-Image <code>krone-master-esp32c3.ota.bin</code> hochladen — <b>nicht</b> die <code>.factory.bin</code>. Das Modul schreibt es in die zweite App-Partition und startet neu; bei einem Fehler bleibt die laufende Firmware aktiv.</p>
<div class="row mt"><label class="btn" style=cursor:pointer>Datei wählen<input type=file id=fw accept=".bin,application/octet-stream" hidden></label>
<span id=fwname class=hint style=margin:0>keine Datei</span></div>
<div id=fwbar style="display:none;margin-top:12px;height:6px;background:var(--p2);border:1px solid var(--line);border-radius:4px;overflow:hidden">
<div id=fwfill style="height:100%;width:0;background:var(--amber);transition:width .15s"></div></div>
<div class="row mt"><button class="btn primary" id=fwgo disabled>Update starten</button>
<span id=fwoff class="hint warn" style=margin:0;display:none>In den Schnittstellen deaktiviert</span></div>
</div>
</div></section>
</div></div>
<div id=toast></div>
<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
const NAMES={dash:"Übersicht",mods:"Module",log:"Log",set:"Einstellungen"};
const STATE=["Idle","Homing","Moving","Fehler"];
const ERRTXT={1:"kein Blattimpuls",2:"kein Leerbildimpuls",3:"Blattzahl unplausibel",4:"Position verloren",5:"Laufzeitüberwachung",6:"Adresskollision"};
let VIEW="dash",cfg={},sys={},st={};

function blattChar(n){if(n<=2||n>40)return"";if(n<=12)return""+(n-3);if(n<=38)return String.fromCharCode(65+(n-13));return n===39?"-":"."}
function bars(r){const q=r>=-55?4:r>=-65?3:r>=-75?2:r>=-85?1:0;let s="";for(let i=1;i<=4;i++)s+=`<i class="${i<=q?'on':''}" style="height:${3+i*2.6}px"></i>`;return s}
function dur(s){s=s|0;const d=s/86400|0,h=(s%86400)/3600|0,m=(s%3600)/60|0;return(d?d+" d ":"")+String(h).padStart(2,"0")+":"+String(m).padStart(2,"0")}
async function J(u,o){const r=await fetch(u,o);if(!r.ok)throw r.status;const t=await r.text();return t?JSON.parse(t):{}}
async function P(u,b){try{await J(u,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(b||{})});return 1}catch(e){toast("Fehler "+e);return 0}}
let tT;function toast(m){const t=$("#toast");t.textContent=m;t.style.opacity=1;t.style.transform="translateX(-50%) translateY(0)";clearTimeout(tT);tT=setTimeout(()=>{t.style.opacity=0;t.style.transform="translateX(-50%) translateY(20px)"},2400)}

function show(v){VIEW=v;$$("#nav button").forEach(b=>b.classList.toggle("on",b.dataset.v===v));
$$(".view").forEach(s=>s.classList.toggle("on",s.dataset.v===v));$("#crumb").textContent=NAMES[v];scrollTo(0,0);
if(v==="log")pollLog();if(v==="set")loadCfg()}
$("#nav").onclick=e=>{const b=e.target.closest("button");if(b)show(b.dataset.v)};

// ── Übersicht ──
$("#modeseg").onclick=e=>{const b=e.target.closest("button");if(!b)return;
$$("#modeseg button").forEach(x=>x.classList.toggle("on",x===b));
$("#textrow").style.display=b.dataset.m==="text"?"flex":"none";
if(b.dataset.m!=="text")sendMode(b.dataset.m)};
$("#alignseg").onclick=e=>{const b=e.target.closest("button");if(!b)return;
$$("#alignseg button").forEach(x=>x.classList.toggle("on",x===b))};
$("#sendbtn").onclick=()=>{P("/api/text",{text:$("#txt").value});toast("Text gesendet")};
function sendMode(m){P("/api/mode",{mode:m,sep:cfg.sep||".",align:+($("#alignseg .on")?.dataset.a||1)});toast("Modus: "+m)}
$("#quick").onclick=e=>{const b=e.target.closest("button");if(!b)return;const q=b.dataset.q;
if(q==="home"){P("/api/home",{});toast("HOME an alle")}
else if(q==="selftest"){P("/api/selftest",{});toast("Selbsttest")}
else if(q==="stop"){P("/api/module",{addr:0,action:"stop"});toast("STOP an alle")}
else if(q==="wifi")show("set")};

function flapCls(m){if(!m.online)return"of";if(m.state===3)return"er";if(m.state===2)return"mv";if(m.state===1)return"hm";return""}
function renderDash(){
const mode=$("#modeseg .on").dataset.m;
$("#flaps").innerHTML=(st.modules||[]).map(m=>{let c="flap "+flapCls(m),ch=blattChar(m.ist);
if(!m.online)ch="–";else if(m.state===3)ch="!";if(mode==="blank"||mode==="off")ch="";
return `<div class="${c}"><div class=cell><span class=ch>${ch||"&nbsp;"}</span></div><div class=tag><span>${m.addr}</span><i></i></div></div>`}).join("");
const on=(st.modules||[]).filter(m=>m.online).length,tot=(st.modules||[]).length;
const errs=(st.modules||[]).filter(m=>m.error).map(m=>m.addr);
const off=(st.modules||[]).filter(m=>!m.online).map(m=>m.addr);
const t=[
["Module online",`<span class="v ok">${on}<span style="color:var(--faint);font-size:15px">/${tot}</span></span>`,off.length?"Adr "+off.join(", ")+" offline":"alle erreichbar"],
["Sammelfehler",errs.length?`<span class="v err">${errs.length}</span>`:`<span class="v ok">0</span>`,errs.length?"Adr "+errs.join(", "):"keine"],
["Uhrzeit",`<span class=v>${(sys.time||"—").slice(0,5)}</span>`,st.time_valid?(sys.time_src||"NTP"):"nicht gesetzt"],
["WLAN",`<span class=v>${sys.rssi??"—"}<span style="font-size:13px;color:var(--faint)"> dBm</span></span>`,sys.ssid||"—"],
["Bus CRC-Fehler",`<span class="v ${sys.crc_err?'warn':'ok'}">${sys.crc_err??0}</span>`,"Timeouts "+(sys.timeouts??0)],
["Freier Heap",`<span class=v>${Math.round((sys.heap_free||0)/1024)}<span style="font-size:13px;color:var(--faint)"> KB</span></span>`,"min "+Math.round((sys.heap_min||0)/1024)+" KB"],
];
$("#tiles").innerHTML=t.map(x=>`<div class="card tile"><span class=lbl>${x[0]}</span><div>${x[1]}</div><div class=sub>${x[2]}</div></div>`).join("");
}

// ── Module ──
function renderMods(){
const M=st.modules||[];
const on=M.filter(m=>m.online).length,er=M.filter(m=>m.error||m.state===3).length;
$("#modsum").innerHTML=`${st.detected||M.length} erkannt · <span class="pill ok"><i></i>${on} online</span> `+(er?`<span class="pill err"><i></i>${er} Fehler</span>`:"");
$("#modtb").innerHTML=M.map(m=>{
const p=!m.online?`<span class="pill mute"><i></i>offline</span>`:
m.state===3?`<span class="pill err"><i></i>Fehler</span>`:
m.state===2?`<span class="pill warn"><i></i>Moving</span>`:
m.state===1?`<span class="pill warn"><i></i>Homing</span>`:`<span class="pill ok"><i></i>Idle</span>`;
const cell=n=>m.online?`${n} <span class=cm>${blattChar(n)||"␣"}</span>`:"–";
const ec=m.error?`0x0${m.error} · ${ERRTXT[m.error]||""}`:"–";
return `<tr><td class=mono>${m.addr}</td><td>${p}</td><td class=mono>${cell(m.ist)}</td><td class=mono>${cell(m.ziel)}</td>
<td>${ec}</td><td class=mono>${m.corr}</td><td class=mono>${m.blatt||"–"}</td><td class=mono>${m.fw?"v"+m.fw:"–"}</td><td class=mono>${m.miss}</td>
<td><div class=ra><button class="btn sm" data-h=${m.addr}>Homing</button><button class="btn sm" data-i=${m.addr}>Identify</button></div></td></tr>`}).join("");
}
$("#modtb").onclick=e=>{const b=e.target.closest("button");if(!b)return;
if(b.dataset.h){P("/api/module",{addr:+b.dataset.h,action:"home"});toast("HOME Adr "+b.dataset.h)}
if(b.dataset.i){P("/api/module",{addr:+b.dataset.i,action:"identify"});toast("IDENTIFY Adr "+b.dataset.i)}};
$("#enumbtn").onclick=()=>{P("/api/enumerate",{});toast("Enumeration neu gestartet")};

// ── Log ──
$("#logf").onclick=e=>{const b=e.target.closest("button");if(!b)return;
$$("#logf button").forEach(x=>x.classList.toggle("on",x===b));pollLog()};
$("#logclear").onclick=async()=>{await P("/api/log/clear",{});pollLog();toast("Log geleert")};
async function pollLog(){if(VIEW!=="log")return;
const f=$("#logf .on").dataset.f;let d;try{d=await J("/api/log?sev="+f)}catch(e){return}
$("#loglist").innerHTML=(d.entries||[]).map(e=>{
const pill=e.sev===2?`<span class="pill err">Fehler</span>`:e.sev===1?`<span class="pill warn">Warnung</span>`:`<span class="pill mute">Info</span>`;
return `<div class=e><time>+${dur(e.t/1000)}</time>${pill}<div><span class=who>${e.src}</span> ${e.msg}</div></div>`}).join("")
||`<p class=hint style=padding:8px>Keine Einträge.</p>`}

// ── Einstellungen ──
const CF=["mqtt_host","mqtt_port","mqtt_user","mqtt_pass","base_topic","node_id","modules","hms","ntp_server","tz","ip","mask","gw","dns"];
const CB=["use_static","ntp_enabled","mqtt_enabled","api_write","ota_enabled","mdns_enabled"];
async function loadCfg(){try{cfg=await J("/api/config")}catch(e){return}
$("#cf_hms").value=Math.round((cfg.hms_timeout_s||600)/60);
$("#cf_modules").value=cfg.module_count;
$("#cf_mqtt_port").value=cfg.mqtt_port;
["mqtt_host","mqtt_user","mqtt_pass","base_topic","node_id","ntp_server","tz","ip","mask","gw","dns"].forEach(k=>{const el=$("#cf_"+k);if(el)el.value=cfg[k]??""});
$("#cf_sep").value=cfg.sep||".";
CB.forEach(k=>{const el=$("#cf_"+k);if(el)el.checked=!!cfg[k]});
$("#ipf").hidden=!cfg.use_static;$("#mqf").style.opacity=cfg.mqtt_enabled?1:.4;
$("#iphint").innerHTML=cfg.use_static?"Feste Adresse — wird beim Speichern übernommen.":`Aktuell per DHCP: <b style="font-family:var(--mono);color:var(--ink)">${sys.ip||"—"}</b>`;
$("#timehint").innerHTML=`Aktuell: <b style="font-family:var(--mono);color:var(--ink)">${sys.time||"—"}</b> · Quelle ${sys.time_src||"—"}`;
$("#wdot").className="dot "+(sys.ssid?"ok":"err");
$("#wkv").innerHTML=`<dt>Verbunden mit</dt><dd>${sys.ssid||"—"}</dd><dt>IP</dt><dd>${sys.ip||"—"}</dd><dt>Signal</dt><dd>${sys.rssi??"—"} dBm</dd>`;
$("#mqdot").className="dot "+(sys.mqtt_connected?"ok":sys.mqtt_enabled?"warn":"");
otaUiState();renderSys()}
function otaUiState(){const on=!!cfg.ota_enabled;
$("#fwoff").style.display=on?"none":"inline";$("#fwgo").disabled=!on||!_fw;
$("#fw").disabled=!on}
$("#cf_use_static").onchange=e=>$("#ipf").hidden=!e.target.checked;
$("#cf_mqtt_enabled").onchange=e=>$("#mqf").style.opacity=e.target.checked?1:.4;
$("#cf_ota_enabled").onchange=e=>{cfg.ota_enabled=e.target.checked;otaUiState()};

function collectCfg(){return{
mqtt_host:$("#cf_mqtt_host").value,mqtt_port:+$("#cf_mqtt_port").value,mqtt_user:$("#cf_mqtt_user").value,
mqtt_pass:$("#cf_mqtt_pass").value,base_topic:$("#cf_base_topic").value,node_id:$("#cf_node_id")?.value||cfg.node_id,
module_count:+$("#cf_modules").value,hms_timeout_s:(+$("#cf_hms").value||10)*60,
ntp_server:$("#cf_ntp_server").value,tz:$("#cf_tz").value,ntp_enabled:$("#cf_ntp_enabled").checked,
sep:$("#cf_sep").value,use_static:$("#cf_use_static").checked,ip:$("#cf_ip").value,mask:$("#cf_mask").value,
gw:$("#cf_gw").value,dns:$("#cf_dns").value,mqtt_enabled:$("#cf_mqtt_enabled").checked,
api_write:$("#cf_api_write").checked,ota_enabled:$("#cf_ota_enabled").checked,mdns_enabled:$("#cf_mdns_enabled").checked}}
$$("[data-save]").forEach(b=>b.onclick=async()=>{
await P("/api/config",collectCfg());toast("Gespeichert");setTimeout(loadCfg,400);
if((b.dataset.save==="host"||b.dataset.save==="iface")&&confirm("Für Hostname/Schnittstellen jetzt neu starten?")){
await P("/api/reboot",{});toast("Neustart …")}});
$("#setclock").onclick=async()=>{const v=$("#mtime").value;if(!v)return toast("Zeit wählen");
await P("/api/time",{iso:v});toast("Uhr gesetzt");setTimeout(refresh,600)};

$("#wscan").onclick=async()=>{$("#wbox").style.display="block";$("#wlist").innerHTML="<div class=hint style=padding:12px>Suche …</div>";
let d;try{d=await J("/api/wifi/scan")}catch(e){$("#wlist").innerHTML="<div class=hint style=padding:12px>Scan fehlgeschlagen</div>";return}
$("#wlist").innerHTML=(d.nets||[]).map((w,i)=>`<button class="wifi n" data-s="${w.ssid.replace(/"/g,'&quot;')}">
<span class=bars>${bars(w.rssi)}</span><span class=ssid>${w.ssid}</span><span class=lock>${w.enc?"🔒":""}</span></button>`).join("")
||"<div class=hint style=padding:12px>Keine Netze</div>"};
$("#wlist").onclick=e=>{const b=e.target.closest("button");if(!b)return;
$$(".wifi .n").forEach(n=>n.classList.remove("sel"));b.classList.add("sel");$("#wpsk").focus()};
$("#wconn").onclick=async()=>{const s=$(".wifi .n.sel");if(!s)return toast("Netz wählen");
await P("/api/wifi",{ssid:s.dataset.s,psk:$("#wpsk").value});toast("Verbindet mit "+s.dataset.s+" …")};
$("#wportal").onclick=async()=>{if(!confirm("Konfigurationsportal öffnen? Die Karte ist dann kurz nur über den AP „"+(cfg.node_id||"krone_anzeige")+"“ erreichbar."))return;
await P("/api/wifi/portal",{});toast("Portal wird geöffnet …")};
$("#reboot").onclick=async()=>{if(!confirm("Zentralsteuerung neu starten?"))return;await P("/api/reboot",{});toast("Neustart …")};
$("#backup").onclick=async()=>{let t;try{t=await(await fetch("/api/backup")).text()}catch(e){return toast("Sicherung fehlgeschlagen")}
const a=document.createElement("a");a.href=URL.createObjectURL(new Blob([t],{type:"application/json"}));
a.download=(cfg.node_id||"krone")+"-backup.json";a.click();URL.revokeObjectURL(a.href);toast("Sicherung heruntergeladen")};
$("#restore").onchange=async e=>{const f=e.target.files[0];e.target.value="";if(!f)return;
const txt=await f.text();try{JSON.parse(txt)}catch(_){return toast("Keine gültige JSON-Datei")}
if(!confirm("Alle Einstellungen (inkl. WLAN) aus der Datei übernehmen und neu starten?"))return;
try{await fetch("/api/backup",{method:"POST",headers:{"Content-Type":"application/json"},body:txt})}catch(_){}
toast("Wiederhergestellt — Neustart …")};

let _fw=null;
$("#fw").onchange=e=>{_fw=e.target.files[0]||null;
$("#fwname").textContent=_fw?`${_fw.name} · ${Math.round(_fw.size/1024)} KB`:"keine Datei";
otaUiState()};
$("#fwgo").onclick=()=>{if(!_fw)return;
if(!/\.bin$/i.test(_fw.name)&&!confirm("Die Datei endet nicht auf .bin — trotzdem einspielen?"))return;
if(!confirm(`Firmware „${_fw.name}“ einspielen und neu starten?`))return;
const fd=new FormData();fd.append("firmware",_fw,_fw.name);
const x=new XMLHttpRequest();x.open("POST","/api/update");
$("#fwbar").style.display="block";$("#fwfill").style.width="0";$("#fwgo").disabled=true;
x.upload.onprogress=ev=>{if(ev.lengthComputable)$("#fwfill").style.width=Math.round(100*ev.loaded/ev.total)+"%"};
x.onload=()=>{if(x.status===200){$("#fwfill").style.width="100%";toast("Update geschrieben — Neustart …")}
else{$("#fwgo").disabled=false;let m=x.responseText;try{m=JSON.parse(m).error}catch(_){}toast("Update fehlgeschlagen: "+m)}};
x.onerror=()=>{$("#fwgo").disabled=false;$("#fwbar").style.display="none";toast("Verbindung abgebrochen")};
x.send(fd)};

function kb(b){return Math.round((b||0)/1024)}
function renderSys(){
const hn=sys.hostname||sys.node_id||"—";
const ht=sys.heap_total||0,hf=sys.heap_free||0;
const hpct=ht?Math.round(100*(1-hf/ht)):0;
const tc=(sys.temp_c!=null&&sys.temp_c>-40&&sys.temp_c<150)?sys.temp_c.toFixed(1)+" °C":"—";
$("#syskv").innerHTML=`
<dt>Hostname</dt><dd>${hn}${sys.mdns_enabled?` · <span style=color:var(--dim)>${hn}.local</span>`:""}</dd>
<dt>Firmware</dt><dd>${sys.fw||"—"}</dd>
<dt>Chip</dt><dd>ESP32-C3 · MAC ${sys.mac||"—"}</dd>
<dt>Uptime</dt><dd>${dur(sys.uptime_s)}</dd>
<dt>CPU-Last</dt><dd>${sys.cpu_load??"—"} % <span style=color:var(--faint)>(grob, Idle-Hook)</span></dd>
<dt>RAM</dt><dd>${kb(hf)} / ${kb(ht)} KB frei · ${hpct} % belegt <span style=color:var(--faint)>(min ${kb(sys.heap_min)} KB)</span></dd>
<dt>Temperatur</dt><dd>${tc}</dd>
<dt>Programm / OTA</dt><dd>${kb(sys.sketch_used)} KB belegt · ${kb(sys.sketch_free)} KB frei für Update</dd>
<dt>OTA (Web-UI)</dt><dd>${sys.ota_enabled?"erlaubt":"gesperrt"}</dd>`}

// ── Poll-Schleifen ──
async function refresh(){
try{st=await J("/api/status")}catch(e){}
try{sys=await J("/api/system")}catch(e){}
$("#foot").innerHTML=`<span class="dot ${sys.ssid?"ok":"err"}"></span> ${sys.ssid?"verbunden":"kein WLAN"}<br>Uptime ${dur(sys.uptime_s)}<br>FW ${(sys.fw||"").split(" ")[0]}`;
const on=(st.modules||[]).filter(m=>m.online).length;
$("#meta").innerHTML=`<div>Module <b>${on}/${(st.modules||[]).length}</b></div>
<div>WLAN <b>${sys.ssid||"—"}</b> · <b>${sys.rssi??"—"} dBm</b></div>
<div>MQTT <b>${sys.mqtt_enabled?(sys.mqtt_connected?"verbunden":"getrennt"):"aus"}</b></div>
<div>NTP <b>${st.time_valid?"gültig":"—"}</b></div>`;
if(VIEW==="dash")renderDash();
if(VIEW==="mods")renderMods();
if(VIEW==="set")renderSys();
if(st.modules&&$("#modeseg .on").dataset.m!==st.mode){
$$("#modeseg button").forEach(x=>x.classList.toggle("on",x.dataset.m===st.mode));
$("#textrow").style.display=st.mode==="text"?"flex":"none";}
}
refresh();setInterval(refresh,2500);setInterval(pollLog,5000);
</script></body></html>)HTML";

static void send_json(int code, const char *body) { web.send(code, "application/json", body); }
static void ok_json() { send_json(200, "{\"ok\":true}"); }

/* Schreib-Guard: schuetzt zustandsaendernde POST-Endpunkte. */
static bool write_allowed()
{
    if (cfg.api_write) {
        return true;
    }
    send_json(403, "{\"error\":\"api_write_disabled\"}");
    return false;
}

static bool body_json(JsonDocument &doc)
{
    if (!web.hasArg("plain")) {
        return false;
    }
    return deserializeJson(doc, web.arg("plain")) == DeserializationError::Ok;
}

/* ================================================================== */
/* REST-Handler                                                        */
/* ================================================================== */

static void handle_status()
{
    static char buf[3072];
    if (masterapp_status_json(&g_app, buf, sizeof(buf)) == 0) {
        send_json(500, "{\"error\":\"buf\"}");
        return;
    }
    send_json(200, buf);
}

static void time_str(char *out, size_t n, const char **src)
{
    struct tm tmv;
    if (getLocalTime(&tmv, 0) && tmv.tm_year > 120) {
        strftime(out, n, "%Y-%m-%d %H:%M:%S", &tmv);
        *src = g_app.time_valid ? (cfg.ntp_enabled ? "ntp" : "manuell") : "manuell";
    } else {
        snprintf(out, n, "nicht gesetzt");
        *src = "keine";
    }
}

static void handle_system()
{
    char tbuf[24];
    const char *tsrc = "keine";
    time_str(tbuf, sizeof(tbuf), &tsrc);

    JsonDocument d;
    d["uptime_s"]       = millis() / 1000UL;
    d["heap_free"]      = ESP.getFreeHeap();
    d["heap_min"]       = ESP.getMinFreeHeap();
    d["ssid"]           = WiFi.isConnected() ? WiFi.SSID() : String("");
    d["ip"]             = WiFi.localIP().toString();
    d["rssi"]           = WiFi.isConnected() ? WiFi.RSSI() : 0;
    d["mac"]            = WiFi.macAddress();
    d["fw"]             = FW_BUILD;
    d["node_id"]        = cfg.node_id;
    d["time"]           = tbuf;
    d["time_src"]       = tsrc;
    d["ntp_server"]     = cfg.ntp_server;
    d["tz"]             = cfg.tz;
    d["ntp_enabled"]    = cfg.ntp_enabled;
    d["mqtt_enabled"]   = cfg.mqtt_enabled;
    d["mqtt_connected"] = mqtt.connected();
    d["ota_enabled"]    = cfg.ota_enabled;
    d["mdns_enabled"]   = cfg.mdns_enabled;
    d["api_write"]      = cfg.api_write;
    d["crc_err"]        = g_bus.crc_errors;
    d["timeouts"]       = g_bus.timeouts;
    d["hostname"]       = cfg.node_id;
    d["cpu_load"]       = g_cpu_load;
    d["heap_total"]     = ESP.getHeapSize();
    d["temp_c"]         = temperatureRead();
    d["sketch_used"]    = ESP.getSketchSize();
    d["sketch_free"]    = ESP.getFreeSketchSpace();

    char out[768];
    serializeJson(d, out, sizeof(out));
    send_json(200, out);
}

static void handle_log()
{
    evlog_sev_t sev = EVLOG_INFO;
    if (web.hasArg("sev")) {
        const String s = web.arg("sev");
        if (s == "warn") sev = EVLOG_WARN;
        else if (s == "err") sev = EVLOG_ERR;
    }
    static char buf[3072];
    if (evlog_json(&g_log, sev, buf, sizeof(buf)) == 0) {
        send_json(500, "{\"error\":\"buf\"}");
        return;
    }
    send_json(200, buf);
}

static void handle_log_clear()
{
    evlog_clear(&g_log);
    ok_json();
}

static void handle_text()
{
    if (!write_allowed()) return;
    JsonDocument doc;
    if (!body_json(doc) || !doc["text"].is<const char *>()) {
        send_json(400, "{\"error\":\"text\"}");
        return;
    }
    masterapp_set_text(&g_app, doc["text"], millis());
    ok_json();
}

static void handle_mode()
{
    if (!write_allowed()) return;
    JsonDocument doc;
    if (!body_json(doc) || !doc["mode"].is<const char *>()) {
        send_json(400, "{\"error\":\"mode\"}");
        return;
    }
    const char *sep = doc["sep"] | ".";
    const uint8_t al = doc["align"] | (uint8_t)cfg.align;
    masterapp_set_mode(&g_app, mode_from_string(doc["mode"]), sep[0], align_from(al), millis());
    ok_json();
}

static void handle_home()
{
    if (!write_allowed()) return;
    JsonDocument doc;
    body_json(doc);
    busmaster_home(&g_bus, (uint8_t)(doc["addr"] | 0));
    ok_json();
}

static void handle_selftest()
{
    if (!write_allowed()) return;
    busmaster_home(&g_bus, PROTO_ADDR_BROADCAST);
    ok_json();
}

static void handle_module()
{
    if (!write_allowed()) return;
    JsonDocument doc;
    if (!body_json(doc) || !doc["action"].is<const char *>()) {
        send_json(400, "{\"error\":\"action\"}");
        return;
    }
    const uint8_t addr = doc["addr"] | 0;
    const char *a = doc["action"];
    if (!strcmp(a, "home"))          busmaster_home(&g_bus, addr);
    else if (!strcmp(a, "stop"))     busmaster_stop(&g_bus, addr);
    else if (!strcmp(a, "identify")) busmaster_identify(&g_bus, addr, doc["s"] | 5);
    else { send_json(400, "{\"error\":\"action\"}"); return; }
    ok_json();
}

static void handle_enumerate()
{
    if (!write_allowed()) return;
    busmaster_start_enumeration(&g_bus, millis());
    evlog_push(&g_log, millis(), EVLOG_INFO, "bus", "Enumeration (Web) gestartet");
    ok_json();
}

static void handle_time()
{
    JsonDocument doc;
    if (!body_json(doc) || !doc["iso"].is<const char *>()) {
        send_json(400, "{\"error\":\"iso\"}");
        return;
    }
    struct tm tmv = {};
    int y, mo, d, h, mi, s = 0;
    if (sscanf(doc["iso"], "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 5) {
        send_json(400, "{\"error\":\"iso\"}");
        return;
    }
    tmv.tm_year = y - 1900; tmv.tm_mon = mo - 1; tmv.tm_mday = d;
    tmv.tm_hour = h; tmv.tm_min = mi; tmv.tm_sec = s; tmv.tm_isdst = -1;
    time_t tt = mktime(&tmv);
    struct timeval tv = { tt, 0 };
    settimeofday(&tv, nullptr);
    masterapp_set_time(&g_app, (uint8_t)h, (uint8_t)mi, (uint8_t)s);
    evlog_push(&g_log, millis(), EVLOG_INFO, "sys", "Uhr manuell gestellt: %02d:%02d", h, mi);
    ok_json();
}

static void handle_wifi_scan()
{
    const int n = WiFi.scanNetworks();
    JsonDocument d;
    JsonArray a = d["nets"].to<JsonArray>();
    for (int i = 0; i < n && i < 20; ++i) {
        JsonObject o = a.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["enc"]  = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    static char out[1536];
    serializeJson(d, out, sizeof(out));
    send_json(200, out);
}

static void handle_wifi_connect()
{
    JsonDocument doc;
    if (!body_json(doc) || !doc["ssid"].is<const char *>()) {
        send_json(400, "{\"error\":\"ssid\"}");
        return;
    }
    wifi_old_ssid  = WiFi.SSID();
    wifi_old_psk   = WiFi.psk();
    wifi_switching = true;
    wifi_switch_ms = millis();
    const char *psk = doc["psk"] | "";
    evlog_push(&g_log, millis(), EVLOG_INFO, "wifi", "Wechsel zu \"%s\"", (const char *)doc["ssid"]);
    WiFi.begin((const char *)doc["ssid"], psk);
    send_json(200, "{\"ok\":true,\"note\":\"connecting\"}");
}

static void handle_wifi_portal()
{
    want_portal = true;
    ok_json();
}

static void handle_reboot()
{
    want_reboot = true;
    reboot_at = millis() + 400;
    ok_json();
}

/* Konfiguration aus einem JSON-Objekt uebernehmen, in NVS sichern und -- wo
 * gefahrlos -- sofort anwenden. Von /api/config und /api/backup genutzt. */
static void apply_config_doc(JsonDocument &doc)
{
    auto cpS = [&](const char *k, char *dst, size_t n) {
        if (doc[k].is<const char *>()) strlcpy(dst, doc[k], n);
    };
    cpS("mqtt_host", cfg.mqtt_host, sizeof(cfg.mqtt_host));
    cpS("mqtt_user", cfg.mqtt_user, sizeof(cfg.mqtt_user));
    cpS("mqtt_pass", cfg.mqtt_pass, sizeof(cfg.mqtt_pass));
    cpS("base_topic", cfg.base_topic, sizeof(cfg.base_topic));
    cpS("node_id", cfg.node_id, sizeof(cfg.node_id));
    cpS("ntp_server", cfg.ntp_server, sizeof(cfg.ntp_server));
    cpS("tz", cfg.tz, sizeof(cfg.tz));
    cpS("ip", cfg.ip, sizeof(cfg.ip));
    cpS("mask", cfg.mask, sizeof(cfg.mask));
    cpS("gw", cfg.gw, sizeof(cfg.gw));
    cpS("dns", cfg.dns, sizeof(cfg.dns));
    if (doc["mqtt_port"].is<unsigned>())     cfg.mqtt_port = doc["mqtt_port"];
    if (doc["module_count"].is<unsigned>())  cfg.module_count = doc["module_count"];
    if (doc["hms_timeout_s"].is<unsigned>()) cfg.hms_timeout_s = doc["hms_timeout_s"];
    if (doc["sep"].is<const char *>()) { const char *s = doc["sep"]; if (s[0]) cfg.sep = s[0]; }
    if (doc["align"].is<unsigned>())          cfg.align = doc["align"];
    if (doc["ntp_enabled"].is<bool>())        cfg.ntp_enabled = doc["ntp_enabled"];
    if (doc["use_static"].is<bool>())         cfg.use_static = doc["use_static"];
    if (doc["mqtt_enabled"].is<bool>())       cfg.mqtt_enabled = doc["mqtt_enabled"];
    if (doc["api_write"].is<bool>())          cfg.api_write = doc["api_write"];
    if (doc["ota_enabled"].is<bool>())        cfg.ota_enabled = doc["ota_enabled"];
    if (doc["mdns_enabled"].is<bool>())       cfg.mdns_enabled = doc["mdns_enabled"];

    settings_save();

    g_app.module_count = cfg.module_count > BUSMASTER_MAX_MODULES
                             ? BUSMASTER_MAX_MODULES : cfg.module_count;
    g_app.hms_timeout_ms = cfg.hms_timeout_s * 1000UL;
    g_app.sep = cfg.sep;
    g_app.align = align_from(cfg.align);
    g_app.have_shown = false;   /* Anzeige mit neuer Ausrichtung neu setzen */
    apply_time_config();
    if (!cfg.mqtt_enabled && mqtt.connected()) mqtt.disconnect();
}

static void config_to_json(JsonDocument &d)
{
    d["mqtt_host"]     = cfg.mqtt_host;
    d["mqtt_port"]     = cfg.mqtt_port;
    d["mqtt_user"]     = cfg.mqtt_user;
    d["mqtt_pass"]     = cfg.mqtt_pass;
    d["base_topic"]    = cfg.base_topic;
    d["node_id"]       = cfg.node_id;
    d["module_count"]  = cfg.module_count;
    d["hms_timeout_s"] = cfg.hms_timeout_s;
    d["ntp_server"]    = cfg.ntp_server;
    d["tz"]            = cfg.tz;
    d["ntp_enabled"]   = cfg.ntp_enabled;
    { char s[2] = { cfg.sep, 0 }; d["sep"] = s; }
    d["align"]         = cfg.align;
    d["use_static"]    = cfg.use_static;
    d["ip"]            = cfg.ip;
    d["mask"]          = cfg.mask;
    d["gw"]            = cfg.gw;
    d["dns"]           = cfg.dns;
    d["mqtt_enabled"]  = cfg.mqtt_enabled;
    d["api_write"]     = cfg.api_write;
    d["ota_enabled"]   = cfg.ota_enabled;
    d["mdns_enabled"]  = cfg.mdns_enabled;
}

static void handle_config()
{
    if (web.method() == HTTP_POST) {
        JsonDocument doc;
        if (!body_json(doc)) { send_json(400, "{\"error\":\"json\"}"); return; }
        apply_config_doc(doc);
        ok_json();
        return;
    }
    JsonDocument d;
    config_to_json(d);
    char out[640];
    serializeJson(d, out, sizeof(out));
    send_json(200, out);
}

/*
 * /api/backup -- vollstaendige Sicherung inkl. WLAN-Zugangsdaten. Zweck: nach
 * einem Flash mit "Erase" (der die NVS-Partition loescht) nicht alles neu
 * eintragen zu muessen. Bei OTA-Updates bleibt die NVS ohnehin erhalten.
 */
static void handle_backup()
{
    if (web.method() == HTTP_POST) {
        JsonDocument doc;
        if (!body_json(doc)) { send_json(400, "{\"error\":\"json\"}"); return; }
        apply_config_doc(doc);
        if (doc["wifi_ssid"].is<const char *>()) {
            const char *ssid = doc["wifi_ssid"];
            const char *psk  = doc["wifi_psk"] | "";
            if (strlen(ssid) > 0) {
                WiFi.begin(ssid, psk);
                evlog_push(&g_log, millis(), EVLOG_INFO, "wifi",
                           "Wiederherstellung: verbinde mit \"%s\"", ssid);
            }
        }
        evlog_push(&g_log, millis(), EVLOG_INFO, "sys", "Einstellungen wiederhergestellt");
        want_reboot = true;
        reboot_at = millis() + 600;
        send_json(200, "{\"ok\":true,\"note\":\"reboot\"}");
        return;
    }

    JsonDocument d;
    config_to_json(d);
    d["wifi_ssid"] = WiFi.SSID();
    d["wifi_psk"]  = WiFi.psk();
    char out[832];
    serializeJson(d, out, sizeof(out));
    web.sendHeader("Content-Disposition", "attachment; filename=\"krone-backup.json\"");
    send_json(200, out);
}

/*
 * /api/update -- OTA aus dem Browser. Hochgeladen wird das App-Image
 * (krone-master-esp32c3.ota.bin), NICHT die .factory.bin. Der Upload-Handler
 * streamt es ueber die Update-Bibliothek in die inaktive App-Partition; bei
 * Fehler bleibt die laufende Firmware aktiv. Danach Neustart.
 *
 * Gated durch den eigenen Schalter cfg.ota_enabled (unabhaengig von api_write).
 * ArduinoOTA (espota, offener UDP-Port ohne Passwort) gibt es nicht mehr --
 * Updates laufen ausschliesslich hierueber bzw. per USB.
 */
static bool g_ota_ok = false;

static void handle_update_upload()
{
    HTTPUpload &up = web.upload();
    if (up.status == UPLOAD_FILE_START) {
        g_ota_ok = false;
        if (!cfg.ota_enabled) {
            return;                         /* Antwort-Handler schickt 403 */
        }
        evlog_push(&g_log, millis(), EVLOG_WARN, "ota", "Update gestartet: %s",
                   up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
            return;
        }
        g_ota_ok = true;
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (g_ota_ok && Update.write(up.buf, up.currentSize) != up.currentSize) {
            Update.printError(Serial);
            g_ota_ok = false;
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (g_ota_ok && Update.end(true)) {
            evlog_push(&g_log, millis(), EVLOG_INFO, "ota",
                       "Update geschrieben (%u B), Neustart", (unsigned)up.totalSize);
        } else {
            g_ota_ok = false;
            Update.printError(Serial);
        }
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        g_ota_ok = false;
        evlog_push(&g_log, millis(), EVLOG_WARN, "ota", "Update abgebrochen");
    }
}

static void handle_update_done()
{
    if (!cfg.ota_enabled) {
        send_json(403, "{\"error\":\"ota_disabled\"}");
        return;
    }
    if (g_ota_ok && !Update.hasError()) {
        send_json(200, "{\"ok\":true,\"note\":\"reboot\"}");
        want_reboot = true;
        reboot_at = millis() + 1200;        /* Zeit fuer das Flushen der Antwort */
    } else {
        char m[96];
        snprintf(m, sizeof(m), "{\"error\":\"%s\"}",
                 Update.hasError() ? Update.errorString() : "kein gueltiges Image");
        send_json(500, m);
    }
}

static void web_begin()
{
    web.on("/", []() { web.send_P(200, "text/html", INDEX_HTML); });
    web.on("/api/status",    HTTP_GET,  handle_status);
    web.on("/api/system",    HTTP_GET,  handle_system);
    web.on("/api/log",       HTTP_GET,  handle_log);
    web.on("/api/log/clear", HTTP_POST, handle_log_clear);
    web.on("/api/text",      HTTP_POST, handle_text);
    web.on("/api/mode",      HTTP_POST, handle_mode);
    web.on("/api/home",      HTTP_POST, handle_home);
    web.on("/api/selftest",  HTTP_POST, handle_selftest);
    web.on("/api/module",    HTTP_POST, handle_module);
    web.on("/api/enumerate", HTTP_POST, handle_enumerate);
    web.on("/api/time",      HTTP_POST, handle_time);
    web.on("/api/wifi/scan", HTTP_GET,  handle_wifi_scan);
    web.on("/api/wifi",      HTTP_POST, handle_wifi_connect);
    web.on("/api/wifi/portal", HTTP_POST, handle_wifi_portal);
    web.on("/api/reboot",    HTTP_POST, handle_reboot);
    web.on("/api/config",    handle_config);
    web.on("/api/backup",    handle_backup);
    web.on("/api/update",    HTTP_POST, handle_update_done, handle_update_upload);
    web.begin();
}

/* ================================================================== */
/* MQTT                                                                */
/* ================================================================== */

static String topic(const char *suffix)
{
    return String(cfg.base_topic) + "/" + suffix;
}

static void mqtt_publish_discovery()
{
    static const ha_entity_t global_ents[] = { HA_ENT_TEXT, HA_ENT_MODE,
                                               HA_ENT_HOME, HA_ENT_SELFTEST,
                                               HA_ENT_ERROR };
    char t[128], p[512];
    for (ha_entity_t e : global_ents) {
        if (hadiscovery_entity(t, sizeof(t), p, sizeof(p), "homeassistant",
                               cfg.base_topic, cfg.node_id, e, 0) == 0) {
            mqtt.publish(t, p, true);
        }
    }
    for (uint8_t n = 1; n <= cfg.module_count; ++n) {
        for (ha_entity_t e : { HA_ENT_MODULE_CHAR, HA_ENT_MODULE_ONLINE }) {
            if (hadiscovery_entity(t, sizeof(t), p, sizeof(p), "homeassistant",
                                   cfg.base_topic, cfg.node_id, e, n) == 0) {
                mqtt.publish(t, p, true);
            }
        }
    }
}

static void mqtt_callback(char *t, uint8_t *payload, unsigned int len)
{
    String body;
    body.reserve(len);
    for (unsigned int i = 0; i < len; ++i) {
        body += (char)payload[i];
    }
    const String tp = t;
    if (tp == topic("text/set")) {
        masterapp_set_text(&g_app, body.c_str(), millis());
    } else if (tp == topic("mode/set")) {
        masterapp_set_mode(&g_app, mode_from_string(body.c_str()), cfg.sep,
                           align_from(cfg.align), millis());
    } else if (tp == topic("home/press")) {
        busmaster_home(&g_bus, PROTO_ADDR_BROADCAST);
    } else if (tp == topic("selftest/press")) {
        busmaster_home(&g_bus, PROTO_ADDR_BROADCAST);
    }
}

static void mqtt_ensure()
{
    if (!cfg.mqtt_enabled || mqtt.connected() || strlen(cfg.mqtt_host) == 0) {
        return;
    }
    if (millis() - last_mqtt_try < 5000) {
        return;
    }
    last_mqtt_try = millis();
    mqtt.setServer(cfg.mqtt_host, cfg.mqtt_port);
    mqtt.setBufferSize(768);
    mqtt.setCallback(mqtt_callback);
    if (mqtt.connect(cfg.node_id, cfg.mqtt_user, cfg.mqtt_pass)) {
        mqtt.subscribe(topic("text/set").c_str());
        mqtt.subscribe(topic("mode/set").c_str());
        mqtt.subscribe(topic("home/press").c_str());
        mqtt.subscribe(topic("selftest/press").c_str());
        mqtt_publish_discovery();
        evlog_push(&g_log, millis(), EVLOG_INFO, "mqtt", "verbunden, Auto-Discovery gesendet");
    } else {
        evlog_push(&g_log, millis(), EVLOG_WARN, "mqtt", "Broker %s nicht erreichbar", cfg.mqtt_host);
    }
}

static void mqtt_publish_state()
{
    bool any_error = false;
    for (uint8_t i = 0; i < cfg.module_count; ++i) {
        const bm_module_t *m = &g_bus.mod[i];
        char st[8];
        snprintf(st, sizeof(st), "%u", m->ist_blatt);
        mqtt.publish((String(cfg.base_topic) + "/module/" + (i + 1) + "/char").c_str(), st);
        mqtt.publish((String(cfg.base_topic) + "/module/" + (i + 1) + "/online").c_str(),
                     m->online ? "1" : "0");
        if (m->fehler != 0) {
            any_error = true;
        }
    }
    mqtt.publish(topic("error/state").c_str(), any_error ? "1" : "0");
}

/* ================================================================== */
/* Zeit + Ereignis-Erkennung                                           */
/* ================================================================== */

static void feed_time()
{
    struct tm tmv;
    if (getLocalTime(&tmv, 0)) {
        masterapp_set_time(&g_app, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    } else if (cfg.ntp_enabled) {
        masterapp_time_invalid(&g_app);
    }
}

static void poll_events(uint32_t now)
{
    static bool inited = false;
    static wl_status_t wl_prev = WL_IDLE_STATUS;
    static bool ntp_logged = false, enum_prev = false, mq_prev = false;
    static bool m_online[BUSMASTER_MAX_MODULES];
    static uint8_t m_err[BUSMASTER_MAX_MODULES];

    if (!inited) {
        for (uint8_t i = 0; i < BUSMASTER_MAX_MODULES; ++i) {
            m_online[i] = g_bus.mod[i].online;
            m_err[i] = g_bus.mod[i].fehler;
        }
        inited = true;
    }

    /* WLAN */
    const wl_status_t wl = WiFi.status();
    if (wl != wl_prev) {
        if (wl == WL_CONNECTED) {
            evlog_push(&g_log, now, EVLOG_INFO, "wifi", "verbunden mit \"%s\" (%d dBm), %s",
                       WiFi.SSID().c_str(), (int)WiFi.RSSI(), WiFi.localIP().toString().c_str());
            wifi_switching = false;
        } else if (wl_prev == WL_CONNECTED) {
            evlog_push(&g_log, now, EVLOG_WARN, "wifi", "Verbindung verloren (Status %d)", (int)wl);
        }
        wl_prev = wl;
    }
    if (wifi_switching && (now - wifi_switch_ms) > 25000UL && wl != WL_CONNECTED) {
        wifi_switching = false;
        if (wifi_old_ssid.length()) {
            evlog_push(&g_log, now, EVLOG_WARN, "wifi", "Wechsel fehlgeschlagen, zurück zu \"%s\"",
                       wifi_old_ssid.c_str());
            WiFi.begin(wifi_old_ssid.c_str(), wifi_old_psk.c_str());
        }
    }

    /* NTP */
    if (!ntp_logged && cfg.ntp_enabled) {
        struct tm tmv;
        if (getLocalTime(&tmv, 0) && tmv.tm_year > 120) {
            evlog_push(&g_log, now, EVLOG_INFO, "ntp", "Zeit synchronisiert");
            ntp_logged = true;
        }
    }

    /* MQTT-Verbindungsabriss */
    const bool mq = mqtt.connected();
    if (mq_prev && !mq && cfg.mqtt_enabled) {
        evlog_push(&g_log, now, EVLOG_WARN, "mqtt", "Verbindung verloren, Reconnect");
    }
    mq_prev = mq;

    /* Enumeration abgeschlossen */
    const bool eb = busmaster_enum_busy(&g_bus);
    if (enum_prev && !eb) {
        evlog_push(&g_log, now, EVLOG_INFO, "bus", "Enumeration: %u Karten adressiert",
                   (unsigned)g_bus.module_count);
    }
    enum_prev = eb;

    /* Modul-Uebergaenge */
    for (uint8_t i = 0; i < g_app.module_count && i < BUSMASTER_MAX_MODULES; ++i) {
        const bm_module_t *m = &g_bus.mod[i];
        if (m->online != m_online[i]) {
            if (m->online) {
                evlog_push(&g_log, now, EVLOG_INFO, "bus", "Adr %u online", i + 1u);
            } else {
                evlog_push(&g_log, now, EVLOG_WARN, "bus", "Adr %u offline (keine Antwort)", i + 1u);
            }
            m_online[i] = m->online;
        }
        if (m->fehler != m_err[i]) {
            if (m->fehler) {
                evlog_push(&g_log, now, EVLOG_ERR, "bus", "Adr %u: Fehler 0x%02X", i + 1u, m->fehler);
            }
            m_err[i] = m->fehler;
        }
    }
}

static void handle_portal_request()
{
    if (!want_portal) {
        return;
    }
    want_portal = false;
    evlog_push(&g_log, millis(), EVLOG_INFO, "wifi", "Konfigurationsportal geöffnet");
    WiFiManager wm;
    wm.setConfigPortalTimeout(300);
    wm.startConfigPortal(cfg.node_id);
    evlog_push(&g_log, millis(), EVLOG_INFO, "wifi", "Portal beendet");
    apply_static_ip();
}

/* ================================================================== */
/* setup / loop                                                        */
/* ================================================================== */

void setup()
{
    Serial.begin(115200);
    settings_load();
    evlog_init(&g_log);
    evlog_push(&g_log, 0, EVLOG_INFO, "sys", "Start, Firmware %s", FW_BUILD);
    esp_register_freertos_idle_hook(idle_hook);

    WiFi.mode(WIFI_STA);
    apply_static_ip();

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.autoConnect(cfg.node_id);

    apply_time_config();

    if (cfg.mdns_enabled) {
        MDNS.begin(cfg.node_id);
        MDNS.addService("http", "tcp", 80);
    }

    bus_begin();
    busmaster_init(&g_bus, bus_tx, nullptr);
    masterapp_init(&g_app, &g_bus, cfg.module_count);
    g_app.sep = cfg.sep;
    g_app.align = align_from(cfg.align);
    g_app.hms_timeout_ms = cfg.hms_timeout_s * 1000UL;

    web_begin();

    busmaster_start_enumeration(&g_bus, millis());
}

void loop()
{
    const uint32_t now = millis();

    web.handleClient();
    handle_portal_request();
    mqtt_ensure();
    mqtt.loop();

    bus_pump(now);
    busmaster_tick(&g_bus, now);
    status_led_tick(now);
    cpu_load_tick(now);
    poll_events(now);

    if (want_reboot && (int32_t)(now - reboot_at) >= 0) {
        ESP.restart();
    }

    if (now - last_time_ms >= 500) {
        last_time_ms = now;
        feed_time();
    }
    masterapp_tick(&g_app, now);

    if (!busmaster_enum_busy(&g_bus) && !g_bus.awaiting &&
        now - last_poll_ms >= 100) {
        last_poll_ms = now;
        const uint8_t count = g_bus.module_count ? g_bus.module_count : cfg.module_count;
        if (count > 0) {
            busmaster_poll_status(&g_bus, poll_addr, now);
            poll_addr = (poll_addr % count) + 1;
            if (poll_addr == 1 && mqtt.connected()) {
                mqtt_publish_state();
            }
        }
    }
}
