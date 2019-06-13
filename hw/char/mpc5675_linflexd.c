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

#define UART_RXI_MASK 0x4
#define UART_TXI_MASK 0x2
#define UART_ERR_MASK 0xc1a8

#define TDFLTFC (0x7 << 13)
#define RDFLRFC (0x7 << 10)
#define RFBM (1 << 9)
#define TFBM (1 << 8)
#define RXEN (1 << 5)
#define TXEN (1 << 4)
#define WL1 (1 << 7)

#define BOF (1 << 7)
#define RMB (1 << 9)

#define DRFRFE (1 << 2)
#define DTFTFF (1 << 1)

#define TDFBM (1 << 5)
#define RDFBM (1 << 4)
#define TDLIS (1 << 3)
#define RDLIS (1 << 2)

/* three operating modes of LINFlexD controller */
typedef enum {
    SLEEP,
    INIT,
    NORMAL
} OPMODE;

typedef enum {
    LIN,
    UART
} MODE;

typedef struct LinState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    // uart timeout counter
    itimer_state *timer;
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
    union {
        uint32_t r;
        struct {
            uint8_t bdr[4];
        } b;
        struct {
            uint16_t bdr[2];
        } w;
    } b_drl;
    uint8_t tx_count;
    union {
        uint32_t r;
        struct {
            uint8_t bdr[4];
        } b;
        struct {
            uint16_t bdr[2];
        } w;
    } b_drm;
    uint8_t rx_count;
    uint32_t if_er;
    uint32_t if_mi;
    uint32_t if_mr;
    uint32_t if_cr[16];
    uint32_t gcr;
    uint32_t uart_pto;
    uint32_t uart_cto;  // read-only
    uint32_t dma_txe;
    uint32_t dma_rxe;

    uint8_t read_fifo[4];
    uint8_t read_pos;
    uint8_t read_count;

    OPMODE operation_mode;
    MODE mode;
    CharDriverState *chr;

    qemu_irq irq[3];
} LinState;

static void LINFlexD_update_irq(LinState *s)
{
    if (s->mode == UART)
    {
        uint32_t rxi_irq = UART_RXI_MASK & s->lin_ier & s->uart_sr;
        uint32_t txi_irq = UART_TXI_MASK & s->lin_ier & s->uart_sr;
        uint32_t err_irq = UART_ERR_MASK & s->lin_ier & s->uart_sr;

        //RXI
        qemu_set_irq(s->irq[0], rxi_irq);

        //TXI
        qemu_set_irq(s->irq[1], txi_irq);

        //ERR
        qemu_set_irq(s->irq[2], err_irq);

    }

    if (s->uart_sr & 0x8)
    {
        s->uart_sr &= ~0x8;
    }
}

static void update_baudrate(LinState *s)
{
    double lfdiv;
    int baudrate;
    
    lfdiv = (s->lin_ibrr & 0xfffff) + (s->lin_fbrr & 0xf) / 16.0;

    if (lfdiv >= 1.5) {
        baudrate = (s->ipg_clock_lin * 1000 * 1000) / (16 * lfdiv);
    } else {
        fprintf(stderr, "LFDIV must be greater than or equal to 1.5d");
    }
    
    /* 
     * The timeout counter is clocked with the baud rate  
     * clock prescaled by a hard-wired scaling factor of 16
     */
    itimer_set_freq(s->timer, baudrate/16);
}

static void get_txrx_count(LinState *s)
{
    if (s->uart_cr & RFBM) {                    //FIFO mode
        s->rx_count = (s->uart_cr & RDFLRFC) >> 10;
        if (s->rx_count > 4) {
            fprintf(stderr, "the receive FIFO counter is more than 4 bytes");
        }
    } else {                                    //buffer mode
        s->rx_count = ((s->uart_cr & 0xc00) >> 10) + 1; 
        if ((s->uart_cr & WL1) && (s->rx_count == 1 || s->rx_count == 3)) {
            fprintf(stderr, "WL is configured as halfword, value invalid");
        }
    }

    if (s->uart_cr & TFBM) {                    //FIFO mode
        s->tx_count = (s->uart_cr & TDFLTFC) >> 13; 
        if (s->tx_count > 4) {
            fprintf(stderr, "the transmit FIFO counter is more than 4 bytes");
        }
    } else {                                    //buffer mode
        s->tx_count = ((s->uart_cr & 0x6000) >> 13) + 1; 
        if ((s->uart_cr & WL1) && (s->tx_count == 1 || s->tx_count == 3)) {
            fprintf(stderr, "WL is configured as halfword, value invalid");
        }
    }
}

