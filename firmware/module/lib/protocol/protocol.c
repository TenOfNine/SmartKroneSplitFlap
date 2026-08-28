/*
 * Implementierung des Busprotokolls, siehe protocol.h und
 * docs/spezifikation.md Abschnitt 5.
 */
#include "protocol.h"

#include <string.h>

/* --- Kommandotabelle (Abschnitt 5.4) ---------------------------------- */

/* payload_len == -1 bedeutet "variabel" (nur SET_ALL). */
static const proto_cmd_info_t k_cmd_table[] = {
    { CMD_SET,         "SET",         ADDRESSING_UNICAST,   1,  true  },
    { CMD_SET_ALL,     "SET_ALL",     ADDRESSING_BROADCAST, -1, false },
    { CMD_GO,          "GO",          ADDRESSING_BROADCAST, 0,  false },
    { CMD_STOP,        "STOP",        ADDRESSING_BOTH,      0,  true  },
    { CMD_GET_STATUS,  "GET_STATUS",  ADDRESSING_UNICAST,   0,  true  },
    { CMD_HOME,        "HOME",        ADDRESSING_BOTH,      0,  true  },
    { CMD_SET_CONFIG,  "SET_CONFIG",  ADDRESSING_UNICAST,   4,  true  },
    { CMD_GET_CONFIG,  "GET_CONFIG",  ADDRESSING_UNICAST,   0,  true  },
    { CMD_IDENTIFY,    "IDENTIFY",    ADDRESSING_UNICAST,   1,  true  },
    { CMD_ENUM_RESET,  "ENUM_RESET",  ADDRESSING_BROADCAST, 0,  false },
    { CMD_ENUM_ASSIGN, "ENUM_ASSIGN", ADDRESSING_BROADCAST, 1,  true  },
    { CMD_ENUM_DONE,   "ENUM_DONE",   ADDRESSING_BROADCAST, 0,  false },
    { CMD_GET_UID,     "GET_UID",     ADDRESSING_UNICAST,   0,  true  },
    { CMD_PING,        "PING",        ADDRESSING_UNICAST,   0,  true  },
};

#define K_CMD_TABLE_LEN (sizeof(k_cmd_table) / sizeof(k_cmd_table[0]))

const proto_cmd_info_t *proto_cmd_lookup(uint8_t cmd)
{
    for (size_t i = 0; i < K_CMD_TABLE_LEN; ++i) {
        if (k_cmd_table[i].cmd == cmd) {
            return &k_cmd_table[i];
        }
    }
    return NULL;
}

bool proto_cmd_is_valid(uint8_t cmd, uint8_t addr, uint8_t payload_len)
{
    const proto_cmd_info_t *info = proto_cmd_lookup(cmd);
    if (info == NULL) {
        return false;
    }

    const bool is_broadcast = (addr == PROTO_ADDR_BROADCAST);
    switch (info->addressing) {
    case ADDRESSING_UNICAST:
        if (is_broadcast || addr > PROTO_ADDR_MAX) {
            return false;
        }
        break;
    case ADDRESSING_BROADCAST:
        if (!is_broadcast) {
            return false;
        }
        break;
    case ADDRESSING_BOTH:
        if (!is_broadcast && addr > PROTO_ADDR_MAX) {
            return false;
        }
        break;
    }

    if (info->payload_len >= 0 && payload_len != (uint16_t)info->payload_len) {
        return false;
    }
    if (payload_len > PROTO_MAX_PAYLOAD) {
        return false;
    }
    return true;
}

/* --- CRC16/MODBUS ---------------------------------------------------- */

uint16_t proto_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* --- Rahmen serialisieren ------------------------------------------- */

size_t proto_encode(const proto_frame_t *frame, uint8_t *out, size_t out_size)
{
    if (frame == NULL || out == NULL) {
        return 0;
    }
    if (frame->payload_len > PROTO_MAX_PAYLOAD) {
        return 0;
    }

    const uint8_t len = (uint8_t)(PROTO_LEN_OVERHEAD + frame->payload_len);
    const size_t total = (size_t)(5u + frame->payload_len + 2u);
    if (out_size < total) {
        return 0;
    }

    out[0] = PROTO_PREAMBLE_0;
    out[1] = PROTO_PREAMBLE_1;
    out[2] = len;
    out[3] = frame->cmd;
    out[4] = frame->addr;
    memcpy(&out[5], frame->payload, frame->payload_len);

    /* CRC ueber Offset 2 .. 4+n, also LEN, CMD, ADDR, PAYLOAD. */
    const uint16_t crc = proto_crc16(&out[2], (size_t)(3u + frame->payload_len));
    out[5 + frame->payload_len]     = (uint8_t)(crc & 0xFFu);
    out[5 + frame->payload_len + 1] = (uint8_t)(crc >> 8);

    return total;
}

/* --- Streaming-Parser --------------------------------------------- */

void proto_parser_reset(proto_parser_t *parser)
{
    parser->pos = 0;
    parser->need = 0;
    parser->sync = 0;
}

static proto_parse_result_t finish_frame(proto_parser_t *parser)
{
    const uint16_t need = parser->need;
    const uint8_t len = parser->buf[2];
    const uint8_t payload_len = (uint8_t)(len - PROTO_LEN_OVERHEAD);

    /* CRC ueber Offset 2 .. 4+payload_len (need-4 Byte ab buf[2]). */
    const uint16_t calc = proto_crc16(&parser->buf[2], (size_t)(need - 4u));
    const uint16_t recv =
        (uint16_t)parser->buf[need - 2] | ((uint16_t)parser->buf[need - 1] << 8);

    proto_parser_reset(parser);

    if (calc != recv) {
        return PARSE_ERR_CRC;
    }

    parser->frame.cmd = parser->buf[3];
    parser->frame.addr = parser->buf[4];
    parser->frame.payload_len = payload_len;
    memcpy(parser->frame.payload, &parser->buf[5], payload_len);
    return PARSE_FRAME_OK;
}

proto_parse_result_t proto_parser_feed(proto_parser_t *parser, uint8_t byte)
{
    /* Phase 1: auf die Praeambel 0xAA 0x55 synchronisieren. */
    if (parser->pos == 0) {
        if (parser->sync == 0) {
            if (byte == PROTO_PREAMBLE_0) {
                parser->sync = 1;
            }
            return PARSE_NEED_MORE;
        }
        /* sync == 1: warten auf 0x55 */
        if (byte == PROTO_PREAMBLE_1) {
            parser->buf[0] = PROTO_PREAMBLE_0;
            parser->buf[1] = PROTO_PREAMBLE_1;
            parser->pos = 2;
            parser->need = 0;
            return PARSE_NEED_MORE;
        }
        if (byte != PROTO_PREAMBLE_0) {
            parser->sync = 0;  /* Fehlstart, erneut auf 0xAA warten */
        }
        return PARSE_NEED_MORE;
    }

    parser->buf[parser->pos++] = byte;

    /* Phase 2: LEN gelesen -> Gesamtlaenge bestimmen. */
    if (parser->pos == 3) {
        const uint8_t len = parser->buf[2];
        if (len < PROTO_LEN_OVERHEAD ||
            len > PROTO_LEN_OVERHEAD + PROTO_MAX_PAYLOAD) {
            proto_parser_reset(parser);
            return PARSE_ERR_LEN;
        }
        parser->need = (uint16_t)(3u + len);
        return PARSE_NEED_MORE;
    }

    /* Phase 3: Rahmen vollstaendig? */
    if (parser->need != 0 && parser->pos == parser->need) {
        return finish_frame(parser);
    }
    return PARSE_NEED_MORE;
}
