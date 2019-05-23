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
#include "hw/itimer.h"

#define TYPE_LINFLEXD "LINFlexD"
#define LINFLEXD(obj) OBJECT_CHECK(LinState, (obj), TYPE_LINFLEXD)

#define TDFLTFC 0x7 << 13
#define RDFLRFC 0x7 << 10
#define RFBM 1 << 9
#define TFBM 1 << 8
#define RXEN 1 << 5
#define TXEN 1 << 4
#define WL1 1 << 7

#define DRFRFE 1 << 2
#define DTFTFF 1 << 1

/* three operating modes of LINFlexD controller */
typedef enum {
    SLEEP,
    INIT,
    NORMAL
} OPMODE;

typedef struct LinState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    // uart timeout counter
    itimer_state timer;
    // the ipg clock MHz
    uint32_t ipg_clock_lin;

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
    uint32_t lin_fbrr_programed;
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

    uint32_t data_count;
    uint32_t receive_byte_nu;

    OPMODE operation_mode;
    CharDriverState *chr;

    qemu_irq irq[3];
} LinState;

static void LINFlexD_update_irq(LinState *s)
{
    /*
     * An interrupt is generated if this bit is set and one of the following is true:
     * 1. LINFlexD is in LIN mode and LINSR[DBEF] is set.
     * 2. LINFlexD is in UART mode and UARTSR[TO] is set.
     */
    qemu_irq_lower(s->irq[2]);
    if (s->lin_ier & 0x8) {
        if (((s->lin_sr & 0x8) && ~(s->uart_sr & 0x1)) || // buffer empty interrupt
            ((s->uart_sr & 0x8) && (s->uart_sr & 0x1))) { // timeout interrupt
            qemu_irq_raise(s->irq[2]);
        }
    }



}

static uint64_t LINFlexD_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    LinState *s = (LinState *)opaque;

    /* IFCR0~15 */
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
        return s->b_idr;
    case 0x38: /* BDRL */
        return s->b_drl;
    case 0x3c: /* BDRM */
        return s->b_drm;
    case 0x40: /* IFER */
        return s->if_er;
    case 0x44: /* IFMI */
        return s->if_mi;
    case 0x48: /* IFMR */
        return s->if_mr;
    case 0x8c: /* GCR */
        return s->gcr & 0x3e;
    case 0x90: /* UARTPTO */
        return s->uart_pto;
    case 0x94: /* UARTCTO */
        s->uart_cto = itimer_get_count(s->timer);
        return s->uart_cto;
    case 0x98: /* DMATXE */
        return s->dma_txe;
    case 0x9c: /* DMARXE */
        return s->dma_txe;
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

static void update_baudrate(LinState *s)
{
    double lfdiv;
    int baudrate
    
    lfdiv = (s->lin_ibrr & 0xfffff) + (s->lin_fbrr & 0xf) / 16;

    if (lfdiv >= 1.5) {
        baudrate = (s->ipg_clock_lin * 1000 * 1000) / (16 * lfdiv);
    } else {
        fprintf(stderr, "LFDIV must be greater than or equal to 1.5d");
    }
    
    /* 
     * The timeout counter is clocked with the baud rate  
     * clock prescaled by a hard-wired scaling factor of 16
     */
    itimer_set_freq(s->tiemr, baudrate/16);
}

