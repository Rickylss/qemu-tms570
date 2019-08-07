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
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/osdep.h"

#define TYPE_FAKESYS "tms570-fakesys"
#define FAKESYS(obj) OBJECT_CHECK(FakesysState, (obj), TYPE_FAKESYS)

typedef struct FakesysState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t syspc[9];  /* syspc0~8 */
    uint32_t csdis;     /* CSDIS CSDISSET CSDISCLR */
    uint32_t cddis;     /* CDDIS CDDISSET CDDISCLR */
    uint32_t ghvsrc;
    uint32_t vclkasrc;
    uint32_t rclksrc;
    uint32_t csvstat;
    uint32_t mstgcr;
    uint32_t minitgcr;
    uint32_t msinena;

    uint32_t mstcgstat;
    uint32_t ministat;
    uint32_t pllctl1;
    uint32_t pllctl2;
    uint32_t syspc10;
    uint64_t dieidl;     /* DIEIDL DIEIDH */
    uint32_t lpomonctl;
    uint32_t clktest;
    uint32_t dftctrlreg1;
    uint32_t dftctrlreg2; 

    uint32_t gpreg1;
    uint32_t impfasts;
    uint32_t impftadd;
    uint32_t ssir[4];
    uint32_t ramgcr;
    uint32_t bmmcr1;

    uint32_t cpurstcr;
    uint32_t clkcntl;
    uint32_t ecpcntl;
    uint32_t devcr1;
    uint32_t sysecr;
    uint32_t sysesr;
    uint32_t systasr;
    uint32_t glbstat;
    uint32_t devid;
    uint32_t ssivec;
    uint32_t ssif;

    uint32_t black_hole;

} FakesysState;

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
    CPUClass *cc;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *async_ram = g_new(MemoryRegion, 1);
    MemoryRegion *sdram = g_new(MemoryRegion, 1);
    uint64_t flash_size = 3 * 1024 * 1024;
    uint64_t ram_size = 256 * 1024;
    uint64_t async_ram_size = 16 * 3 * 1024 * 1024;
    uint64_t sdram_size = 128 * 1024 * 1024;
    qemu_irq pic[95];
    qemu_irq rti[2];
    DeviceState *dev;
    SysBusDevice *vimdev;
    char **cpustr;
    const char *cpu_model = machine->cpu_model;
    Error *err = NULL;
    const char *typename;
    int n;


    switch (board_type) {
    case BOARD_LS3137 :
        // set flags;
        break;
    default :
        break;
    }

    cpustr = g_strsplit(cpu_model, ",", 2);

    cpu_oc = cpu_class_by_name(TYPE_ARM_CPU, cpustr[0]);
    if (!cpu_oc) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    typename = object_class_get_name(cpu_oc);

    cc = CPU_CLASS(cpu_oc);
    cc->parse_features(typename, cpustr[1], &err);
    g_strfreev(cpustr);
    if (err) {
        error_report_err(err);
        exit(1);
    }

    cpuobj = object_new(typename);

    /* ARM cortex-r4f do not support el3 */
    if (object_property_find(cpuobj, "has_el3", NULL)) {
        object_property_set_bool(cpuobj, false, "has_el3", &error_fatal);
    }

    object_property_set_bool(cpuobj, true, "realized", &error_fatal);

    cpu = ARM_CPU(cpuobj);

    memory_region_allocate_system_memory(flash, NULL, "tms570ls31x.flash",
                                         flash_size);
    memory_region_allocate_system_memory(ram, NULL, "tms570ls31x.ram",
                                         ram_size);
    memory_region_allocate_system_memory(async_ram, NULL, "tms570ls31x.async.ram",
                                         async_ram_size);
    memory_region_allocate_system_memory(sdram, NULL, "tms570ls31x.sdram",
                                         sdram_size);
    /* ??? RAM should repeat to fill physical memory space.  */
    /* FLASH at address 0x00000000. */
    memory_region_add_subregion(sysmem, 0x00000000, flash);
    /* RAM at address 0x08000000.  */
    memory_region_add_subregion(sysmem, 0x08000000, ram);
    /* ASYNCRAM at address 0x60000000.  */
    memory_region_add_subregion(sysmem, 0x60000000, async_ram);
    /* SDRAM at address 0x80000000.  */
    memory_region_add_subregion(sysmem, 0x80000000, sdram);

    machine->ram_size = 0x08000000 + ram_size;

    //sysbus_create_simple("tms570-vimram", 0xfff82000, NULL); /* VIMRAM */
    /* VIM at address 0xfffffe00 */
    dev = sysbus_create_varargs("tms570-vim", 0xfffffe00,
                                qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ),
                                qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ),
                                NULL);
    for (n = 0; n < 95; n++) { 
        pic[n] = qdev_get_gpio_in(dev, n);
    }
    vimdev = SYS_BUS_DEVICE(dev);
    /* channel 0 and 1 are reserved */
    //sysbus_create_simple("esm", 0x, pic[0]);
    //sysbus_create_simple("adc", 0x, pic[1]);

    /* N2HET at address 0xfff7b800 */
    // sysbus_create_varargs("tms570-n2het", 0xfff7b800, pic[10], pic[24], NULL);
    // sysbus_create_varargs("tms570-n2het", 0xfff7b900, pic[63], pic[73], NULL);

    /* RTI at address 0xfffffc00 
     * RTI compare interrupt 0~3  pic 2~5
     * RTI overflow interrupt 0~1 pic 6~7
     * RTI timebase interrupt pic 8
     */
    dev = sysbus_create_varargs("tms570-rti", 0xfffffc00,
                            pic[2], pic[3], pic[4], pic[5], pic[6], pic[7], pic[8], NULL);
    for (n = 0; n < 2; n++)
    {
        rti[n] = qdev_get_gpio_in(dev, n);
    }
    
    /* two special interrupts link to rti */
    sysbus_connect_irq(vimdev, 2, rti[0]);
    sysbus_connect_irq(vimdev, 3, rti[1]);

    /* SCI at address 0xfff7e500 */
    //sysbus_create_varargs("tms570-sci", 0xfff7e500, pic[64], pic[74], NULL);
    sci_create(0xfff7e400, pic[13], pic[27], serial_hds[0]);
    sci_create(0xfff7e500, pic[64], pic[74], serial_hds[1]);

    /* GPIO at address 0xfff7bc00 portA portB*/
    //sysbus_create_varargs("pl061", 0xfff7bc00, pic[9], pic[23], NULL);

    sysbus_create_simple("tms570-fakesys", 0xffffff00, NULL);

    /* Memory map for tms570ls3137:  */
    /* 0xfff7b800 HET1 */
    /* 0xfff7b900 HET2 */
    /* 0xfff7bc00 GIO */
    /* 0xfff7d400 i2c */
    /* 0xfff7e500 SCI */
    /* 0xfff7e400 SCI(LIN) */
    /* 0xfffff000 DMA */
    /* 0xffffe100 system register2 */
    /* 0xfffffc00 RTI */
    /* 0xfffffe00 VIM */
    /* 0xffffff00 system register1 */

    tms570_binfo.ram_size = machine->ram_size;
    tms570_binfo.kernel_filename = machine->kernel_filename;
    tms570_binfo.kernel_cmdline = machine->kernel_cmdline;
    tms570_binfo.initrd_filename = machine->initrd_filename;
    tms570_binfo.board_id = realview_board_id[board_type];
    tms570_binfo.endianness = ARM_ENDIANNESS_BE8;
    //arm_load_kernel(cpu, &tms570_binfo);
    arm_load_app(cpu, &tms570_binfo);
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

