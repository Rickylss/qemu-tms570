#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/ptimer.h"
#include "qemu-common.h"
#include "hw/qdev.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/bitops.h"
#include "exec/address-spaces.h"
#define TYPE_TSI107EPIC  "tsi107epic"
#define TSI107EPIC(obj)  OBJECT_CHECK(tsi107EPICState,(obj),TYPE_TSI107EPIC)
#define INTSWAP(x) ((((x)&0xff)<<24) | ((((x)>>8)& 0xff)<<16) |((((x)>>16)&0xff)<<8) |((((x)>>24)&0xff)))
#define TSI107TIEMERNUM  4
#define TSI107IRQNUM   (TSI107TIEMERNUM+5)   //4 timers and 5 irq
#define TSI107ISRMAX  TSI107IRQNUM   //I don't know the value,so ....
#define DEBUG_TSI107
#ifdef DEBUG_TSI107
#define tsi107_debug(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
#define tsi107_debug(fmt, ...)
#endif

#define TSI107_RG_BASE_COUNT  0x7fffffff

#define TSI107EPIC_GTVPR_M(x)  (x  >> 31)
#define TSI107EPIC_IVPR_M(x)  (x >> 31)
#define TSI107EPIC_SET_PENDING(index)  (s->pending | (1u << index))
#define TSI107EPIC_GET_PENDING(index)  ((s->pending >> index) & 1u)


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
*   tsi107 config space
*/
typedef struct tsi107config{
    uint32_t data;
    uint32_t eumbbar;       //:0xfc000000;
    //  ...
    // uint32_t size;
}tsi107config;

/*
* tsi107 epic Registers
*/
typedef struct tsi107EPICisr{
    bool isrflag;
    uint32_t* vpr;
}tsi107EPICisr;

typedef struct tsi107EPICState
{
	SysBusDevice parent_obj;

	MemoryRegion iomem;
    //tsi107 config space
    MemoryRegion configAddrmem;
    MemoryRegion configDatamem;
    tsi107config config;
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
  
    uint32_t pending;
    uint32_t isr;  // 
    uint32_t irr;   //0-7:vector,8-11:priority,12-15:irqpin(its source identification)
    uint32_t pctpr; //WR   indicate the relative importance of the task running on the local processor
    uint32_t iack;  //OR
    uint32_t eoi;  //OW
    bool iackflag;  //1:in iack cycle, 0:not in
}tsi107EPICState;


static inline uint32_t tsi107_get_bit(uint32_t data,int index){
    return (data >> index) & 0x1u;
}
static inline void tsi107_clear_bit(uint32_t* addr,int index){
    *addr &= ~(0x1u<<index);
}
static inline void tsi107_set_bit(uint32_t* addr,int index){
    *addr |= (0x1u << index);
}

static uint32_t tsi107epic_get_isr_priority(tsi107EPICState* s,int index){
    uint32_t res;
    if(!s->isr)
        return 0;   //第一个中断服务请求，此时isr为空
    if(!((s->isr >>index) & 0x1))
        return 0xff;    //该位没在isr中
    if(index < 4){
        res = (s->gtvpr[index] >> 16) & 0xf;
    }else{
        res = (s->ivpr[index-4] >>16) & 0xf;
    }
    return res;
}

/*
*
*function: 从pending中选出最高优先级，若大于PCTPR、ISR，则向CPU 发送INTA 信号。
*注意：在向CPU 发送INTA信号后、cpu 读IACK之前的这段时间（我的理解为IACK cycle），不能再向cpu发送INTA 信号
*/
static void tsi107epic_update_pending(tsi107EPICState* s)
{
    uint32_t irqindex=0;
    if(!s->iackflag){
        uint32_t temp = 0;
        while(irqindex < TSI107IRQNUM){
            if(TSI107EPIC_GET_PENDING(irqindex)){
                //need queue???
                if(irqindex<TSI107TIEMERNUM){
                    if(((temp >> 8)&0xfu) < ((s->gtvpr[irqindex] >> 16) & 0xf)){
                        temp = (s->gtvpr[irqindex] & 0xff) | ((s->gtvpr[irqindex] >>8) & 0xf00) | (irqindex<< 0xc);
                    }
                }else{
                    if(((temp >> 8)&0xfu) <((s->ivpr[irqindex-4] >> 16) &0xf)){
                        temp = (s->ivpr[irqindex-4] & 0xff) | ((s->ivpr[irqindex-4] >>8) & 0xf00) |(irqindex << 0xc);
                    }
                }
            }
            irqindex++ ;
        }
        int i = 0;
        for(;i<TSI107ISRMAX;i++){
            if(((s->pctpr & 0xf) < ((temp >> 8)&0xf)) && (tsi107epic_get_isr_priority(s,i) < ((temp >> 8)&0xf))){
                s->irr = temp;
                s->iack = temp & 0xff;
                tsi107_debug("call tsi107epic_update   232\n");
                qemu_set_irq(s->parent_irq,1);
                // tsi107_debug("qemu set irq level:1\n");
                s->iackflag = 1;
                break;
            }
        }
        
    }    
}

