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
    ptimer_state *timer[3];

    uint32_t overflow_flag[2];
    uint32_t ptimer_running[2];

    uint32_t global_ctrl;               /* RTIGCTRL */
    uint32_t timebase_ctrl;             /* RTITBCTRL */
    uint32_t capture_ctrl;              /* RTICAPCTRL */
    uint32_t compare_ctrl;              /* RTICOMPCTRL */

    uint32_t free_running_counter[2];   /* RTIFRC0~1 >>5 */
    uint32_t up_counter[2];             /* RTIUC0~1 >>5 */
    uint32_t compare_up_counter[2];     /* RTICPUC0~1 >>5 */
    uint32_t capture_free_counter[2];   /* RTICAFRC0~1 >>5 */
    uint32_t capture_up_counter[2];     /* RTICAUC0~1 >>5 */

    uint32_t compare[4];                /* RTICOMP0~3 >>3 */
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

    uint32_t rti_reset_flag;
    
    qemu_irq irq_compare[4];
    qemu_irq irq_overflow[2];
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

static void rti_update_irq(RTIState *s)
{
    /* enabled and pending interrupt */
    uint32_t en_pend_irq = s->int_ctrl & s->int_flag & 0x0007000f;

    for (size_t i = 0; i < 4; i++)
    {
        qemu_set_irq(s->irq_compare[i], en_pend_irq & (0x1 << i));
    }

    for (size_t i = 0; i < 2; i++)
    {
        qemu_set_irq(s->irq_overflow[i], en_pend_irq & (0x1 << (i + 17)));
    }

    qemu_set_irq(s->irq_timebase, en_pend_irq & (0x1 << 16));

}

static void rti_update(RTIState *s)
{
    rti_update_irq(s);
}

static void rti_update_compare(RTIState *s, int counter_num)
{
    for (size_t i = 0; i < 4; i++)
    {
        if (((s->compare_ctrl >> (i*4)) & 0x1) == counter_num)
        {
            if (s->free_running_counter[counter_num] == s->compare[i])
            { //compare interrupt
                s->compare[i] += s->update_compare[i]; 
                s->int_flag |= 0x1 << i;
            } else
            {
                s->int_flag &= ~(0x1 << i);
            }
            rti_update_irq(s);
        }
    }
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
            return s->update_compare[(index >> 1)];
        } else 
        { /* RTICOMP0~3 */
            return s->compare[(index >> 1)];
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
            s->up_counter[0] = s->compare_up_counter[0] - ptimer_get_count(s->timer[0]);
            return s->free_running_counter[0];
        case 0x14: /* RTIUC0 */
            return s->up_counter[0];
        case 0x18: /* RTICPUC0 */
            return s->compare_up_counter[0];
        case 0x20: /* RTICAFRC0 */
            return s->capture_free_counter[0];
        case 0x24: /* RTICAUC0 */
            return s->capture_up_counter[0];

        case 0x30: /* RTIFRC1 */
            s->up_counter[1] = s->compare_up_counter[1] - ptimer_get_count(s->timer[1]);
            return s->free_running_counter[1];
        case 0x34: /* RTIUC1 */
            return s->up_counter[1];
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
            return s->rti_dw_down_counter = ptimer_get_count(s->timer[2]);
        case 0xa4: /* RTIWWDRXNCTRL */
            return s->dww_reaction_ctrl;
        case 0xa8: /* RTIWWDSIZECTRL */
            return s->dww_size_ctrl;

        default:
             qemu_log_mask(LOG_GUEST_ERROR,
                      "rti_read: Bad offset %x\n", (int)offset);
            return 0;
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
            s->update_compare[(index >> 1)] = val;
        } else 
        { /* RTICOMP0~3 */
            s->compare[(index >> 1)] = val;
        }
        
    }

    /* Writes to Reserved registers may clear the pending RTI interrupt. */
    if (offset >= 0xb0 && offset < 0xc0)
    { /* RTICOMP0~3CLR */
        uint64_t index = (offset - 0xb0) >> 2;

        s->int_flag &= ~(0x1 << (index - 1));
    }

    switch (offset)
    {
        case 0x00: /* RTIGCTRL */
            s->global_ctrl = val;
            for (size_t i = 0; i < 2; i++)
            {
                int bit = 0x1 << i;
                /* counter0~1 */
                if ((s->global_ctrl & bit) == bit)
                { //enable
                    ptimer_run(s->timer[i], 0);
                    s->ptimer_running[i] = 1;
                } else
                { //disable
                    ptimer_stop(s->timer[i]);
                    s->ptimer_running[i] = 0;
                }
            }
            // if ((s->global_ctrl & 0x8000) != 0x8000)
            // {   /* halting debug mode */
            //     ptimer_stop(s->timer[0]);
            //     ptimer_stop(s->timer[1]);
            // }
            break;
        case 0x04: /* RTITBCTRL */
            s->timebase_ctrl = val; //todo
            break;
        case 0x08: /* RTICAPCTRL */
            s->capture_ctrl = val;
            break;
        case 0x0c: /* RTICOMPCTRL */
            s->compare_ctrl = val & 0x00001111;
            break; 

        case 0x10: /* RTIFRC0 */
            s->free_running_counter[0] = val;
            s->overflow_flag[0] = 0;
            break;
        case 0x14: /* RTIUC0 */
            s->up_counter[0] = val;
            ptimer_set_count(s->timer[0], s->compare_up_counter[0] - s->up_counter[0]);
            break; 
        case 0x18: /* RTICPUC0 */
            if ((s->timebase_ctrl & 0x1) == 0x0)
            {
                s->compare_up_counter[0] = val;
                ptimer_set_limit(s->timer[0], s->compare_up_counter[0], 1);
                //ptimer_set_count(s->timer[0], s->compare_up_counter[0] - s->up_counter[0]);
            }
            break;
        case 0x20: /* RTICAFRC0 RO*/
            break;
        case 0x24: /* RTICAUC0 RO*/
            break;

        case 0x30: /* RTIFRC1 */
            s->free_running_counter[1] = val;
            s->overflow_flag[0] = 0;
            break;
        case 0x34: /* RTIUC1 */
            s->up_counter[1] = val;
            ptimer_set_count(s->timer[1], s->compare_up_counter[1] - s->up_counter[1]);
            break; 
        case 0x38: /* RTICPUC1 */
            s->compare_up_counter[1] = val;
            ptimer_set_limit(s->timer[1], s->compare_up_counter[1], 1);
            break;
        case 0x40: /* RTICAFRC1 RO*/
            break;
        case 0x44: /* RTICAUC1 RO*/
            break;

        case 0x70: /* RTITBLCOMP */
            if ((s->timebase_ctrl & 0x1) == 0x0)
            {
                s->timebase_low = val;
                ptimer_set_count(s->timer[1], s->compare_up_counter[1] - s->up_counter[1]);
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
            rti_update(s);
            break;
        case 0x84: 
            s->int_ctrl &= ~val;
            rti_update(s);
            break;
        case 0x88: /* RTIINTFLAG */
            s->int_flag &= ~val;
            rti_update(s);
            break;

        /*--------------------Digital Watchdog--------------------*/
        case 0x90: /* RTIDWDCTRL */
            if (val == 0xa98559da)
            {
                s->dw_ctrl = val;
                ptimer_run(s->timer[2], 1);
            }
            break;
        case 0x94: /* RTIDWPRLD */
            if (s->dw_ctrl == 0x5312aced)
            {
                s->dw_preload = val;
                ptimer_set_limit(s->timer[2], s->dw_preload, 1);
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
                    dw_reset(s);
                }else if (val == 0xe51a)
                {   /* WDKEY is enabled for reset or NMI by next A35Ch */
                    s->rti_reset_flag = 1;
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
        case 0xac: /* RTIINTCLRENABLE */
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                      "rti_write: Bad offset %x\n", (int)offset);
            break;
    }
}

