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
#define TYPE_VIM_RAM "tms570-vimram"
#define VIM(obj) OBJECT_CHECK(VimState, (obj), TYPE_VIM)
#define VIMRAM(obj) OBJECT_CHECK(VimRamState, (obj), TYPE_VIM_RAM)
#define VIM_MAX_IRQ 95  

typedef struct VimRamState {
    SysBusDevice parent_obj;

    MemoryRegion vimram;

    uint32_t isrFunc[94];

} VimRamState;

VimRamState *vimram;

typedef struct VimState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;

    uint32_t is_read;

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

    VimRamState *vimram;

    qemu_irq irq;
    qemu_irq fiq;
    qemu_irq rti0;
    qemu_irq rti1;
} VimState;

static void vim_update(VimState *s)
{
    qemu_set_irq(s->irq, s->first_irq_channel);

    qemu_set_irq(s->fiq, s->first_fiq_channel);
}

/* Update interrupts */
static void vim_update_vectors(VimState *s)
{
    uint32_t irq[3];
    uint32_t fiq[3];
    int i;
    
    for(i = 0; i < 3; i++)
    {
        /* interrupt channel enable and pending */
        fiq[i] = s->is_pending[i] & s->is_enabled[i] & s->fiq_or_irq[i];
        if (fiq[i]) {
            uint32_t first_bit = fiq[i] & (~(fiq[i]-1));
            uint8_t channel = (32 * i) + first_bit / 2;
            s->first_fiq_channel = channel;
            s->first_fiq_isr = s->vimram->isrFunc[channel + 1];
            if (s->is_read)
            {
                s->is_pending[i] &= ~first_bit;
                s->is_read = 0;
            }
            break;
        }
    }
    if ((fiq[0] || fiq[1] || fiq[2]) == 0) {
        s->first_fiq_channel = 0x00;
        s->first_fiq_isr = s->vimram->isrFunc[0];
    }

    for(i = 0; i < 3; i++)
    {
        irq[i] = s->is_pending[i] & s->is_enabled[i] & ~s->fiq_or_irq[i];
        if (irq[i]) {
            uint32_t first_bit = irq[i] & (~(irq[i]-1));
            uint8_t channel = (32 * i) + first_bit / 2;
            s->first_irq_channel = channel;
            s->first_irq_isr = s->vimram->isrFunc[channel + 1];
            if (s->is_read)
            {
                s->is_pending[i] &= ~first_bit;
                s->is_read = 0;
            }
            break;
        }
    }
    if ((irq[0] || irq[1] || irq[2]) == 0) {
        s->first_irq_channel = 0x00;
        s->first_irq_isr = s->vimram->isrFunc[0];
    }

    vim_update(s);

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
            if (((s->int_map_channel[i] >> (8 * j)) & 0xff) - irq ) {
                continue;
            } else
            {
                int channel = (4 * i + (3 - j)) & 0xff;
                int index = channel / 32;
                int bit = (channel % 32);
                if (level == 0)
                {
                    s->is_pending[index] &= ~(1u << bit);
                } else
                {
                    s->is_pending[index] |= 1u << bit;
                }
            }
        }
    }

    vim_update_vectors(s);
}

static uint64_t vim_read(void *opaque, hwaddr offset, 
                        unsigned size)
{
    VimState *s = (VimState *)opaque;
    uint32_t tmp;

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
            tmp = s->first_irq_channel;
            s->is_read = 1;
            vim_update_vectors(s);
            return tmp;
        case 0x04: /* FIQINDEX */
            tmp = s->first_fiq_channel;
            s->is_read = 1;
            vim_update_vectors(s);
            return tmp;
        case 0x70: /* IRQVECREG */
            tmp = s->first_irq_isr;
            s->is_read = 1;
            vim_update_vectors(s);
            return tmp;
        case 0x74: /* FIQVECREG */
            tmp = s->first_fiq_isr;
            s->is_read = 1;
            vim_update_vectors(s);
            return tmp;
        case 0x78: /* CAPEVT */
            return s->cap_to_rti;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                      "vim_read: Bad offset %x\n", (int)offset);
            return 0;
    }
    
}

static void vim_write(void *opaque, hwaddr offset,
                        uint64_t val, unsigned size)
{
    VimState *s = (VimState *)opaque;
    int index;

    if (offset >= 0x10 && offset < 0x20){ /* FIRQPR */
        index = (offset - 0x10) >> 2;
        if (index == 0) {
            s->fiq_or_irq[index] = val | 0x3; /* bit0 and bit1 must be3 */
        } else
        {
            s->fiq_or_irq[index] = val;
        }
    }
    if (offset >= 0x20 && offset < 0x30){ /* INTREQ */
        index = (offset - 0x20) >> 2;
        s->is_pending[index] &= ~val;
    }
    if (offset >= 0x30 && offset < 0x40){ /* REQENASET */
        s->is_enabled[(offset - 0x30) >> 2] |= val;
    } else if (offset >= 0x40 && offset < 0x50) { /* REQENACLR */
        index = (offset - 0x40) >> 2;
        if (index == 0) {
            s->is_enabled[index] &= (~val | 0x3);   /* bit0 and bit1 must be 0x3 */
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
                      "vim_write: Bad offset %x\n", (int)offset);
            break;
    }

    vim_update_vectors(s);
}

static uint64_t vimram_read(void *opaque, hwaddr offset, 
                        unsigned size)
{
    VimRamState *s = (VimRamState *)opaque;
    int index = offset >> 2;

    return s->isrFunc[index];
}

static void vimram_write(void *opaque, hwaddr offset,
                        uint64_t val, unsigned size)
{
    VimRamState *s = (VimRamState *)opaque;
    int index = offset >> 2;

    s->isrFunc[index] = val;
}

/* read/write operations */
static const MemoryRegionOps vim_ops = {
    .read = vim_read,
    .write = vim_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps vimram_ops = {
    .read = vimram_read,
    .write = vimram_write,
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
    s->first_irq_isr = s->vimram->isrFunc[0]; /* Phantom Vector */
    s->first_fiq_isr = s->vimram->isrFunc[0];
    
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
        s->int_map_channel[i] = (base << 24) | ((base + 1) << 16) | ((base + 2) << 8) | (base + 3);
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
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->fiq);
    sysbus_init_irq(sbd, &s->rti0);
    sysbus_init_irq(sbd, &s->rti1); // TODO add rti CAPEVT support

    sysbus_create_simple("tms570-vimram", 0xfff82000, NULL); /* VIMRAM */

    s->vimram = vimram;
}

static void vim_ram_init(Object *obj)
{
    VimRamState *s = VIMRAM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* init i/o opreations for the memory_region */
    memory_region_init_io(&s->vimram, obj, &vimram_ops, s, "tms570-vimram", 0x180);
    sysbus_init_mmio(sbd, &s->vimram);

    vimram = s;
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
        VMSTATE_UINT32_ARRAY(fiq_or_irq, VimState, 3),
        VMSTATE_UINT32_ARRAY(is_pending, VimState, 3),
        VMSTATE_UINT32_ARRAY(is_enabled, VimState, 3),
        VMSTATE_UINT32_ARRAY(is_weakup, VimState, 3),
        VMSTATE_UINT32_ARRAY(int_map_channel, VimState, 24),
        VMSTATE_END_OF_LIST()
    }
};

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

static const TypeInfo vimram_info = {
    .name          = TYPE_VIM_RAM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VimRamState),
    .instance_init = vim_ram_init,
};

static void vim_register_types(void)
{
    type_register_static(&vim_info);
    type_register_static(&vimram_info);
}

type_init(vim_register_types)