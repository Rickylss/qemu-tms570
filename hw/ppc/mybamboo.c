/*
 * QEMU PowerPC 440 Bamboo board emulation
 *
 * Copyright 2007 IBM Corporation.
 * Authors:
 *	Jerone Young <jyoung5@us.ibm.com>
 *	Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 *	Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "net/net.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/boards.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "sysemu/device_tree.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "hw/char/serial.h"
#include "hw/ppc/ppc.h"
#include "ppc405.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"

#define BINARY_DEVICE_TREE_FILE "bamboo.dtb"

/* from u-boot */
#define KERNEL_ADDR  0x1000000
#define FDT_ADDR     0x1800000
#define RAMDISK_ADDR 0x1900000

#define PPC440EP_PCI_CONFIG     0xeec00000
#define PPC440EP_PCI_INTACK     0xeed00000
#define PPC440EP_PCI_SPECIAL    0xeed00000
#define PPC440EP_PCI_REGS       0xef400000
#define PPC440EP_PCI_IO         0xe8000000
#define PPC440EP_PCI_IOLEN      0x00010000

#define PPC440EP_SDRAM_NR_BANKS 4
#define RAM_SIZES_ALIGN            (64UL << 20)
static const unsigned int ppc440ep_sdram_bank_sizes[] = {
    256<<20, 128<<20, 64<<20, 32<<20, 16<<20, 8<<20, 0
};

static hwaddr entry;

static void mmubooke_create_initial_mapping(CPUPPCState *env,
                                     target_ulong va,
                                     hwaddr pa)
{
    ppcemb_tlb_t *tlb = &env->tlb.tlbe[1];

    // tlb->attr = 0;
    tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    // tlb->size = 1U << 31; /* up to 0x80000000  */
    // tlb->EPN = va & TARGET_PAGE_MASK;
    // tlb->RPN = pa & TARGET_PAGE_MASK;
    // tlb->PID = 0;

    // tlb = &env->tlb.tlbe[1];
    // tlb->attr = 0;
    // tlb->prot = PAGE_VALID | ((PAGE_READ | PAGE_WRITE | PAGE_EXEC) << 4);
    // tlb->size = 1U << 31; /* up to 0xffffffff  */
    // tlb->EPN = 0x80000000 & TARGET_PAGE_MASK;
    // tlb->RPN = 0x80000000 & TARGET_PAGE_MASK;
    // tlb->PID = 0;
}

static void main_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    cpu_reset(CPU(cpu));
    // env->gpr[1] = (16<<20) - 8;
    // env->gpr[3] = FDT_ADDR;
    env->nip = entry;

    /* Create a mapping for the kernel.  */
    mmubooke_create_initial_mapping(env, 0, 0);
}

static void bamboo_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    PowerPCCPU *cpu;
    // CPUPPCState *env;
    uint64_t elf_entry;
    uint64_t elf_lowaddr;
    hwaddr loadaddr = 0;

    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *address_space_mem = get_system_memory();

    int success;


    /* Setup CPU. */
    if (machine->cpu_model == NULL) {
        machine->cpu_model = "e200";
    }
    cpu = cpu_ppc_init(machine->cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to initialize CPU!\n");
        exit(1);
    }
    // env = &cpu->env;

    qemu_register_reset(main_cpu_reset, cpu);
    // ppc_booke_timers_init(cpu, 400000000, 0);
    // ppc_dcr_init(env, NULL, NULL);

    ram_size &= ~(RAM_SIZES_ALIGN - 1);
    machine->ram_size = ram_size;

    /* Register Memory */
    memory_region_allocate_system_memory(ram, NULL, "mpc8544ds.ram", ram_size);
    memory_region_add_subregion(address_space_mem, 0, ram);

    /* Load kernel. */
    if (kernel_filename) {
        success = load_uimage(kernel_filename, &entry, &loadaddr, NULL,
                              NULL, NULL);
        if (success < 0) {
            success = load_elf(kernel_filename, NULL, NULL, &elf_entry,
                               &elf_lowaddr, NULL, 1, PPC_ELF_MACHINE,
                               0, 0);
            entry = elf_entry;
            loadaddr = elf_lowaddr;
        }
        /* XXX try again as binary */
        if (success < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }


}

static void bamboo_machine_init(MachineClass *mc)
{
    mc->desc = "mybamboo";
    mc->init = bamboo_init;
}

DEFINE_MACHINE("mybamboo", bamboo_machine_init)
