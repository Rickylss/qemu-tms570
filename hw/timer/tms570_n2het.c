/* 
 * tms570 n2het high-end timer
 * 
 * Copyright (c) 2019 FormalTech.
 * Written by xiaohaibiao
 * 
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qemu-common.h"
#include "hw/qdev.h"
#include "hw/ptimer.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"

#define TYPE_N2HET "n2het"
#define N2HET(obj) OBJECT_CHECK(N2HETState, (obj), TYPE_N2HET)

typedef struct N2HETState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;


    qemu_irq irq;
} N2HETState;

/* Merge the IRQs from the two component devices.  */
static void n2het_set_irq(void *opaque, int irq, int level)
{
    N2HETState *s = (N2HETState *)opaque;

}

static void n2het_read(void *opaque, hwaddr offset,
                           unsigned size)
{

}

static void n2het_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{

}

static const MemoryRegionOps n2het_ops = {
    .read = n2het_read,
    .write = n2het_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_n2het = {
    .name = "n2het",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void n2het_init(Object *obj)
{
    N2HETState *s = N2HET(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init_io(&s->iomem, obj, &n2het_ops, s,
                          "n2het", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void n2het_realize(DeviceState *dev, Error **errp)
{
    N2HETState *s = N2HET(dev);

    s->timer[0] = arm_timer_init(s->freq0);
    s->timer[1] = arm_timer_init(s->freq1);
    s->timer[0]->irq = qemu_allocate_irq(sp804_set_irq, s, 0);
    s->timer[1]->irq = qemu_allocate_irq(sp804_set_irq, s, 1);
}

static Property n2het_properties[] = {
    DEFINE_PROP_UINT32("freq0", N2HETState, freq0, 1000000),
    DEFINE_PROP_UINT32("freq1", N2HETState, freq1, 1000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void n2het_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->realize = n2het_realize;
    k->props = n2het_properties;
    k->vmsd = &vmstate_n2het;
}

static const TypeInfo n2het_info = {
    .name          = TYPE_N2HET,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(N2HETState),
    .instance_init = n2het_init,
    .class_init    = n2het_class_init,
};

static void n2het_register_types(void)
{
    type_register_static(&n2het_info);
}

type_init(n2het_register_types)