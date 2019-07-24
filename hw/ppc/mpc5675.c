/*
 * QEMU PPC5675 hardware System Emulator
 *
 * Copyright (c) 2019 FormalTech
 * Written by xiaohaibiao
 * 
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/char/serial.h"
#include "hw/block/fdc.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/ppc/ppc.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "sysemu/block-backend.h"
#include "sysemu/arch_init.h"
#include "sysemu/qtest.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "elf.h"
#include "qemu/cutils.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/char/mpc5675_linflexd.h"
#include "user/app.h"

#define EPAPR_MAGIC                (0x45504150)
#define MAX_CPUS 1
#define RESET_TSIZE (2 << MAS1_TSIZE_SHIFT) // 0b00010 SIZE 4KB page size
#define RESET_MAPSIZE (1ULL << 10 << 2)
// #if defined (TARGET_PPC64)
// #define PPC_ELF_MACHINE     EM_PPC64
// #else
// #define PPC_ELF_MACHINE     EM_PPC
// #endif

// #define RAM_SIZES_ALIGN            (64UL << 20)

struct boot_info
{
    uint32_t dt_base;
    uint32_t dt_size;
    uint32_t entry;
};

/* Create reset TLB entries for BookE, spanning the 32bit addr space.  */
static void mmubooke_create_initial_mapping(CPUPPCState *env)
{
    ppcmas_tlb_t *tlb = booke206_get_tlbm(env, 1, 0, 0);

    tlb->mas1 = MAS1_VALID | MAS1_IPROT | RESET_TSIZE;
    tlb->mas2 = 0;
    tlb->mas7_3 = 0;
    tlb->mas7_3 |= MAS3_UR | MAS3_UW | MAS3_UX | MAS3_SR | MAS3_SW | MAS3_SX;

    env->tlb_dirty = true;
    env->spr[SPR_BOOKE_MAS0] = MAS0_TLBSEL_TLB1;
    env->spr[SPR_BOOKE_MAS1] = tlb->mas1;
    env->spr[SPR_BOOKE_MAS2] = tlb->mas2;
    env->spr[SPR_BOOKE_MAS3] = tlb->mas7_3;
    env->spr[SPR_BOOKE_MAS4] = MAS0_TLBSEL_TLB1;
}

static void ppc_5675_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    struct boot_info *bi = env->load_info;

    cpu_reset(cs);

    //fprintf(stderr,"bios entry:%x\n",bi->entry);
        /* Set initial guest state. */
    cs->halted = 0;
    env->gpr[1] = (16<<20) - 8;
    env->gpr[3] = bi->dt_base;
    env->gpr[4] = 0;
    env->gpr[5] = 0;
    env->gpr[6] = EPAPR_MAGIC;
    env->gpr[7] = RESET_MAPSIZE;
    env->gpr[8] = 0;
    env->gpr[9] = 0;
    env->nip = bi->entry;
    mmubooke_create_initial_mapping(env);
}

