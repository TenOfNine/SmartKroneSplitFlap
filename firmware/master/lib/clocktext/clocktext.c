/* Siehe clocktext.h und docs/spezifikation.md 7.6 / 7.7. */
#include "clocktext.h"

size_t clocktext_format(char *out, size_t out_size, uint8_t hh, uint8_t mm,
                        uint8_t ss, bool with_seconds, char sep)
{
    if (hh > 23 || mm > 59 || ss > 59) {
        return 0;
    }
    if (sep != '.' && sep != '-') {
        sep = '.';
    }

    const size_t need = with_seconds ? 8u : 5u;
    if (out_size < need + 1u) {
        return 0;
    }

    size_t i = 0;
    out[i++] = (char)('0' + hh / 10u);
    out[i++] = (char)('0' + hh % 10u);
    out[i++] = sep;
    out[i++] = (char)('0' + mm / 10u);
    out[i++] = (char)('0' + mm % 10u);
    if (with_seconds) {
        out[i++] = sep;
        out[i++] = (char)('0' + ss / 10u);
        out[i++] = (char)('0' + ss % 10u);
    }
    out[i] = '\0';
    return i;
}
