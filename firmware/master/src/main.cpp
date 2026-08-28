/*
 * Zentralsteuerung der KRONE-REW-Fallblattanzeige (ESP32).
 *
 * Bindet die hardwareunabhaengigen, host-getesteten Bibliotheken an die
 * ESP32-Peripherie:
 *   lib/protocol     Rahmen/CRC (mit der Modul-Firmware geteilt)
 *   lib/busmaster    Master-Protokollseite, Enumeration, Modul-Statustabelle
 *   lib/charmap      Zeichenabbildung Text -> Fallblatt
 *   lib/clocktext    Uhrzeit -> Text
 *   lib/masterapp    Betriebsarten, Auto-Rueckfall Sekundenanzeige, Status-JSON
 *   lib/hadiscovery  Home-Assistant-MQTT-Auto-Discovery
 *
 * Abweichungen von Spezifikation 7.2 (dependency-arm, siehe docs/toolchain.md):
 *   Web-UI/REST  -> eingebauter WebServer statt ESPAsyncWebServer
 *   Konfig       -> Preferences (NVS) statt LittleFS
 *   Captive Portal-> WiFiManager (unveraendert)
 *
 * Bezug: docs/spezifikation.md Kapitel 7.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>

#include "driver/uart.h"

extern "C" {
#include "busmaster.h"
#include "charmap.h"
#include "hadiscovery.h"
#include "masterapp.h"
#include "protocol.h"
}

/* --- Hardware ---------------------------------------------------- */

static constexpr int      RS485_UART   = UART_NUM_2;
static constexpr int      RS485_RX_PIN = 16;
static constexpr int      RS485_TX_PIN = 17;
static constexpr int      RS485_DE_PIN = 5;    /* an XDIR-Logik / RTS */
static constexpr int      CHAIN_PIN    = 4;    /* CHAIN-Ausgang zur ersten Karte */
static constexpr uint32_t BUS_BAUD     = 115200;

static constexpr char TZ_INFO[] = "CET-1CEST,M3.5.0,M10.5.0/3";  /* Spez. 7.2 */

/* --- Zustand -------------------------------------------------- */

static Preferences   prefs;
static WebServer     web(80);
static WiFiClient     net;
static PubSubClient  mqtt(net);

static busmaster_t   g_bus;
static masterapp_t   g_app;

struct Settings {
    char     mqtt_host[64] = "";
    uint16_t mqtt_port     = 1883;
    char     mqtt_user[32] = "";
    char     mqtt_pass[32] = "";
    char     base_topic[48] = "krone/anzeige";
    char     node_id[32]   = "krone_anzeige";
    uint8_t  module_count  = 10;
    uint32_t hms_timeout_s = 600;
} cfg;

static uint32_t last_poll_ms;
static uint8_t  poll_addr = 1;
static uint32_t last_time_ms;
static uint32_t last_mqtt_try;

/* --- Bus-Transport ------------------------------------------ */

static void bus_tx(void *, const uint8_t *data, size_t len)
{
    /* Hardware-RS485-Halbduplex: der Treiber schaltet DE selbst. */
    uart_write_bytes(RS485_UART, reinterpret_cast<const char *>(data), len);
    uart_wait_tx_done(RS485_UART, pdMS_TO_TICKS(20));
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
}

static void bus_pump(uint32_t now)
{
    uint8_t b;
    while (uart_read_bytes(RS485_UART, &b, 1, 0) == 1) {
        busmaster_on_rx_byte(&g_bus, b, now);
    }
    digitalWrite(CHAIN_PIN, g_bus.chain_active ? HIGH : LOW);
}

/* --- Einstellungen -------------------------------------------- */

