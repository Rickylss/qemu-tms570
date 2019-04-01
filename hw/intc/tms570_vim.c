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
#define PHANTOM_VECTOR 0xfff82000
#define VIM_MAX_IRQ 96 

typedef struct VimState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;

    /* TODO: Parity-related Registers */

    /* Control Registers */
    uint32_t first_irq_channel; /* IRQ Index Offset Vector Register */
    uint32_t first_fiq_channel; /* FIQ Index Offset Vector Register */
    uint32_t fiq_or_irq[3]; /* FIQ/IRQ Program Control Register */
    uint32_t is_pending[3]; /* Pending Interrupt Read Location Register */
    /* Interrupt Enable Set Register Interrupt Enable Clear Register */
    uint32_t is_enabled[3];
    /* Wake-up Enable Set Register Wake-up Enable Clear Register */
    uint32_t is_weakup[3];
    uint32_t first_irq_isr; /* IRQ Interrupt Vector Register */
    uint32_t first_fiq_isr; /* FIQ Interrupt Vector Register */
    uint32_t cap_to_rti; /* Capture Event Register */
    uint32_t int_map_channel[24]; /* VIM Interrupt Control Register */

    qemu_irq irq;
    qemu_irq fiq;
} VimState;

/* Update interrupts */
static void vim_update(VimState *s)
{
    uint32_t irq[3];
    uint32_t fiq[3];
    
    for(i = 0; i < 3; i++)
    {
        /* interrupt channel enable and pending */
        fiq[i] = s->is_pending[i] & s->is_enabled[i] & s->fiq_or_irq[i];
        if (fiq[i]) {
            uint32_t first_bit = fiq[i] & (~(fiq[i]-1));
            uint8_t channel = (32 * i) + first_bit / 2;
            s->first_fiq_channel = channel;
            s->first_fiq_isr = PHANTOM_VECTOR + (0x4 * (channel + 1));
            qemu_irq_raise(s->fiq);
            s->is_pending &= ~first_bit;
            break;
        }
    }

    for(i = 0; i < 3; i++)
    {
        irq[i] = s->is_pending[i] & s->is_enabled[i] & ~s->fiq_or_irq[i];
        if (irq[i]) {
            uint32_t first_bit = fiq[i] & (~(fiq[i]-1));
            uint8_t channel = (32 * i) + first_bit / 2;
            s->first_irq_channel = channel;
            s->first_irq_isr = PHANTOM_VECTOR + (0x4 * (channel + 1));
            qemu_irq_raise(s->irq);
            s->is_pending &= ~first_bit;
            break;
        }
    }

}

static void vim_set_irq(void *opaque, int irq, int level)
{
    VimState *s = (VimState *)opaque;
    int i, j;

    if (irq >= VIM_MAX_IRQ) {
        fprintf(stderr, "%s: IRQ %d out of range\n", __func__, irq);
        abort();
    }

    /* channel mapping */
    for (i = 0; i < 24; i++) {
        for (j = 3; j >= 0; j--) {
            if ((s->int_map_channel[i] >> (8 * j) & 0xff) - irq ) {
                continue;
            } else
            {
                int channel = (4 * i + (3 - j)) & 0xff;
                int index = channel / 32;
                int bit = channel % 32;
                s->is_pending[index] |= 1u << bit;
            }
        }
    }

    vim_update(s);
}

static uint64_t vim_read(void *opaque, hwaddr offset, 
                        unsigned size)
{
    VimState *s = (VimState *)opaque;

    if (offset >= 0x10 && offset < 0x20){ /* FIRQPR */
        return s->fiq_or_irq[(offset - 0x10) >> 2];
    }
    if (offset >= 0x20 && offset < 0x30){ /* INTREQ */
        return s->is_pending[(offset - 0x20) >> 2];
    }
    if (offset >= 0x30 && offset < 0x40){ /* REQENASET */
        return s->is_enabled[(offset - 0x30) >> 2];
    } else if (offset >= 0x40 && offset < 0x50) { /* REQENACLR */
        return s->is_enabled[(offset - 0x40) >> 2];
    }
    if (offset >= 0x50 && offset < 0x60) { /* WAKEENASET */
        return s->is_weakup[(offset - 0x50) >> 2];
    } else if (offset >= 0x60 && offset < 0x70) { /* WAKEENACLR */
        return s->is_weakup[(offset - 0x60) >> 2];
    }
    if (offset >= 0x80 && offset < 0xdc) {
        return s->int_map_channel[(offset - 0x80) >> 2];
    }
    switch (offset)
    {
        case 0x00: /* IRQINDEX */
            return s->first_irq_channel;
        case 0x04: /* FIQINDEX */
            return s->first_fiq_channel;
        case 0x70: /* IRQVECREG */
            return s->first_irq_isr;
        case 0x74: /* FIQVECREG */
            return s->first_fiq_isr;
        case 0x78: /* CAPEVT */
            return s->cap_to_rti;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                      "vim_read: Bad offset %x\n", (int)offset);
            return 0;
    }

    vim_update(s);
}

