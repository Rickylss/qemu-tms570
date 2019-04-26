/*
 * QEMU PPC 5675 hardware System Emulator
 *
 * Copyright (c) 2003-2007 Jocelyn Mayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
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
// #include "hw/char/pl011.h"
// #include "hw/char/pc16552d.h"
#define MAX_CPUS 1
#define KERNEL_LOAD_ADDR 0x01000000
#define BIOS_SIZE (1024 * 1024)
#if defined (TARGET_PPC64)
#define PPC_ELF_MACHINE     EM_PPC64
#else
#define PPC_ELF_MACHINE     EM_PPC
#endif

#define RAM_SIZES_ALIGN            (64UL << 20)

struct boot_info
{
    uint32_t dt_base;
    uint32_t dt_size;
    uint32_t entry;
};

// /* Create -kernel TLB entries for BookE.  */
// static hwaddr booke206_page_size_to_tlb(uint64_t size)
// {
//     return 63 - clz64(size >> 10);
// }

// static int booke206_initial_map_tsize(CPUPPCState *env)
// {
//     struct boot_info *bi = env->load_info;
//     hwaddr dt_end;
//     int ps;

//     /* Our initial TLB entry needs to cover everything from 0 to
//        the device tree top */
//     dt_end = bi->dt_base + bi->dt_size;
//     ps = booke206_page_size_to_tlb(dt_end) + 1;
//     if (ps & 1) {
//         /* e500v2 can only do even TLB size bits */
//         ps++;
//     }
//     return ps;
// }

// static void mmubooke_create_initial_mapping(CPUPPCState *env)
// {
//     ppcmas_tlb_t *tlb = booke206_get_tlbm(env, 0, 0, 0);
//     hwaddr size;
//     int ps;

//     ps = booke206_initial_map_tsize(env);
//     size = (ps << MAS1_TSIZE_SHIFT);
//     tlb->mas1 = MAS1_VALID | size;
//     // tlb->mas2 = 0;
//     // tlb->mas7_3 = 0;
//     tlb->mas7_3 |= MAS3_UR | MAS3_UW | MAS3_UX | MAS3_SR | MAS3_SW | MAS3_SX;

//     env->tlb_dirty = true;
// }

/* Create reset TLB entries for BookE, spanning the 32bit addr space.  */
static void mmubooke_create_initial_mapping(CPUPPCState *env,
                                     target_ulong va,
                                     hwaddr pa)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[0];

    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 1U << 31; /* up to 0x80000000  */
    tlb->EPN = va & TARGET_PAGE_MASK;
    tlb->RPN = pa & TARGET_PAGE_MASK;
    tlb->PID = 0;

    tlb = &env->tlb.tlbe[1];
    tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    tlb->size = 1U << 31; /* up to 0xffffffff  */
    tlb->EPN = 0x80000000 & TARGET_PAGE_MASK;
    tlb->RPN = 0x80000000 & TARGET_PAGE_MASK;
    tlb->PID = 0;
}

static void ppc_5675_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));

    // CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    struct boot_info *bi = env->load_info;

    fprintf(stderr,"bios entry:%" PRIX32 "\n",bi->entry);
    env->nip = bi->entry;
    // env->gpr[1] = (16<<20) - 8;
    mmubooke_create_initial_mapping(env,0,0);
}

static void ppc_5675board_init(MachineState *machine)
{
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    CPUPPCState *env = NULL;
    uint64_t loadaddr;
    int kernel_size = 0;
    hwaddr dt_base = 0;
    char *filename;
    hwaddr bios_entry = 0;
    target_long bios_size;
    struct boot_info *boot_info;
    // int dt_size;
    int i;
    CPUPPCState *firstenv = NULL;

    /* Setup CPUs */
    if (machine->cpu_model == NULL) {
        machine->cpu_model = "e200z6";
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

    /* Fixup Memory size on a alignment boundary */
    ram_size &= ~(RAM_SIZES_ALIGN - 1);
    fprintf(stderr,"ram_size:%" PRIX32 "\n",ram_size);
    machine->ram_size = ram_size;

    /* Register Memory */
    memory_region_allocate_system_memory(ram, NULL, "mpc8544ds.ram", ram_size);
    memory_region_add_subregion(address_space_mem, 0, ram);

    /*
     * Smart firmware defaults ahead!
     *
     * We follow the following table to select which payload we execute.
     *
     *  -kernel | -bios | payload
     * ---------+-------+---------
     *     N    |   Y   | u-boot
     *     N    |   N   | u-boot
     *     Y    |   Y   | u-boot
     *     Y    |   N   | kernel
     *
     * This ensures backwards compatibility with how we used to expose
     * -kernel to users but allows them to run through u-boot as well.
     */
    if (bios_name == NULL) {
        if (machine->kernel_filename) {
            bios_name = machine->kernel_filename;
        } else {
            // bios_name = "u-boot.e500";
        }
    }
    if(!bios_name){
        fprintf(stderr,"qemu:not bios\n");
        exit(1);
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

    bios_size = load_elf(filename, NULL, NULL, &bios_entry, &loadaddr, NULL,
                         1, PPC_ELF_MACHINE, 0, 0);
    if (bios_size < 0) {
        /*
         * Hrm. No ELF image? Try a uImage, maybe someone is giving us an
         * ePAPR compliant kernel
         */
        kernel_size = load_uimage(filename, &bios_entry, &loadaddr, NULL,
                                  NULL, NULL);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load firmware '%s'\n", filename);
            exit(1);
        }
    }
    g_free(filename);

    boot_info = env->load_info;
    boot_info->entry = bios_entry;
    boot_info->dt_base = dt_base;
}
static void ppc5675board_machine_init(MachineClass *mc)
{
    mc->desc = "mpc5675 platform";
    mc->init = ppc_5675board_init;
    mc->max_cpus = 1;
    mc->default_ram_size = 128 * M_BYTE;
    mc->default_boot_order = "cadc";
}

DEFINE_MACHINE("mpc5675",ppc5675board_machine_init)