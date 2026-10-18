/* Siehe selftest.h und docs/spezifikation.md 7.3. */
#include "selftest.h"

#include <stdio.h>
#include <string.h>

/* GET_TEST-Zustand "fertig" (Modul: MOTION_TEST_DONE). */
#define TEST_STATE_DONE 2u

void selftest_init(selftest_t *st)
{
    memset(st, 0, sizeof(*st));
}

bool selftest_start(selftest_t *st, busmaster_t *bm, uint8_t count, uint32_t now_ms)
{
    if (st->running || count == 0u) {
        return false;
    }
    if (count > BUSMASTER_MAX_MODULES) {
        count = BUSMASTER_MAX_MODULES;
    }
    memset(st, 0, sizeof(*st));
    st->running = true;
    st->count = count;
    st->start_ms = now_ms;
    busmaster_selftest(bm, PROTO_ADDR_BROADCAST);
    return true;
}

/* Modul abgeschlossen: Ergebnis da, oder es antwortet nicht. */
static bool module_final(const selftest_t *st, const bm_module_t *m, uint8_t i)
{
    if (m->test_known) {
        return m->test_state == TEST_STATE_DONE;
    }
    return st->polls[i] >= SELFTEST_MAX_POLLS;
}

bool selftest_tick(selftest_t *st, busmaster_t *bm, uint32_t now_ms)
{
    if (!st->running) {
        return false;
    }
    const uint32_t age = now_ms - st->start_ms;
    if (age < SELFTEST_SETTLE_MS) {
        return false;
    }

    bool all = true;
    bool asked = false;
    for (uint8_t i = 0; i < st->count; ++i) {
        const bm_module_t *m = &bm->mod[i];
        if (module_final(st, m, i)) {
            continue;
        }
        all = false;
        /* hoechstens eine Abfrage je Aufruf, Warteschlange nicht fluten */
        if (!asked && bm->queue_count < BUSMASTER_QUEUE_LEN / 2u &&
            (st->polls[i] == 0u || (uint32_t)(now_ms - st->last_poll_ms[i]) >= SELFTEST_POLL_MS)) {
            busmaster_poll_test(bm, (uint8_t)(i + 1u));
            st->last_poll_ms[i] = now_ms;
            if (st->polls[i] < 255u) {
                st->polls[i]++;
            }
            asked = true;
        }
    }
    if (!all && age < SELFTEST_TIMEOUT_MS) {
        return false;
    }
    for (uint8_t i = 0; i < st->count; ++i) {
        st->result[i] = (uint8_t)selftest_evaluate(&bm->mod[i]);
    }
    st->running = false;
    st->done = true;
    return true;
}

selftest_result_t selftest_evaluate(const bm_module_t *m)
{
    if (!m->test_known) {
        return ST_RES_NO_ANSWER;
    }
    if (m->test_state != TEST_STATE_DONE || m->test_rot_ms == 0u) {
        return ST_RES_INCOMPLETE;
    }
    const uint8_t z = m->test_blattzahl;
    if (z != 40u && z != 64u && z != 80u) {
        return ST_RES_BLATTZAHL;
    }
    if (m->test_error == 0x03u) {
        return ST_RES_BLATTZAHL;   /* gueltig, aber ungleich der Konfiguration */
    }
    const uint32_t soll = (uint32_t)z * SELFTEST_MS_PER_BLATT;
    const uint32_t tol = soll * SELFTEST_TOL_PCT / 100u;
    const uint32_t rot = m->test_rot_ms;
    if (rot + tol < soll || rot > soll + tol || m->test_max_ms > SELFTEST_STEP_MAX_MS) {
        return ST_RES_TIMING;
    }
    return ST_RES_OK;
}

const char *selftest_result_name(selftest_result_t r)
{
    switch (r) {
    case ST_RES_OK:         return "ok";
    case ST_RES_NO_ANSWER:  return "keine_antwort";
    case ST_RES_INCOMPLETE: return "unvollstaendig";
    case ST_RES_BLATTZAHL:  return "blattzahl";
    case ST_RES_TIMING:     return "timing";
    case ST_RES_NONE:
    default:                return "nicht_getestet";
    }
}

size_t selftest_json(const selftest_t *st, const busmaster_t *bm, char *out, size_t out_size)
{
    int n = snprintf(out, out_size, "{\"running\":%s,\"done\":%s,\"modules\":[",
                     st->running ? "true" : "false", st->done ? "true" : "false");
    if (n < 0 || (size_t)n >= out_size) {
        return 0;
    }
    size_t pos = (size_t)n;
    for (uint8_t i = 0; i < st->count; ++i) {
        const bm_module_t *m = &bm->mod[i];
        const selftest_result_t r = st->done ? (selftest_result_t)st->result[i] : ST_RES_NONE;
        n = snprintf(out + pos, out_size - pos,
                     "%s{\"addr\":%u,\"res\":\"%s\",\"blatt\":%u,\"rot\":%u,"
                     "\"min\":%u,\"max\":%u,\"err\":%u,\"state\":%u}",
                     i == 0u ? "" : ",", (unsigned)(i + 1u), selftest_result_name(r),
                     (unsigned)m->test_blattzahl, (unsigned)m->test_rot_ms,
                     (unsigned)m->test_min_ms, (unsigned)m->test_max_ms,
                     (unsigned)m->test_error, m->test_known ? (unsigned)m->test_state : 0u);
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
