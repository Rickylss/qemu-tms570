/* 
 * tms570 rti Real-Time Interrupt module
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

#define TYPE_RTI "tms570-rti"
#define RTI(obj) OBJECT_CHECK(RTIState, (obj), TYPE_RTI)

typedef struct RTIState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t global_ctrl;               /* RTIGCTRL */
    uint32_t timebase_ctrl;             /* RTITBCTRL */
    uint32_t capture_ctrl;              /* RTICAPCTRL */
    uint32_t compare_ctrl;              /* RTICOMPCTRL */

    uint32_t free_running_counter[2];   /* RTIFRC0~1 >>5 */
    uint32_t up_counter[2];             
    uint32_t up_counter_reg[2]          /* RTIUC0~1 >>5 */
    uint32_t compare_up_counter[2];     /* RTICPUC0~1 >>5 */
    uint32_t capture_free_counter[2];   /* RTICAFRC0~1 >>5 */
    uint32_t capture_up_counter[2];     /* RTICAUC0~1 >>5 */

    uint32_t compare[4];                /* RTICOMP0~3 >>3*/
    uint32_t update_compare[4];         /* RTIUDCP0~3 >>3 */

    uint32_t timebase_low;              /* RTITBLCOMP */
    uint32_t timebase_high;             /* RTITBHCOMP */

    uint32_t int_ctrl;                  /* RTISETINTENA && RTICLEARINTENA */
    uint32_t int_flag;                  /* RTIINTFLAG */

    uint32_t dw_ctrl;                   /* RTIDWDCTRL */
    uint32_t dw_preload;                /* RTIDWPRLD */
    uint32_t dw_status;                 /* RTIDWSTATUS */
    uint32_t rti_wd_key;                /* RTIWDKEY */
    uint32_t rti_dw_down_counter;       /* RTIDWDCNTR */
    uint32_t dww_reaction_ctrl;         /* RTIWWDRXNCTRL */
    uint32_t dww_size_ctrl;             /* RTIWWDSIZECTRL */
    
    qemu_irq irq_compare0;
    qemu_irq irq_compare1;
    qemu_irq irq_compare2;
    qemu_irq irq_compare3;
    qemu_irq irq_overflow0;
    qemu_irq irq_overflow1;
    qemu_irq irq_timebase;
} RTIState;

static void dw_reset(RTIState *s)
{
    s->dw_ctrl = 0x5312aced;
    s->dw_preload = 0xfff;
    s->rti_wd_key = 0xa35c;
    s->rti_dw_down_counter = 0xffff;
    s->dww_reaction_ctrl = 0x5;
}

