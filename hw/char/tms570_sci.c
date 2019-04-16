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

#define INT_MASK 0x07000303
#define SCIFLR_FE (1 << 26)
#define SCIFLR_OE (1 << 25)
#define SCIFLR_PE (1 << 24)
#define SCIFLR_RX_WAKE (1 << 12)
#define SCIFLR_RX_RDY (1 << 9)
#define SCIFLR_BRKDT (1)
#define SCIFLR_WAKEUP (1 << 2)
#define SCIFLR_TX_EMPTY (1 << 11)
#define SCIFLR_TX_RDY (1 << 8)

#define TXENA 0x02000000
#define RXENA 0x01000000

static const unsigned char sci_vect_offset[7] = 
 { 0x1, 0x3, 0x6, 0x7, 0x9, 0xb, 0xc };

static const uint32_t sci_int_mask[7] = 
 {  SCIFLR_WAKEUP, 
    SCIFLR_PE, 
    SCIFLR_FE, 
    SCIFLR_BRKDT, 
    SCIFLR_OE, 
    SCIFLR_RX_RDY, 
    SCIFLR_TX_RDY
 };

typedef struct SCIState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t scigcr0; /* SCIGCR0 */
    uint32_t scigcr1; /* SCIGCR1 */
    uint32_t intreg; /* SCISETINT SCICLEARINT */

    uint32_t intlevel; /* SCISETINTLVL SCICLEARINTLVL */

    uint32_t flag; /* SCIFLR */

    uint32_t vecoffset[2]; /* SCIINTVECT0~1 RO*/

    uint32_t format; /* SCIFORMAT */

    uint32_t brs; /* BRS */

    uint32_t buff; /* SCIED, SCIRD, SCITD */

    uint32_t pio_control[9]; /* SCIPIO 0~8 */

    uint32_t ioerr; /* IODFTCTRL */

    CharDriverState *chr;

    qemu_irq irq_level0;
    qemu_irq irq_level1;
} SCIState;

static void sci_update(SCIState *s)
{
    // enabled and pending interrupt for int0 or int1 line
    uint32_t en_pend_int0, en_pend_int1; 
    int i;

    en_pend_int0 = s->flag & s->intreg & INT_MASK & ~s->intlevel;
    en_pend_int1 = s->flag & s->intreg & INT_MASK & s->intlevel;

    
    //update SCIINTVECT1
    if (en_pend_int1 == 0)
    {
        qemu_set_irq(s->irq_level1, en_pend_int1);
    } else
    {
        for(i = 0; i < 7; i++)
        {
            if (en_pend_int1 & sci_int_mask[i]) { //smaller index has higer priority
                s->vecoffset[1] = sci_vect_offset[i];
                qemu_set_irq(s->irq_level1, en_pend_int1);
                break;
            }
        }
    }

    //update SCIINTVECT0
    if (en_pend_int0 == 0)
    {
        qemu_set_irq(s->irq_level0, en_pend_int0);
    } else
    {
        for(i = 0; i < 7; i++)
        {
            if (en_pend_int0 & sci_int_mask[i]) { //smaller index has higer priority
                s->vecoffset[0] = sci_vect_offset[i];
                qemu_set_irq(s->irq_level0, en_pend_int0);
                break;
            }
        }
    }
    
}

static uint64_t sci_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    SCIState *s = (SCIState *)opaque;

    if (offset >= 0x3c && offset < 0x90) { //SCIPIO0~8
        return s->pio_control[(offset - 0x3c) >> 2];
    }
    if (offset >= 0x20 && offset < 0x28) { //SCIINTVECT0~1
        int index = (offset - 0x20) >> 2;
        switch (s->vecoffset[index]) {
            case 1: //Wakeup
                s->flag &= ~(s->intreg & s->flag & SCIFLR_WAKEUP);
            case 3: //PE
                s->flag &= ~(s->intreg & s->flag & SCIFLR_PE);
            case 6: //FE
                s->flag &= ~(s->intreg & s->flag & SCIFLR_FE);
            case 7: //BRKDT
                s->flag &= ~(s->intreg & s->flag & SCIFLR_BRKDT);
            case 9: //OE
                s->flag &= ~(s->intreg & s->flag & SCIFLR_OE);
            case 11: //Receive
            case 12: //Transmit
            default: //Reserved
                sci_update(s);
                return s->vecoffset[index];
        }
    }
    
    switch (offset)
    {
        case 0x00:
            return s->scigcr0;
        case 0x04:
            return s->scigcr1;
        case 0x0C:
        case 0x10:
            return s->intreg;
        case 0x14:
        case 0x18:
            return s->intlevel;
        case 0x1C:
            return s->flag;
        case 0x28:
            return s->format;
        case 0x2c:
            return s->brs;
        case 0x34: //SCIRD RO
            s->flag &= ~(s->flag & SCIFLR_RX_RDY);
            sci_update(s);
            if (s->chr && (s->scigcr1 & 0x80)) {
                qemu_chr_accept_input(s->chr);
            }
        case 0x30: //SCIED RO
        case 0x38: //SCITD RW
            return s->buff;
        case 0x90:
            return s->ioerr;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                      "tms570_sci_read: Bad offset %x\n", (int)offset);
            return 0;
    }
}

