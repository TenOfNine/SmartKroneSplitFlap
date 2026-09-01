#include "eventlog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void evlog_init(evlog_t *lg)
{
    memset(lg, 0, sizeof(*lg));
}

void evlog_clear(evlog_t *lg)
{
    lg->head = 0;
    lg->count = 0;
    /* seq laeuft weiter -- die UI erkennt daran, dass etwas passiert ist */
}

void evlog_push(evlog_t *lg, uint32_t t_ms, evlog_sev_t sev,
                const char *src, const char *fmt, ...)
{
    evlog_entry_t *e = &lg->buf[lg->head];
    e->t_ms = t_ms;
    e->sev  = sev;

    e->src[0] = '\0';
    if (src != NULL) {
        strncpy(e->src, src, EVLOG_SRC_MAX - 1u);
        e->src[EVLOG_SRC_MAX - 1u] = '\0';
    }

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(e->msg, EVLOG_MSG_MAX, fmt, ap);
    va_end(ap);
    if (n < 0) {
        e->msg[0] = '\0';
    }

    lg->head = (uint16_t)((lg->head + 1u) % EVLOG_CAPACITY);
    if (lg->count < EVLOG_CAPACITY) {
        lg->count++;
    }
    lg->seq++;
}

/* JSON-String-Escaping fuer die Nachricht (src ist projektintern, ASCII). */
static size_t json_str(const char *s, char *out, size_t out_size)
{
    size_t o = 0;
    if (o + 1u >= out_size) {
        return 0;
    }
    out[o++] = '"';
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p) {
        const char *esc = NULL;
        char ubuf[7];
        switch (*p) {
        case '"':  esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\n': esc = "\\n";  break;
        case '\r': esc = "\\r";  break;
        case '\t': esc = "\\t";  break;
        default:
            if (*p < 0x20u) {
                snprintf(ubuf, sizeof(ubuf), "\\u%04x", (unsigned)*p);
                esc = ubuf;
            }
            break;
        }
        if (esc != NULL) {
            size_t l = strlen(esc);
            if (o + l + 1u >= out_size) {
                return 0;
            }
            memcpy(out + o, esc, l);
            o += l;
        } else {
            if (o + 2u >= out_size) {
                return 0;
            }
            out[o++] = (char)*p;
        }
    }
    if (o + 2u >= out_size) {
        return 0;
    }
    out[o++] = '"';
    out[o] = '\0';
    return o;
}

size_t evlog_json(const evlog_t *lg, evlog_sev_t min_sev,
                  char *out, size_t out_size)
{
    int n = snprintf(out, out_size, "{\"seq\":%lu,\"entries\":[",
                     (unsigned long)lg->seq);
    if (n < 0 || (size_t)n >= out_size) {
        return 0;
    }
    size_t pos = (size_t)n;
    bool first = true;

    /* neueste zuerst: vom zuletzt geschriebenen Platz rueckwaerts */
    for (uint16_t k = 0; k < lg->count; ++k) {
        uint16_t idx = (uint16_t)((lg->head + EVLOG_CAPACITY - 1u - k) % EVLOG_CAPACITY);
        const evlog_entry_t *e = &lg->buf[idx];
        if ((int)e->sev < (int)min_sev) {
            continue;
        }

        char msg_json[EVLOG_MSG_MAX * 2u + 4u];
        if (json_str(e->msg, msg_json, sizeof(msg_json)) == 0) {
            return 0;
        }

        n = snprintf(out + pos, out_size - pos,
                     "%s{\"t\":%lu,\"sev\":%d,\"src\":\"%s\",\"msg\":%s}",
                     first ? "" : ",", (unsigned long)e->t_ms, (int)e->sev,
                     e->src, msg_json);
        if (n < 0 || pos + (size_t)n >= out_size) {
            return 0;
        }
        pos += (size_t)n;
        first = false;
    }

    n = snprintf(out + pos, out_size - pos, "]}");
    if (n < 0 || pos + (size_t)n >= out_size) {
        return 0;
    }
    return pos + (size_t)n;
}
