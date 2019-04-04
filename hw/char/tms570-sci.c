/* 
 * tms570 sci serial communication interface
 * 
 * Copyright (c) 2019 FormalTech.
 * Written by xiaohaibiao
 * 
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "sysemu/char.h"
#include "qemu/log.h"

#define TYPE_SCI "tms570-sci"
#define SCI(obj) OBJECT_CHECK(SCIState, (obj), TYPE_SCI)

typedef struct SCIState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t scigcr0; /* SCIGCR0 */
    uint32_t scigcr0; /* SCIGCR0 */
    uint32_t intreg; /* SCISETINT SCICLEARINT */

    uint32_t intlevel; /* SCISETINTLVL SCICLEARINTLVL */

    uint32_t flag; /* SCIFLR */

    uint32_t vecoffset0; /* SCIINTVECT0 */
    uint32_t vecoffset1; /* SCIINTVECT1 */

    uint32_t format; /* SCIFORMAT */

    uint32_t brs; /* BRS */

    uint32_t buff; /* SCIED, SCIRD, SCITD */

    uint32_t pio_control[9]; /* SCIPIO 0~8 */

    uint32_t ioerr; /* IODFTCTRL */

    qemu_irq irq;
    const unsigned char *id;
} SCIState;

static uint64_t sci_read(void *opaque, hwaddr offset,
                           unsigned size)
{

}

static void sci_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{

}

static const MemoryRegionOps sci_ops = {
    .read = sci_read,
    .write = sci_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_sci = {
    .name = "tms570-sci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void sci_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SCIState *s = SCI(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &sci_ops, s, "tms570-sci", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void sci_realize(DeviceState *dev, Error **errp)
{
    SCIState *s = SCI(dev);

    if (s->chr) {
        qemu_chr_add_handlers(s->chr, sci_can_receive, sci_receive,
                              sci_event, s);
    }
}

static void sci_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = sci_realize;
    dc->vmsd = &vmstate_sci;
    //dc->props = sci_properties;
}

static const TypeInfo sci_info = {
    .name          = TYPE_SCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SCIState),
    .instance_init = sci_init,
    .class_init    = sci_class_init,
};

static void sci_register_types(void)
{
    type_register_static(&sci_info);
}

type_init(sci_register_types)