static uint64_t rti_read(void *opaque, hwaddr offset,
                        unsigned size)
{
    RTIState *s = (RTIState *)opaque;

    if (offset >= 0x50 && offset < 0x70)
    {
        uint64_t index = (offset - 0x50) >> 2;

        if (index & 0x001)
        { /* RTIUDCP0~3 */
            return update_compare[(index >> 1)];
        } else 
        { /* RTICOMP0~3 */
            return compare[(index >> 1)];
        }
        
    }
    

    switch (offset)
    {
        case 0x00: /* RTIGCTRL */
            return s->global_ctrl;
        case 0x04: /* RTITBCTRL */
            return s->timebase_ctrl;
        case 0x08: /* RTICAPCTRL */
            return s->capture_ctrl;
        case 0x0c: /* RTICOMPCTRL */
            return s->compare_ctrl;

        case 0x10: /* RTIFRC0 */
            s->up_counter_reg[0] = s->up_counter[0]
            return s->free_running_counter[0];
        case 0x14: /* RTIUC0 */
            return s->up_counter_reg[0];
        case 0x18: /* RTICPUC0 */
            return s->compare_up_counter[0];
        case 0x20: /* RTICAFRC0 */
            return s->capture_free_counter[0];
        case 0x24: /* RTICAUC0 */
            return s->capture_up_counter[0];

        case 0x30: /* RTIFRC1 */
            s->up_counter_reg[1] = s->up_counter[1]
            return s->free_running_counter[1];
        case 0x34: /* RTIUC1 */
            return s->up_counter_reg[1];
        case 0x38: /* RTICPUC1 */
            return s->compare_up_counter[1];
        case 0x40: /* RTICAFRC1 */
            return s->capture_free_counter[1];
        case 0x44: /* RTICAFRC1 */
            return s->capture_up_counter[1];

        case 0x70: /* RTITBLCOMP */
            return s->timebase_low;
        case 0x74: /* RTITBHCOMP */
            return s->timebase_high;
        
        case 0x80:
        case 0x84: /* RTISETINTENA && RTICLEARINTENA */
            return s->int_ctrl;
        case 0x88: /* RTIINTFLAG */
            return s->int_flag;

        /*--------------------Digital Watchdog--------------------*/
        case 0x90: /* RTIDWDCTRL */
            return s->dw_ctrl;
        case 0x94: /* RTIDWPRLD */
            return s->dw_preload;
        case 0x98: /* RTIDWSTATUS */
            return s->dw_status;
        case 0x9c: /* RTIWDKEY */
            return s->rti_wd_key;
        case 0xa0: /* RTIDWDCNTR */
            return s->rti_dw_down_counter;
        case 0xa4: /* RTIWWDRXNCTRL */
            return s->dww_reaction_ctrl;
        case 0xa8: /* RTIWWDSIZECTRL */
            return s->dww_size_ctrl;

        default:
            break;
    }

}

static void rti_write(void *opaque, hwaddr offset,
                      uint64_t val, unsigned size)
{
    RTIState *s = (RTIState *)opaque;

    if (offset >= 0x50 && offset < 0x70)
    {
        uint64_t index = (offset - 0x50) >> 2;

        if (index & 0x001)
        { /* RTIUDCP0~3 */
            update_compare[(index >> 1)] = val;
        } else 
        { /* RTICOMP0~3 */
            compare[(index >> 1)] = val;
        }
        
    }

    switch (offset)
    {
        case 0x00: /* RTIGCTRL */
            s->global_ctrl = val;
            break;
        case 0x04: /* RTITBCTRL */
            s->timebase_ctrl = val;
            break;
        case 0x08: /* RTICAPCTRL */
            s->capture_ctrl = val;
            break;
        case 0x0c: /* RTICOMPCTRL */
            s->compare_ctrl = val;
            break; 

        case 0x10: /* RTIFRC0 */
            s->free_running_counter[0] = val;
            break;
        case 0x14: /* RTIUC0 */
            s->up_counter[0] = val;
            break; 
        case 0x18: /* RTICPUC0 */
            if ((s->timebase_ctrl & 0x1) == 0x0)
            {
                s->compare_up_counter[0] = val;
            }
            break;
        case 0x20: /* RTICAFRC0 RO*/
            break;
        case 0x24: /* RTICAUC0 RO*/
            break;

        case 0x30: /* RTIFRC1 */
            s->free_running_counter[1] = val;
            break;
        case 0x34: /* RTIUC1 */
            s->up_counter[1] = val;
            break; 
        case 0x38: /* RTICPUC1 */
            s->compare_up_counter[1] = val;
            break;
        case 0x40: /* RTICAFRC1 RO*/
            break;
        case 0x44: /* RTICAUC1 RO*/
            break;

        case 0x70: /* RTITBLCOMP */
            if ((s->timebase_ctrl & 0x1) == 0x0)
            {
                s->timebase_low = val;
            }
            break;
        case 0x74: /* RTITBHCOMP */
            if ((s->timebase_ctrl & 0x1) == 0x0)
            {
                s->timebase_high = val;
            }
            break;

        case 0x80: /* RTISETINTENA && RTICLEARINTENA */
            s->int_ctrl |= val;
            break;
        case 0x84: 
            s->int_ctrl &= ~val;
            break;
        case 0x88: /* RTIINTFLAG */
            s->int_flag &= ~val;
            break;

        /*--------------------Digital Watchdog--------------------*/
        case 0x90: /* RTIDWDCTRL */
            if (val == 0xa98559da)
            {
                s->dw_ctrl = val;
            }
            break;
        case 0x94: /* RTIDWPRLD */
            if (s->dw_ctrl == 0x5312aced)
            {
                s->dw_preload = val;
            }
            break;
        case 0x98: /* RTIDWSTATUS */
            s->dw_status &= ~val;
            break;
        case 0x9c: /* RTIWDKEY */
            if (s->rti_wd_key == 0xe51a)
            {
                if (val == 0xa35c)
                {   /* watchdog is reset */
                    
                }else if (val == 0xe51a)
                {   /* WDKEY is enabled for reset or NMI by next A35Ch */

                }
                break;
            }
            s->rti_wd_key = 0xa35c;
            break;
        case 0xa4: /* RTIWWDRXNCTRL */
            s->dww_reaction_ctrl = val;
            if (val == 0x5)
            {   
                /* The windowed watchdog will cause a reset if the watchdog
                 * is serviced outside the time window defined by the configuration, 
                 * or if the watchdog is not serviced at all 
                */
            }else if (val == 0xa)
            {
                /* The windowed watchdog will generate a non-maskable interrupt 
                 * to the CPU if the watchdog is serviced outside the time window 
                 * defined by the configuration, or if the watchdog is not servicedat all. 
                */
            }else
            {
                /* The windowed watchdog will cause a reset if the watchdog is serviced 
                 * outside the time window defined by the configuration, or if the watchdog 
                 * is not serviced at all 
                */
            }
            break;
        case 0xa8: /* RTIWWDSIZECTRL */
            s->dww_size_ctrl = val;
            break;

        default:
            break;
    }
}