static void vim_write(void *opaque, hwaddr offset,
                        uint64_t val, unsigned size)
{
    VimState *s = (VimState *)opaque;
    int index;

    if (offset >= 0x10 && offset < 0x20){ /* FIRQPR */
        index = (offset - 0x10) >> 2;
        if (index == 0) {
            s->fiq_or_irq[index] = val & 0x11 ; /* bit0 and bit1 must be 0x11 */
        } else
        {
            s->fiq_or_irq[index] = val;
        }
    }
    if (offset >= 0x20 && offset < 0x30){ /* INTREQ */
        /* This is a readonly register for user */
    }
    if (offset >= 0x30 && offset < 0x40){ /* REQENASET */
        s->is_enabled[(offset - 0x30) >> 2] |= val;
    } else if (offset >= 0x40 && offset < 0x50) { /* REQENACLR */
        index = (offset - 0x40) >> 2;
        if (index == 0) {
            s->is_enabled[index] &= (~val | 0x11);   /* bit0 and bit1 must be 0x11 */
        } else
        {
            s->is_enabled[index] &= ~val;
        }
    }
    if (offset >= 0x50 && offset < 0x60) { /* WAKEENASET */
        s->is_weakup[(offset - 0x50) >> 2] |= val;
    } else if (offset >= 0x60 && offset < 0x70) { /* WAKEENACLR */
        s->is_weakup[(offset - 0x60) >> 2] &= ~val;
    }
    if (offset >= 0x80 && offset < 0xdc) {
        int index = (offset - 0x80) >> 2;
        if (index == 0) {
            s->int_map_channel[index] = (val & 0xffff) | 0x00010000;
        }
        s->int_map_channel[index] = val;
    }
    switch (offset)
    {
        case 0x00: /* IRQINDEX */
            /* This is a readonly register */
            break;
        case 0x04: /* FIQINDEX */
            /* This is a readonly register */
            break;
        case 0x70: /* IRQVECREG */
            /* This is a readonly register */
            break;
        case 0x74: /* FIQVECREG */
            /* This is a readonly register */
            break;
        case 0x78: /* CAPEVT */
            s->cap_to_rti = val;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                      "vim_read: Bad offset %x\n", (int)offset);
            return 0;
    }

    vim_update(s);
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

    s->first_irq_channel = 0x00;
    s->first_fiq_channel = 0x00;
    s->first_irq_isr = PHANTOM_VECTOR; /* Phantom Vector */
    s->first_fiq_isr = PHANTOM_VECTOR;
    
    for (i = 0; i < 3; i++){
        if(i == 0){
            s->fiq_or_irq[i] = 3; /* channel 0 and 1 are FIQ only */
            s->is_enabled[i] = 3; /* always enable channel 0 and 1*/
        } else
        {
             s->fiq_or_irq[i] = 0;
             s->is_enabled[i] = 0;
        }
        s->is_pending[i] = 0;
        s->is_weakup[i] = 0;
    }

    s->cap_to_rti = 0;

    for (i = 0; i < 24; i++) {
        int base = 4 * i;
        int_map_channel[i] = (base << 24) | ((base + 1) << 16) | ((base + 2) << 8) | (base + 3);
    }

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
    qdev_init_gpio_in(dev, vim_set_irq, 95);
    sysbus_init_irq(sbd, &s-irq);
    sysbus_init_irq(sbd, &s->fiq);
}

static const VMStateDescription vmstate_vim = {
    .name = "tms570-vim",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        /* TODO */
        VMSTATE_UINT32(first_irq_channel, VimState),
        VMSTATE_UINT32(first_fiq_channel, VimState),
        VMSTATE_UINT32(first_fiq_isr, VimState),
        VMSTATE_UINT32(first_irq_isr, VimState),
        VMSTATE_UINT32(cap_to_rti, VimState),
        VMSTATE_UINT32_ARRAY(fiq_or_irq, VimState),
        VMSTATE_UINT32_ARRAY(is_pending, VimState),
        VMSTATE_UINT32_ARRAY(is_enabled, VimState),
        VMSTATE_UINT32_ARRAY(is_weakup, VimState),
        VMSTATE_UINT32_ARRAY(int_map_channel, VimState),
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