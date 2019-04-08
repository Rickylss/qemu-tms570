/*
 * pc16552d UART
 *
 * refer to:MPC8379 USER MANUAL CHARTER 22
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "sysemu/char.h"
#include "qemu/log.h"

// #define DEBUG_PC16552D
#ifdef DEBUG_PC16552D
#define pc16552d_debug(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
#define pc16552d_debug(fmt, ...)
#endif

#define TYPE_PC16552D "pc16552d"
#define PC16552D(obj) OBJECT_CHECK(PC16552DState, (obj), TYPE_PC16552D)

#define PC16552D_FIFO_COUNT 16


typedef struct PC16552DState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint8_t read_fifo[2][PC16552D_FIFO_COUNT];
    uint8_t flags[2];
    uint8_t read_pos[2];
    uint8_t read_count[2];
    uint8_t read_trigger[2];
    uint8_t write_count[2];
    uint8_t write_trigger[2];
    CharDriverState *chr[2];
    qemu_irq irq;
    uint8_t mode[2];
    uint8_t urbr[2];
    uint8_t ulcr[2];
    uint8_t udlb[2];
    uint8_t udmb[2];
    uint8_t uier[2];
    uint8_t uiir[2];
    uint8_t ufcr[2];
    uint8_t umcr[2];
    uint8_t ulsr[2];
    uint8_t umsr[2];
    uint8_t uscr[2];
    uint8_t udsr[2];
    // const unsigned char *id;
} PC16552DState;

/*register address 
*
*       usart0:0x45xx       usart1:0x46xx
*/