static void rti_get_cap(void *opaque, int irq, int level)
{
    RTIState *s = (RTIState *)opaque;
    int capture = irq - 2;

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

    s->global_ctrl = 0x0;
    s->timebase_ctrl = 0x0;
    s->capture_ctrl = 0x0;
    s->compare_ctrl = 0x0;


    for (size_t i = 0; i < 2; i++)
    {
        s->free_running_counter[i] = 0;
        s->up_counter[i] = 0;
        s->compare_up_counter[i] = 0;
        s->capture_free_counter[i] = 0;
        s->capture_up_counter[i] = 0;
    }

    for (size_t i = 0; i < 4; i++)
    {
        s->compare[i] = 0;
        s->update_compare[i] = 0;
    }
    
    s->timebase_low = 0;
    s->timebase_high = 0;
    s->int_ctrl = 0;
    s->int_flag = 0;

    dw_reset(s);
    rti_update(s);
}

static const MemoryRegionOps rti_ops = {
    .read = rti_read,
    .write = rti_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void rti_timer_tick(void *opaque)
{
    RTIState *s = (RTIState *)opaque;

    for (size_t i = 0; i < 2; i++)
    {
        if (s->ptimer_running[i])
        {
            s->up_counter[i] = 0;
            s->free_running_counter[i]++;
            s->overflow_flag[i] = 1;
            rti_update_compare(s, i);
        }

        if ((s->free_running_counter[i] == 0) && s->overflow_flag[i])
        {  /* overflow */
            s->int_flag |= 0x1 << (17 + i); // overflow int
            s->overflow_flag[i] = 0;
            rti_update_irq(s);
        }
    }
}

static void rti_watchdog_tick(void *opaque)
{
    RTIState *s = (RTIState *)opaque;

    if (ptimer_get_count(s->timer[2]) == 0 && s->rti_reset_flag)
    {
        s->rti_reset_flag = 0;
        rti_reset((DeviceState *)opaque);
    }
}

static void rti_init(Object *obj)
{
    RTIState *s = RTI(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    QEMUBH *bh[3];

    memory_region_init_io(&s->iomem, obj, &rti_ops, s, "tms570-rti", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);

    /* init timer */
    for (size_t i = 0; i < 2; i++)
    {
        bh[i] = qemu_bh_new(rti_timer_tick, s);
        s->timer[i] = ptimer_init(bh[i]);
    }

    bh[2] = qemu_bh_new(rti_watchdog_tick, s);
    s->timer[2] = ptimer_init(bh[2]);

    /* HF LPO run at 10MHz.  */
    for (size_t i = 0; i < 2; i++)
    {
        ptimer_set_freq(s->timer[i], 10000000);
    }
    
    /* CAP event rti0 & rti1 */
    qdev_init_gpio_in(dev, rti_get_cap, 2);

    for (size_t i = 0; i < 4; i++)
    {
        sysbus_init_irq(sbd, &s->irq_compare[i]);
    }

    for (size_t i = 0; i < 2; i++)
    {
        sysbus_init_irq(sbd, &s->irq_overflow[i]);
    }

    sysbus_init_irq(sbd, &s->irq_timebase);
}

static const VMStateDescription vmstate_rti = {
    .name = "tms570-rti",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void rti_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    //k->props = rti_properties;
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