static uint64_t fakesys_read(void *opaque, hwaddr offset, 
                        unsigned size)
{
    FakesysState *s = (FakesysState *)opaque;

    switch (offset)
    {
    case 0x30:      /* CSDIS CSDISSET CSDISCLR */
    case 0x34:
    case 0x38:
        return s->csdis;
    case 0x54:      /* CSVSTAT */
        return (s->csdis ^ 0xff);
    
    default:
        return s->black_hole;
    }
    
}

static void fakesys_write(void *opaque, hwaddr offset,
                        uint64_t val, unsigned size)
{
    FakesysState *s = (FakesysState *)opaque;

    switch (offset)
    {
    case 0x30:      /* CSDIS */
        s->csdis = val;
        break;
    case 0x34:      /* CSDISSET */
        s->csdis |= val & 0xFF;
        break;
    case 0x38:      /* CSDISCLR */
        s->csdis &= ~(val & 0xFF);
        break;
    case 0x54:      /* CSVSTAT */
        break;
    
    default:
        s->black_hole = val;
        break;
    }
    
}

static const MemoryRegionOps fakesys_ops = {
    .read = fakesys_read,
    .write = fakesys_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void fakesys_init(Object *obj)
{
    FakesysState *s = FAKESYS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &fakesys_ops, s,
                          "tms570-fakesys", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void sysfake_reset(DeviceState *d)
{
    FakesysState *s = FAKESYS(d);
    
    s->csdis = 0xce;
    s->csvstat = 0x77;
}

static const VMStateDescription vmstate_sysfake = {
    .name = "tms570-sysfake",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        /* TODO */
        VMSTATE_UINT32(black_hole, FakesysState),
        VMSTATE_END_OF_LIST()
    }
};

static void fakesys_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = sysfake_reset;
    dc->vmsd = &vmstate_sysfake;
}

static const TypeInfo fakesys_info = {
    .name          = TYPE_FAKESYS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(FakesysState),
    .instance_init = fakesys_init,
    .class_init    = fakesys_class_init,
};

static void fakesys_register_types(void)
{
    type_register_static(&fakesys_info);
}

type_init(fakesys_register_types)