static void LINFlexD_write(void *opaque, hwaddr offset,
                        uint64_t val, unsigned size)
{
    LinState *s = (LinState *)opaque;

    /* IFCR0~15 */
    if (offset >= 0x4c && offset < 0x8c) {
        int index = (offset - 0x4c) >> 2;
        if (s->operation_mode == INIT) {
            s->if_cr[index] = val & 0xff3f;
        }
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
        s->uart_cr = val & 0x30;
        if (s->operation_mode == INIT) {
            s->uart_cr |= val & 0x1;
            if (s->uart_cr & 0x1) {
                s->uart_cr |= val & 0xffce;
            }
        }

        if (s->uart_cr & RXEN) {
            itimer_run(s->timer, 1);
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
            s->lin_fbrr_programed = 1;
        }
        break;
    case 0x28: /* LINIBRR */
        if ((s->operation_mode == INIT) && s->lin_fbrr_programed) {
            s->lin_ibrr = val & 0xfffff;
            s->lin_fbrr_programed = 0;
            update_baudrate(s);
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
        s->b_idr &= ~(val & 0x300);
        s->b_idr |= val & 0xfc3f;
        break;
    case 0x38: /* BDRL */
        if (s->uart_cr & 0x1) {
            uint32_t mask;
            mask = (s->uart_cr & TDFLTFC) >> 13;
            if (s->uart_cr & RFBM && mask <= 4) {
                s->b_drl = val & ~(0xffffffff << mask * 8);
            } else if(mask <= 3) {
                s->b_drl = val & ~(0xffffffff << (mask+1) * 8); 
                if ((s-uart_cr & WL1) && (mask == 1 || mask == 3)) {
                    fprintf(stderr, "WL is configured as halfword, value invalid");
                }
            }
        break;
    case 0x3c: /* BDRM */
        if (s->uart_cr & 0x1) {
            if (s->uart_cr & RFBM) {
                s->receive_byte_nu = (s->uart_cr & RDFLRFC) >> 10; 
            } else {
                s->receive_byte_nu = ((s->uart_cr & 0xc00) >> 10) + 1; 
                if ((s-uart_cr & WL1) && (mask == 1 || mask == 3)) {
                    fprintf(stderr, "WL is configured as halfword, value invalid");
                }
            }
        }
        break;
    case 0x40: /* IFER */
        if (s->operation_mode == INIT) {
            s->if_er = val & 0xff;
        }
        break;
    case 0x44: /* IFMI user read-only*/
        break;
    case 0x48: /* IFMR */
        if (s->operation_mode == INIT) {
            s->if_mr = val & 0xf;
        }
        break;
    case 0x8c: /* GCR */
        if (s->operation_mode == INIT)
        {
            s->gcr = val & 0x3f;
        }
        break;
    case 0x90: /* UARTPTO */
        s->uart_pto = val & 0xfff;
        itimer_set_compare(s->timer, s->uart_pto);
        break;
    case 0x94: /* UARTCTO read-only*/
        break;
    case 0x98: /* DMATXE */
        s->dma_txe = val & 0xffff;
        break;
    case 0x9c: /* DMARXE */
        s->dma_rxe = val & 0xffff;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "LINFlexD_read: Bad offset %x\n", (int)offset);
        break;
    }
}

static int LINFlexD_can_receive(void *opaque)
{
    LinState *s = (LinState *)opaque;

    /* 
     * Reception of a data byte is started as soon as the software 
     * completes the following tasks in order:
     * 1. Exits initialization mode.
     * 2. Sets the UARTCR[RXEN] field.
     * 3. Detects the start bit of the data frame
     */
    if ((s->uart_cr & RXEN) && (s->operation_mode == NORMAL))
    {
        return s->read_count < s->receive_byte_nu;
    }
}

static void LINFlexD_put_fifo(void *opaque, uint32_t value)
{

    SCIState *s = (SCIState *)opaque;

    s->buff = *buf;
    sci_update(s);
    s->flag &= ~SCIFLR_RX_RDY;
    if (s->buff) {
        s->flag |= SCIFLR_RX_RDY; 
    }

    LinState *s = (LinState *)opaque;
    int slot;

    slot = s->read_pos + s->read_count;
    if (slot >= 16)
        slot -= 16;
    s->read_fifo[slot] = value;
    s->read_count++;
    s->uart_sr &= ~DRFRFE;
    if (!(s->lcr & 0x10) || s->read_count == 16) {
        s->flags |= PL011_FLAG_RXFF;
    }
    if (s->read_count == s->read_trigger) {
        s->int_level |= PL011_INT_RX;
        pl011_update(s);
    }

    if (s->uart_cr & RXEN) {
        itimer_run(s->timer, 1);
    }
}

static void LINFlexD_receive(void *opaque, const uint8_t *buf, int size)
{   
    LINFlexD_put_fifo(opaque, *buf);
    itimer_set_count(s->timer, 0);
}

/* TODO */
static void LINFlexD_event(void *opaque, int event)
{
    if (event == CHR_EVENT_BREAK)
        LINFlexD_put_fifo(opaque, 0x400);
}

static void uart_timeout_tick(void *opaque) 
{
    LinState *s = LINFLEXD(opaque);

    /* set timeout flag in UARTSR[TO] */
    s->uart_sr |= 0x8;
    LINFlexD_update_irq(s); //timeout
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
        VMSTATE_UINT32(lin_cr1, LinState),
        VMSTATE_UINT32(lin_ier, LinState),
        VMSTATE_UINT32(lin_sr, LinState),
        VMSTATE_UINT32(lin_esr, LinState),
        VMSTATE_UINT32(uart_cr, LinState),
        VMSTATE_UINT32(uart_sr, LinState),
        VMSTATE_UINT32(lin_tcsr, LinState),
        VMSTATE_UINT32(lin_ocr, LinState),
        VMSTATE_UINT32(lin_tocr, LinState),
        VMSTATE_UINT32(lin_fbrr, LinState),
        VMSTATE_UINT32(lin_fbrr_programed, LinState),
        VMSTATE_UINT32(lin_ibrr, LinState),
        VMSTATE_UINT32(lin_cfr, LinState),
        VMSTATE_UINT32(lin_cr2, LinState),
        VMSTATE_UINT32(b_idr, LinState),
        VMSTATE_UINT32(b_drl, LinState),
        VMSTATE_UINT32(b_drm, LinState),
        VMSTATE_UINT32(if_er, LinState),
        VMSTATE_UINT32(if_mi, LinState),
        VMSTATE_UINT32(if_mr, LinState),
        VMSTATE_UINT32(gcr, LinState),
        VMSTATE_UINT32(uart_pto, LinState),
        VMSTATE_UINT32(uart_cto, LinState),
        VMSTATE_UINT32(dma_txe, LinState),
        VMSTATE_UINT32(dma_rxe, LinState),
        VMSTATE_UINT32_ARRAY(if_cr, LinState,16),
        VMSTATE_END_OF_LIST()
    }
};

