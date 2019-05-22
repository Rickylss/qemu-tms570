/*
 * mpc5675 LIN Controller
 *
 *
 * Copyright (c) 2019 FormalTech
 * Written by xiaohaibiao
 * 
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "sysemu/char.h"
#include "qemu/log.h"

#define TYPE_LINFLEXD "LINFlexD"
#define LINFLEXD(obj) OBJECT_CHECK(LinState, (obj), TYPE_LINFLEXD)

/* three operating modes of LINFlexD controller */
typedef enum {
    SLEEP,
    INIT,
    NORMAL
} OPMODE;

typedef struct LinState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t lin_cr1;
    uint32_t lin_ier;
    uint32_t lin_sr;
    uint32_t lin_esr;
    uint32_t uart_cr;
    uint32_t uart_sr;
    uint32_t lin_tcsr;
    uint32_t lin_ocr;
    uint32_t lin_tocr;
    uint32_t lin_fbrr;
    uint32_t lin_ibrr;
    uint32_t lin_cfr;
    uint32_t lin_cr2;
    uint32_t b_idr;
    uint32_t b_drl;
    uint32_t b_drm;
    uint32_t if_er;
    uint32_t if_mi;
    uint32_t if_mr;
    uint32_t if_cr[16];
    uint32_t gcr;
    uint32_t uart_pto;
    uint32_t uart_cto;  // read-only
    uint32_t dma_txe;
    uint32_t dma_rxe;

    OPMODE operation_mode;
    CharDriverState *chr;

    qemu_irq irq[3];
} LinState;

static void pl011_update(PL011State *s)
{
    uint32_t flags;

    flags = s->int_level & s->int_enabled;
    qemu_set_irq(s->irq, flags != 0);
}

static uint64_t LINFlexD_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    LinState *s = (LinState *)opaque;

    if (offset >= 0x4c && offset < 0x8c) {
        int index = (offset - 0x4c) >> 2;
        return s->if_cr[index];
    }

    switch (offset) {
    case 0x00: /* LINCR1 */
        return s->lin_cr1;
    case 0x04: /* LINIER */
        return s->lin_ier;
    case 0x08: /* LINSR */
        return s->lin_sr;
    case 0x0c: /* LINESR */
        return s->lin_esr;
    case 0x10: /* UARTCR */
        return s->uart_cr;
    case 0x14: /* UARTSR */
        return s->uart_sr;
    case 0x18: /* LINTCSR */
        return s->lin_tcsr;
    case 0x1c: /* LINOCR */
        return s->lin_ocr;
    case 0x20: /* LINTOCR */
        return s->lin_tocr;
    case 0x24: /* LINFBRR */
        return s->lin_fbrr;
    case 0x28: /* LINIBRR */
        return s->lin_ibrr;
    case 0x2c: /* LINCFR */
        return s->lin_cfr;
    case 0x30: /* LINCR2 */
        return s->int_level;
    case 0x34: /* BIDR */
        return s->int_level & s->int_enabled;
    case 0x38: /* BDRL */
        return s->dmacr;
    case 0x3c: /* BDRM */
        return s->dmacr;
    case 0x40: /* IFER */
        return s->dmacr;
    case 0x44: /* IFMI */
        return s->dmacr;
    case 0x48: /* IFMR */
        return s->dmacr;
    case 0x8c: /* GCR */
        return s->dmacr;
    case 0x90: /* UARTPTO */
        return s->dmacr;
    case 0x94: /* UARTCTO */
        return s->dmacr;
    case 0x98: /* DMATXE */
        return s->dmacr;
    case 0x9c: /* DMARXE */
        return s->dmacr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "LINFlexD_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void LINFlexD_switch_operating_mode(LinState *s)
{
    if ((s->lin_cr1 & 0x3) == 0x2) {
        s->operation_mode = SLEEP;
    } else if((s->lin_cr1 & 0x3) == 0x0) {
        s->operation_mode = NORMAL;
    } else {
        s->operation_mode = INIT;
    }
}

static void LINFlexD_write(void *opaque, hwaddr offset,
                        uint64_t val, unsigned size)
{
    LinState *s = (LinState *)opaque;

    if (offset >= 0x4c && offset < 0x8c) {
        int index = (offset - 0x4c) >> 2;
        return s->if_cr[index];
    }

    switch (offset) {
    case 0x00: /* LINCR1 */
        if (s->operation_mode == INIT) {
            s->lin_cr1 = val & 0xffff;
        } else {
            s->lin_cr1 = val & 0x3;
        }
        break;
    case 0x04: /* LINIER */
        s->lin_ier &= ~(val & 0xf9ff);
        break;
    case 0x08: /* LINSR */
        s->lin_sr &= ~(val & 0xf2ff);
        /* 
         * The sleep mode is exited automatically by hardware on receive 
         * dominant state detection. The LINCR[SLEEP] bit is cleared by 
         * hardware whenever LINSR[WUF] = 1 
         */
        if ((s->lin_sr & 0x20) && (s->lin_cr1 & 0x1000))
        {
            s->lin_cr1 &= ~0x2;
            LINFlexD_switch_operating_mode(s);
        }
        break;
    case 0x0c: /* LINESR */
        s->lin_sr &= ~(val & 0xff81);
        break;
    case 0x10: /* UARTCR */
        s->uart_cr = val & 0xb3;
        if (s->operation_mode == INIT) {
            s->uart_cr |= val & 0x34c;
        }
        if (s->uart_cr & 0x1) {
            s->uart_cr |= val & 0xfc00;
        }
        break;
    case 0x14: /* UARTSR */
        s->uart_sr &= ~(val & 0xffef);
        break;
    case 0x18: /* LINTCSR */
        if (s->operation_mode == INIT) {
            s->lin_tcsr &= ~(val & 0x700);
        }
        break;
    case 0x1c: /* LINOCR */
        /* If LINTCSR[LTOM] = 1, these fields are read-only */
        if ((s->lin_tcsr & 0x400) == 0) {
            s->lin_ocr &= ~(val & 0xffff);
        }
        break;
    case 0x20: /* LINTOCR */
        s->lin_tocr = val & 0xf00;
        if (s->lin_cr1 & 0x10)
        {
           s->lin_tocr |= val & 0x7f;
        }
        break;
    case 0x24: /* LINFBRR */
        if (s->operation_mode == INIT) {
            s->lin_fbrr = val & 0xf;
        }
        break;
    case 0x28: /* LINIBRR */
        if (s->operation_mode == INIT) {
            s->lin_ibrr = val & 0xfffff;
        }
        break;
    case 0x2c: /* LINCFR */
        s->lin_cfr = val & 0xff;
        break;
    case 0x30: /* LINCR2 */
        s->lin_cr2 &= ~(val & 0x1f00);
        if (s->operation_mode == INIT)
        {
            s->lin_cr2 |= val & 0x6000;
        }
        break;
    case 0x34: /* BIDR */
        return s->int_level & s->int_enabled;
    case 0x38: /* BDRL */
        return s->dmacr;
    case 0x3c: /* BDRM */
        return s->dmacr;
    case 0x40: /* IFER */
        return s->dmacr;
    case 0x44: /* IFMI */
        return s->dmacr;
    case 0x48: /* IFMR */
        return s->dmacr;
    case 0x8c: /* GCR */
        return s->dmacr;
    case 0x90: /* UARTPTO */
        return s->dmacr;
    case 0x94: /* UARTCTO read-only*/
        break;
    case 0x98: /* DMATXE */
        return s->dmacr;
    case 0x9c: /* DMARXE */
        return s->dmacr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "LINFlexD_read: Bad offset %x\n", (int)offset);
        break;
    }
}

