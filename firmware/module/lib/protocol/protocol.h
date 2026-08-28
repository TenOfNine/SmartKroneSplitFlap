/*
 * Busprotokoll der KRONE-REW-Modulsteuerung.
 *
 * Hardwareunabhaengig, auf dem Host testbar (pio test -e native).
 * Kein Registerzugriff, keine Annahmen ueber USART oder Zeitgeber.
 *
 * Grundlage: docs/spezifikation.md Abschnitt 5.3 (Rahmenformat), 5.4 (Kommandos),
 * 5.5 (Statusantwort). Bei Widerspruch zur Spezifikation gilt die Spezifikation.
 *
 * Rahmen:
 *   Offset  Laenge  Feld
 *     0       1     Praeambel 0xAA
 *     1       1     Praeambel 0x55
 *     2       1     LEN   = Anzahl Bytes ab Offset 3 einschliesslich CRC (= n + 4)
 *     3       1     CMD
 *     4       1     ADDR  (0 = Broadcast, 1..250 = Modul)
 *     5       n     PAYLOAD
 *    5+n      2     CRC16/MODBUS ueber Offset 2 .. 4+n, Little Endian
 */
#ifndef KRONE_PROTOCOL_H
#define KRONE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Konstanten des Rahmens ------------------------------------------------ */

#define PROTO_PREAMBLE_0 0xAAu
#define PROTO_PREAMBLE_1 0x55u

/* LEN deckt CMD + ADDR + PAYLOAD + CRC ab, also mindestens 4 (leeres Payload). */
#define PROTO_LEN_OVERHEAD 4u

/* Groesstes Payload: SET_ALL fuer die spezifizierten 10 Module belegt 10 Byte.
 * 32 gibt Reserve, ohne den RAM des ATtiny1616 zu belasten. */
#define PROTO_MAX_PAYLOAD 32u

/* Vollstaendiger Rahmen: 2 Praeambel + LEN + CMD + ADDR + Payload + 2 CRC. */
#define PROTO_MAX_FRAME (5u + PROTO_MAX_PAYLOAD + 2u)

#define PROTO_ADDR_BROADCAST 0u
#define PROTO_ADDR_MIN 1u
#define PROTO_ADDR_MAX 250u
#define PROTO_ADDR_SERVICE 250u

/* --- Kommandos (Abschnitt 5.4) ------------------------------------------- */

typedef enum {
    CMD_SET        = 0x01,
    CMD_SET_ALL    = 0x02,
    CMD_GO         = 0x03,
    CMD_STOP       = 0x04,
    CMD_GET_STATUS = 0x10,
    CMD_HOME       = 0x20,
    CMD_SET_CONFIG = 0x30,
    CMD_GET_CONFIG = 0x31,
    CMD_IDENTIFY   = 0x40,
    CMD_ENUM_RESET  = 0x50,
    CMD_ENUM_ASSIGN = 0x51,
    CMD_ENUM_DONE   = 0x52,
    CMD_GET_UID     = 0x53,
    CMD_PING        = 0xF0,
} proto_cmd_t;

/* Adressierungsart eines Kommandos. */
typedef enum {
    ADDRESSING_UNICAST,    /* nur an eine Karte (ADDR 1..250) */
    ADDRESSING_BROADCAST,  /* nur als Broadcast (ADDR 0) */
    ADDRESSING_BOTH,       /* beides zulaessig */
} proto_addressing_t;

typedef struct {
    uint8_t            cmd;
    const char        *name;
    proto_addressing_t addressing;
    /* Erwartete Payload-Laenge. -1 = variabel (SET_ALL), sonst exakt. */
    int16_t            payload_len;
    bool               has_response;  /* Slave sendet eine Antwort */
} proto_cmd_info_t;

/* Liefert den Tabelleneintrag zu cmd oder NULL, wenn das Kommando unbekannt ist. */
const proto_cmd_info_t *proto_cmd_lookup(uint8_t cmd);

/* Prueft cmd/addr/len gegen die Kommandotabelle. */
bool proto_cmd_is_valid(uint8_t cmd, uint8_t addr, uint8_t payload_len);

/* --- CRC ---------------------------------------------------------------- */

/* CRC16/MODBUS: Polynom 0xA001 (reflektiert), Startwert 0xFFFF, kein XorOut. */
uint16_t proto_crc16(const uint8_t *data, size_t len);

/* --- Rahmen ------------------------------------------------------------- */

typedef struct {
    uint8_t cmd;
    uint8_t addr;
    uint8_t payload[PROTO_MAX_PAYLOAD];
    uint8_t payload_len;
} proto_frame_t;

/*
 * Serialisiert frame nach out (Kapazitaet out_size).
 * Rueckgabe: Laenge des Rahmens in Byte, oder 0 bei Fehler
 * (payload_len zu gross, Puffer zu klein).
 */
size_t proto_encode(const proto_frame_t *frame, uint8_t *out, size_t out_size);

/* --- Streaming-Parser -------------------------------------------------- */

typedef enum {
    PARSE_NEED_MORE,   /* Rahmen noch unvollstaendig */
    PARSE_FRAME_OK,    /* vollstaendiger, CRC-gepruefter Rahmen in parser->frame */
    PARSE_ERR_LEN,     /* LEN ausserhalb des zulaessigen Bereichs */
    PARSE_ERR_CRC,     /* CRC stimmt nicht */
} proto_parse_result_t;

typedef struct {
    /* interner Zustand, nicht von aussen anfassen */
    uint8_t  buf[PROTO_MAX_FRAME];
    uint16_t pos;
    uint16_t need;       /* Gesamtlaenge des aktuellen Rahmens, sobald LEN bekannt */
    uint8_t  sync;       /* Praeambel-Fortschritt: 0, 1 */

    /* nach PARSE_FRAME_OK gueltig */
    proto_frame_t frame;
} proto_parser_t;

void proto_parser_reset(proto_parser_t *parser);

/*
 * Fuettert ein empfangenes Byte. Bei PARSE_FRAME_OK steht der Rahmen in
 * parser->frame und der interne Zustand ist bereits fuer den naechsten Rahmen
 * zurueckgesetzt. Bei PARSE_ERR_* ebenso; der Parser synchronisiert sich dann
 * wieder auf die Praeambel.
 */
proto_parse_result_t proto_parser_feed(proto_parser_t *parser, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_PROTOCOL_H */
