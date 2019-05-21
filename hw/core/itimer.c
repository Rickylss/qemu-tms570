/*
 * General purpose implementation of a simple periodic up-counter timer.
 *
 * Copyright (c) 2019 xiaohaibiao.
 *
 * This code is licensed under the GNU LGPL.
 */
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "qemu/timer.h"
#include "hw/itimer.h"
#include "qemu/host-utils.h"
#include "sysemu/replay.h"

struct itimer_state
{
    QEMUBH *bh;
    QEMUTimer *timer;

    uint8_t enabled; /* 0 = disabled, 1 = periodic, 2 = oneshot.  */
    uint64_t delta;
    uint32_t period_frac;
    int64_t period;
    int64_t last_event;
    int64_t next_event;
    uint64_t compare;
};

/* Use a bottom-half routine to avoid reentrancy issues.  */
static void itimer_trigger(itimer_state *s)
{
    if (s->bh) {
        replay_bh_schedule_event(s->bh);
    }
}

static void itimer_reload(itimer_state *s)
{
    uint32_t period_frac = s->period_frac;
    uint64_t period = s->period;
    uint64_t delta = s->compare - s->delta;

    if (delta == 0) {
        itimer_trigger(s);
        delta = 0xffffffff;//rolls over at 0xffffffff to 0x00000000
    }
    if (delta == 0 || s->period == 0) {
        fprintf(stderr, "Timer with period zero, disabling\n");
        s->enabled = 0;
        return;
    }

    /*
     * Artificially limit timeout rate to something
     * achievable under QEMU.  Otherwise, QEMU spends all
     * its time generating timer interrupts, and there
     * is no forward progress.
     * About ten microseconds is the fastest that really works
     * on the current generation of host machines.
     */

    if (s->enabled == 1 && (delta * period < 10000) && !use_icount) {
        period = 10000 / delta;
        period_frac = 0;
    }

    s->last_event = s->next_event;
    s->next_event = s->last_event + delta * period;
    if (period_frac) {
        s->next_event += ((int64_t)period_frac * delta) >> 32;
    }
    timer_mod(s->timer, s->next_event);
}

static void itimer_tick(void *opaque)
{
    itimer_state *s = (itimer_state *)opaque;
    itimer_trigger(s);
    s->delta = s->compare;
    itimer_reload(s);
}

uint64_t itimer_get_count(itimer_state *s)
{
    uint64_t counter;
    uint64_t delta = s->compare - s->delta;

    if (s->enabled) {
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        int64_t next = s->next_event;
        bool expired = (now - next >= 0);

        uint64_t rem;
        uint64_t div;
        int clz1, clz2;
        int shift;
        uint32_t period_frac = s->period_frac;
        uint64_t period = s->period;

        if ((delta * period < 10000) && !use_icount) {
            period = 10000 / delta;
            period_frac = 0;
        }
        /* We need to divide time by period, where time is stored in
           rem (64-bit integer) and period is stored in period/period_frac
           (64.32 fixed point).
          
           Doing full precision division is hard, so scale values and
           do a 64-bit division.  The result should be rounded down,
           so that the rounding error never causes the timer to go
           backwards.
        */
       if (expired) {
            rem = now - next;
       } else {
            rem = next - now;
       }
       
        div = period;
        clz1 = clz64(rem);
        clz2 = clz64(div);
        shift = clz1 < clz2 ? clz1 : clz2;
        rem <<= shift;
        div <<= shift;
        if (shift >= 32) {
            div |= ((uint64_t)period_frac << (shift - 32));
        } else {
            if (shift != 0)
                div |= (period_frac >> (32 - shift));
            /* Look at remaining bits of period_frac and round div up if 
               necessary.  */
            if ((uint32_t)(period_frac << shift))
                div += 1;
        }

        counter = rem / div;
        if (expired) {
            counter =  counter % 0xffffffff + s->compare;
        }

    } else {
        counter = s->delta;
    }
    return counter;
}

void itimer_set_count(itimer_state *s, uint64_t count)
{
    s->delta = count;
    if (s->enabled) {
        s->next_event = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        itimer_reload(s);
    }
}

void itimer_run(itimer_state *s)
{
    bool was_disabled = !s->enabled;

    if (was_disabled && s->period == 0) {
        fprintf(stderr, "Timer with period zero, disabling\n");
        return;
    }
    s->enabled = 1;
    if (was_disabled) {
        s->next_event = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        itimer_reload(s);
    }
}

/* Pause a timer.  Note that this may cause it to "lose" time, even if it
   is immediately restarted.  */
void itimer_stop(itimer_state *s)
{
    if (!s->enabled)
        return;

    s->delta = itimer_get_count(s);
    timer_del(s->timer);
    s->enabled = 0;
}

/* Set counter increment interval in nanoseconds.  */
void itimer_set_period(itimer_state *s, int64_t period)
{
    s->delta = itimer_get_count(s);
    s->period = period;
    s->period_frac = 0;
    if (s->enabled) {
        s->next_event = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        itimer_reload(s);
    }
}

/* Set counter frequency in Hz.  */
void itimer_set_freq(itimer_state *s, uint32_t freq)
{
    s->delta = itimer_get_count(s);
    s->period = 1000000000ll / freq;
    s->period_frac = (1000000000ll << 32) / freq;
    if (s->enabled) {
        s->next_event = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        itimer_reload(s);
    }
}

/* Set compare value */
void itimer_set_compare(itimer_state *s, uint32_t compare)
{
    s->compare = compare;
    if (s->enabled) {
        s->next_event = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        itimer_reload(s);
    }
}

const VMStateDescription vmstate_itimer = {
    .name = "itimer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(enabled, itimer_state),
        VMSTATE_UINT64(compare, itimer_state),
        VMSTATE_UINT64(delta, itimer_state),
        VMSTATE_UINT32(period_frac, itimer_state),
        VMSTATE_INT64(period, itimer_state),
        VMSTATE_INT64(last_event, itimer_state),
        VMSTATE_INT64(next_event, itimer_state),
        VMSTATE_TIMER_PTR(timer, itimer_state),
        VMSTATE_END_OF_LIST()
    }
};

itimer_state *itimer_init(QEMUBH *bh)
{
    itimer_state *s;

    s = (itimer_state *)g_malloc0(sizeof(itimer_state));
    s->bh = bh;
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, itimer_tick, s);
    return s;
}
