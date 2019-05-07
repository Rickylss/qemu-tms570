/*
 * QEMU PPC 755 hardware System Emulator
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
#define BIOS_SIZE 0x7fffff      //8Mbyte
#if defined (TARGET_PPC64)
#define PPC_ELF_MACHINE     EM_PPC64
#else
#define PPC_ELF_MACHINE     EM_PPC
#endif

#define APPNAMELENGTH   30
#define APPMAXCOUNT    30
typedef struct {
    char appname[APPNAMELENGTH];
    uint32_t appaddr;
}APPinfo;

extern APPinfo app[APPMAXCOUNT];
extern int appcount;
extern uint32_t apptestaddr;
static void ppc_755_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static void ppc_755board_init(MachineState *machine)
{
    int64_t ram_size = (int64_t)machine->ram_size;
    MemoryRegion *sysmem = get_system_memory();
    PowerPCCPU *cpu = NULL;
    CPUPPCState *env = NULL;
    qemu_irq pic[5];
    MemoryRegion *ram;
    DeviceState *dev;
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
        qemu_register_reset(ppc_755_reset, cpu);
    }

    /* allocate RAM */
    hwaddr offset = 0;
    uint64_t temp=0;
    char ram_name[20]={0};
    int index = 0;
    while(ram_size > 0){   
        ram = g_new(MemoryRegion, 1);
        temp = (!(ram_size%(128*M_BYTE)))?128*M_BYTE:ram_size%(128*M_BYTE);
        // fprintf(stderr,"offset:%lx   ram_size_temp:%lx\n",offset,temp);
        memset(ram_name,0,sizeof(ram_name));
        sprintf(ram_name,"ppc_prep.ram.%d",index);
        memory_region_allocate_system_memory(ram, NULL, (const char*)ram_name,temp);
        memory_region_add_subregion(sysmem, offset, ram);
        ram_size -= 128*M_BYTE;
        offset += 128*M_BYTE;
        index ++;
    }
    MemoryRegion *bios = g_new(MemoryRegion, 1);
    memory_region_init_ram(bios, NULL, "bios", BIOS_SIZE,
                           &error_fatal);
    // memory_region_set_readonly(bios, true);
    memory_region_add_subregion(get_system_memory(), 0xff800000,bios);
    
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        error_report("Only 6xx bus is supported on ppc755 machine");
        exit(1);
    }
    dev=sysbus_create_varargs("tsi107epic",0xfc000000,cpu->env.irq_inputs[PPC6xx_INPUT_INT]);

    for(i=0;i<5;i++){
        pic[i] = qdev_get_gpio_in(dev,i);
    }
    // pl011_create(0xa0000000,pic[0],serial_hds[0]);
    pc16552d_create(0xa0000000,pic[4],pic[0],serial_hds[0],serial_hds[1]);
    int appindex=0;
    int appsize=-1;
    for(;appindex<appcount;appindex++){
        appsize = load_image_targphys(app[appindex].appname,app[appindex].appaddr,ram_size-app[appindex].appaddr);
        if(appsize < 0){
            hw_error("qemu:could not load app:%s\n",app[appindex].appname);
        }
    }
}
static void ppc755board_machine_init(MachineClass *mc)
{
    mc->desc = "PowerPC 755test platform";
    mc->init = ppc_755board_init;
    mc->max_cpus = 1;
    mc->default_ram_size = 256 * M_BYTE;
    mc->default_boot_order = "cadc";
}

DEFINE_MACHINE("ppc755",ppc755board_machine_init)