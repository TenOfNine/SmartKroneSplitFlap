/* Siehe posring.h und docs/spezifikation.md Abschnitt 6.2. */
#include "posring.h"

/* a ist neuer als b (Folgenummern modulo 255, Abstand 1..127). */
static bool seq_newer(uint8_t a, uint8_t b)
{
    const uint8_t d = (uint8_t)(((uint16_t)a + POSRING_SEQ_MOD - b) % POSRING_SEQ_MOD);
    return d != 0u && d < 128u;
}

static uint16_t slot_addr(const posring_t *r, uint8_t slot)
{
    return (uint16_t)(r->base + (uint16_t)slot * 2u);
}

/* Laenge der lueckenlosen seq-Kette, die in Slot i endet (i, i-1, ... mit
 * seq, seq-1, ...). Der echte neueste Eintrag traegt die laengste Kette; ein
 * beim Schreiben abgerissenes seq-Byte (zusaetzliche 1-Bits) steht allein und
 * kann ihn so nicht verdraengen, auch wenn es modulo 255 "neuer" aussieht. */
static uint8_t chain_len(const posring_t *r, uint8_t i, uint8_t seq)
{
    uint8_t n = 1;
    uint8_t j = i;
    uint8_t want = seq;
    while (n < r->slots) {
        j = (uint8_t)((j + r->slots - 1u) % r->slots);
        want = (uint8_t)((want + POSRING_SEQ_MOD - 1u) % POSRING_SEQ_MOD);
        if (r->read(r->ctx, slot_addr(r, j)) != want) {
            break;
        }
        n++;
    }
    return n;
}

void posring_init(posring_t *r, uint16_t base, uint8_t slots,
                  posring_read_fn read, posring_write_fn write, void *ctx)
{
    r->read = read;
    r->write = write;
    r->ctx = ctx;
    r->base = base;
    r->slots = slots;
    r->newest = POSRING_NONE;
    r->newest_seq = 0;
    r->newest_pos = POSRING_INVALID;

    uint8_t best_len = 0;
    for (uint8_t i = 0; i < slots; ++i) {
        const uint8_t seq = read(ctx, slot_addr(r, i));
        if (seq >= POSRING_SEQ_MOD) {
            continue;  /* leerer Slot (0xFF) */
        }
        const uint8_t len = chain_len(r, i, seq);
        if (r->newest == POSRING_NONE || len > best_len ||
            (len == best_len && seq_newer(seq, r->newest_seq))) {
            r->newest = i;
            r->newest_seq = seq;
            best_len = len;
        }
    }
    if (r->newest != POSRING_NONE) {
        r->newest_pos = read(ctx, (uint16_t)(slot_addr(r, r->newest) + 1u));
    }
}

uint8_t posring_position(const posring_t *r)
{
    if (r->newest == POSRING_NONE || r->newest_pos == 0xFFu) {
        return POSRING_INVALID;
    }
    return r->newest_pos;
}

static void write_entry(posring_t *r, uint8_t pos)
{
    uint8_t slot = 0;
    uint8_t seq = 0;
    if (r->newest != POSRING_NONE) {
        slot = (uint8_t)((r->newest + 1u) % r->slots);
        seq = (uint8_t)((r->newest_seq + 1u) % POSRING_SEQ_MOD);
    }
    const uint16_t a = slot_addr(r, slot);
    r->write(r->ctx, (uint16_t)(a + 1u), pos);   /* erst pos ... */
    r->write(r->ctx, a, seq);                    /* ... dann seq */
    r->newest = slot;
    r->newest_seq = seq;
    r->newest_pos = pos;
}

void posring_store(posring_t *r, uint8_t pos)
{
    if (pos == POSRING_INVALID || pos == 0xFFu) {
        (void)posring_invalidate(r);
        return;
    }
    if (posring_position(r) == pos) {
        return;  /* unveraendert gueltig, kein Verschleiss */
    }
    write_entry(r, pos);
}

bool posring_invalidate(posring_t *r)
{
    if (posring_position(r) == POSRING_INVALID) {
        return false;  /* schon ungueltig, kein Verschleiss */
    }
    write_entry(r, POSRING_INVALID);
    return true;
}
