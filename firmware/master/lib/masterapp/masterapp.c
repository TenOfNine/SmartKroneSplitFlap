/* Siehe masterapp.h und docs/spezifikation.md 7. */
#include "masterapp.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "clocktext.h"

static uint8_t field_width(const masterapp_t *app)
{
    return app->module_count > BUSMASTER_MAX_MODULES ? (uint8_t)BUSMASTER_MAX_MODULES
                                                     : app->module_count;
}

void masterapp_init(masterapp_t *app, busmaster_t *bus, uint8_t module_count)
{
    memset(app, 0, sizeof(*app));
    app->bus = bus;
    app->module_count = module_count > BUSMASTER_MAX_MODULES
                            ? BUSMASTER_MAX_MODULES
                            : module_count;
    app->mode = APP_MODE_BLANK;
    app->sep = '.';
    app->align = CHARMAP_ALIGN_CENTER;
    app->hms_timeout_ms = APP_HMS_TIMEOUT_DEFAULT_MS;
    app->text[0] = '\0';
}

void masterapp_set_mode(masterapp_t *app, app_mode_t mode, char sep,
                        charmap_align_t align, uint32_t now_ms)
{
    app->mode = mode;
    if (sep == '.' || sep == '-') {
        app->sep = sep;
    }
    app->align = align;
    if (mode == APP_MODE_CLOCK_HMS) {
        app->hms_since_ms = now_ms;
    }
    app->have_shown = false;   /* ausdruecklicher Befehl: neu senden */
}

/* Kuerzt s (Laenge len) so, dass keine angeschnittene UTF-8-Sequenz am Ende
 * steht. */
static void utf8_trim_tail(char *s, size_t len)
{
    size_t i = len;
    size_t cont = 0;
    while (i > 0u && cont < 3u && ((unsigned char)s[i - 1u] & 0xC0u) == 0x80u) {
        --i;
        ++cont;
    }
    if (i == 0u) {
        s[0] = '\0';
        return;
    }
    const unsigned char lead = (unsigned char)s[i - 1u];
    size_t need = 1u;
    if ((lead & 0xE0u) == 0xC0u) {
        need = 2u;
    } else if ((lead & 0xF0u) == 0xE0u) {
        need = 3u;
    } else if ((lead & 0xF8u) == 0xF0u) {
        need = 4u;
    }
    if (cont + 1u < need) {
        s[i - 1u] = '\0';
    }
}

void masterapp_set_text(masterapp_t *app, const char *text, uint32_t now_ms)
{
    (void)now_ms;
    if (text == NULL) {
        text = "";
    }
    strncpy(app->text, text, APP_TEXT_MAX);
    app->text[APP_TEXT_MAX] = '\0';
    if (strlen(text) > APP_TEXT_MAX) {
        utf8_trim_tail(app->text, APP_TEXT_MAX);
    }
    app->mode = APP_MODE_TEXT;
    app->have_shown = false;   /* derselbe Text erneut = neu senden */
}

void masterapp_set_time(masterapp_t *app, uint8_t hh, uint8_t mm, uint8_t ss)
{
    app->hh = hh;
    app->mm = mm;
    app->ss = ss;
    app->time_valid = (hh <= 23 && mm <= 59 && ss <= 59);
}

void masterapp_time_invalid(masterapp_t *app)
{
    app->time_valid = false;
}

/* Zielanzeige der aktuellen Betriebsart berechnen. */
void masterapp_current_blaetter(const masterapp_t *app, uint8_t *out)
{
    const size_t w = field_width(app);

    switch (app->mode) {
    case APP_MODE_TEXT:
        charmap_render(app->text, out, w, app->align);
        return;

    case APP_MODE_CLOCK_HM:
    case APP_MODE_CLOCK_HMS: {
        if (!app->time_valid) {
            for (size_t i = 0; i < w; ++i) {
                out[i] = CHARMAP_LEERBILD;
            }
            return;
        }
        char buf[12];
        const bool sec = (app->mode == APP_MODE_CLOCK_HMS);
        clocktext_format(buf, sizeof(buf), app->hh, app->mm, app->ss, sec, app->sep);
        charmap_render(buf, out, w, app->align);
        return;
    }

    case APP_MODE_BLANK:
    case APP_MODE_OFF:
    default:
        for (size_t i = 0; i < w; ++i) {
            out[i] = CHARMAP_LEERBILD;
        }
        return;
    }
}

void masterapp_tick(masterapp_t *app, uint32_t now_ms)
{
    /* Auto-Rueckfall der Sekundenanzeige (7.7). */
    if (app->mode == APP_MODE_CLOCK_HMS && app->hms_timeout_ms > 0 &&
        (uint32_t)(now_ms - app->hms_since_ms) >= app->hms_timeout_ms) {
        app->mode = APP_MODE_CLOCK_HM;
    }

    const size_t w = field_width(app);
    if (w == 0u) {
        return;   /* keine Module -> nichts anzuzeigen, kein SET_ALL(0)/GO */
    }

    if (app->mode == APP_MODE_OFF) {
        /* "Aus": Module nicht ansteuern und nicht nachfuehren; beim
         * Verlassen von "Aus" die Anzeige neu senden. */
        busmaster_clear_targets(app->bus);
        app->have_shown = false;
        return;
    }

    uint8_t want[BUSMASTER_MAX_MODULES];
    masterapp_current_blaetter(app, want);

    bool changed = !app->have_shown;
    for (size_t i = 0; i < w && !changed; ++i) {
        if (want[i] != app->shown[i]) {
            changed = true;
        }
    }
    if (!changed) {
        return;
    }

    busmaster_show(app->bus, want, (uint8_t)w);
    memcpy(app->shown, want, w);
    app->have_shown = true;
}