static void ppc_5675board_init(MachineState *machine)
{
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *ebi = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *dram = g_new(MemoryRegion, 1);
    SysBusDevice *busdev;
    CPUPPCState *env = NULL;
    hwaddr dt_base = 0;
    struct boot_info *boot_info;
    int i;
    uint32_t reset_vector = 0;
    CPUPPCState *firstenv = NULL;
    DeviceState *dev, *stm;
    qemu_irq irq[2];
    qemu_irq reset_exc[2];
    qemu_irq pic[337];

    /* Setup CPUs */
    if (machine->cpu_model == NULL) {
        machine->cpu_model = "e200z7";
    }

    for (i = 0; i < smp_cpus; i++) {
        PowerPCCPU *cpu;
        cpu = cpu_ppc_init(machine->cpu_model);
        if (cpu == NULL) {
            fprintf(stderr, "Unable to initialize CPU!\n");
            exit(1);
        }
        env = &cpu->env;
        // cs = CPU(cpu);

        if (!firstenv) {
            firstenv = env;
        }

        irq[i] = env->irq_inputs[PPCE200_INPUT_INT];
        reset_exc[i] = env->irq_inputs[PPCE200_INPUT_RESET_SYS];

        ppc_booke_timers_init(cpu, 400000000, PPC_TIMER_BOOKE);

        /* Register reset handler */
        if (!i) {
            /* Primary CPU */
            struct boot_info *boot_info;
            boot_info = g_malloc0(sizeof(struct boot_info));
            qemu_register_reset(ppc_5675_reset, cpu);
            env->load_info = boot_info;
        } else {
            /* Secondary CPUs */
            // qemu_register_reset(ppce500_cpu_reset_sec, cpu);
        }
    }
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_BookE) {
        error_report("Only BOOKE bus is supported on E200 machine");
        exit(1);
    }
    env = firstenv;

    uint64_t flash_size = 12*M_BYTE;
    memory_region_allocate_system_memory(flash, NULL, "mpc5675.flash", flash_size);
    memory_region_add_subregion(address_space_mem, 0x00000000, flash);

    uint64_t ebi_size = 512*M_BYTE;
    memory_region_allocate_system_memory(ebi, NULL, "mpc5675.ebi", ebi_size);
    memory_region_add_subregion(address_space_mem, 0x20000000, ebi);

    uint64_t sram_size = 256*M_BYTE;
    memory_region_allocate_system_memory(sram, NULL, "mpc5675.sram", sram_size);
    memory_region_add_subregion(address_space_mem, 0x40000000, sram);

    uint64_t dram_size = 256*M_BYTE;
    memory_region_allocate_system_memory(dram, NULL, "mpc5675.dram", dram_size);
    memory_region_add_subregion(address_space_mem, 0x60000000, dram);

    machine->ram_size = flash_size + ebi_size + sram_size + dram_size;

    /* intc0 external interrupt ivor4 */
    dev = sysbus_create_varargs("mpc5675-intc", 0xfff48000,
                                irq[0], NULL);

    for (int n = 0; n < 337; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    /* PIT */
    dev = sysbus_create_varargs("mpc5675-pit", 0xc3ff0000,
                                pic[59], pic[60], pic[61], pic[127], NULL);

    uint32_t freq_base = 50 * 1000 * 1000; // 50MHz
    stm = qdev_create(NULL, "mpc5675-stm");
    qdev_prop_set_uint32(stm, "freq_base", freq_base);
    qdev_init_nofail(stm);
    busdev = SYS_BUS_DEVICE(stm);
    sysbus_mmio_map(busdev, 0, 0xfff3c000);

    for (int i = 30; i < 34; i++)
    {
        sysbus_connect_irq(busdev, i-30, pic[i]);
    }

    /* intc0 external interrupt system reset */
    dev = sysbus_create_varargs("mpc5675-swt", 0xfff38000,
                                reset_exc[0], pic[28], NULL);

    /* LINFlexD 0~3 */
    linflexd_create(0xffe40000, pic[79], pic[80], pic[81], serial_hds[0]);

    linflexd_create(0xffe44000, pic[99], pic[100], pic[101], serial_hds[1]);

    linflexd_create(0xffe48000, pic[119], pic[120], pic[121], serial_hds[2]);

    linflexd_create(0xffe4c000, pic[122], pic[123], pic[124], serial_hds[3]);

    // /* intc1 external interrupt ivor4 */
    // dev = sysbus_create_varargs("mpc5675-intc", 0x8ff48000,
    //                             irq[1], NULL);

    // for (int n = 0; n < 337; n++) {
    //     pic[n] = qdev_get_gpio_in(dev, n);
    // }

    int appindex=0;
    int appsize=-1;
    for(; appindex < appcount; appindex++){
        if (app[appindex].appaddr == 0) {
            appsize = load_image_bam_targphys(app[0].appname,app[0].appaddr,machine->ram_size-app[0].appaddr, &reset_vector);
        } else {
            appsize = load_image_targphys(app[appindex].appname,app[appindex].appaddr,machine->ram_size-app[appindex].appaddr);
        }
        if(appsize < 0){
            hw_error("qemu:could not load app:%s\n",app[appindex].appname);
        }
    }

    if(reset_vector == 0xdead){
        hw_error("qemu:could not set reset vector\n");
    }

    boot_info = env->load_info;
    boot_info->entry = reset_vector;
    boot_info->dt_base = dt_base;
}

static void ppc5675board_machine_init(MachineClass *mc)
{
    mc->desc = "mpc5675 platform";
    mc->init = ppc_5675board_init;
    mc->max_cpus = 1;
    //mc->default_ram_size = 128 * M_BYTE;
    mc->default_boot_order = "cad";
}

DEFINE_MACHINE("mpc5675", ppc5675board_machine_init)
