#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "qemu-common.h"
#include "hw/qdev.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#define TYPE_TSI107_HOSTBRIDGE  "tsi107hostbridge"
#define TYPE_TSI107GTR "tsi107GlobalandTimeRegister"
#define TYPE_TSI107EPIC  "tsi107epic"
#define TSI107EPIC(obj)  OBJECT_CHECK(tsi107EPICState,(obj),TYPE_TSI107EPIC)
#define TSI107TIEMERNUM  4
#define DEBUG_TSI107
#ifdef DEBUG_TSI107
#define tsi107_debug(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
#define tsi107_debug(fmt, ...)
#endif

#define TSI107_RG_BASE_COUNT  0x7fffffff
/*
*   EPIC Register Address Map
*   
*/
enum TSI107EPIC_RG{
    /*
    *   Global EPIC register map
    */
    FRR     =   0X41000,    // Feature reporting register               NIRQ, NCPU, VID
    //  0X41010     Reserved
    GCR     =   0X41020,    //Global configuration register             R (reset), M (mode)
    EICR    =   0X41030,    // EPIC interrupt configuration register    R (clock ratio), SIE
    //0X41040-0X41070   Reserved
    EVI     =   0X41080,    // EPIC vendor identification register      STEP, DEVICE_ID, VENDOR_ID
    PI      =   0X41090,    //Processor initialization register         P0
    //0X410A0-0X410D0   Reserved
    SVR     =   0X410E0,    //Spurious vector register                  VECTOR 
    TFRR    =   0X410F0,    //Timer frequency reporting registe         TIMER_FREQ

    /*
    *   Global timer register map       four timers
    */
    GTCCR0  =   0x41100,    //Global timer 0 current count register     T (toggle), COUNT
    GTBCR0  =   0x41110,    //Global timer 0 base count registe         CI, BASE_COUNT
    GTVPR0  =   0x41120,    //Global timer 0 vector/priority register   M, A, PRIORITY, VECTOR
    GTDR0   =   0x41130,    //Global timer 0 destination register       P0

    GTCCR1  =   0x41140,    //Global timer 1 current count register     T (toggle), COUNT
    GTBCR1  =   0x41150,    //Global timer 1 base count registe         CI, BASE_COUNT
    GTVPR1  =   0x41160,    //Global timer 1 vector/priority register   M, A, PRIORITY, VECTOR
    GTDR1   =   0x41170,    //Global timer 1 destination register       P0

    GTCCR2  =   0x41180,    //Global timer 2 current count register     T (toggle), COUNT
    GTBCR2  =   0x41190,    //Global timer 2 base count registe         CI, BASE_COUNT
    GTVPR2  =   0x411A0,    //Global timer 2 vector/priority register   M, A, PRIORITY, VECTOR
    GTDR2   =   0x411B0,    //Global timer 2 destination register       P0
    
    GTCCR3  =   0x411C0,    //Global timer 1 current count register     T (toggle), COUNT
    GTBCR3  =   0x411D0,    //Global timer 1 base count registe         CI, BASE_COUNT
    GTVPR3  =   0x411E0,    //Global timer 1 vector/priority register   M, A, PRIORITY, VECTOR
    GTDR3   =   0x411F0,    //Global timer 1 destination register       P0
    //0X41200-0X501F0   Reserved

    /*
    *   Interrupt Source Configuration Registers--direct interrupt registers(four)
    */
   IVPR0    =   0x50200,    //IRQ0 vector/priority register             M, A, P, S, PRIORITY, VECTOR
   IDR0     =   0x50210,    //IRQ0 destination register                 P0
   IVPR1    =   0x50220,    //IRQ1 vector/priority register             M, A, P, S, PRIORITY, VECTOR
   IDR1     =   0x50230,    //IRQ1 destination register                 P0
   IVPR2    =   0x50240,    //IRQ2 vector/priority register             M, A, P, S, PRIORITY, VECTOR
   IDR2     =   0x50250,    //IRQ2 destination register                 P0
   IVPR3    =   0x50260,    //IRQ3 vector/priority register             M, A, P, S, PRIORITY, VECTOR
   IDR3     =   0x50270,    //IRQ3 destination register                 P0
   IVPR4    =   0x50280,    //IRQ4 vector/priority register             M, A, P, S, PRIORITY, VECTOR
   IDR4     =   0x50290,    //IRQ4 destination register                 P0

