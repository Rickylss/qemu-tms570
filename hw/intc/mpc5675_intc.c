/*
 * mpc5675 interrupt controller
 *
 * Copyright (c) 2019 FormalTech
 * Written by xiaohaibiao
 */


#include "hw/pci/pci.h"
#include "hw/ppc/ppc_e500.h"
#include "hw/sysbus.h"
#include "hw/pci/msi.h"
#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "qemu/log.h"

#define TYPE_INTC "mpc5675-intc"
#define INTC(obj) OBJECT_CHECK(IntcState, (obj), TYPE_INTC)
#define MAX_IRQ (329 + 8)

typedef enum {
    SOFTWARE_VECTOR_MODE,
    HARDWARE_VECTOR_MODE,
} Vector_Mode;

typedef struct IntcState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;

    Vector_Mode mode;

    uint32_t LIFO[14];
    uint32_t vector_table_entry_size;

    /* Behavior control */
    uint32_t bcr; /* INTC Block Configuration Register */
    uint32_t cpr_prc0; /* INTC Current Priority Register for Processor 0 */
    uint32_t iackr_prc0; /* INTC Interrupt Acknowledge Register for Processor 0 */
    uint32_t eoir_prc0; /* INTC End of Interrupt Register for Processor 0 */
    uint32_t sscir[8]; /* INTC Software Set/Clear Interrupt Register */
    uint8_t pri[337]; /* INTC Priority Select Registers */

    uint32_t irq;
} IntcState;

static void switch_vector_mode(IntcState *s, uint32_t is_hardware_mode)
{
    if (is_hardware_mode)
    {
        s->mode = HARDWARE_VECTOR_MODE;
    } else
    {
        s->mode = SOFTWARE_VECTOR_MODE;
    }
}

static void set_vector_table_entry_size(IntcState *s, uint32_t is_8_bytes)
{
    if (is_8_bytes)
    {
        s->vector_table_entry_size = 8;//bytes
    } else
    {
        s->vector_table_entry_size = 4;//bytes
    }
}

static void push_LIFO(IntcState *s)
{

}

static void pop_LIFO(IntcState *s)
{

}

static void intc_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    IntcState *s = opaque;

    switch (offset)
    {
    case 0x00:
        s->bcr = val;
        /* if bcr & 0x1 swtich to hardware vector mode */
        switch_vector_mode(s, (s->bcr & 0x1));
        set_vector_table_entry_size(s, (s->bcr & 0x20));
        break;
    case 0x08:
        s->cpr_prc0 = val & 0xf;
        intc_update_vectors(s);
        break;
    case 0x10:
        s->iackr_prc0 = val;
        break;
    
    default:
        break;
    }
}

static uint64_t intc_read(void *opaque, hwaddr offset, unsigned size)
{
    IntcState *s = opaque;

    switch (offset)
    {
    case 0x00:
        return s->bcr;
    case 0x08:
        return s->cpr_prc0;
    case 0x10:
        switch (s->mode)
        {
        case SOFTWARE_VECTOR_MODE:
            push_LIFO(s);
            s->cpr_prc0 = 
            break;
        case HARDWARE_VECTOR_MODE:
        default:
            break;
        }
              
        return s->iackr_prc0;

    default:
        break;
    }
}

static const MemoryRegionOps intc_ops = {
    .write = intc_write,
    .read  = intc_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_intc = {
    .name = "mpc5675-intc",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(gcr, OpenPICState),
        VMSTATE_UINT32(vir, OpenPICState),
        VMSTATE_UINT32(pir, OpenPICState),
        VMSTATE_UINT32(spve, OpenPICState),
        VMSTATE_UINT32(tfrr, OpenPICState),
        VMSTATE_UINT32(max_irq, OpenPICState),
        VMSTATE_STRUCT_VARRAY_UINT32(src, OpenPICState, max_irq, 0,
                                     vmstate_openpic_irqsource, IRQSource),
        VMSTATE_UINT32_EQUAL(nb_cpus, OpenPICState),
        VMSTATE_STRUCT_VARRAY_UINT32(dst, OpenPICState, nb_cpus, 0,
                                     vmstate_openpic_irqdest, IRQDest),
        VMSTATE_STRUCT_ARRAY(timers, OpenPICState, OPENPIC_MAX_TMR, 0,
                             vmstate_openpic_timer, OpenPICTimer),
        VMSTATE_STRUCT_ARRAY(msi, OpenPICState, MAX_MSI, 0,
                             vmstate_openpic_msi, OpenPICMSI),
        VMSTATE_UINT32(irq_ipi0, OpenPICState),
        VMSTATE_UINT32(irq_tim0, OpenPICState),
        VMSTATE_UINT32(irq_msi, OpenPICState),
        VMSTATE_END_OF_LIST()
    }
};

static void intc_init(Object *obj)
{
    IntcState *s = INTC(obj);
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    IntcState *s = INTC(dev);

    memory_region_init_io(&s->mem, obj, &intc_ops, s, "mpc5675-intc", 0x4000);
    sysbus_init_mmio(d, &s->mem);
    qdev_init_gpio_in(dev, intc_set_irq, MAX_IRQ);

    sysbus_init_irq(d, &s->irq);
}

static void intc_realize(DeviceState *dev, Error **errp)
{

}

static Property intc_properties[] = {
    DEFINE_PROP_UINT32("model", OpenPICState, model, OPENPIC_MODEL_FSL_MPIC_20),
    DEFINE_PROP_UINT32("nb_cpus", OpenPICState, nb_cpus, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void intc_reset(DeviceState *d)
{
    IntcState *s = INTC(d);

    s->cpr_prc0 = 0x0000000f;
    s->vector_table_entry_size = 4;
}

static void intc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = intc_realize;
    dc->props = intc_properties;
    dc->reset = intc_reset;
    dc->vmsd = &vmstate_intc;
    //set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo intc_info = {
    .name          = TYPE_INTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IntcState),
    .instance_init = intc_init,
    .class_init    = intc_class_init,
};

static void intc_register_types(void)
{
    type_register_static(&intc_info);
}

type_init(intc_register_types)