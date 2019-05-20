/*
 * mpc5675 System Timer Module
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
#include "hw/itimer.h"

#define TYPE_STM "mpc5675-stm"
#define STM(obj) OBJECT_CHECK(StmState, (obj), TYPE_STM)

typedef struct StmState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;
    itimer_state *timer;

    uint32_t freq_base;

    uint32_t stm_cr;    //STM Control Register
    uint32_t stm_cnt;   //STM Counter Value
    uint32_t stm_ccr[4];  //STM Channel 0~3 Control Register
    uint32_t stm_cir[4];   //STM Channel 0~3 Interrupt Register
    uint32_t stm_cmp[4];  //STM Channel 0~3 Compare Register 

    uint32_t current_cmp;

    qemu_irq irq[4];
} StmState;

static void stm_update(StmState *s)
{
    bool irq; 

    for (size_t i = 0; i < 4; i++)
    {
        irq = (s->stm_cir[i] & 0x1) && (s->stm_ccr[i] & 0x1);
        qemu_set_irq(s->irq[i], irq);
        s->stm_cir[i] &= ~ 0x1;
    }
}

static void update_channel(StmState *s)
{
    uint32_t n;
    uint32_t enabled_cmp[4];

    for (int i = 0; i < 4; i++)
    {
        if (s->stm_ccr[i] & 0x1)
        {
            enabled_cmp[i] = s->stm_cmp[i];
        }
        
    }
    

    s->current_cmp = n;
    itimer_set_compare(s->timer, s->stm_cmp[s->current_cmp]);
}

static uint64_t stm_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    StmState *s = (StmState *)opaque;
    int index;

    switch (offset)
    {
    case 0x00: //STM Control Register
        return s->stm_cr;
    case 0x04: //STM Count Register
        s->stm_cnt = itimer_get_count(s->timer);
        return s->stm_cnt;
    case 0x10: //STM Channel Control Register n
    case 0x20:
    case 0x30:
    case 0x40:
        index = (offset - 0x10) >> 4;
        return s->stm_ccr[index];
    case 0x14: //STM Channel Interrupt Register n
    case 0x24:
    case 0x34:
    case 0x44:
        index = (offset - 0x14) >> 4;
        return s->stm_cir[index];
    case 0x18: //STM Channel Compare Register n
    case 0x28:
    case 0x38:
    case 0x48:
        index = (offset - 0x18) >> 4;
        return s->stm_cmp[index];
    default:
        return 0;
    }

}

static void stm_write(void * opaque, hwaddr offset,
                        uint64_t val, unsigned size)
{
    StmState *s = (StmState *)opaque;
    int index;

    switch (offset)
    {
    case 0x000: //STM Control Register
        s->stm_cr = val & 0xff03;
        int freq = s->freq_base / (((s->stm_cr >> 8) & 0xff) + 1);
        itimer_set_freq(s->timer, freq);
        if (s->stm_cr & 0x1) {   /* Counter is enabled */
            itimer_run(s->timer);
        } else {
            itimer_stop(s->timer);
        }

        if (s->stm_cr & 0x2) {
            /* TODO: timers stopp in debug  */
        } else {
            /* TODO: timers run in debug */
        }
        break;
    case 0x004: //STM Count Register
        s->stm_cnt = val;
        itimer_set_count(s->timer, s->stm_cnt);
        break;
    case 0x010: //STM Channel Control Register n
    case 0x020:
    case 0x030:
    case 0x040:
        index = (offset - 0x10) >> 4;
        s->stm_ccr[index] = val & 0x1;
        update_channel(s);
        break;
    case 0x14: //STM Channel Interrupt Register n
    case 0x24:
    case 0x34:
    case 0x44:
        index = (offset - 0x14) >> 4;
        s->stm_cir[index] &= ~(val & 0x1);
        stm_update(s);
        break;
    case 0x18: //STM Channel Compare Register n
    case 0x28:
    case 0x38:
    case 0x48:
        index = (offset - 0x18) >> 4;
        s->stm_cmp[index] = val;
        update_channel(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps stm_ops = {
    .read = stm_read,
    .write = stm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void stm_timer_tick(void *opaque)
{
    StmState *s = (StmState *)opaque;

    // causes an interrupt request;
    s->stm_cir[s->current_cmp] = 0x1;

    update_channel(s);
    //ptimer_set_count(s->timer[index], s->ldval[index]);
    //ptimer_run(s->timer[index], 1);

    stm_update(s);
}

static void stm_init(Object *obj)
{
    StmState *s = STM(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    QEMUBH *bh;
    int freq;

    memory_region_init_io(&s->mem, obj, &pit_ops, s, "mpc5675-stimer", 0x4000);
    sysbus_init_mmio(dev, &s->mem);

    sysbus_init_irq(dev, &s->irq[i]);

    bh = qemu_bh_new(stm_timer_tick, s);
    s->timer = itimer_init(bh);
    freq = s->freq_base / (((s->stm_cr >> 8) & 0xff) + 1);
    itimer_set_freq(s->timer, freq);
}

static const VMStateDescription vmstate_stm = {
    .name = "mpc5675-stm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(freq_base, PitState),
        VMSTATE_UINT32(pitmcr, PitState),
        VMSTATE_UINT32_ARRAY(ldval, PitState, 4),
        VMSTATE_UINT32_ARRAY(cval, PitState, 4),
        VMSTATE_UINT32_ARRAY(tctrl, PitState, 4),
        VMSTATE_UINT32_ARRAY(tflg, PitState, 4),
        VMSTATE_END_OF_LIST()
    }
};

static Property stm_properties[] = {
    DEFINE_PROP_UINT32("freq_base", StmState, freq_base, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = stm_properties;
    dc->vmsd = &vmstate_stm;
}

static const TypeInfo stm_info = {
    .name          = TYPE_STM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(StmState),
    .instance_init = stm_init,
    .class_init    = stm_class_init,
};

static void stm_register_types(void)
{
    type_register_static(&stm_info);
}

type_init(stm_register_types)