    /*
    *   Interrupt Source Configuration Registers-- serial interrupt registers(sexteen) 
    */
   SVPR0    =   0x50200,     //Serial interrupt 0 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR0     =   0x50210,     //Serial interrupt 0 destination register       P0
   SVPR1    =   0x50220,     //Serial interrupt 1 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR1     =   0x50230,     //Serial interrupt 1 destination register       P0
   SVPR2    =   0x50240,     //Serial interrupt 2 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR2     =   0x50250,     //Serial interrupt 2 destination register       P0
   SVPR3    =   0x50260,     //Serial interrupt 3 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR3     =   0x50270,     //Serial interrupt 3 destination register       P0
   SVPR4    =   0x50280,     //Serial interrupt 4 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR4     =   0x50290,     //Serial interrupt 4 destination register       P0
   SVPR5    =   0x502A0,     //Serial interrupt 5 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR5     =   0x502B0,     //Serial interrupt 5 destination register       P0
   SVPR6    =   0x502C0,     //Serial interrupt 6 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR6     =   0x502D0,     //Serial interrupt 6 destination register       P0
   SVPR7    =   0x502E0,     //Serial interrupt 7 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR7     =   0x502F0,     //Serial interrupt 7 destination register       P0
   SVPR8    =   0x50300,     //Serial interrupt 8 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR8     =   0x50310,     //Serial interrupt 8 destination register       P0
   SVPR9    =   0x50320,     //Serial interrupt 9 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR9     =   0x50330,     //Serial interrupt 9 destination register       P0
   SVPR10    =   0x50340,     //Serial interrupt 10 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR10     =   0x50350,     //Serial interrupt 10 destination register       P0
   SVPR11    =   0x50360,     //Serial interrupt 11 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR11     =   0x50370,     //Serial interrupt 11 destination register       P0
   SVPR12    =   0x50380,     //Serial interrupt 12 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR12     =   0x50390,     //Serial interrupt 12 destination register       P0
   SVPR13    =   0x503A0,     //Serial interrupt 13 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR13     =   0x503B0,     //Serial interrupt 13 destination register       P0
   SVPR14    =   0x503C0,     //Serial interrupt 14 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR14     =   0x503D0,    //Serial interrupt 14 destination register       P0
   SVPR15    =   0x503E0,     //Serial interrupt 15 vector/priority register   M, A, P, S, PRIORITY, VECTOR
   SDR15     =   0x503F0,     //Serial interrupt 15 destination register       P0
    //0X50400--0X51010  Reserved
   IIVPR0    =  0x51020,    //I2C interrupt vector/priority register           M, A, PRIORITY, VECTOR
   IIDR0     =  0X51030,    //I2C interrupt destination register                P0
   IIVPR1    =  0x51040,    
   IIDR1     =  0X51050,
   IIVPR2    =  0x51060,    
   IIDR2     =  0X51070,
   //0x51080--0x510B0
   IIVPR3    =  0x510C0,    
   IIDR3     =  0X510D0,
   //0X510E0--0X5FFF0

   /*
   *    EPIC Register Address Map—Processor-Related Registers
   */
  PCTPR     =   0x60080,     //Processor current task priority register         TASKP
  IACK      =   0x600A0,     // Processor interrupt acknowledge register        VECTOR
  EOI       =   0X600B0,      //Processor end-of-interrupt register              EOI_CODE
};


/*
* tsi107 epic Registers
*/
typedef struct tsi107EPICState
{
	SysBusDevice parent_obj;

	MemoryRegion iomem;
    
    qemu_irq   parent_irq;
    ptimer_state *timer[TSI107TIEMERNUM];
    uint32_t frr;           //OR
    uint32_t gcr;           //WR
    uint32_t eicr;          //WR
    uint32_t evi;           //OR
    uint32_t pi;            //WR
    uint32_t svr;           //WR
    
    uint32_t tfrr;          //WR
    
    uint32_t gtccr[4];        //OR
    uint32_t gtbcr[4];        //WR
    uint32_t gtvpr[4];        //WR
    uint32_t gtdr[4];         //OR
    
    //ivprx:WR       idrx:OR
    uint32_t ivpr[5];     
    uint32_t idr[5];
  

    uint32_t pctpr; //WR   indicate the relative importance of the task running on the local processor
    uint32_t iack;  //OR
    uint32_t eoi;  //OW
}tsi107EPICState;

static void tsi107epic_upadate(tsi107EPICState* s){

}

