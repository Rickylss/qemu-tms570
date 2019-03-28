/* 
 * tms570 vim Vector Interrupt Manager
 * 
 * Copyright (c) 2019 FormalTech.
 * Written by xiaohaibiao
 * 
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"

#define TYPE_VIM "tms570-vim"
#define VIM(obj) OBJECT_CHECK(VimState, (obj), TYPE_VIM)

typedef struct VimState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;

    /* TODO: Parity-related Registers */

    /* Control Registers */
    uint32_t first_irq; /* IRQ Index Offset Vector Register */
    uint32_t first_fiq; /* FIQ Index Offset Vector Register */
    uint32_t fiq_or_irq[2]; /* FIQ/IRQ Program Control Register0~2 */
    uint32_t is_pending[2]; /* Pending Interrupt Read Location Register0~2 */
    /* Interrupt Enable Set Register0~2 Interrupt Enable Clear Register0~2 */
    uint32_t is_enabled[2];
    /* Wake-up Enable Set Register0~2 Wake-up Enable Clear Register0~2 */
    uint32_t iw_weakup[2];
    uint32_t first_irq_isr; /* IRQ Interrupt Vector Register */
    uint32_t first_fiq_isr; /* FIQ Interrupt Vector Register */
    uint32_t cap_for_rti; /* Capture Event Register */
    uint32_t int_map_channel[23]; /* VIM Interrupt Control Register */

    /* Mask containing interrupts with higher priority than this one.  */

    qemu_irq irq;
    qemu_irq fiq;
} VimState;

static uint64_t vim_read(void *opaque, hwaddr offset, 
                        unsigned size)
{

}

static void vim_write(void *opaque, hwaddr offset,
                        uint64_t val, unsigned size)
{
    VimState *s = (VimState *)opaque;
}

/* read/write operations */
static const MemoryRegionOps vim_ops = {
    .read = vim_read,
    .write = vim_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
 * rest vim
 */
static void vim_reset(DeviceState *d)
{
    VimState *s = VIM(d);
    int i;
    
    s->iir = 0;
    s->fir = 0;
}

/* init vim instance
 * create memory region for vim and init gpio_in,
 * once attache to cpu, use qdev_get_gpio_in() get qemu_irq[]。
 */
static void vim_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    VimState *s = VIM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* init i/o opreations for the memory_region */
    memory_region_init_io(&s->mem, obj, &vim_ops, s, "tms570-vim", 0x100);
    sysbus_init_mmio(sbd, &s->mem);
    qdev_init_gpio_in(dev, vim_set_irq, 94);
    sysbus_init_irq(sbd, &s-irq);
    sysbus_init_irq(sbd, &s->fiq);
}

static const VMStateDescription vmstate_vim = {
    .name = "tms570-vim",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        /* TODO */
        VMSTATE_END_OF_LIST()
    }
}

static void vim_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    //dc->realize = vim_realize;
    //dc->props = vim_properties;
    dc->reset = vim_reset;
    dc->vmsd = &vmstate_vim;
}

static const TypeInfo vim_info = {
    .name          = TYPE_VIM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VimState),
    .instance_init = vim_init,
    .class_init    = vim_class_init,
};

static void vim_register_types(void)
{
    type_register_static(&vim_info);
}

type_init(vim_register_types)