static void settings_load()
{
    prefs.begin("krone", true);
    prefs.getString("mqtt_host", cfg.mqtt_host, sizeof(cfg.mqtt_host));
    cfg.mqtt_port = prefs.getUShort("mqtt_port", cfg.mqtt_port);
    prefs.getString("mqtt_user", cfg.mqtt_user, sizeof(cfg.mqtt_user));
    prefs.getString("mqtt_pass", cfg.mqtt_pass, sizeof(cfg.mqtt_pass));
    prefs.getString("base_topic", cfg.base_topic, sizeof(cfg.base_topic));
    prefs.getString("node_id", cfg.node_id, sizeof(cfg.node_id));
    cfg.module_count = prefs.getUChar("modules", cfg.module_count);
    cfg.hms_timeout_s = prefs.getULong("hms_to", cfg.hms_timeout_s);
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
    prefs.end();
}

/* --- Betriebsart aus Text --------------------------------- */

static app_mode_t mode_from_string(const char *s)
{
    if (!strcmp(s, "clock_hm"))  return APP_MODE_CLOCK_HM;
    if (!strcmp(s, "clock_hms")) return APP_MODE_CLOCK_HMS;
    if (!strcmp(s, "blank"))     return APP_MODE_BLANK;
    if (!strcmp(s, "off"))       return APP_MODE_OFF;
    return APP_MODE_TEXT;
}

/* --- REST / Web ------------------------------------------- */

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><meta charset=utf-8><title>KRONE Fallblattanzeige</title>
<style>body{font-family:sans-serif;max-width:40em;margin:2em auto;padding:0 1em}
input,select,button{font-size:1em;padding:.3em}</style>
<h1>KRONE Fallblattanzeige</h1>
<p><input id=t size=20 placeholder=Text> <button onclick="send('text',{text:t.value})">Anzeigen</button>
<p>Modus:
<select id=m onchange="send('mode',{mode:m.value,sep:'.'})">
<option value=text>Text<option value=clock_hm>Uhr hh.mm
<option value=clock_hms>Uhr hh.mm.ss<option value=blank>Leer<option value=off>Aus</select>
<p><button onclick="post('home',{})">Homing alle</button>
<button onclick="post('selftest',{})">Selbsttest</button>
<pre id=s></pre>
<script>
function post(p,b){return fetch('/api/'+p,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})}
function send(p,b){post(p,b).then(refresh)}
function refresh(){fetch('/api/status').then(r=>r.json()).then(j=>s.textContent=JSON.stringify(j,null,1))}
setInterval(refresh,2000);refresh()
</script>
)HTML";

static void send_json(int code, const char *body)
{
    web.send(code, "application/json", body);
}

static void handle_status()
{
    char buf[1024];
    masterapp_status_json(&g_app, buf, sizeof(buf));
    send_json(200, buf);
}

static bool body_json(JsonDocument &doc)
{
    if (!web.hasArg("plain")) {
        return false;
    }
    return deserializeJson(doc, web.arg("plain")) == DeserializationError::Ok;
}

static void handle_text()
{
    JsonDocument doc;
    if (!body_json(doc) || !doc["text"].is<const char *>()) {
        send_json(400, "{\"error\":\"text\"}");
        return;
    }
    masterapp_set_text(&g_app, doc["text"], millis());
    send_json(200, "{\"ok\":true}");
}

static void handle_mode()
{
    JsonDocument doc;
    if (!body_json(doc) || !doc["mode"].is<const char *>()) {
        send_json(400, "{\"error\":\"mode\"}");
        return;
    }
    const char *sep = doc["sep"] | ".";
    masterapp_set_mode(&g_app, mode_from_string(doc["mode"]), sep[0],
                       CHARMAP_ALIGN_CENTER, millis());
    send_json(200, "{\"ok\":true}");
}

static void handle_home()
{
    JsonDocument doc;
    body_json(doc);
    const uint8_t addr = doc["addr"] | 0;
    busmaster_home(&g_bus, addr);
    send_json(200, "{\"ok\":true}");
}

static void handle_selftest()
{
    /* Selbsttest = Homing aller Module; die Blattzahlpruefung macht die
     * Modul-Firmware bei jedem Kalthoming selbst (Spez. 6.2). */
    busmaster_home(&g_bus, PROTO_ADDR_BROADCAST);
    send_json(200, "{\"ok\":true}");
}

