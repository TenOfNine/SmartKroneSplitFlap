/* Siehe masterapp.h und docs/spezifikation.md 7. */
#include "masterapp.h"

#include <stdio.h>
#include <string.h>

#include "clocktext.h"

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
}

void masterapp_set_text(masterapp_t *app, const char *text, uint32_t now_ms)
{
    (void)now_ms;
    if (text == NULL) {
        text = "";
    }
    strncpy(app->text, text, APP_TEXT_MAX);
    app->text[APP_TEXT_MAX] = '\0';
    app->mode = APP_MODE_TEXT;
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
    const size_t w = app->module_count;

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

    uint8_t want[BUSMASTER_MAX_MODULES];
    masterapp_current_blaetter(app, want);

    const size_t w = app->module_count;
    bool changed = !app->have_shown;
    for (size_t i = 0; i < w && !changed; ++i) {
        if (want[i] != app->shown[i]) {
            changed = true;
        }
    }
    if (!changed) {
        return;
    }

    if (app->mode != APP_MODE_OFF) {
        busmaster_show(app->bus, want, (uint8_t)w);
    }
    memcpy(app->shown, want, w);
    app->have_shown = true;
}

static const char *mode_name(app_mode_t m)
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

size_t masterapp_status_json(const masterapp_t *app, char *out, size_t out_size)
{
    int n = snprintf(out, out_size,
                     "{\"mode\":\"%s\",\"sep\":\"%c\",\"text\":\"%s\","
                     "\"time_valid\":%s,\"align\":%u,\"detected\":%u,"
                     "\"enum_busy\":%s,\"modules\":[",
                     mode_name(app->mode), app->sep, app->text,
                     app->time_valid ? "true" : "false",
                     (unsigned)app->align, (unsigned)app->bus->module_count,
                     busmaster_enum_busy(app->bus) ? "true" : "false");
    if (n < 0 || (size_t)n >= out_size) {
        return 0;
    }
    size_t pos = (size_t)n;

    for (uint8_t i = 0; i < app->module_count; ++i) {
        const bm_module_t *m = &app->bus->mod[i];
        n = snprintf(out + pos, out_size - pos,
                     "%s{\"addr\":%u,\"online\":%s,\"ist\":%u,\"ziel\":%u,"
                     "\"state\":%u,\"error\":%u,\"corr\":%u,"
                     "\"blatt\":%u,\"fw\":%u,\"miss\":%u}",
                     (i == 0) ? "" : ",", (unsigned)(i + 1u),
                     m->online ? "true" : "false", m->ist_blatt, m->ziel_blatt,
                     m->zustand, m->fehler, m->korrektur,
                     m->blattzahl, m->fw_version, m->miss_count);
        if (n < 0 || pos + (size_t)n >= out_size) {
            return 0;
        }
        pos += (size_t)n;
    }

    n = snprintf(out + pos, out_size - pos, "]}");
    if (n < 0 || pos + (size_t)n >= out_size) {
        return 0;
    }
    return pos + (size_t)n;
}