/*
*  功能1：在接受外部中断请求后，从pending中更新,选择优先级最高的中断，若符合条件，向cpu发送INTA信号。当外部信号变为negative时，转为功能2
*  功能2：a.当cpu读IACK register时，更新pending。
*/
static void tsi107epic_update(tsi107EPICState* s,int irqpin,int level){
    if(level){
        //功能1
        tsi107epic_update_pending(s);
    }else{
        //功能2
        if(TSI107EPIC_GET_PENDING(irqpin)){
            s->pending &= ~(1u<<irqpin);
            s->isr |=  1<<irqpin;
            qemu_set_irq(s->parent_irq,0);
        }
    }
}


/*
*irq[0-3]: timer[0-3]  ---->pending[0-3]
*irq[4-8]: irq[0-4]    --->pending[4-8]
*function: set pending,update IS
*/
static void tsi107epic_set_irq(void* opaque,int irq,int level){
    tsi107EPICState* s = (tsi107EPICState*)opaque;
    if(level){
        if(irq>-1 && irq < TSI107IRQNUM){
            //timer
            if((irq < 4)&&!TSI107EPIC_GTVPR_M(s->gtvpr[irq])){
                tsi107_set_bit(&s->pending,irq);
                tsi107_set_bit(s->gtvpr+irq,30);// activity bit
            }else if((irq>4 && irq<9)&&!TSI107EPIC_IVPR_M(s->ivpr[irq-4])){
                tsi107_set_bit(&s->pending,irq);
                tsi107_set_bit(s->ivpr+(irq-4),30);
            }else{
                printf("the irq:%d is masked\n",irq);
                //next to do? manual note :when mask is cleared,the irq is requested .
                //i think the signal remain positive then do it.
            }
            tsi107epic_update(s,irq,1);
        }else{
            printf("the irq:%d not supported\n",irq);
        }
    }
}

/*
*function:在isr队列中找出最高优先级的中断源。返回源身份：irqpin,使用指针target_vpr返回该源对应的vpr 寄存器地址
*若isr中无源（此时没有运行的中断服务程序）,irqpin 为0xff,target_vpr指向NULL
*/
static uint8_t tsi107epic_get_highest_priority_isr(tsi107EPICState* s,uint32_t** target_vpr)
{
    if(!s->isr){
        *target_vpr = NULL;  //isr 为空
        return 0xff;
    }
    int index = 0;
    uint8_t max_index = 0xffu;
    uint32_t temp=0;
    *target_vpr = &temp;
    for(;index<TSI107ISRMAX;index++){
        if(tsi107_get_bit(s->isr,index)){
            if(index<4){
                if((**target_vpr & 0xf0000u)< (s->gtvpr[index]&0xf0000u)){   //比较priority;
                    *target_vpr = s->gtvpr+index;   
                    max_index = index;
                }  
            }
            if(index>3){
                if((**target_vpr & 0xf0000u)< (s->ivpr[index-4]&0xf0000u)){  //比较priority;
                    *target_vpr = s->ivpr+index-4;    
                    max_index = index;                    
                }
            }
        }
    }
    return max_index;
}

/*
*function:当cpu对eoi 寄存器写操作，标志着当前中断服务程序的终结。此时需清理isr，若pending中没有该源的待处理中断请求，则清除VPR的activity位
*
*/
static void tsi107epic_write_eoi(tsi107EPICState* s){
    uint32_t * temp = NULL;
    uint32_t** target_vpr = &temp;
    uint8_t irqpin;
    irqpin = tsi107epic_get_highest_priority_isr(s,target_vpr); 
    if(irqpin<TSI107IRQNUM){
        tsi107_clear_bit(&s->isr,irqpin);
        if(!tsi107_get_bit(s->pending,irqpin))
            tsi107_clear_bit(*target_vpr,30);
    }else{
        assert(irqpin == 0xffu);
    }
    tsi107epic_update_pending(s);
}

