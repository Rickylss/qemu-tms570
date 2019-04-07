/*
 * QEMU PPC PREP hardware System Emulator
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
#include "hw/char/pl011.h"
#include "hw/char/pc16552d.h"
#define MAX_CPUS 1
#define KERNEL_LOAD_ADDR 0x01000000
#define BIOS_SIZE (1024 * 1024)
#if defined (TARGET_PPC64)
#define PPC_ELF_MACHINE     EM_PPC64
#else
#define PPC_ELF_MACHINE     EM_PPC
#endif
static void ppc_755_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static void ppc_755board_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    // const char *kernel_cmdline = machine->kernel_cmdline;
    // const char *initrd_filename = machine->initrd_filename;
    // const char *boot_device = machine->boot_order;
    MemoryRegion *sysmem = get_system_memory();
    PowerPCCPU *cpu = NULL;
    CPUPPCState *env = NULL;
    qemu_irq pic[5];
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    DeviceState *dev;
    // int linux_boot;
    // linux_boot = (kernel_filename != NULL);

    /* init CPUs */
    if (machine->cpu_model == NULL)
        machine->cpu_model = "755";
    int i;
    for (i = 0; i < smp_cpus; i++) {
        cpu = cpu_ppc_init(machine->cpu_model);
        if (cpu == NULL) {
            fprintf(stderr, "Unable to find PowerPC CPU definition\n");
            exit(1);
        }
        env = &cpu->env;

        if (env->flags & POWERPC_FLAG_RTC_CLK) {
            /* POWER / PowerPC 601 RTC clock frequency is 7.8125 MHz */
            cpu_ppc_tb_init(env, 7812500UL);
        } else {
            /* Set time-base frequency to 100 Mhz */
            cpu_ppc_tb_init(env, 100UL * 1000UL * 1000UL);
        }
        qemu_register_reset(ppc_755_reset, cpu);
    }

    /* allocate RAM */
    memory_region_allocate_system_memory(ram, NULL, "ppc_prep.ram", ram_size);
    memory_region_add_subregion(sysmem, 0, ram);
    if(!!kernel_filename){
       uint32_t kernel_base = KERNEL_LOAD_ADDR;
       long kernel_size = load_image_targphys(kernel_filename,kernel_base,ram_size - kernel_base);
       if(kernel_size < 0){
           error_report("could not load kernel '%s'",kernel_filename);
           exit(1);
       }
    }
    int bios_size = -1;
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    memory_region_init_ram(bios, NULL, "bios", BIOS_SIZE,
                           &error_fatal);
    // memory_region_set_readonly(bios, true);
    memory_region_add_subregion(get_system_memory(), (uint32_t)(-BIOS_SIZE),bios);
    
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        error_report("Only 6xx bus is supported on ppc755 machine");
        exit(1);
    }
    dev=sysbus_create_varargs("tsi107epic",0xfc000000,cpu->env.irq_inputs[PPC6xx_INPUT_INT]);

    for(i=0;i<5;i++){
        pic[i] = qdev_get_gpio_in(dev,i);
    }
    // pl011_create(0xa0000000,pic[0],serial_hds[0]);
    pc16552d_create(0xa0000000,pic[4],serial_hds[0],serial_hds[1]);
    if (bios_name == NULL) {
        if (machine->kernel_filename) {
            bios_name = machine->kernel_filename;
        } else {
            error_report("could not load bios ");
            exit(1);
        }
    }
    if (bios_name) {
        char* filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (filename) {
          
            bios_size = load_elf(filename, NULL, NULL, NULL,
                                     NULL, NULL, 1, PPC_ELF_MACHINE, 0, 0);
            
            if (bios_size < 0) {
                bios_size = get_image_size(filename);
                if (bios_size > 0 && bios_size <= BIOS_SIZE) {
                    hwaddr bios_addr;
                    bios_size = (bios_size + 0xfff) & ~0xfff;
                    bios_addr = (uint32_t)(-BIOS_SIZE);
                    bios_size = load_image_targphys(filename, bios_addr,
                                                    bios_size);
                }
            }
        }
        if (bios_size < 0 || bios_size > BIOS_SIZE) {
            /* FIXME should error_setg() */
            hw_error("qemu: could not load bios image \n");
        }
        g_free(filename);
    }
}
static void ppc755board_machine_init(MachineClass *mc)
{
    mc->desc = "PowerPC 755test platform";
    mc->init = ppc_755board_init;
    mc->max_cpus = 1;
    mc->default_ram_size = 128 * M_BYTE;
    mc->default_boot_order = "cadc";
}

DEFINE_MACHINE("ppc755",ppc755board_machine_init)