/* 
 * tms570 n2het high-end timer
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

#define TYPE_N2HET "tms570-n2het"
#define N2HET(obj) OBJECT_CHECK(N2HETState, (obj), TYPE_N2HET)
#define HET_RAM1 0xff460000
#define HET_PARITY_RAM 0xff462000
#define HET_RAM2 0xff440000
#define HET_PARITY_RAM 0xff442000

typedef struct N2HETState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t gcr; /* HETGCR */
    uint32_t pfr; /* HETPFR */

    uint32_t ier; /*HETINTENAS & HETINTENAC*/
    uint32_t pry; /* HETPRY */
    uint32_t and; /* HETAND */
    uint32_t hrsh; /* HETHRSH */
    uint32_t xor; /* HETXOR */

    uint32_t dir; /* HETDIR */
    uint32_t pdr; /* HETPDR */

    uint32_t puldis; /* HETPULDIS */
    uint32_t psl; /* HETPSL */
    uint32_t pcr; /* HETPCR */

    /*----------------------------*/
    uint32_t addr; /* HETADDR */
    uint32_t off1; /* HETOFF1 */
    uint32_t off2; /* HETOFF2 */

    uint32_t ecr1; /* HETEXC1 */
    uint32_t ecr2; /* HETEXC2 */

    uint32_t flg; /* HETFLG */

    uint32_t rer; /* HETREQENS & HETREQENC*/
    uint32_t rdsr; /* HETREQDS */

    uint32_t ndior; /* HETDIN HETDOUT */
    uint32_t ndscr; /* HETDSET HETDCLR */

    uint32_t par; /* HETPAR not be reset*/
    uint32_t ppr; /* HETPPR */

    uint32_t sfpr; /* HETSFPRLD */
    uint32_t sfer; /* HETSFENA */
    
    uint32_t lbpsr; /* HETLBPSEL */
    uint32_t lbpdr; /* HETLBPDIR */

    uint32_t npdr; /* HETPINDIS */

    qemu_irq irq;
} N2HETState;

/* Merge the IRQs from the two component devices.  */
static void n2het_set_irq(void *opaque, int irq, int level)
{
    N2HETState *s = (N2HETState *)opaque;

}

static void n2het_update(N2HETState *s)
{
    
}

static uint64_t n2het_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    N2HETState *s = (N2HETState *)opaque;

    switch (offset)
    {
        case 0x00:
            return s->gcr;
        
        case 0x04:
            return s->pfr;

        case 0x08:
            return s->addr;

        case 0x0c:
            /* reading the corresponding HETOFF1/2 will automatically clear HETFLG flag */
            s->flg &= ~(0x1 << s->off1);
            return s->off1;

        case 0x10:
            /* reading the corresponding HETOFF1/2 will automatically clear HETFLG flag */
            s->flg &= ~(0x1 << s->off2);
            return s->off2;

        case 0x14:
        case 0x18:
            return s->ier;

        case 0x1c:
            return s->ecr1;

        case 0x20:
            return s->ecr2;

        case 0x24:
            return s->pry;

        case 0x28:
            return s->flg;

        case 0x2c：
            return s->and;

        case 0x34:
            return s->hrsh;

        case 0x38:
            return s->xor;

        case 0x3c:
        case 0x40:
            return s->rer;

        case 0x44:
            return s->rdsr;

        case 0x4c:
            return s->dir;

        case 0x50:
        case 0x54:
            return s->ndior;

        case 0x58:
        case 0x5c:
            /* read from this address return the value of the HETDOUT*/
            return s->ndior;

        case 0x60:
            return s->pdr;

        case 0x64:
            return s->puldis;

        case 0x68:
            return s->psl;
            
        case 0x74:
            return s->pcr;

        case 0x78:
            return s->par;

        case 0x7c:
            return s->ppr;

        case 0x80:
            return s->sfpr;

        case 0x84:
            return s->sfer;

        case 0x8c:
            return s->lbpsr;

        case 0x90:
            return s->lbpdr;

        case 0x94:
            return s->npdr;
    
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                      "n2het_read: Bad offset %x\n", (int)offset);
            return 0;
    }
}

static void n2het_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    N2HETState *s = (N2HETState *)opaque;

    n2het_update(s)
}

static const MemoryRegionOps n2het_ops = {
    .read = n2het_read,
    .write = n2het_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void n2het_init(Object *obj)
{
    N2HETState *s = N2HET(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &n2het_ops, s, "tms570-n2het", 0x100);
    sysbus_init_mmio(dev, &s->iomem);

    sysbus_init_irq(dev, &s->irq);
}

static void n2het_reset(DeviceState *d)
{
    N2HETState *s = N2HET(d);

}

static const VMStateDescription vmstate_n2het = {
    .name = "tms570-n2het",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void n2het_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    //k->realize = n2het_realize;
    k->reset = n2het_reset;
    k->vmsd = &vmstate_n2het;
}

static const TypeInfo n2het_info = {
    .name          = TYPE_N2HET,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(N2HETState),
    .instance_init = n2het_init,
    .class_init    = n2het_class_init,
};

static void n2het_register_types(void)
{
    type_register_static(&n2het_info);
}

type_init(n2het_register_types)