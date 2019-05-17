/*
 * mpc5675 interrupt controller
 *
 * Copyright (c) 2019 FormalTech
 * Written by xiaohaibiao
 * 
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include <math.h>

#define TYPE_INTC "mpc5675-intc"
#define INTC(obj) OBJECT_CHECK(IntcState, (obj), TYPE_INTC)
#define MAX_IRQ (329 + 8)

typedef enum {
    DPM,
    LSM
} Operation_Mode;

typedef enum {
    SOFTWARE_VECTOR_MODE,
    HARDWARE_VECTOR_MODE
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
    Operation_Mode op_mode;

    stack LIFO;
    uint32_t entry_size;

    /* Behavior control */
    uint32_t bcr; /* INTC Block Configuration Register */
    uint32_t cpr_prc0; /* INTC Current Priority Register for Processor 0 */
    uint32_t cpr_prc1; /* INTC Current Priority Register for Processor 1 */
    uint32_t iackr_prc0; /* INTC Interrupt Acknowledge Register for Processor 0 */
    uint32_t iackr_prc1; /* INTC Interrupt Acknowledge Register for Processor 1 */
    uint32_t eoir_prc0; /* INTC End of Interrupt Register for Processor 0 */
    uint32_t eoir_prc1; /* INTC End of Interrupt Register for Processor 1 */
    uint32_t sscir[2]; /* INTC Software Set/Clear Interrupt Register */
    uint32_t pri[337]; /* INTC Priority Select Registers */

    uint8_t asserted_int[337];
    uint32_t current_irq;

    qemu_irq irq;
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
        s->entry_size = 8;//bytes
    } else {
        s->entry_size = 4;//bytes
    }
}

static void push_LIFO(IntcState *s)
{
    if (++(s->LIFO.top) < 16) {
        s->LIFO.pri[s->LIFO.top] = s->cpr_prc0 & 0xf;
    } else { // overwritten the priorities first pushed
        for (int i=0; i < 15; i++) {
            if (i == 14) {
                s->LIFO.pri[i] = s->cpr_prc0 & 0xf;
            } else {
                s->LIFO.pri[i] = s->LIFO.pri[i+1];
            }
        }
    }
}

static uint32_t pop_LIFO(IntcState *s)
{
    if (s->LIFO.top == 0) {
        return 0;
    } else {
        return s->LIFO.pri[(s->LIFO.top)--];
    }
}

static void intc_update_vectors(IntcState *s)
{
    int irq = 0;
    int32_t temp_pri = 0;

    for (int i = 0; i < MAX_IRQ; i++) {
        if (s->asserted_int[i])
        { // asserted interrupt
            if (s->pri[i] > temp_pri) { //only the one with the lowest vector is chosen
                temp_pri = s->pri[i];
                irq = i;
            }
        }
    }

    /* compare to current pri */
    if (temp_pri > (s->cpr_prc0 & 0xf))
    {
        s->current_irq = irq;
        push_LIFO(s);
        s->cpr_prc0 = temp_pri & 0xf;
        /* set INTC_IACKR_PRC0 with current isr */
        s->iackr_prc0 += s->entry_size * irq;

        qemu_irq_raise(s->irq);

        s->asserted_int[irq] = 0;
    }

    qemu_irq_lower(s->irq);
}

static void intc_set_software_irq(IntcState *s)
{
    /* get asserted software-settable interrupt irq */
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            if(s->sscir[i] & (0x1 << (3-j)*8)) {
                s->asserted_int[i*4 + j] = 1;
            } else {
                s->asserted_int[i*4 + j] = 0;
            }
        }
    }

    intc_update_vectors(s);
}

static void intc_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    IntcState *s = opaque;

    if ( offset >= 0x20 && offset < 0x28) { // SSCIRn
        int index = (offset - 0x20) >> 2;
        s->sscir[index] &= ~(val & 0x01010101);
        s->sscir[index] |= (val >> 1) & 0x01010101;
        intc_set_software_irq(s);
    }

    if (offset >= 0x40 && offset < 0x0191) { // PSRn
        int index = offset - 0x40;
        if (index < 337) {
            s->pri[index] = val & 0xff;
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
        s->pri[s->current_irq] = 0;
        intc_update_vectors(s);
        break;
    case 0x10:
        s->iackr_prc0 = val;
        intc_update_vectors(s);
        break;
    case 0x18:
        s->cpr_prc0 = pop_LIFO(s);
        intc_update_vectors(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "intc_write: Bad offset %x\n", (int)offset);
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
            intc_update_vectors(s);
            break;
        case HARDWARE_VECTOR_MODE:
        default:
            break;
        }  
        return s->iackr_prc0;
    case 0x18:
        return 0x0;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "intc_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void intc_set_peripheral_irq(void *opaque, int irq, int level)
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

    /* enable asserted interrupt */
    if (level) {
        s->asserted_int[irq] = 1;
    } else {
        s->asserted_int[irq] = 0;
    }

    intc_update_vectors(s);
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
        VMSTATE_UINT32(bcr, IntcState),
        VMSTATE_UINT32(cpr_prc0, IntcState),
        VMSTATE_UINT32(iackr_prc0, IntcState),
        VMSTATE_UINT32(eoir_prc0, IntcState),
        VMSTATE_UINT32_ARRAY(sscir, IntcState, 2),
        VMSTATE_UINT32_ARRAY(pri, IntcState, MAX_IRQ),
        VMSTATE_UINT8_ARRAY(asserted_int, IntcState, MAX_IRQ),
        VMSTATE_END_OF_LIST()
    }
};

static void intc_init(Object *obj)
{
    IntcState *s = INTC(obj);
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mem, obj, &intc_ops, s, "mpc5675-intc", 0x4000);
    sysbus_init_mmio(d, &s->mem);
    qdev_init_gpio_in(dev, intc_set_peripheral_irq, MAX_IRQ);

    /* external interrupt ivor4 */
    sysbus_init_irq(d, &s->irq);

    qdev_init_gpio_in_named(dev, operating_mode_set, "lsm_dpm", 1);
}

static void intc_reset(DeviceState *d)
{
    IntcState *s = INTC(d);

    s->cpr_prc0 = 0x0000000f;
    s->entry_size = 4;//byte
}

static void intc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = intc_reset;
    dc->vmsd = &vmstate_intc;
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