static void tsi107epic_set_irq(void* opaque,int irq,int level){
    tsi107EPICState* s = (tsi107EPICState*)opaque;

    tsi107epic_upadate(s);
}
/*
*   when board reset,tsi107epic reset value
*/
static void tsi107EPIC_reset(DeviceState *d)
{
    tsi107EPICState* s = TSI107EPIC(d);
    s->frr = (0x17<<16) | 0x02;
    s->gcr = 0;
    s->eicr = 0x4<<28;
    s->evi = 0x01<<16;
    s->pi = 0;
    s->svr = 0xff;
    s->tfrr = 0;
    //reset timers
    int i = 0;
    for(i=0;i<TSI107TIEMERNUM;i++){
        s->gtccr[i] = 0;
        s->gtbcr[i] = 1<<31;
        s->gtvpr[i] = 1<<31;
        s->gtdr[i] = 1;

    }
    printf("tsi107epic reset\n");
}



/*guideline         interrupt and timer initialization,reading the inerrupt acknowledge register(IACK)
*   exceptions: 
*   1、 EOI register,which returns zeros on reads
*   2、 Activity bit (A) of the vector/priority registers, which returns the value according to
*     the status of the current interrupt source

*/
// static void tsi107timer_update(void* opaque){

// }
// extern static uint64_t tsi107_read_gtccr(tsi107EPICState,int8_t index);
/*
*   index :timer index
*/
static void tsi107_write_gtbcr(tsi107EPICState* s,int8_t index,uint64_t value){
    uint32_t gtbcr = s->gtbcr[index];
    s->gtbcr[index] = value;
    //bit_31:   1 ----> 0  enable timer
    if(!(value >>31) && (gtbcr >> 31)){
        // ptimer_set_count(s->timer[index],value & TSI107_RG_BASE_COUNT);
        // ptimer_set_period();
        tsi107_debug("timer run\n");
        // ptimer_set_freq(s->timer[index],32768/16);
        ptimer_set_limit(s->timer[index],value & TSI107_RG_BASE_COUNT,1);
        ptimer_run(s->timer[index],0);
    }else if(!(gtbcr >> 31) && (value >> 31)){
        ptimer_stop(s->timer[index]);
    }else{
        printf("not supported \n");
    }
    tsi107_debug("tsi107 write gtbcr[%d]  value0:%x  value1:%lx\n",index,gtbcr,value);
}

static uint64_t tsi107_read_gtccr(tsi107EPICState* s,int8_t index){
    uint64_t res = ptimer_get_count(s->timer[index]);
    tsi107_debug("tsi107 read gtccr[%d] res:%lx\n",index,res);
    return res;
}
static void timer0_tick_callback(void *opaque){
    tsi107EPICState* s = opaque;

    tsi107_debug("timer0 tick callback \n");
    tsi107_read_gtccr(s,0);
    static int8_t flag;
    if(!flag){
        flag = 1;
        tsi107EPICState* s = opaque;
        qemu_set_irq(s->parent_irq,1);
        qemu_set_irq(s->parent_irq,0);
    }
}

static void timer1_tick_callback(void *opaque){
    tsi107_debug("timer1 tick callback\n");
}
static void timer2_tick_callback(void *opaque){
    tsi107_debug("timer2 tick callback\n");
}
static void timer3_tick_callback(void *opaque){
    tsi107_debug("timer3 tick callback\n");
}
typedef   void (*tsi107timer_tick_call)(void*);
static tsi107timer_tick_call  timer_tick_callback[TSI107TIEMERNUM]={
    timer0_tick_callback,
    timer1_tick_callback,
    timer2_tick_callback,
    timer3_tick_callback
};



