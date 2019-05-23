/*
 * General purpose implementation of a simple periodic up-counter timer.
 *
 * Copyright (c) 2019 xiaohaibiao.
 *
 * This code is licensed under the GNU LGPL.
 */
#ifndef ITIMER_H
#define ITIMER_H

#include "qemu-common.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"

/* itimer.c */
typedef struct itimer_state itimer_state;
typedef void (*itimer_cb)(void *opaque);

itimer_state *itimer_init(QEMUBH *bh);
void itimer_set_period(itimer_state *s, int64_t period);
void itimer_set_freq(itimer_state *s, uint32_t freq);
uint64_t itimer_get_count(itimer_state *s);
void itimer_set_count(itimer_state *s, uint64_t count);
void itimer_run(itimer_state *s, int reset);
void itimer_stop(itimer_state *s);
void itimer_set_compare(itimer_state *s, uint32_t compare);

extern const VMStateDescription vmstate_itimer;

#define VMSTATE_ITIMER(_field, _state) \
    VMSTATE_STRUCT_POINTER_V(_field, _state, 1, vmstate_itimer, itimer_state)

#define VMSTATE_ITIMER_ARRAY(_f, _s, _n)                                \
    VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(_f, _s, _n, 0,                   \
                                       vmstate_itimer, itimer_state)

#endif
