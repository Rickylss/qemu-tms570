/*
 * QEMU PPC5675 hardware System Emulator
 * 
 * Copyright (c) 2019 FormalTech.
 * Written by xiaohaibiao
 * 
 */

#ifndef HW_MPC5675_LIN_H
#define HW_MPC5675_LIN_H

static inline DeviceState *linflexd_create(hwaddr addr,
                                            qemu_irq irq0,
                                            qemu_irq irq1,
                                            qemu_irq irq2,
                                            CharDriverState *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "LINFlexD");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq0);
    sysbus_connect_irq(s, 1, irq1);
    sysbus_connect_irq(s, 2, irq2);

    return dev;
}

#endif /* HW_MPC5675_LIN_H */