static uint32_t tsi107EPIC_read_iack(tsi107EPICState* s){
    uint32_t res = s->iack;
    if(s->iackflag){
        uint32_t irqpin = (s->irr >> 12) & 0xf;
        if(irqpin<4){
            assert((s->gtvpr[irqpin] & 0xff) == (s->irr & 0xff));
            assert(!(s->gtvpr[irqpin]>>31));
            assert(((s->gtvpr[irqpin]>>16) &0xf) == ((s->irr >>8)&0xf));
        }else{
            assert((s->ivpr[irqpin-4] & 0xff) == (s->irr & 0xff));
            assert(!(s->ivpr[irqpin-4]>>31));
            assert(((s->ivpr[irqpin-4]>>16) &0xf) == ((s->irr >>8)&0xf));
            if(s->ivpr[irqpin-4]>>22 & 0x1u){
                // return res;
            }  
        }
        tsi107epic_update(s,irqpin,0);
        s->iackflag = 0;   
    }
    return res;
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
}


static void tsi107_write_gtbcr(tsi107EPICState* s,int8_t index,uint64_t value){
    uint32_t gtbcr = s->gtbcr[index];
    s->gtbcr[index] = value;
    //bit_31:   1 ----> 0  enable timer
    if(!(value >>31) && (gtbcr >> 31)){
        ptimer_set_limit(s->timer[index],value & TSI107_RG_BASE_COUNT,1);
        ptimer_run(s->timer[index],0);
    }else if(!(gtbcr >> 31) && (value >> 31)){
        ptimer_stop(s->timer[index]);
    }else if(!(value>>31) && !(gtbcr>>31)){
        ptimer_set_limit(s->timer[index],value & TSI107_RG_BASE_COUNT,1);
    }else{
        printf("only set base count \n");
    }
}

/*
*   whether did vector or priority change?
*/
static inline bool is_tsi107_change_vp(uint32_t vpr,uint64_t value){
    return (vpr & 0xffu)^(value & 0xffu) || (vpr >>16 & 0xfu)^(value >> 16 & 0xfu);
}

/*
*   function:对vpr寄存器进行写操作
*/
static void tsi107_write_vpr(tsi107EPICState* s,int8_t index,uint64_t value){
    if(index<4){
        //timer vpr
        if(is_tsi107_change_vp(s->gtvpr[index],value) && tsi107_get_bit(s->gtvpr[index],30)){
            error_report("The VECTOR and PRIORITY values in gtvpr should not be changed while the A bit is set");
            return;
        }
        s->gtvpr[index] = value;
    }else{
        //ivpr
        if(is_tsi107_change_vp(s->ivpr[index-4],value) && tsi107_get_bit(s->ivpr[index-4],30)){
            error_report("The VECTOR and PRIORITY values in ivpr should not be changed while the A bit is set.\n");
            return;
        }
        s->ivpr[index-4] = value;    
    }
}

inline static uint64_t tsi107_read_gtccr(tsi107EPICState* s,int8_t index){
    uint64_t res = ptimer_get_count(s->timer[index]);
    return res;
}

/*
*   当GTCCR降为0时向EPIC触发中断请求。
*/
inline static void timer0_tick_callback(void *opaque){
    tsi107epic_set_irq(opaque,0,1);
}

inline static void timer1_tick_callback(void *opaque){
    tsi107epic_set_irq(opaque,1,1);
}
inline static void timer2_tick_callback(void *opaque){
    tsi107epic_set_irq(opaque,2,1);
}
inline static void timer3_tick_callback(void *opaque){
    tsi107epic_set_irq(opaque,3,1);
}

typedef   void (*tsi107timer_tick_call)(void*);
static tsi107timer_tick_call  timer_tick_callback[TSI107TIEMERNUM]={
    timer0_tick_callback,
    timer1_tick_callback,
    timer2_tick_callback,
    timer3_tick_callback
};