static void handle_config()
{
    if (web.method() == HTTP_POST) {
        JsonDocument doc;
        if (!body_json(doc)) {
            send_json(400, "{\"error\":\"json\"}");
            return;
        }
        if (doc["mqtt_host"].is<const char *>()) {
            strlcpy(cfg.mqtt_host, doc["mqtt_host"], sizeof(cfg.mqtt_host));
        }
        if (doc["mqtt_port"].is<uint16_t>()) {
            cfg.mqtt_port = doc["mqtt_port"];
        }
        if (doc["modules"].is<uint8_t>()) {
            cfg.module_count = doc["modules"];
        }
        if (doc["hms_timeout_s"].is<uint32_t>()) {
            cfg.hms_timeout_s = doc["hms_timeout_s"];
        }
        settings_save();
        g_app.hms_timeout_ms = cfg.hms_timeout_s * 1000UL;
        send_json(200, "{\"ok\":true}");
        return;
    }

    JsonDocument doc;
    doc["mqtt_host"] = cfg.mqtt_host;
    doc["mqtt_port"] = cfg.mqtt_port;
    doc["base_topic"] = cfg.base_topic;
    doc["modules"] = cfg.module_count;
    doc["hms_timeout_s"] = cfg.hms_timeout_s;
    char out[256];
    serializeJson(doc, out, sizeof(out));
    send_json(200, out);
}

static void web_begin()
{
    web.on("/", []() { web.send_P(200, "text/html", INDEX_HTML); });
    web.on("/api/status", HTTP_GET, handle_status);
    web.on("/api/text", HTTP_POST, handle_text);
    web.on("/api/mode", HTTP_POST, handle_mode);
    web.on("/api/home", HTTP_POST, handle_home);
    web.on("/api/selftest", HTTP_POST, handle_selftest);
    web.on("/api/config", handle_config);
    web.begin();
}

/* --- MQTT ------------------------------------------------ */

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
        masterapp_set_mode(&g_app, mode_from_string(body.c_str()), '.',
                           CHARMAP_ALIGN_CENTER, millis());
    } else if (tp == topic("home/press")) {
        busmaster_home(&g_bus, PROTO_ADDR_BROADCAST);
    } else if (tp == topic("selftest/press")) {
        busmaster_home(&g_bus, PROTO_ADDR_BROADCAST);
    }
}

static void mqtt_ensure()
{
    if (mqtt.connected() || strlen(cfg.mqtt_host) == 0) {
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

/* --- Zeit --------------------------------------------- */

static void feed_time()
{
    struct tm tmv;
    if (getLocalTime(&tmv, 0)) {
        masterapp_set_time(&g_app, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    } else {
        masterapp_time_invalid(&g_app);
    }
}

/* --- setup / loop ----------------------------------- */

void setup()
{
    Serial.begin(115200);
    settings_load();

    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    wm.autoConnect(cfg.node_id);

    configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");

    bus_begin();
    busmaster_init(&g_bus, bus_tx, nullptr);
    masterapp_init(&g_app, &g_bus, cfg.module_count);
    g_app.hms_timeout_ms = cfg.hms_timeout_s * 1000UL;

    web_begin();
    ArduinoOTA.setHostname(cfg.node_id);
    ArduinoOTA.begin();

    busmaster_start_enumeration(&g_bus, millis());
}

void loop()
{
    const uint32_t now = millis();

    ArduinoOTA.handle();
    web.handleClient();
    mqtt_ensure();
    mqtt.loop();

    bus_pump(now);
    busmaster_tick(&g_bus, now);

    if (now - last_time_ms >= 500) {
        last_time_ms = now;
        feed_time();
    }
    masterapp_tick(&g_app, now);

    /* Statuspolling: ein Modul je 100 ms (Spez. 5.6). */
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