static void tsi107EPIC_write(void *opaque, hwaddr offset, uint64_t value,unsigned size){
    tsi107EPICState* s = opaque;
    tsi107_debug("tsi107 epic write offset:"TARGET_FMT_plx"  res:%lx\n",offset,value);
    switch (offset)
    {
        case GCR:/* constant-expression */
            /* code */
            s->gcr = value;
            if(value & (1<<31)){
                //all pending and in-service interrupts are cleared
                //all interrupt mask bits are set
                //All timer base count values are reset to zero and count inhibited.
                //The processor current task priority is reset to 0xF thus disabling interrupt delivery to the processor.
                //Spurious vector resets to 0xFF.
                s->pi = 0xff;
                //EPIC defaults to pass-through mode.
                //The serial clock ratio resets to 0x4.

                s->gcr |= ~(1<<31);
            }
            if(value & (1<<29)){ //M
                //mixed-mode
            }else{
                //pass-through mode 
            }
            break;
        case EICR:
            s->eicr = value;
            if(s->gcr << 29){
                if(value & (1<<27)){
                    //serial interrupts mode 
                }else{
                    //direct interrupts mode 
                }
            }
            break;
        // case EVI:  OR
        case PI:
            s->pi = value;
            if(value & 1){
                //causing soft reset exception
            }
            break;
        case SVR:       
            s->svr = value;
            break;
        case TFRR:
            s->tfrr = value;  //software set this ,telling timer Hz to people
            break;
        case GTBCR0:
            tsi107_write_gtbcr(s,0,value);
            // s->gtbcr[0] = value;
            // if(value & (1<<31)){
            //     //inhibits counting for this timer
            // }else{
            //     //enable temer
            //     s->gtccr[0] = value;
            // }
            break;
        case GTBCR1:
            s->gtbcr[1] = value;
            if(value & (1<<31)){
                //inhibits counting for this timer
            }else{
                //enable temer
                //copy to current count register
                s->gtccr[1] = value;
            }
            break;
        case GTBCR2:
            s->gtbcr[2] = value;
            if(value & (1<<31)){
                //inhibits counting for this timer
            }else{
                //enable temer
                s->gtccr[2] = value;
            }
            break;
        case GTBCR3:
            s->gtbcr[3] = value;
            if(value & (1<<31)){
                //inhibits counting for this timer
            }else{
                //enable temer
                s->gtccr[3] = value;
            }
            break;
        case GTVPR0:
            // s->gtvpr0 |= (value & ~(1 << 30));  //30 bit is read-only;
            if(value & (1<<31)){  //M
                //further interrupts from this timer are disabled

            }else{
                //If the mask bit is cleared while the corresponding IPR bit is set, INT is asserted to the processor 
                s->gtvpr[0] |= ~(1<<31);
            }
            if(!(value & (1<<30))){ //A 
                // this bit is read-only;
                if((s->gtvpr[0] ^ value) & 0xf00ff){
                    //The VECTOR and PRIORITY values should not be changed while the A bit is set.
                    s->gtvpr[0] |= value & 0xf00ff;
                }
            }
            break;
        case GTVPR1:
            // s->gtvpr0 |= (value & ~(1 << 30));  //30 bit is read-only;
            if(value & (1<<31)){  //M
                //further interrupts from this timer are disabled

            }else{
                //If the mask bit is cleared while the corresponding IPR bit is set, INT is asserted to the processor 
                s->gtvpr[1] |= ~(1<<31);
            }
            if(!(value & (1<<30))){ //A 
                // this bit is read-only;
                if((s->gtvpr[1] ^ value) & 0xf00ff){
                    //The VECTOR and PRIORITY values should not be changed while the A bit is set.
                    s->gtvpr[1] |= value & 0xf00ff;
                }
            }
            break;
        case GTVPR2:
            // s->gtvpr0 |= (value & ~(1 << 30));  //30 bit is read-only;
            if(value & (1<<31)){  //M
                //further interrupts from this timer are disabled

            }else{
                //If the mask bit is cleared while the corresponding IPR bit is set, INT is asserted to the processor 
                s->gtvpr[2] |= ~(1<<31);
            }
            if(!(value & (1<<30))){ //A 
                // this bit is read-only;
                if((s->gtvpr[2] ^ value) & 0xf00ff){
                    //The VECTOR and PRIORITY values should not be changed while the A bit is set.
                    s->gtvpr[2] |= value & 0xf00ff;
                }
            }
            break;
        case GTVPR3:
            // s->gtvpr0 |= (value & ~(1 << 30));  //30 bit is read-only;
            if(value & (1<<31)){  //M
                //further interrupts from this timer are disabled

            }else{
                //If the mask bit is cleared while the corresponding IPR bit is set, INT is asserted to the processor 
                s->gtvpr[3] |= ~(1<<31);
            }
            if(!(value & (1<<30))){ //A 
                // this bit is read-only;
                if((s->gtvpr[3] ^ value) & 0xf00ff){
                    //The VECTOR and PRIORITY values should not be changed while the A bit is set.
                    s->gtvpr[3] |= value & 0xf00ff;
                }
            }
            break;
        case IVPR0:
            // if(value )
            break;
        

        case PCTPR:
            s->pctpr = value;
            break;
        case EOI:
            s->eoi = 0;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,"%s: Bad offset %"HWADDR_PRIx"\n",__func__, offset);
            break;
    }
}