static void tsi107EPIC_write(void *opaque, hwaddr offset, uint64_t val,unsigned int size){
    uint64_t value = INTSWAP(val);
    tsi107EPICState* s = opaque;
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
            break;
        case EICR:
            s->eicr = value;
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
            break;
        case GTBCR1:
            tsi107_write_gtbcr(s,1,value);
            break;
        case GTBCR2:
            tsi107_write_gtbcr(s,2,value);
            break;
        case GTBCR3:
            tsi107_write_gtbcr(s,3,value);
            break;
        case GTVPR0:
            tsi107_write_vpr(s,0,value);
            break;
        case GTVPR1:
            tsi107_write_vpr(s,1,value);
            break;
        case GTVPR2:
            tsi107_write_vpr(s,2,value);
            break;
        case GTVPR3:
            tsi107_write_vpr(s,3,value);
            break;
        case IVPR0:
            tsi107_write_vpr(s,4,value);
            break;       
        case IVPR1:
            tsi107_write_vpr(s,5,value);
            break;
        case IVPR2:
            tsi107_write_vpr(s,6,value);
            break;
        case IVPR3:
            tsi107_write_vpr(s,7,value);
            break;
        case IVPR4:
            tsi107_write_vpr(s,8,value);
            break;
        case PCTPR:
            s->pctpr = value;
            break;
        case EOI:         
            tsi107epic_write_eoi(s);
            s->eoi = 0;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,"%s: Bad offset %"HWADDR_PRIx"\n",__func__, offset);
            break;
    }
}




static uint64_t tsi107EPIC_read(void *opaque, hwaddr offset,unsigned int size)
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
            res = tsi107_read_gtccr(s,1);
            break;
        case GTCCR2:
            res = tsi107_read_gtccr(s,2);
            break;
        case GTCCR3:
            res = tsi107_read_gtccr(s,3);
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
        case IVPR0:
            res = s->ivpr[0];
            break;
        case IVPR1:
            res = s->ivpr[1];
            break;
        case IVPR2:
            res = s->ivpr[2];
            break;
        case IVPR3:
            res = s->ivpr[3];
            break;
        case IVPR4:
            res = s->ivpr[4];
            break;
        case PCTPR:
            res = s->pctpr;
            break;
        case IACK:
            res = tsi107EPIC_read_iack(s);
            break;
        case EOI:
            res = 0;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,"%s: Bad offset %"HWADDR_PRIx"\n",__func__, offset);
            break;
    }
    uint64_t rest = INTSWAP(res);
    return rest;
}

