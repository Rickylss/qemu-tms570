/*
 * mpc5675 Software Watchdog Timer
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

#define TYPE_SWT "mpc5675-swt"
#define SWT(obj) OBJECT_CHECK(SwtState, (obj), TYPE_SWT)

typedef struct SwtState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ptimer_state *timer;

    uint32_t swt_cr;
    uint32_t swt_ir;
    uint32_t swt_to;
    uint32_t swt_wn;
    uint32_t swt_sr;
    uint32_t swt_co;
    uint32_t swt_sk;

    uint32_t timeout_count;

    qemu_irq irq;
    qemu_irq exception; // PPCE200_INPUT_RESET_SYS system reset
} SwtState;

static void swt_update_irq(SwtState *s)
{
    /* 
     * if SWT_CR[ITR] is set, Generate an interrupt on an 
     * initial timeout, reset on a second consecutive timeout.
     * else generate a reset on a timeout.
     */
    if (s->swt_cr & 0x40) {
        if (s->timeout_count == 1) {
            s->swt_ir |= 0x1;
            qemu_set_irq(s->irq, s->swt_ir);
            ptimer_set_count(s->timer, s->swt_to);
            ptimer_run(s->timer, 1);
        } else if(s->timeout_count == 2) {
            qemu_irq_raise(s->exception);
            s->timeout_count = 0;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                      "system should be reset\n");
            s->timeout_count = 0;
        }
    } else {
        qemu_irq_raise(s->exception);
        s->timeout_count = 0;
    }
    
}

static void swt_update_windowed(SwtState *s)
{
    if (s->swt_cr & 0x100) {    // cause a system reset if WEN = 1
        if (s->swt_cr & 0x1)
            qemu_irq_raise(s->exception);
    } else {                    // generates a bus error
        /* TODO */
    }
}

static uint64_t swt_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    SwtState *s = (SwtState *)opaque;

    switch (offset)
    {
    case 0x00:  //SWT_CR
        return s->swt_cr;
    case 0x04:  //SWT_IR
        return s->swt_ir & 0x1;
    case 0x08:  //SWT_TO
        return s->swt_to;
    case 0x0c:  //SWT_WN
        return s->swt_wn;
    case 0x10:  //SWT_SR writeonly
        return 0;
    case 0x14:  //SWT_CO
        /*
         *  When the watchdog is disabled (SWT_CR[WEN] = 0), 
         * this field shows the valueof the internal down counter
         */
        if ((s->swt_cr & 0x1) == 0) {
            s->swt_co = ptimer_get_count(s->timer);
        } else {
            return 0;
        }
        return s->swt_co;
    case 0x18:  //SWT_SK writeonly
        return 0;
    default:
        return 0;
    }
}

inline void gen_code(uint32_t key)
{
    return (17 * key + 3) % 0x10000;
}

static void service_sequence(SwtState *s, uint32_t val)
{
    if (s->swt_cr & 0x200) {    // Keyed Service Mode
        if (gen_code(s->swt_sk) == s->swt_sk && gen_code(s->swt_sr) == val) { // valid key
            s->swt_sk = val & 0xffff;
            ptimer_set_count(s->timer, s->swt_to);
        }
        s->swt_sr = val & 0xffff;
    } else {                    //Fixed Service Sequence
        /* 0xC520 followed by 0xD928 clear the soft lock bit SWT_CR[SLK] */
        if ((s->swt_sr == 0xc520) && (val == 0xd928)) {
            s->swt_cr &= ~0x10;
            ptimer_set_count(s->timer, s->swt_to);
        }
        s->swt_sr = val & 0xffff;
    }
}