static void LINFlexD_switch_operating_mode(LinState *s)
{
    if (s->lin_cr1 & 0x1) {
        s->operation_mode = INIT;
        s->uart_sr &= ~RMB;
        s->lin_sr = (s->lin_sr & 0xffff0fff) | 0x1000; //LIN state Initialization mode
    } else if (s->lin_cr1 & 0x2) {
        s->operation_mode = SLEEP;
        s->lin_sr = s->lin_sr & 0xffff0fff; //LIN state Sleep mode
        itimer_stop(s->timer);
    } else {
        s->operation_mode = NORMAL;
        if (s->uart_cr & RXEN) {
            itimer_run(s->timer, 1);
        }
    }
}

static void inversion_8_bit(uint8_t *c) {
    *c = ( *c & 0x55 ) << 1 | ( *c & 0xAA ) >> 1;
    *c = ( *c & 0x33 ) << 2 | ( *c & 0xCC ) >> 2;
    *c = ( *c & 0x0F ) << 4 | ( *c & 0xF0 ) >> 4;
}

static void inversion_32_bit(uint32_t *c) {

}

static void modify_data(LinState *s)
{
    if (s->gcr & TDFBM) { // Transmit data first bit MSB
        for (size_t i = 0; i < 4; i++) {
            inversion_8_bit(&s->b_drl.b.bdr[i]);
        }
    }

    if (s->gcr & RDFBM) { // Received data first bit MSB
        for (size_t i = 0; i < 4; i++) {
            inversion_8_bit(&s->b_drm.b.bdr[i]);
        }
    }

    if (s->gcr & TDLIS) { // Transmit data level inversion selection
        inversion_32_bit(&s->b_drl.r);
    }  
    
    if (s->gcr & RDLIS) { // Received data level inversion selection
        inversion_32_bit(&s->b_drm.r);
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
        return s->lin_cr2;
    case 0x34: /* BIDR */
        return s->b_idr;
    case 0x38: /* BDRL */
        return s->b_drl.r;
    case 0x3c: /* BDRM */
        if (s->mode == UART && s->uart_sr & RMB) {
            memcpy(&s->b_drm.r, s->read_fifo, sizeof(s->read_fifo));
            modify_data(s);
            s->read_count -= s->rx_count;
            if (s->chr) {
                qemu_chr_accept_input(s->chr);
            }
        }
        return s->b_drm.r;
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

    /* BDRL uart transmitter*/
    if (offset >= 0x38 && offset < 0x3c) {
        int index = (offset - 0x38);

        if (s->mode == UART) {                                  //UART mode
            if (s->uart_cr & TXEN) {                            //open tx
                if (s->uart_cr & TFBM && index == 0) {          //FIFO mode
                    if (s->uart_cr & WL1) {                     //Halfword
                        s->b_drl.w.bdr[0] = val & 0xffff;
                        s->tx_count = 0;
                        qemu_chr_fe_write(s->chr, s->b_drl.b.bdr, 2);
                    } else {                                    //Byte
                        s->b_drl.b.bdr[0] = val & 0xff;
                        s->tx_count = 0;
                        qemu_chr_fe_write(s->chr, s->b_drl.b.bdr, 1);
                    }
                    if (s->tx_count == 0) { //FIFO is full;
                        s->uart_sr |= DTFTFF;
                    }
                } else if(s->uart_cr & TFBM && index != 0) {    // IPS transfer error
                    fprintf(stderr, "IPS transfer error");
                } else {                                        //buffer mode
                    s->b_drl.r = val;
                    qemu_chr_fe_write(s->chr, s->b_drl.b.bdr, s->tx_count);
                    s->uart_sr |= DTFTFF;
                    LINFlexD_update_irq(s);
                }
            }
        }
    }

    /* BDRM */
    if (offset >= 0x3c && offset < 0x40) {
        if (s->mode == UART)
        {
            fprintf(stderr, "IPS transfer error");
        }
    }
    
    switch (offset) {
    case 0x00: /* LINCR1 */
        s->lin_cr1 = val & 0x3;
        LINFlexD_switch_operating_mode(s);

        if (s->operation_mode == INIT) {
            s->lin_cr1 = val & 0xffff;
        }

        break;
    case 0x04: /* LINIER */
        s->lin_ier |= val & 0xf9ff;
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
                s->mode = UART;
                get_txrx_count(s);
            } else {
                s->mode = LIN;
            } 
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

        if (s->gcr & 0x1) { //soft reset
            s->b_drl.r = 0x0;
            s->b_drm.r = 0x0;
            s->uart_sr = 0x0;
            s->lin_sr = 0x40;
            s->lin_esr = 0x0;
            itimer_run(s->timer, 1);
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

    itimer_set_count(s->timer, 0);
    /* 
     * Reception of a data byte is started as soon as the software 
     * completes the following tasks in order:
     * 1. Exits initialization mode.
     * 2. Sets the UARTCR[RXEN] field.
     * 3. Detects the start bit of the data frame
     */
    if ((s->uart_cr & RXEN) && (s->operation_mode == NORMAL)) {
        return s->read_count < s->rx_count;
    } else {
        return 0;
    }
    
}

static void LINFlexD_put_fifo(void *opaque, uint8_t value)
{
    LinState *s = (LinState *)opaque;
    int slot;

    slot = s->read_pos + s->read_count;
    if (slot >= s->rx_count)
        slot -= s->rx_count;
    s->read_fifo[slot] = value;
    s->read_count++;
    if (!(s->uart_cr & RXEN) || (s->read_count == s->rx_count)) {
        s->uart_sr |= DRFRFE;
        s->uart_sr |= RMB;
        if (!(s->uart_cr & RFBM)) { // while in uart fifo mode no interrupt generated
            LINFlexD_update_irq(s);
        }
    }
}

static void LINFlexD_receive(void *opaque, const uint8_t *buf, int size)
{   
        
    LinState *s = (LinState *)opaque;
    /*
     * A new byte has been received, but the last received
     * frame has not been read from the buffer
     */
    if (s->uart_sr & RMB)
    {
        if (s->lin_cr1 & 0x4) { // Receive Buffer locked against overrun
            s->uart_sr |= BOF;
        } else {           // Receive Buffer not locked on overrun
            s->read_fifo[s->rx_count - 1] = *buf;
            s->uart_sr |= BOF;
        }
        LINFlexD_update_irq(s); // overrun interrupt
    } else {
        LINFlexD_put_fifo(opaque, *buf);
    }

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
        VMSTATE_UINT32(if_er, LinState),
        VMSTATE_UINT32(if_mi, LinState),
        VMSTATE_UINT32(if_mr, LinState),
        VMSTATE_UINT32(gcr, LinState),
        VMSTATE_UINT32(uart_pto, LinState),
        VMSTATE_UINT32(uart_cto, LinState),
        VMSTATE_UINT32(dma_txe, LinState),
        VMSTATE_UINT32(dma_rxe, LinState),
        VMSTATE_UINT32_ARRAY(if_cr, LinState, 16),
        VMSTATE_END_OF_LIST()
    }
};

static Property LINFlexD_properties[] = {
    DEFINE_PROP_UINT32("ipg_clock_lin", LinState, ipg_clock_lin, 16),
    DEFINE_PROP_CHR("chardev", LinState, chr),
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
                              NULL, s);
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
    s->mode = LIN;
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
