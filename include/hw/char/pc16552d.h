/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_PC16552D_H
#define HW_PC16552D_H

static inline DeviceState *pc16552d_create(hwaddr addr,
                                        qemu_irq irq,
                                        CharDriverState *chr0,CharDriverState* chr1)
{
    DeviceState *dev;
    SysBusDevice *s;
    dev = qdev_create(NULL, "pc16552d");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev0", chr0);
    qdev_prop_set_chr(dev,"chardev1",chr1);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);

    return dev;
}


#endif
