/*
 * mpc5675 interrupt controller
 *
 * Copyright (c) 2019 FormalTech
 * Written by xiaohaibiao
 * 
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
    DPM,
    LSM,
} Operation_Mode;

typedef enum {
    SOFTWARE_VECTOR_MODE,
    HARDWARE_VECTOR_MODE,
} Vector_Mode;

typedef struct stack {
    int top;
    int pri[15];
} stack;

typedef struct IntcState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;

    Vector_Mode vect_mode;
    Operation_mode op_mode;

    stack LIFO;
    uint32_t vector_table_entry_size;

    /* Behavior control */
    uint32_t bcr; /* INTC Block Configuration Register */
    uint32_t cpr_prc0; /* INTC Current Priority Register for Processor 0 */
    uint32_t iackr_prc0; /* INTC Interrupt Acknowledge Register for Processor 0 */
    uint32_t eoir_prc0; /* INTC End of Interrupt Register for Processor 0 */
    uint32_t sscir[2]; /* INTC Software Set/Clear Interrupt Register */
    uint32_t pri[337]; /* INTC Priority Select Registers */

    //uint32_t asserted_irq[];

    uint32_t irq;
} IntcState;

static void switch_vector_mode(IntcState *s, uint32_t is_hardware_mode)
{
    if (is_hardware_mode) {
        s->vect_mode = HARDWARE_VECTOR_MODE;
    } else {
        s->vect_mode = SOFTWARE_VECTOR_MODE;
    }
}

static void set_vector_table_entry_size(IntcState *s, uint32_t is_8_bytes)
{
    if (is_8_bytes) {
        s->vector_table_entry_size = 8;//bytes
    } else {
        s->vector_table_entry_size = 4;//bytes
    }
}

static void push_LIFO(IntcState *s, uint32_t pri)
{
    if (++(s->LIFO.top) < 16) {
        s->LIFO.pri[s->LIFO.top] = pri;
    } else { // overwritten the priorities first pushed
        for (int i=0; i < 15; i++) {
            if (i == 14) {
                s->LIFO.pri[i] = pri;
            } else {
                s->LIFO.pri[i] = s->LIFO.pri[i+1];
            }
        }
    }
}

static uint32_t pop_LIFO(IntcState *s)
{
    if (s->LIFO.top = 0) {
        return 0;
    } else {
        return s->LIFO.pri[(s->LIFO.top)--];
    }
}

static void intc_update_vectors(IntcState *s)
{

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            if (s->sscir[i] & (0x1 << (3-j)*8)) {
                if(s->pri[(4*i+j)] <= s->cpr_prc0) {
                    continue;
                }
                
            }
        }
    }

}

static void intc_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    IntcState *s = opaque;

    if ( offset >= 0x20 && offset < 0x28) { // SSCIRn
        int index = (offset - 0x20) >> 2;
        s->sscir[index] &= ~(val & 0x01010101);
        s->sscir[index] |= (val >> 1) & 0x01010101;
        intc_update_vectors(s);
    }

    if (offset >= 0x40 && offset < 0x0194) { // PSRn
        int pri_index;
        int index = (offset - 0x40) >> 2;
        int start_index = index * 4;
        for (int i = 0; i < 4; i++) {
            pri_index = start_index + i;
            if (pri_index < 337) {
                s->pri[pri_index] = (val >> 8*(3-i)) & 0xff;
            }
        }
    }

    switch (offset)
    {
    case 0x00:
        s->bcr = val & 0x21;
        /* if bcr & 0x1 swtich to hardware vector mode */
        switch_vector_mode(s, (s->bcr & 0x1));
        /* set vector table entry size */
        set_vector_table_entry_size(s, (s->bcr & 0x20));
        break;
    case 0x08:
        s->cpr_prc0 = val & 0xf;
        intc_update_vectors(s);
        break;
    case 0x10:
        s->iackr_prc0 = val;
        break;
    case 0x18:
        pop_LIFO(s);
        break;
    default:
        break;
    }
}

static uint64_t intc_read(void *opaque, hwaddr offset, unsigned size)
{
    IntcState *s = opaque;

    if ( offset >= 0x20 && offset < 0x28) { // SSCIRn
        int index = (offset - 0x20) >> 2;
        return s->sscir[index] & 0x01010101; //SETx bit is always read as 0
    }

    if (offset >= 0x40 && offset < 0x0194) { // PSRn
        int pri_index;
        int index = (offset - 0x40) >> 2;
        int start_index = index * 4;
        uint32_t ret;
        for (int i = 0; i < 4; i++) {
            pri_index = start_index + i;
            if (pri_index < 337) {
                ret += s->pri[pri_index] << 8*(3-i);
            }
        }
        return ret;
    }

    switch (offset)
    {
    case 0x00:
        return s->bcr;
    case 0x08:
        return s->cpr_prc0;
    case 0x10:
        switch (s->vect_mode)
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
    case 0x18:
        return 0x0;

    default:
        break;
    }
}

static void intc_set_irq(void *opaque, int irq, int level)
{
    IntcState *s = opaque;

    if (irq >= MAX_IRQ) {
        fprintf(stderr, "%s: IRQ %d out of range\n", __func__, irq);
        abort();
    }

    if (irq < 7) {
        fprintf(stderr, "%s: IRQ %d is software-setable\n", __func__, irq);
        abort();
    }

    /* find the correspond pri for irq */
    uint8_t pri = s->psr[irq];
    
    if (level) {
        if (pri > (s->cpr_prc0 &0xff)) {
            s->iackr_prc0 = new vector;
            push_LIFO(s);
            s->cpr_prc0 = pri;
        }
    }

}

static void operating_mode_set(void *opaque, int line, int level)
{
    IntcState *s = opaque;
    if (level) {
        s->op_mode = LSM;
    } else {
        s->op_mode = DPM;
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

    qdev_init_gpio_in_named(dev, operating_mode_set, "lsm_dpm", 1);
}

static void intc_realize(DeviceState *dev, Error **errp)
{

}

static Property intc_properties[] = {
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