static void swt_write(void *opaque, hwaddr offset,
                        uint64_t val, unsigned size)
{
    SwtState *s = (SwtState *)opaque;

    switch (offset)
    {
    case 0x00:  //SWT_CR
        if ((s->swt_cr & 0x30) == 0) { //write able
            if (s->swt_cr & 0x1) {
                ptimer_run(s->timer, 0);
            } else {
                ptimer_stop(s->timer);
            }
            if (s->swt_cr & 0x8) {  // use oscillator clock
                ptimer_set_freq(s->timer, 16 * 1000 * 1000);
            } else {                // use system clock
                ptimer_set_freq(s->timer, 50 * 1000 * 1000);
            }
        }
        break;
    case 0x04:  //SWT_IR
        s->swt_ir &= ~(val & 0x1);
        break;
    case 0x08:  //SWT_TO
        if ((s->swt_cr & 0x30) == 0) { //write able
            s->swt_to = val;
            /* SWT is enabled or the service sequence is written */
            if (s->swt_cr & 0x1 || s->swt_sr) { 
                ptimer_set_count(s->timer, (s->swt_to > 0x100) ? s->swt_to : 0x100);
            }
        }
        break;
    case 0x0c:  //SWT_WN
        if ((s->swt_cr & 0x30) == 0) { //write able
            s->swt_wn = val;
        }
        break;
    case 0x10:  //SWT_SR
        if (s->swt_cr & 0x80) {     // Windowed mode
            /* service sequene is only valid when the down counter 
             * is less than the value in the SWT_WN register 
             */
            if (ptimer_get_count(s->timer) < s->swt_wn) { 
                service_sequence(s, val);
            } else {                // invalid access
                swt_update_windowed(s);
            }
        } else {                    // regular mode
            service_sequence(s, val);
        }
        break;
    case 0x14:  //SWT_CO readonly 
        break;  
    case 0x18:  //SWT_SK
        if ((s->swt_cr & 0x30) == 0) { //write able
            s->swt_sk = val & 0xffff;
        }
        break;
    default:
        break;
    }

}

static void swt_timer_tick(void *opaque) 
{
    SwtState *s = SWT(opaque);

    s->timeout_count++;
    swt_update_irq(s);
}

static const MemoryRegionOps swt_ops = {
    .read = swt_read,
    .write = swt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_swt = {
    .name = "mpc5675-swt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(swt_cr, SwtState),
        VMSTATE_UINT32(swt_ir, SwtState),
        VMSTATE_UINT32(swt_to, SwtState),
        VMSTATE_UINT32(swt_wn, SwtState),
        VMSTATE_UINT32(swt_sr, SwtState),
        VMSTATE_UINT32(swt_co, SwtState),
        VMSTATE_UINT32(swt_sk, SwtState),
        VMSTATE_END_OF_LIST()
    }
};

static void swt_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SwtState *s = SWT(obj);
    QEMUBH *bh;

    memory_region_init_io(&s->iomem, OBJECT(s), &swt_ops, s, "mpc5675-swt", 0x4000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->exception);
    sysbus_init_irq(sbd, &s->irq);

    bh = qemu_bh_new(swt_timer_tick, s);
    s->timer = ptimer_init(bh);

    s->swt_cr = 0xff00011b;
    s->swt_to = 0x0003fde0;

    /* Default to IRCOSC 16MHz internal RC oscillator */
    ptimer_set_freq(s->timer, 16 * 1000 * 1000);
    ptimer_set_count(s->timer, 0x0003fde0);
    ptimer_run(s->timer, 1);
}

static void swt_reset(DeviceState *d)
{
    SwtState *s = SWT(d);

    s->swt_cr = 0xff00011b;
    s->swt_to = 0x0003fde0;

    ptimer_set_count(s->timer, 0x0003fde0);
    ptimer_run(s->timer, 0);
}

static void swt_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = swt_reset;
    dc->vmsd = &vmstate_swt;
}

static const TypeInfo swt_info = {
    .name          = TYPE_SWT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SwtState),
    .instance_init = swt_init,
    .class_init    = swt_class_init,
};

static void swt_register_types(void)
{
    type_register_static(&swt_info);
}

type_init(swt_register_types)