static Property LINFlexD_properties[] = {
    DEFINE_PROP_UINT32("ipg_clock_lin", LinState, ipg_clock_lin, 16),
    DEFINE_PROP_END_OF_LIST(),
};

static void LINFlexD_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    LinState *s = LINFLEXD(obj);
    QEMUBH *bh;

    memory_region_init_io(&s->iomem, OBJECT(s), &LINFlexD_ops, s, "LINFlexD", 0x4000);
    sysbus_init_mmio(sbd, &s->iomem);
    for (int i = 0; i < 4; i++)
    {
        sysbus_init_irq(sbd, &s->irq[i]);
    }

    bh = qemu_bh_new(uart_timeout_tick, s);
    s->timer = itimer_init(bh);
}

static void LINFlexD_realize(DeviceState *dev, Error **errp)
{
    LinState *s = LINFLEXD(dev);

    if (s->chr) {
        qemu_chr_add_handlers(s->chr, LINFlexD_can_receive, LINFlexD_receive,
                              LINFlexD_event, s);
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
    s->uart_pto = 0xfff;

    itimer_set_count(s->timer, 0);
    itimer_set_compare(s->timer, s->uart_pto);
    
    LINFlexD_switch_operating_mode(s);
}

static void LINFlexD_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = LINFlexD_reset;
    dc->realize = LINFlexD_realize;
    dc->vmsd = &vmstate_LINFlexD;
    dc->props = LINFlexD_properties;
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