const char *masterapp_mode_name(app_mode_t m)
{
    switch (m) {
    case APP_MODE_TEXT:      return "text";
    case APP_MODE_CLOCK_HM:  return "clock_hm";
    case APP_MODE_CLOCK_HMS: return "clock_hms";
    case APP_MODE_BLANK:     return "blank";
    case APP_MODE_OFF:       return "off";
    default:                 return "text";
    }
}

/* --- JSON-Ausgabe ------------------------------------------------------ */

/* Schreibt hoechstens cap-1 Zeichen, zaehlt aber die volle Laenge mit. */
typedef struct {
    char  *out;
    size_t cap;
    size_t len;
} jw_t;

static void jw_putc(jw_t *w, char c)
{
    if (w->len + 1u < w->cap) {
        w->out[w->len] = c;
    }
    w->len++;
}

static void jw_puts(jw_t *w, const char *s)
{
    while (*s != '\0') {
        jw_putc(w, *s++);
    }
}

static void jw_printf(jw_t *w, const char *fmt, ...)
{
    char tmp[MASTERAPP_JSON_MODULE_MAX + 32u];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) {
        jw_puts(w, tmp);
    }
}

/* JSON-Zeichenkette mit Anfuehrungszeichen; ", \ und Steuerzeichen maskiert,
 * Bytes ab 0x80 (UTF-8) unveraendert. */
static void jw_str_n(jw_t *w, const char *s, size_t n)
{
    jw_putc(w, '"');
    for (size_t i = 0; i < n && s[i] != '\0'; ++i) {
        const unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  jw_puts(w, "\\\""); break;
        case '\\': jw_puts(w, "\\\\"); break;
        case '\n': jw_puts(w, "\\n");  break;
        case '\r': jw_puts(w, "\\r");  break;
        case '\t': jw_puts(w, "\\t");  break;
        default:
            if (c < 0x20u) {
                jw_printf(w, "\\u%04x", (unsigned)c);
            } else {
                jw_putc(w, (char)c);
            }
            break;
        }
    }
    jw_putc(w, '"');
}

static void emit_status(jw_t *w, const masterapp_t *app)
{
    const busmaster_t *bus = app->bus;

    jw_printf(w, "{\"mode\":\"%s\",\"sep\":", masterapp_mode_name(app->mode));
    jw_str_n(w, &app->sep, 1u);
    jw_puts(w, ",\"text\":");
    jw_str_n(w, app->text, APP_TEXT_MAX);
    jw_printf(w, ",\"time_valid\":%s,\"align\":%u,\"detected\":%u,"
                 "\"enum_busy\":%s,\"warn\":%u,\"modules\":[",
              app->time_valid ? "true" : "false", (unsigned)app->align,
              (unsigned)bus->module_count,
              busmaster_enum_busy(bus) ? "true" : "false",
              (unsigned)busmaster_warnings(bus));

    const uint8_t n = field_width(app);
    for (uint8_t i = 0; i < n; ++i) {
        const bm_module_t *m = &bus->mod[i];
        const unsigned uidw = (m->uid_dup ? 1u : 0u) | (m->uid_changed ? 2u : 0u);
        jw_printf(w,
                  "%s{\"addr\":%u,\"online\":%s,\"ist\":%u,\"ziel\":%u,\"soll\":%u,"
                  "\"state\":%u,\"error\":%u,\"corr\":%u,\"blatt\":%u,\"fw\":%u,"
                  "\"miss\":%u,\"coll\":%u,\"uidw\":%u}",
                  (i == 0) ? "" : ",", (unsigned)(i + 1u),
                  m->online ? "true" : "false", (unsigned)m->ist_blatt,
                  (unsigned)m->ziel_blatt, m->soll_valid ? (unsigned)m->soll : 0u,
                  (unsigned)m->zustand, (unsigned)m->fehler, (unsigned)m->korrektur,
                  (unsigned)m->blattzahl, (unsigned)m->fw_version,
                  (unsigned)m->miss_count, (unsigned)m->collisions, uidw);
    }
    jw_puts(w, "]}");
}

size_t masterapp_status_json(const masterapp_t *app, char *out, size_t out_size)
{
    jw_t w = { out, out_size, 0 };
    emit_status(&w, app);
    if (out_size == 0u) {
        return 0;
    }
    if (w.len >= out_size) {
        out[out_size - 1u] = '\0';   /* abgeschnitten: sicher terminiert */
        return 0;
    }
    out[w.len] = '\0';
    return w.len;
}

size_t masterapp_status_json_len(const masterapp_t *app)
{
    jw_t w = { NULL, 0, 0 };
    emit_status(&w, app);
    return w.len;
}

size_t masterapp_status_json_max(uint8_t module_count)
{
    if (module_count > BUSMASTER_MAX_MODULES) {
        module_count = BUSMASTER_MAX_MODULES;
    }
    return MASTERAPP_STATUS_JSON_MAX(module_count);
}
