/* Siehe fwupdate.h. */
#include "fwupdate.h"

#include <string.h>

/* CRC16/MODBUS, gleiche Definition wie proto_crc16 -- hier eigenstaendig, damit
 * der Bootloader lib/protocol nicht komplett einbinden muss. */
static uint16_t crc16_step(uint16_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

void fwupdate_begin(fwupdate_t *fu, fwupdate_write_page_fn write_page, void *ctx,
                    uint32_t total_len, uint16_t expect_crc)
{
    memset(fu, 0, sizeof(*fu));
    fu->write_page = write_page;
    fu->ctx = ctx;
    fu->total_len = total_len;
    fu->expect_crc = expect_crc;
    fu->crc = 0xFFFFu;
    memset(fu->page, 0xFF, sizeof(fu->page));
}

static bool flush_page(fwupdate_t *fu)
{
    if (fu->page_fill == 0u) {
        return true;
    }
    const bool ok = fu->write_page(fu->ctx, fu->page_base, fu->page);
    memset(fu->page, 0xFF, sizeof(fu->page));
    fu->page_base += FWUPDATE_PAGE;
    fu->page_fill = 0u;
    return ok;
}

fwupdate_result_t fwupdate_chunk(fwupdate_t *fu, uint32_t offset,
                                 const uint8_t *data, uint8_t len)
{
    if (offset != fu->received) {
        return FWUPDATE_ERR_ORDER;   /* nur streng aufsteigend, kein Nachreichen */
    }
    if (offset > fu->total_len || (uint32_t)len > fu->total_len - offset) {
        return FWUPDATE_ERR_RANGE;
    }

    fu->crc = crc16_step(fu->crc, data, len);
    fu->received += len;

    for (uint8_t i = 0; i < len; ++i) {
        const uint32_t abs = offset + i;
        const uint32_t base = abs - (abs % FWUPDATE_PAGE);
        if (fu->page_fill != 0u && base != fu->page_base) {
            if (!flush_page(fu)) {
                return FWUPDATE_ERR_WRITE;
            }
        }
        if (fu->page_fill == 0u) {
            fu->page_base = base;
        }
        fu->page[abs % FWUPDATE_PAGE] = data[i];
        fu->page_fill = (uint8_t)((abs % FWUPDATE_PAGE) + 1u);
        if (fu->page_fill == FWUPDATE_PAGE) {
            if (!flush_page(fu)) {
                return FWUPDATE_ERR_WRITE;
            }
        }
    }
    return FWUPDATE_OK;
}

fwupdate_result_t fwupdate_finish(fwupdate_t *fu)
{
    if (!flush_page(fu)) {
        return FWUPDATE_ERR_WRITE;
    }
    if (fu->received != fu->total_len) {
        return FWUPDATE_ERR_LENGTH;
    }
    if (fu->crc != fu->expect_crc) {
        return FWUPDATE_ERR_CRC;
    }
    return FWUPDATE_OK;
}