static const MemoryRegionOps tsi107epic_ops = {
    .read       = tsi107EPIC_read,
    .write      = tsi107EPIC_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_tsi107_config = {
    .name = "tsi107config",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]){
        VMSTATE_UINT32(eumbbar,tsi107config),
        VMSTATE_UINT32(data, tsi107config),   
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_tsi107_epic = {
        .name = TYPE_TSI107EPIC,
        .version_id = 1,
        .minimum_version_id = 1,
        .fields = (VMStateField[]){
            VMSTATE_STRUCT(config,tsi107EPICState,1,vmstate_tsi107_config,tsi107config),
            VMSTATE_UINT32(frr,tsi107EPICState),
            VMSTATE_UINT32(gcr, tsi107EPICState),
            VMSTATE_UINT32(eicr, tsi107EPICState),
            VMSTATE_UINT32(evi,tsi107EPICState),
            VMSTATE_UINT32(pi,tsi107EPICState),
            VMSTATE_UINT32(svr, tsi107EPICState),
            VMSTATE_UINT32(tfrr, tsi107EPICState),
            VMSTATE_UINT32_ARRAY(gtccr,tsi107EPICState,4),
            VMSTATE_UINT32_ARRAY(gtbcr,tsi107EPICState,4),
            VMSTATE_UINT32_ARRAY(gtvpr,tsi107EPICState,4),
            VMSTATE_UINT32_ARRAY(gtdr,tsi107EPICState,4),
            VMSTATE_UINT32_ARRAY(ivpr,tsi107EPICState,5),
            VMSTATE_UINT32_ARRAY(idr,tsi107EPICState,5),
            VMSTATE_UINT32(pending,tsi107EPICState),
            VMSTATE_UINT32(isr,tsi107EPICState),
            VMSTATE_UINT32(irr,tsi107EPICState),
            VMSTATE_UINT32(pctpr,tsi107EPICState),
            VMSTATE_UINT32(iack,tsi107EPICState),
            VMSTATE_UINT32(eoi,tsi107EPICState),
            VMSTATE_BOOL(iackflag,tsi107EPICState),
            VMSTATE_PTIMER_ARRAY(timer,tsi107EPICState,4),
            VMSTATE_END_OF_LIST()
        }
};

static void tsi107EPIC_class_init(ObjectClass* klass,void* data){
    DeviceClass* dc = DEVICE_CLASS(klass);
    dc->reset = tsi107EPIC_reset;
    dc->vmsd = &vmstate_tsi107_epic;
}

/*
*   function:外设向tsi107 epic 发送中断请求信号
*/
static void tsi107epic_set_external_irq(void* opaque,int irq,int level){
    tsi107epic_set_irq(opaque,irq+4,level);
}



static uint64_t tsi107config_addr_read(void *opaque, hwaddr offset,unsigned int size)
{
    tsi107_debug("tsi107config  addr read   offset:%lx\n",offset);
    return 1;
}

inline static void  tsi107config_data_select(tsi107EPICState* s,uint64_t val)
{ 
    if((val & 0xffu) == 0x78u){
        tsi107_debug("1     eumbbar:%x\n",s->config.eumbbar);
        s->config.data = s->config.eumbbar;
        // tsi107_debug()
    }
}

static void tsi107config_addr_write(void *opaque, hwaddr offset, uint64_t val,unsigned int size)
{   
    uint64_t val1 = INTSWAP(val);
    tsi107EPICState* s = opaque;
    switch(offset){
        case 0:
            tsi107config_data_select(s,val1);
            break;
        default:
            error_report("tsi107config addr write val:%lx   offset:%lx\n",val,offset);
            break;
    }
}

static const MemoryRegionOps tsi107_config_addr_ops = {
    .read       = tsi107config_addr_read,
    .write      = tsi107config_addr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};
static uint64_t tsi107config_data_read(void* opaque,hwaddr offset,unsigned int size)
{
    uint64_t res;
    tsi107EPICState* s = opaque;
    switch(offset){
        case 0:
            res = s->config.data;
            break;
        default:
            error_report("tsi107config data offset:%lx\n",offset);
            break;
    }
    return INTSWAP(res);
}
static void tsi107config_data_write(void* opaque,hwaddr offset,uint64_t val,unsigned int size)
{
    tsi107_debug("tsi107config data write val:%lx   offset:%lx\n",val,offset);
}
static const MemoryRegionOps tsi107_config_data_ops ={
    .read       = tsi107config_data_read,
    .write      = tsi107config_data_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void tsi107epic_init(Object* obj){
    DeviceState *dev = DEVICE(obj);
    tsi107EPICState *s = TSI107EPIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MemoryRegion *address_space_mem = get_system_memory();
    memory_region_init_io(&s->iomem,obj,&tsi107epic_ops,s,"tsi107epic",0x200000);
    sysbus_init_mmio(sbd,&s->iomem);
    memory_region_init_io(&s->configAddrmem,obj,&tsi107_config_addr_ops,s,"tsi107ConfigAddr",0x1FFFFF);
    memory_region_add_subregion(address_space_mem, 0xfec00000, &s->configAddrmem);                          //0xFEC0 0000 ------ 0xFEDF FFFF:   CONFIG_ADDR  
    memory_region_init_io(&s->configDatamem,obj,&tsi107_config_data_ops,s,"tsi107ConfigData",0xFFFFF);      //0xFEE0 0000 ------ 0xFEEF FFFF:   CONFIG_DATA
    memory_region_add_subregion(address_space_mem,0xfee00000,&s->configDatamem);
    qdev_init_gpio_in(dev,tsi107epic_set_external_irq,5);                           
    sysbus_init_irq(sbd,&s->parent_irq);

    //timer init
    QEMUBH *bh[TSI107TIEMERNUM]; 
    int i = 0;
    for(i=0;i<TSI107TIEMERNUM;i++){
        bh[i] = qemu_bh_new(timer_tick_callback[i], s);
        s->timer[i] = ptimer_init(bh[i]);
    // set freq statically,
        ptimer_set_freq(s->timer[i], 1000000);
    }
    //配置空间固化？
    s->config.eumbbar = 0xfc000000;
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