static int pl011_can_receive(void *opaque)
{
    PL011State *s = (PL011State *)opaque;

    if (s->lcr & 0x10)
        return s->read_count < 16;
    else
        return s->read_count < 1;
}

static void pl011_put_fifo(void *opaque, uint32_t value)
{
    PL011State *s = (PL011State *)opaque;
    int slot;

    slot = s->read_pos + s->read_count;
    if (slot >= 16)
        slot -= 16;
    s->read_fifo[slot] = value;
    s->read_count++;
    s->flags &= ~PL011_FLAG_RXFE;
    if (!(s->lcr & 0x10) || s->read_count == 16) {
        s->flags |= PL011_FLAG_RXFF;
    }
    if (s->read_count == s->read_trigger) {
        s->int_level |= PL011_INT_RX;
        pl011_update(s);
    }
}

static void pl011_receive(void *opaque, const uint8_t *buf, int size)
{
    pl011_put_fifo(opaque, *buf);
}

static void pl011_event(void *opaque, int event)
{
    if (event == CHR_EVENT_BREAK)
        pl011_put_fifo(opaque, 0x400);
}

static const MemoryRegionOps LINFlexD_ops = {
    .read = LINFlexD_read,
    .write = LINFlexD_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_LINFlexD = {
    .name = "LINFlexD",
    .version_id = 1,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {

        VMSTATE_END_OF_LIST()
    }
};

static void LINFlexD_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    LinState *s = LINFLEXD(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &LINFlexD_ops, s, "LINFlexD", 0x4000);
    sysbus_init_mmio(sbd, &s->iomem);
    for (int i = 0; i < 4; i++)
    {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
}

static void LINFlexD_realize(DeviceState *dev, Error **errp)
{
    LinState *s = LINFLEXD(dev);

    if (s->chr) {
        qemu_chr_add_handlers(s->chr, pl011_can_receive, pl011_receive,
                              pl011_event, s);
    }
}

static void LINFlexD_reset(DeviceState *d)
{
    LinState *s = LINFLEXD(d);

    /* Resets to 0 in slave mode and to 1 in master mode */
    if (s->lin_cr1 & 0x10) {    // master mode
        s->lin_cr1 = 0x92;
        s->lin_tocr = 0xe1c;
        s->lin_cr2 = 0x4000;
    } else {                    // slave mode
        s->lin_cr1 = 0x82;
        s->lin_tocr = 0xe2c;
        s->lin_cr2 = 0x6000;
    }

    s->lin_sr = 0x40;
    s->lin_tcsr = 0x200;
    s->lin_ocr = 0xffff;

    
    
    LINFlexD_switch_operating_mode(s);
}

static void LINFlexD_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = LINFlexD_reset;
    dc->realize = LINFlexD_realize;
    dc->vmsd = &vmstate_LINFlexD;
}

static const TypeInfo LINFlexD_info = {
    .name          = TYPE_LINFLEXD,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LinState),
    .instance_init = LINFlexD_init,
    .class_init    = LINFlexD_class_init,
};

static void LINFlexD_register_types(void)
{
    type_register_static(&LINFlexD_info);
}

type_init(LINFlexD_register_types)
