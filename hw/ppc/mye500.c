#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"

#include "net/net.h"
#include "qemu/config-file.h"
#include "hw/hw.h"
#include "hw/char/serial.h"
#include "hw/pci/pci.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "sysemu/device_tree.h"
#include "hw/ppc/openpic.h"
#include "hw/ppc/ppc.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"
#include "qemu/host-utils.h"
#include "hw/pci-host/ppce500.h"
#include "qemu/error-report.h"
#include "hw/platform-bus.h"
#include "hw/net/fsl_etsec/etsec.h"
#include "qemu/cutils.h"

#define RAM_SIZES_ALIGN            (64UL << 20)
#define DTC_LOAD_PAD               0x1800000
#define DTB_MAX_SIZE               (8 * 1024 * 1024)
#define DTC_PAD_MASK               0xFFFFF

struct boot_info
{
    uint32_t dt_base;
    uint32_t dt_size;
    uint32_t entry;
};



/* Create -kernel TLB entries for BookE.  */
static hwaddr booke206_page_size_to_tlb(uint64_t size)
{
    return 63 - clz64(size >> 10);
}

static int booke206_initial_map_tsize(CPUPPCState *env)
{
    struct boot_info *bi = env->load_info;
    hwaddr dt_end;
    int ps;

    /* Our initial TLB entry needs to cover everything from 0 to
       the device tree top */
    dt_end = bi->dt_base + bi->dt_size;
    ps = booke206_page_size_to_tlb(dt_end) + 1;
    if (ps & 1) {
        /* e500v2 can only do even TLB size bits */
        ps++;
    }
    return ps;
}

static void mmubooke_create_initial_mapping(CPUPPCState *env)
{
    ppcmas_tlb_t *tlb = booke206_get_tlbm(env, 1, 0, 0);
    hwaddr size;
    int ps;

    ps = booke206_initial_map_tsize(env);
    size = (ps << MAS1_TSIZE_SHIFT);
    tlb->mas1 = MAS1_VALID | size;
    // tlb->mas2 = 0;
    // tlb->mas7_3 = 0;
    tlb->mas7_3 |= MAS3_UR | MAS3_UW | MAS3_UX | MAS3_SR | MAS3_SW | MAS3_SX;

    env->tlb_dirty = true;
}


static void ppce500_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    struct boot_info *bi = env->load_info;

    cpu_reset(cs);

    /* Set initial guest state. */
    // cs->halted = 0;
    // env->gpr[1] = (16<<20) - 8;
    // env->gpr[3] = bi->dt_base;
    // env->gpr[4] = 0;
    // env->gpr[5] = 0;
    // env->gpr[6] = EPAPR_MAGIC;
    // env->gpr[7] = mmubooke_initial_mapsize(env);
    // env->gpr[8] = 0;
    // env->gpr[9] = 0;
    env->nip = bi->entry;
    mmubooke_create_initial_mapping(env);
}



static void mye500_init(MachineState *machine)
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
    int dt_size;
    int i;
    /* irq num for pin INTA, INTB, INTC and INTD is 1, 2, 3 and
     * 4 respectively */
    CPUPPCState *firstenv = NULL;

    /* Setup CPUs */
    if (machine->cpu_model == NULL) {
        machine->cpu_model = "e500";
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
            qemu_register_reset(ppce500_cpu_reset, cpu);
            env->load_info = boot_info;
        } else {
            /* Secondary CPUs */
            // qemu_register_reset(ppce500_cpu_reset_sec, cpu);
        }
    }

    env = firstenv;

    /* Fixup Memory size on a alignment boundary */
    ram_size &= ~(RAM_SIZES_ALIGN - 1);
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
            bios_name = "u-boot.e500";
        }
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

    /* Reserve space for dtb */
    dt_base = (loadaddr + bios_size + DTC_LOAD_PAD) & ~DTC_PAD_MASK;

    assert(dt_size < DTB_MAX_SIZE);

    boot_info = env->load_info;
    boot_info->entry = bios_entry;
    boot_info->dt_base = dt_base;
}

static void mye500_machine_init(MachineClass* mc)
{
    mc->desc = "my e500 platform";
    mc->init = mye500_init;
    mc->max_cpus = 1;
    mc->default_ram_size = 128*M_BYTE;
    mc->default_boot_order = "abc";
}

DEFINE_MACHINE("mye500",mye500_machine_init)