static uint64_t pc16552d_read_0(PC16552DState* s,uint32_t index){
    pc16552d_debug("pc16552d read 0 index:%d\n",index);
    uint64_t res = -1;
    if(!(s->ulcr[index] & 0x80u)){
        if(s->uiir[index] & 0x80u){
                //fifo
            res = s->read_fifo[index][s->read_pos[index]];
            if(s->read_count[index] > 0){
                if(++s->read_pos[index] == 16)
                    s->read_pos[index] = 0;
                if(!--s->read_count[index]){
                    s->udsr[index] |= 0x1u;
                    s->ulsr[index] &= 0xfeu;
                }
            }
            // if(read_count == 0)
        }else{
                //no fifo
            res = s->read_fifo[index][0];
            s->ulsr[index] &= 0xfeu;
        }
    }else{
        res = s->udlb[index];
    }
    return res;
}
static uint64_t pc16552d_read_1(PC16552DState* s,uint32_t index){
    pc16552d_debug("pc16552d read 1 index:%d\n",index);
    if(s->ulcr[index] & 0x80u){
        return s->udmb[index];
    }else{
        return s->uier[index];
    }
}
static uint64_t pc16552d_read_uiir(PC16552DState* s,uint8_t index)
{   
    pc16552d_debug("pc16552d read uiir index:%d\n",index);

    return s->uiir[index];
}
static uint64_t pc16552d_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    // pc16552d_debug("pc16552d read offset:%lx\n",offset);
    PC16552DState* s = opaque;
    uint32_t index = 0;
    uint64_t res;
    if((offset>>2 &0xfu) == 6){
        index = 1;      //usart1
    }
    switch (offset & 0xff)
    {
        case 0:
            res=pc16552d_read_0(s,index);
            break;
        case 1:
            res = pc16552d_read_1(s,index);
            break;
        case 2:
            res = pc16552d_read_uiir(s,index);
            break;
        case 3:
            res = s->ulcr[index];
            break;
        case 4:
            res = s->umcr[index];
            break;
        case 5:
            res = s->ulsr[index];
            break;
        case 6:
            res = s->umsr[index];
            break;
        case 7:
            res = s->uscr[index];
            break;
        case 0x10:
            res = s->udsr[index];
            break;
        default:
            fprintf(stderr,"read error offset:%lx\n",offset);
            res = -1;
            break;
    }
    // pc16552d_debug("pc16552d read res:%lx     offset:%lx\n",res,offset);
    return res;                           
}  
inline static void pc16552d_send_trigger(PC16552DState* s,uint8_t index)
{   
    qemu_set_irq(s->irq,1);
}
static void pc16552d_send_update(PC16552DState* s,uint8_t index){
    pc16552d_debug("pc16552d send update\n");
    // if(s->mode[index] && s->write[count]<PC16552D_FIFO_COUNT){
    // s->udsr[index] &= 0xfdu;    //TXRDY 置0
    // }
    if((s->uiir[index]>>7) && (s->write_count[index] > PC16552D_FIFO_COUNT)){
        s->write_count[index] = 0; 
        if((s->uier[index] &0xf2u) >> 2){
            s->uiir[index] |= 0xfcu;
            pc16552d_send_trigger(s,index);         //UTHR empty interrupt
        } 
    }
    if((!(s->uiir[index]>>7)) && ((s->uier[index] &0xf2u) >> 2)){
        s->uiir[index] |= 0xfcu;
        pc16552d_send_trigger(s,index);             //UTHR empty interrupt
    }
    // if(!s->write_count[index])
        // s->udsr[index] &= 0xfd;     //TXRDY 置0
}
inline static void pc16552d_write_0(PC16552DState* s,uint64_t val,uint8_t index){
    pc16552d_debug("pc16552d write 0 value:%lx  index:%d\n",val,index);
    if((s->ulcr[index] & 0x80u) != 0x80u){          //thr
        // if(1){
        //         //fifo
        // }else{
        //         //no fifo
        // }
        unsigned char ch = val;
        if (s->chr[index]){
            // if(((s->write_count[index] == 1) && !s->mode[index]) ||
            //         ((s->write_count[index] == PC16552D_FIFO_COUNT) && s->mode[index])){
            //                 s->udsr[index] |= 0x2u;
            // }
            qemu_chr_fe_write(s->chr[index], &ch, 1);
            s->write_count[index] ++ ;
        }
        pc16552d_send_update(s,index);
    }else{   //dlb
        s->udlb[index] = val;
    }
}
inline static void pc16552d_write_1(PC16552DState* s,uint64_t val,uint8_t index){
    pc16552d_debug("pc16552d write 1 value:%lx  index:%d\n",val,index);
    if(!(s->ulcr[index] & 0x80u)){
        s->uier[index] = val;
    }else{
        s->udmb[index] = val;
    }
}
static void pc16552d_transmitter_fifo_reset(PC16552DState* s,uint8_t index)
{
    pc16552d_debug("pc16552d transmitter fifo reset\n");
    s->write_count[index] = 0;
}
static void pc16552d_receiver_fifo_reset(PC16552DState* s,uint8_t index)
{
    pc16552d_debug("pc16552d receiver fifo reset\n");
    s->read_pos[index] = 0;
    s->read_count[index] = 0;
}
static void pc16552d_write_2(PC16552DState* s,uint64_t val,uint8_t index){
    // uint8_t temp = val & 0xffu;
    pc16552d_debug("pc16552d write 2 val:%lx    index:%d\n",val,index);
    switch ((val & 0xc0u) >> 6)
    {
        case 0x0:
            /* code */
            s->read_trigger[index] = 1;
            break;
        case 0x01:
            s->read_trigger[index]  = 4;
            break;
        case 0x10:
            s->read_trigger[index] = 8;
            break;
        case 0x11:
            s->read_trigger[index] = 14;
            break;
        default:
            break;
    }
    s->uiir[index] |= ((val & 0x1u)<<7);
    if((val & 0x8u) == 0x8u && (val & 0x1u) == 0x1u){
        s->mode[index] = 1;
    }else{
        s->mode[index] = 0;
    }
    if((val & 0x4u) == 0x4u){
        pc16552d_transmitter_fifo_reset(s,index);
    }
    if((val & 0x2u) == 0x2u){
        pc16552d_receiver_fifo_reset(s,index);
    }
}
static void pc16552d_write(void *opaque, hwaddr offset, uint64_t val,unsigned size)
{
    PC16552DState*  s = opaque;
    uint8_t index = 0;
    pc16552d_debug("pc16552d write,val:%lx  offset:%lx\n",val,offset);
    if((offset >> 2 &0xf) == 6){
        index = 1;
    }
    switch(offset & 0xffu){
        case 0:
            pc16552d_write_0(s,val,index);
            if (s->chr[index]) {
                qemu_chr_accept_input(s->chr[index]);
            }
            break;
        case 1:
            pc16552d_write_1(s,val,index);    //ier
            break;
        case 2:
            pc16552d_write_2(s,val,index);
            break;
        case 3:
            s->ulcr[index] = val;
            break;
        case 4:
            s->umcr[index] = val;
            break;
        case 7:
            s->uscr[index] = val;
            break;  
        default:
            fprintf(stderr,"pc16552d write error,offset:%lx  val:%lx\n",offset,val);
            break;   
    }
    // pc16552d_debug("pc16552d write,val:%lx  offset:%lx\n",val,offset);
}
inline static void pc16552d_receive_trigger(PC16552DState* s,uint8_t index)
{   
    if(s->uier[index]&0x1u){
        s->uiir[index] |= 0x8u;
        qemu_set_irq(s->irq,1);
    }
}
static void pc16552d_put_fifo(void *opaque, uint32_t value,uint8_t index)
{
    pc16552d_debug(stderr,"pc16552d value:%x\n",value);
    PC16552DState *s = (PC16552DState *)opaque;
    int slot;
    s->ulsr[index] |= 0x1u;
    if((s->uiir[index] & 0x80u) == 0x80u){   //fifo mode 
        slot = s->read_pos[index] + s->read_count[index];
        if (slot >= 16)
            slot -= 16;
        s->read_fifo[index][slot] = value;
        s->read_count[index] ++;
        if((s->read_count[index] < s->read_trigger[index]) && s->mode[index]){
            s->udsr[index] |= 0x1u;
        }
        if(s->read_count[index] == s->read_trigger[index]){
            s->udsr[index] &= 0xfeu;
            pc16552d_receive_trigger(s,index);
        }
        if(!s->mode[index]){
            s->udsr[index] &= ~0x1u;  //RXRDY  置0
        }     
    }else{
        s->read_fifo[index][0] = value;
        pc16552d_receive_trigger(s,index);
    }
}
static int pc16552d_can_receive_1(void *opaque){
    // pc16552d_debug("pc16552d_can_receive\n");
    // PC16552DState *s = (PC16552DState *)opaque;
    // if (s->uiir[0] & 0x1u)
    //     return s->read_count[0] < 16;
    // else
    //     return s->read_count[0] < 1;
    return 1;
}

