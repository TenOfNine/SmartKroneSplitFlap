/* Siehe charmap.h und docs/spezifikation.md 7.4 / Anhang A. */
#include "charmap.h"

#include <string.h>

uint8_t charmap_blatt(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c >= '0' && c <= '9') {
        return (uint8_t)(3u + (c - '0'));
    }
    if (c >= 'A' && c <= 'Z') {
        return (uint8_t)(13u + (c - 'A'));
    }
    if (c == '-') {
        return 39u;
    }
    if (c == '.') {
        return 40u;
    }
    return CHARMAP_LEERBILD;
}

/*
 * UTF-8-Eingabe in eine ASCII-Grossbuchstabenfolge aus dem darstellbaren
 * Zeichenvorrat expandieren. reduce_umlauts = 1 bildet die Umlaute auf ihren
 * Grundbuchstaben ab statt auf zwei Zeichen.
 * Rueckgabe: Laenge der erzeugten Folge (kann out_max ueberschreiten -> dann
 * abgeschnitten zurueckgegeben).
 */
static size_t expand(const char *s, char *out, size_t out_max, int reduce_umlauts)
{
    size_t n = 0;

    #define PUT1(ch)             \
        do {                     \
            if (n < out_max) {   \
                out[n] = (ch);   \
            }                    \
            n++;                 \
        } while (0)

    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == 0xC3u && p[1] != 0) {
            const unsigned char u = p[1];
            ++p;
            char a = 0, b = 0;
            switch (u) {
            case 0x84: case 0xA4: a = 'A'; b = 'E'; break;  /* Ä ä */
            case 0x96: case 0xB6: a = 'O'; b = 'E'; break;  /* Ö ö */
            case 0x9C: case 0xBC: a = 'U'; b = 'E'; break;  /* Ü ü */
            case 0x9F:            a = 'S'; b = 'S'; break;  /* ß   */
            default:              a = 0;   b = 0;   break;
            }
            if (a == 0) {
                PUT1(' ');
            } else if (reduce_umlauts) {
                PUT1(a);
            } else {
                PUT1(a);
                PUT1(b);
            }
            continue;
        }
        if (*p >= 0x80u) {
            /* sonstiges Mehrbyte-Zeichen: ueberspringen, als Leerbild werten */
            PUT1(' ');
            if ((*p & 0xE0u) == 0xC0u && p[1]) {
                p += 1;
            } else if ((*p & 0xF0u) == 0xE0u && p[1] && p[2]) {
                p += 2;
            } else if ((*p & 0xF8u) == 0xF0u && p[1] && p[2] && p[3]) {
                p += 3;
            }
            continue;
        }

        char c = (char)*p;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '.' || c == ' ') {
            PUT1(c);
        } else {
            PUT1(' ');   /* nicht darstellbar -> Leerbild */
        }
    }

    #undef PUT1
    return n;
}

size_t charmap_render(const char *utf8, uint8_t *blaetter, size_t field_width,
                      charmap_align_t align)
{
    if (utf8 == NULL) {
        utf8 = "";
    }

    char buf[64];
    const size_t cap = sizeof(buf);

    size_t len = expand(utf8, buf, cap, 0);
    if (len > field_width) {
        /* Platz reicht nicht: Umlaute auf den Grundbuchstaben reduzieren. */
        len = expand(utf8, buf, cap, 1);
    }
    if (len > cap) {
        len = cap;
    }
    if (len > field_width) {
        len = field_width;   /* immer noch zu lang -> abschneiden */
    }

    size_t pad_left = 0;
    switch (align) {
    case CHARMAP_ALIGN_RIGHT:
        pad_left = field_width - len;
        break;
    case CHARMAP_ALIGN_CENTER:
        pad_left = (field_width - len) / 2u;
        break;
    case CHARMAP_ALIGN_LEFT:
    default:
        pad_left = 0;
        break;
    }

    for (size_t i = 0; i < field_width; ++i) {
        if (i < pad_left || i >= pad_left + len) {
            blaetter[i] = CHARMAP_LEERBILD;
        } else {
            blaetter[i] = charmap_blatt(buf[i - pad_left]);
        }
    }
    return field_width;
}
