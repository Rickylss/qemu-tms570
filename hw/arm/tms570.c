/* 
 * TMS570 HDK Baseboard System emulation
 * 
 * Copyright (c) 2019 FormalTech.
 * Written by xiaohaibiao
 * 
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "sysemu/block-backend.h"
#include "exec/address-spaces.h"
#include "hw/block/flash.h"
#include "qemu/error-report.h"
#include "hw/char/tms570_sci.h"

/* Board init */

static struct arm_boot_info tms570_binfo;

/* The following two lists must be consistent.  */
enum tms570_board_type {
    BOARD_LS2124,
    BOARD_LS2125,
    BOARD_LS2134,
    BOARD_LS2135,
    BOARD_LS3134,
    BOARD_LS3135,
    BOARD_LS3137,
};

static const int realview_board_id[] = {
    0x2124,
    0x2125,
    0x2134,
    0x2135,
    0x3134,
    0x3135,
    0x3137
};

static void tms570_init(MachineState *machine,
                        enum tms570_board_type board_type)
{
    ObjectClass *cpu_oc;
    Object *cpuobj;
    ARMCPU *cpu;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    qemu_irq pic[95];
    qemu_irq rti[2];
    DeviceState *dev, *rtidev, *vimdev;
    int n;


    switch (board_type) {
    case BOARD_LS3137 :
        // set flags;
        break;
    default :
        break;
    }

    cpu_oc = cpu_class_by_name(TYPE_ARM_CPU, machine->cpu_model);
    if (!cpu_oc) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    cpuobj = object_new(object_class_get_name(cpu_oc));

    /* ARM cortex-r4f do not support el3 */
    if (object_property_find(cpuobj, "has_el3", NULL)) {
        object_property_set_bool(cpuobj, false, "has_el3", &error_fatal);
    }

    object_property_set_bool(cpuobj, true, "realized", &error_fatal);

    cpu = ARM_CPU(cpuobj);

    memory_region_allocate_system_memory(flash, NULL, "tms570ls31x.flash",
                                         3*1024*1024);
    memory_region_allocate_system_memory(ram, NULL, "tms570ls31x.ram",
                                         machine->ram_size);

    /* ??? RAM should repeat to fill physical memory space.  */
    /* FLASH at address 0x00000000. */
    memory_region_add_subregion(sysmem, 0x00000000, flash);
    /* SDRAM at address 0x80000000.  */
    memory_region_add_subregion(sysmem, 0x80000000, ram);

    /* VIM at address 0xfffffe00 */
    vimdev = sysbus_create_varargs("tms570-vim", 0xfffffe00,
                                qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ),
                                qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ),
                                NULL);
    for (n = 0; n < 95; n++) { 
        pic[n] = qdev_get_gpio_in(vimdev, n);
    }
    /* channel 0 and 1 are reserved */
    //sysbus_create_simple("esm", 0x, pic[0]);
    //sysbus_create_simple("adc", 0x, pic[1]);

    /* N2HET at address 0xfff7b800 */
    // sysbus_create_varargs("tms570-n2het", 0xfff7b800, pic[10], pic[24], NULL);
    // sysbus_create_varargs("tms570-n2het", 0xfff7b900, pic[63], pic[73], NULL);

    /* RTI at address 0xfffffd00 
     * RTI compare interrupt 0~3  pic 2~5
     * RTI overflow interrupt 0~1 pic 6~7
     * RTI timebase interrupt pic 8
     */
    rtidev = sysbus_create_varargs("tms570-rti", 0xfffffd00,
                            pic[2], pic[3], pic[4], pic[5], pic[6], pic[7], pic[8], NULL);
    for (n = 0; n < 2; n++)
    {
        rti[n] = qdev_get_gpio_in(rtidev, n);
    }
    
    /* two special interrupts link to rti */
    sysbus_connect_irq(vimdev, 2, rti[0]);
    sysbus_connect_irq(vimdev, 3, rti[1]);
    
    /* SCI at address 0xfff7e500 */
    //sysbus_create_varargs("tms570-sci", 0xfff7e500, pic[64], pic[74], NULL);
    sci_create(0xfff7e500, pic[64], pic[74], serial_hds[0]);

    /* GPIO at address 0xfff7bc00 portA portB*/
    sysbus_create_varargs("pl061", 0xfff7bc00, pic[9], pic[23], NULL);

    /* Memory map for tms570ls3137:  */
    /* 0xfff7b800 HET1 */
    /* 0xfff7b900 HET2 */
    /* 0xfff7bc00 GIO */
    /* 0xfff7d400 i2c */
    /* 0xfff7e500 SCI */
    /* 0xfff7e400 SCI(LIN) */
    /* 0xfffff000 DMA */
    /* 0xffffe100 system register2 */
    /* 0xfffffd00 RTI */
    /* 0xfffffe00 VIM */
    /* 0xffffff00 system register1 */

    tms570_binfo.ram_size = machine->ram_size;
    tms570_binfo.kernel_filename = machine->kernel_filename;
    tms570_binfo.kernel_cmdline = machine->kernel_cmdline;
    tms570_binfo.initrd_filename = machine->initrd_filename;
    tms570_binfo.board_id = realview_board_id[board_type];
    arm_load_kernel(cpu, &tms570_binfo);
}

static void tms570_ls3137_init(MachineState *machine)
{
    if (!machine->cpu_model) {
        machine->cpu_model = "cortex-r4f";
    }
    tms570_init(machine, BOARD_LS3137);
}

static void tms570_ls3137_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hercules Safety MCU (tms570ls31x)";
    mc->init = tms570_ls3137_init;
    mc->block_default_type = IF_SD;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->default_ram_size = 128 * 1024 * 1024;
}

static const TypeInfo tms570_ls3137_type = {
    .name = MACHINE_TYPE_NAME("tms570-ls3137"),
    .parent = TYPE_MACHINE,
    .class_init = tms570_ls3137_class_init,
};

static void tms570_machine_init(void)
{
    type_register_static(&tms570_ls3137_type);
}

type_init(tms570_machine_init)