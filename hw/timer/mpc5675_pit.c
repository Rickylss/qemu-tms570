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

#define TYPE_PIT "mpc5675-pit"
#define PIT(obj) OBJECT_CHECK(PitState, (obj), TYPE_PIT)

typedef struct PitState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;
    QEMUTimer *timer;

    uint32_t pitmcr;
    uint32_t ldval[4];  //Timer 0~3 Load Value Register
    uint32_t cval[4];   //Timer 0~3 Current Value Register
    uint32_t tctrl[4];  //Timer 0~3 Control Register
    uint32_t tflg[4];   //Timer 0~3 Flag Register

    qemu_irq irq;
} PitState;

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
        index = (offset - 0x100) >> 4;
        s->cval[index] = ptimer_get_count(); //get current time
        return s->cval[index];
    case 0x108: //TCTRL
    case 0x118:
    case 0x128:
    case 0x138:
        index = (offset - 0x100) >> 4;
        return s->tctrl[index];
    case 0x10c: //TFLG
    case 0x11c:
    case 0x12c:
    case 0x13c:
        index = (offset - 0x100) >> 4;
        return s->tflg[index];
    default:
        return 0;
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
        if (s->pitmcr & 0x2) {
            /* disable timers */
        } else {
            /* enable timers */
        }
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
        index = (offset - 0x100) >> 4;
        s->tctrl[index] = val & 0x3;
        pit_update();//update interrupt
        break;
    case 0x10c: //TFLG
    case 0x11c:
    case 0x12c:
    case 0x13c:
        index = (offset - 0x100) >> 4;
        s->tflg[index] &= ~(val & 0x1);
        pit_update();//update interrupt
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

static void pit_init(Object *obj)
{
    PL031State *s = PL031(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    struct tm tm;

    memory_region_init_io(&s->iomem, obj, &pl031_ops, s, "pl031", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);

    sysbus_init_irq(dev, &s->irq);
    qemu_get_timedate(&tm, 0);
    s->tick_offset = mktimegm(&tm) -
        qemu_clock_get_ns(rtc_clock) / NANOSECONDS_PER_SECOND;

    s->timer = timer_new_ns(rtc_clock, pl031_interrupt, s);
}

static const VMStateDescription vmstate_pit = {
    .name = "mpc5675-pit",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = pl031_pre_save,
    .post_load = pl031_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(tick_offset_vmstate, PitState),
        VMSTATE_UINT32(mr, PitState),
        VMSTATE_UINT32(lr, PitState),
        VMSTATE_UINT32(cr, PitState),
        VMSTATE_UINT32(im, PitState),
        VMSTATE_UINT32(is, PitState),
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

static void pl031_register_types(void)
{
    type_register_static(&pit_info);
}

type_init(pit_register_types)