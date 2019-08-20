/*
 * mpc5675 Periodic Interrupt Timer
 *
 * Copyright (c) 2019 FormalTech
 * Written by xiaohaibiao
 * 
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "hw/ptimer.h"

#define TYPE_PIT "mpc5675-pit"
#define PIT(obj) OBJECT_CHECK(PitState, (obj), TYPE_PIT)
#define ALL_TIMER 4

typedef struct PitState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;
    ptimer_state *timer[4];

    uint32_t pitmcr;
    uint32_t ldval[4];  //Timer 0~3 Load Value Register
    uint32_t cval[4];   //Timer 0~3 Current Value Register
    uint32_t tctrl[4];  //Timer 0~3 Control Register
    uint32_t tflg[4];   //Timer 0~3 Flag Register

    qemu_irq irq[4];
} PitState;

static void pit_update(PitState *s, int index)
{
    bool irq; 

    if (index > 4)
    {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pit_update: timer %d out of range\n", index);
    }
    
    irq = (s->tflg[index] & 0x1) && (s->tctrl[index] & 0x2);
    qemu_set_irq(s->irq[index], irq);
    s->tflg[index] &= ~ 0x1;

}

static uint64_t pit_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PitState *s = (PitState *)opaque;
    int index;

    switch (offset)
    {
    case 0x000: //PITMCR
        return s->pitmcr;
    case 0x100: //LDVAL
    case 0x110:
    case 0x120:
    case 0x130:
        index = (offset - 0x100) >> 4;
        return s->ldval[index];
    case 0x104: //CVAL
    case 0x114:
    case 0x124:
    case 0x134:
        index = (offset - 0x104) >> 4;
        s->cval[index] = ptimer_get_count(s->timer[index]); //get current time
        return s->cval[index];
    case 0x108: //TCTRL
    case 0x118:
    case 0x128:
    case 0x138:
        index = (offset - 0x108) >> 4;
        return s->tctrl[index];
    case 0x10c: //TFLG
    case 0x11c:
    case 0x12c:
    case 0x13c:
        index = (offset - 0x10c) >> 4;
        return s->tflg[index];
    default:
        return 0;
    }

}

static void change_timer_state(PitState *s, int index)
{
    if (~(s->pitmcr & 0x2)) {   
        // pit is enabled 
        if (index < 4) {
            if ( s->tctrl[index] & 0x1) {
                // timer[index] is active
                ptimer_run(s->timer[index], 1);
            } else {
                // timer[index] is disabled
                ptimer_stop(s->timer[index]);
            } 
        } else if (index == 4) {
            for (size_t i = 0; i < 4; i++)
            {
                if ( s->tctrl[i] & 0x1) {
                    // timer[i] is active
                    ptimer_run(s->timer[i], 1);
                } else {
                    // timer[i] is disabled
                    ptimer_stop(s->timer[i]);
                } 
            }
        }
    }
}

static void pit_write(void * opaque, hwaddr offset,
                        uint64_t val, unsigned size)
{
    PitState *s = (PitState *)opaque;
    int index;

    switch (offset)
    {
    case 0x000: //PITMCR
        s->pitmcr = val & 0x3;
        change_timer_state(s, ALL_TIMER);
        if (s->pitmcr & 0x1) {
            /* timers stopp in debug  */
        } else {
            /* timers run in debug */
        }
        
        break;
    case 0x100: //LDVAL
    case 0x110:
    case 0x120:
    case 0x130:
        index = (offset - 0x100) >> 4;
        s->ldval[index] = val;
        ptimer_set_count(s->timer[index], s->ldval[index]);
        break;
    case 0x104: //CVAL
    case 0x114: //read-only
    case 0x124:
    case 0x134:
        break;
    case 0x108: //TCTRL
    case 0x118:
    case 0x128:
    case 0x138:
        index = (offset - 0x108) >> 4;
        s->tctrl[index] = val & 0x3;
        change_timer_state(s, index);
        pit_update(s, index);//update interrupt
        break;
    case 0x10c: //TFLG
    case 0x11c:
    case 0x12c:
    case 0x13c:
        index = (offset - 0x10c) >> 4;
        s->tflg[index] &= ~(val & 0x1);
        pit_update(s, index);//update interrupt
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pit_ops = {
    .read = pit_read,
    .write = pit_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pit_timer_tick_all(void *opaque, int index)
{
    PitState *s = (PitState *)opaque;

    // causes an interrupt request;
    s->tflg[index] |= 0x1;

    pit_update(s, index);

    ptimer_set_count(s->timer[index], s->ldval[index]);
    ptimer_run(s->timer[index], 1);
}

inline static void timer0_tick(void *opaque){
    pit_timer_tick_all(opaque, 0);
}

inline static void timer1_tick(void *opaque){
    pit_timer_tick_all(opaque, 1);
}

inline static void timer2_tick(void *opaque){
    pit_timer_tick_all(opaque, 2);
}

inline static void timer3_tick(void *opaque){
    pit_timer_tick_all(opaque, 3);
}

typedef void (*pit_timer_tick_call) (void *);
static pit_timer_tick_call pit_timer_tick[4]={
    timer0_tick,
    timer1_tick,
    timer2_tick,
    timer3_tick
};

static void pit_init(Object *obj)
{
    PitState *s = PIT(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    QEMUBH *bh[4];

    memory_region_init_io(&s->mem, obj, &pit_ops, s, "mpc5675-timer", 0x4000);
    sysbus_init_mmio(dev, &s->mem);

    for (size_t i = 0; i < 4; i++)
    {
        sysbus_init_irq(dev, &s->irq[i]);
        bh[i] = qemu_bh_new(pit_timer_tick[i], s);
        s->timer[i] = ptimer_init(bh[i]);
        ptimer_set_freq(s->timer[i], 1000000);
    }
    
}

static const VMStateDescription vmstate_pit = {
    .name = "mpc5675-pit",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(pitmcr, PitState),
        VMSTATE_UINT32_ARRAY(ldval, PitState, 4),
        VMSTATE_UINT32_ARRAY(cval, PitState, 4),
        VMSTATE_UINT32_ARRAY(tctrl, PitState, 4),
        VMSTATE_UINT32_ARRAY(tflg, PitState, 4),
        VMSTATE_END_OF_LIST()
    }
};

static void pit_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_pit;
}

static const TypeInfo pit_info = {
    .name          = TYPE_PIT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PitState),
    .instance_init = pit_init,
    .class_init    = pit_class_init,
};

static void pit_register_types(void)
{
    type_register_static(&pit_info);
}

type_init(pit_register_types)