static uint64_t tsi107EPIC_read(void *opaque, hwaddr offset,unsigned size)
{
    
    tsi107EPICState* s = opaque;
    uint64_t res = 0;
    switch (offset)
    {
        case FRR:
            /* code */
            res = s->frr;
            break;
        case GCR:
            res = s->gcr;
            break;
        case EICR:
            res = s->eicr;
            break;
        case EVI:
            res = s->evi;
            break;
        case PI:
            res = s->pi;
            break;
        case SVR:
            res = s->svr;
            break;
        case TFRR:
            res = s->tfrr;
            break;
        case GTCCR0:
            res = tsi107_read_gtccr(s,0);
            break;
        case GTCCR1:
            res = s->gtccr[1];
            break;
        case GTCCR2:
            res = s->gtccr[2];
            break;
        case GTCCR3:
            res = s->gtccr[3];
            break;
        case GTBCR0:
            res = s->gtbcr[0];
            break;
        case GTBCR1:
            res = s->gtbcr[1];
            break;
        case GTBCR2:
            res = s->gtbcr[2];
            break;
        case GTBCR3:
            res = s->gtbcr[3];
            break;
        case GTVPR0:
            res = s->gtvpr[0];
            break;
        case GTVPR1:
            res = s->gtvpr[1];
            break;
        case GTVPR2:
            res = s->gtvpr[2];
            break;
        case GTVPR3:
            res = s->gtvpr[3];
            break;
        case GTDR0:
            res = s->gtdr[0];
            break;
        case GTDR1:
            res = s->gtdr[1];
            break;
        case GTDR2:
            res = s->gtdr[2];
            break;
        case GTDR3:
            res = s->gtdr[3];
            break;
        case PCTPR:
            res = s->pctpr;
            break;
        case IACK:
            res = s->iack;
            break;
        case EOI:
            res = 0;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,"%s: Bad offset %"HWADDR_PRIx"\n",__func__, offset);
            break;
    }
    tsi107_debug("tsi107 epic read offset:"TARGET_FMT_plx"  res:%lx\n",offset,res);
    return res;
}

static const MemoryRegionOps tsi107epic_ops = {
    .read       = tsi107EPIC_read,
    .write      = tsi107EPIC_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_tsi107_epic = {
        .name = TYPE_TSI107EPIC,
        .version_id = 1,
        .minimum_version_id = 1,
        .fields = (VMStateField[]){
            VMSTATE_UINT32(gcr, tsi107EPICState),
            VMSTATE_UINT32(eicr, tsi107EPICState),
            VMSTATE_UINT32(svr, tsi107EPICState),
            VMSTATE_UINT32(tfrr, tsi107EPICState),
            // VMSTATE_PTIMER(timer, SAM9263EPICState),
            VMSTATE_END_OF_LIST()
        }
};

static void tsi107EPIC_class_init(ObjectClass* klass,void* data){
    DeviceClass* dc = DEVICE_CLASS(klass);
    dc->reset = tsi107EPIC_reset;
    dc->vmsd = &vmstate_tsi107_epic;
}

static void tsi107epic_init(Object* obj){
    DeviceState *dev = DEVICE(obj);
    tsi107EPICState *s = TSI107EPIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    memory_region_init_io(&s->iomem,obj,&tsi107epic_ops,s,"tsi107epic",0x200000);
    sysbus_init_mmio(sbd,&s->iomem);
    qdev_init_gpio_in(dev,tsi107epic_set_irq,5);
    sysbus_init_irq(sbd,&s->parent_irq);

    //timer init
    QEMUBH *bh[TSI107TIEMERNUM]; // mysterious
    int i = 0;
    for(i=0;i<TSI107TIEMERNUM;i++){
        bh[i] = qemu_bh_new(timer_tick_callback[i], s); // magic
        s->timer[i] = ptimer_init(bh[i]);
    // set freq statically,
    // value is according to arm_timer
        ptimer_set_freq(s->timer[i], 1000000);
    }
   printf("pit realized called \n");

//    tsi107EPIC_reset(dev);
//    vmstate_register(&sbd->qdev,-1,&vmstate_tsi107_epic,s);
}
static const TypeInfo tsi107EPIC_info = {
    .name           =   TYPE_TSI107EPIC,
    .parent         =   TYPE_SYS_BUS_DEVICE,
    .instance_size  =   sizeof(tsi107EPICState),
    .instance_init  =   tsi107epic_init,
    .class_init     =   tsi107EPIC_class_init, 
};
static void tsi107epic_register_types(void){
    type_register_static(&tsi107EPIC_info);
}

type_init(tsi107epic_register_types)