static void pc16552d_receive_1(void *opaque, const uint8_t *buf, int size){
    PC16552DState* s = opaque;
    pc16552d_debug("pc16552d_receive\n");
    pc16552d_debug("buf:%c\n",*buf);
    // qemu_set_irq(s->irq,0);
    pc16552d_put_fifo(s,*buf,0);
}
static void pc16552d_event_1(void *opaque, int event){
    pc16552d_debug("pc16552d_event\n");
    if (event == CHR_EVENT_BREAK)
        pc16552d_put_fifo(opaque, 0x400,0);
}


static int pc16552d_can_receive_2(void *opaque){
    // pc16552d_debug("pc16552d_can_receive\n");
    // PC16552DState *s = (PC16552DState *)opaque;
    // if (s->uiir[1] & 0x80u)
    //     return s->read_count[1] < 16;
    // else
    //     return s->read_count[1] < 1;
    return 1;
}

static void pc16552d_receive_2(void *opaque, const uint8_t *buf, int size){
    pc16552d_debug("pc16552d_receive\n");
    pc16552d_debug("buf:%c\n",*buf);
    pc16552d_put_fifo(opaque,*buf,1);
}
static void pc16552d_event_2(void *opaque, int event){
    pc16552d_debug("pc16552d_event\n");
    if (event == CHR_EVENT_BREAK)
        pc16552d_put_fifo(opaque, 0x400,1);
}
static const MemoryRegionOps pc16552d_ops = {
    .read = pc16552d_read,
    .write = pc16552d_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static const VMStateDescription vmstate_pc16552d = {
    .name = "pc16552d",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_2DARRAY(read_fifo, PC16552DState,2,16),
        VMSTATE_UINT8_ARRAY(flags, PC16552DState,2),
        // VMSTATE_UINT32(int_enabled, PC16552DState),
        // VMSTATE_UINT32(int_level, PC16552DState),
        VMSTATE_UINT8_ARRAY(read_pos, PC16552DState,2),
        VMSTATE_UINT8_ARRAY(read_count, PC16552DState,2),
        VMSTATE_UINT8_ARRAY(read_trigger, PC16552DState,2),
        VMSTATE_UINT8_ARRAY(write_count,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(mode,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(urbr,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(udlb,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(ulcr,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(udmb,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(uier,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(uiir,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(ufcr,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(umcr,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(ulsr,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(umsr,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(uscr,PC16552DState,2),
        VMSTATE_UINT8_ARRAY(udsr,PC16552DState,2),
        VMSTATE_END_OF_LIST()
    }
};

static Property pc16552d_properties[] = {
    DEFINE_PROP_CHR("chardev0", PC16552DState, chr[0]),
    DEFINE_PROP_CHR("chardev1",PC16552DState,chr[1]),
    DEFINE_PROP_END_OF_LIST(),
};

static void pc16552d_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    PC16552DState *s = PC16552D(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &pc16552d_ops, s, "PC16552D", 0x10000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    // s->read_trigger = 1;
    // s->ifl = 0x12;
    // s->cr = 0x300;
    // s->flags = 0x90;

    // s->id = PC16552D_id_arm;
}

static void pc16552d_realize(DeviceState *dev, Error **errp)
{
    PC16552DState *s = PC16552D(dev);

    if (s->chr[0]) {
        qemu_chr_add_handlers(s->chr[0], pc16552d_can_receive_1, pc16552d_receive_1,
                              pc16552d_event_1, s);
    }
    if (s->chr[1]) {
        qemu_chr_add_handlers(s->chr[1], pc16552d_can_receive_2, pc16552d_receive_2,
                              pc16552d_event_2, s);
    }
}

static void pc16552d_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pc16552d_realize;
    dc->vmsd = &vmstate_pc16552d;
    dc->props = pc16552d_properties;
}

static const TypeInfo pc16552d_info = {
    .name          = TYPE_PC16552D,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PC16552DState),
    .instance_init = pc16552d_init,
    .class_init    = pc16552d_class_init,
};

static void pc16552d_register_type(void)
{
    type_register_static(&pc16552d_info);
}

type_init(pc16552d_register_type)