static void rti_get_cap(void *opaque, int irq, int level)
{
    RTIState *s = (RTIState *)opaque;
    int capture = irq - 2;
    int i;

    if (capture >= 2)
    {
        fprintf(stderr, "%s: capture event sources %d out of range\n", __func__, irq);
        abort();
    }

    if ((s->capture_ctrl & 0x1) == capture) {
        s->capture_free_counter[0] = s->free_running_counter[0];
        s->capture_up_counter[0] = s->up_counter[0];
    }

    if (((s->capture_ctrl >> 1) & 0x1) == capture) {
        s->capture_free_counter[1] = s->free_running_counter[1];
        s->capture_up_counter[1] = s->up_counter[1];
    }
}

static void rti_reset(DeviceState *dev)
{
    RTIState *s = RTI(dev);

    s->dw_ctrl = 0x5312aced;
    s->dw_preload = 0xfff;
    s->rti_wd_key = 0xa35c;
    s->rti_dw_down_counter = 0xffff;
    s->dww_reaction_ctrl = 0x5;
}

static const MemoryRegionOps rti_ops = {
    .read = rti_read,
    .write = rti_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void rti_init(Object *obj)
{
    RTIState *s = RTI(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &rti_ops, s, "tms570-rti", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);

    /* CAP event rti0 & rti1 */
    qdev_init_gpio_in(dev, rti_get_cap, 2);

    sysbus_init_irq(dev, &s->irq_compare0);
    sysbus_init_irq(dev, &s->irq_compare1);
    sysbus_init_irq(dev, &s->irq_compare2);
    sysbus_init_irq(dev, &s->irq_compare3);
    sysbus_init_irq(dev, &s->irq_overflow0);
    sysbus_init_irq(dev, &s->irq_overflow1);
    sysbus_init_irq(dev, &s->irq_timebase);
}

static void rti_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->relize = rti_realize;
    k->props = rti_properties;
    k->vmsd = &vmstate_rti;
    k->reset = &rti_reset;
}

static const TypeInfo rti_info = {
    .name           = TYPE_RTI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(RTIState),
    .instance_init  = rti_init,
    .class_init     = rti_class_init,
};

static void rti_register_types(void)
{
    type_register_static(&rti_info);
}

type_init(rti_register_types)