/*
 * TMS570 HDK Baseboard System emulation
 * 
 * Copyright (c) 2019 FormalTech.
 * Written by xiaohaibiao
 * 
 */

#ifndef HW_TMS570_SCI_H
#define HW_TMS570_SCI_H

static inline DeviceState *sci_create(hwaddr addr,
                                        qemu_irq irq0,
                                        qemu_irq irq1,
                                        CharDriverState *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "tms570-sci");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq0);
    sysbus_connect_irq(s, 1, irq1);

    return dev;
}

#endif /* HW_TMS570_SCI_H */
