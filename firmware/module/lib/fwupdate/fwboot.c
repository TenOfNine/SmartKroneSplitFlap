/* Siehe fwboot.h. */
#include "fwboot.h"

#include <string.h>

#include "protocol.h"

bool fwboot_app_valid(uint16_t first_word, uint8_t marker)
{
    if (first_word == 0xFFFFu) {
        return false;   /* App-Bereich leer */
    }
    return marker == FWBOOT_MARKER_VALID || marker == FWBOOT_MARKER_BLANK;
}

void fwboot_init(fwboot_t *bt, uint8_t eeprom_addr, bool forced, bool app_valid,
                 uint32_t app_max, uint8_t bl_version,
                 fwupdate_write_page_fn write_page, fwboot_set_marker_fn set_marker,
                 void *ctx)
{
    memset(bt, 0, sizeof(*bt));
    bt->write_page = write_page;
    bt->set_marker = set_marker;
    bt->ctx = ctx;
    bt->app_max = app_max;
    /* wie die App (lib/enumeration): ungueltige EEPROM-Adresse -> Serviceadresse */
    bt->addr = (eeprom_addr >= PROTO_ADDR_MIN && eeprom_addr <= PROTO_ADDR_MAX)
                   ? eeprom_addr : PROTO_ADDR_SERVICE;
    bt->bl_version = bl_version;
    bt->app_valid = app_valid;
    bt->window = app_valid && !forced;
}

uint16_t fwboot_timeout_ms(const fwboot_t *bt)
{
    return bt->window ? FWBOOT_WINDOW_MS : FWBOOT_IDLE_MS;
}

fwboot_action_t fwboot_on_timeout(fwboot_t *bt)
{
    bt->window = false;
    /* Nach FW_BEGIN ist app_valid false, bis FW_END gelingt: eine
     * unterbrochene Uebertragung bleibt im Bootloader und wiederaufsetzbar. */
    return bt->app_valid ? FWBOOT_START_APP : FWBOOT_NONE;
}

static fwboot_action_t reply1(fwboot_t *bt, uint8_t cmd, uint8_t code)
{
    bt->reply_cmd = cmd;
    bt->reply[0] = code;
    bt->reply_len = 1u;
    return FWBOOT_REPLY;
}

static fwboot_action_t on_begin(fwboot_t *bt, const uint8_t *pl)
{
    const uint32_t total = (uint32_t)pl[0] | ((uint32_t)pl[1] << 8);
    const uint16_t crc = (uint16_t)(pl[2] | (pl[3] << 8));

    bt->active = false;
    if (total == 0u || total > bt->app_max) {
        return reply1(bt, CMD_FW_BEGIN, 0x00u);   /* nichts geschrieben */
    }
    /* Marker vor der ersten Seite ungueltig setzen (auch bei Neubeginn). */
    bt->set_marker(bt->ctx, FWBOOT_MARKER_UPDATE);
    bt->app_valid = false;
    fwupdate_begin(&bt->fu, bt->write_page, bt->ctx, total, crc);
    bt->active = true;
    return reply1(bt, CMD_FW_BEGIN, 0x01u);
}

static fwboot_action_t on_end(fwboot_t *bt)
{
    if (!bt->active) {
        return reply1(bt, CMD_FW_END, 0x00u);   /* keine Uebertragung offen */
    }
    bt->active = false;   /* Erfolg oder Fehler: Neubeginn nur per FW_BEGIN */
    const fwupdate_result_t r = fwupdate_finish(&bt->fu);
    if (r != FWUPDATE_OK) {
        return reply1(bt, CMD_FW_END, (uint8_t)r);
    }
    bt->set_marker(bt->ctx, FWBOOT_MARKER_VALID);
    bt->app_valid = true;
    reply1(bt, CMD_FW_END, 0x01u);
    return FWBOOT_REPLY_RESET;
}

fwboot_action_t fwboot_on_frame(fwboot_t *bt, uint8_t cmd, uint8_t addr,
                                const uint8_t *payload, uint8_t len)
{
    if (addr != bt->addr) {
        return FWBOOT_IGNORE;   /* Broadcast, Serviceadresse, fremde Karten */
    }
    const bool begin = (cmd == CMD_FW_BEGIN && len == 4u);

    if (bt->window) {
        if (!begin) {
            return FWBOOT_START_APP;   /* Master spricht die App an */
        }
        bt->window = false;
    }

    switch (cmd) {
    case CMD_GET_VERSION:
        if (len != 0u) {
            break;
        }
        bt->reply_cmd = CMD_GET_VERSION;
        bt->reply[0] = 1u;
        bt->reply[1] = 0xFFu;   /* App-Version im Bootloader unbekannt */
        bt->reply[2] = 0xFFu;
        bt->reply[3] = (uint8_t)(PROTO_VER_FLAG_BOOTLOADER |
                                 (bt->app_valid ? PROTO_VER_FLAG_APP_VALID : 0u));
        bt->reply[4] = bt->bl_version;
        bt->reply_len = 5u;
        return FWBOOT_REPLY;

    case CMD_FW_BEGIN:
        if (!begin) {
            break;
        }
        return on_begin(bt, payload);

    case CMD_FW_DATA: {
        if (len < 3u || len > 2u + PROTO_FW_CHUNK) {
            break;
        }
        const uint32_t off = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8);
        const bool ok = bt->active &&
            fwupdate_chunk(&bt->fu, off, &payload[2], (uint8_t)(len - 2u)) == FWUPDATE_OK;
        return reply1(bt, CMD_FW_DATA, ok ? 0x01u : 0x00u);
    }

    case CMD_FW_END:
        if (len != 0u) {
            break;
        }
        return on_end(bt);

    case CMD_ENTER_BOOTLOADER:
        if (len != 0u) {
            break;
        }
        return FWBOOT_NONE;   /* schon im Bootloader */

    default:
        break;
    }
    return FWBOOT_IGNORE;
}