static int sci_transmit(void *opaque)
{
    SCIState *s = (SCIState *)opaque;

    if (s->scigcr1 & TXENA) { //TXENA
        if ((s->flag & SCIFLR_TX_RDY) == 0) { // TX_RDY not pending
            s->flag |= SCIFLR_TX_RDY;
        }
    }
    s->flag &= ~SCIFLR_TX_EMPTY;
    
    return (s->flag & SCIFLR_TX_RDY);
}

static void sci_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    SCIState *s = (SCIState *)opaque;
    unsigned char ch;

    /* The regs are writable only after the RESET bit is 1 */
    if (offset == 0x00) { //privileged mode only
        s->scigcr0 = value;
        return;
    }
    
    if ((s->scigcr0 & 0x01) != 0x01) {
        return;
    } 

    if (offset >= 0x3c && offset < 0x90) { //SCIPIO0~8
        s->pio_control[(offset - 0x3c) >> 2] = value;
        sci_update(s);
        return;
    }
    if (offset >= 0x20 && offset < 0x28) { //SCIINTVECT0~1
        //Read only
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tms570_sci_read: readonly offset %x\n", (int)offset);
        return;
    }
    
    switch (offset)
    {
        case 0x04:
            if ((value & 0x80) == 0x80) {   //set SWnRESET = 1 ready
                s->scigcr1 |= 0x00000080;
            } else
            {   // SWnRESET = 0 start configur
                s->scigcr1 = value;
                s->flag = 0x0;
                s->flag |= SCIFLR_TX_RDY | SCIFLR_TX_EMPTY;
            }
            break;
        case 0x0C:
            s->intreg |= value;
            break;
        case 0x10:
            s->intreg &= ~value;
            break;
        case 0x14:
            s->intlevel |= value;
            break;
        case 0x18:
            s->intlevel &= ~value;
            break;
        case 0x1C:
            s->flag = value & ~(s->intreg & s->flag);
            break;
        case 0x28:
            s->format = value;
            break;
        case 0x2c:
            s->brs = value;
            break;
        case 0x30: //SCIED RO
        case 0x34: //SCIRD RO
            break;
        case 0x38: //SCITD RW
            ch = value;
            if (s->chr && sci_transmit(s) && (s->scigcr1 & 0x80))
                qemu_chr_fe_write(s->chr, &ch, 1);
            s->flag |= SCIFLR_TX_EMPTY;
            break;
        case 0x90:
            s->ioerr = value;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                      "tms570_sci_read: Bad offset %x\n", (int)offset);
            break;
    }
    sci_update(s);
}

static int sci_can_receive(void *opaque)
{
    SCIState *s = (SCIState *)opaque;

    if (s->scigcr1 & RXENA) { //RXENA
        if ((s->flag & SCIFLR_RX_RDY) == 0) { // RX_RDY not pending
            s->flag |= SCIFLR_RX_RDY; 
        }
    }

    return (s->flag & SCIFLR_RX_RDY);
}

static void sci_receive(void *opaque, const uint8_t *buf, int size)
{
    SCIState *s = (SCIState *)opaque;

    s->buff = *buf;
    sci_update(s);
}

static void sci_event(void *opaque, int event)
{
    if (event == CHR_EVENT_BREAK) {
        /* TODO */
    }
}

static void sci_reset(DeviceState *dev)
{
    SCIState *s = SCI(dev);

    /* start reset */
    s->scigcr0 = 0x0;

    /* reset values */
    s->scigcr1 = 0x0;
    s->flag = 0x0;
    s->flag |= SCIFLR_TX_RDY | SCIFLR_TX_EMPTY;

    /* finish reset */
    s->scigcr0 = 0x1;
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
        VMSTATE_UINT32(scigcr0, SCIState),
        VMSTATE_UINT32(scigcr1, SCIState),
        VMSTATE_UINT32(intreg, SCIState),
        VMSTATE_UINT32(intlevel, SCIState),
        VMSTATE_UINT32(flag, SCIState),
        VMSTATE_UINT32(format, SCIState),
        VMSTATE_UINT32(brs, SCIState),
        VMSTATE_UINT32(buff, SCIState),
        VMSTATE_UINT32(ioerr, SCIState),
        VMSTATE_UINT32_ARRAY(vecoffset, SCIState, 2),
        VMSTATE_UINT32_ARRAY(pio_control, SCIState, 9),
        VMSTATE_END_OF_LIST()
    }
};

static void sci_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SCIState *s = SCI(obj);

    memory_region_init_io(&s->iomem, obj, &sci_ops, s, "tms570-sci", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq_level0);
    sysbus_init_irq(sbd, &s->irq_level1);
}

static void sci_realize(DeviceState *dev, Error **errp)
{
    SCIState *s = SCI(dev);

    if (s->chr) {
        qemu_chr_add_handlers(s->chr, sci_can_receive, sci_receive,
                              sci_event, s);
    }
}

static Property sci_properties[] = {
    DEFINE_PROP_CHR("chardev", SCIState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void sci_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = sci_realize;
    dc->vmsd = &vmstate_sci;
    dc->props = sci_properties;
    dc->reset = &sci_reset;
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
