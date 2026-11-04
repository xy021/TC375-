/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#ifndef CAN1_H_
#define CAN1_H_

#include "Ifx_Types.h"
#include "IfxCan_Can.h"

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
/* 板载 CAN1 通道。板子上丝印 / CANoe 里叫 CAN1，MCU 里面是 CAN00 这个内核：
 *
 *   控制器   MODULE_CAN0，节点 IfxCan_NodeId_0（节点基地址 0xF0200000 + 0x8100）
 *   收发器   SBC U104 TLE9471-3ES 内部的那一路，不是外挂芯片
 *   引脚     P33.8 = CAN00_TXD（输出，MCU -> SBC 的 TXDCAN）
 *            P33.7 = CAN00_RXDE（输入，SBC 的 RXD -> MCU）
 *            引脚复用配置在 ALT.h 的 ALT_getCan1Pins() 里
 *
 * 和 CAN2 对照着看（CAN2 已经调通，参数就是照它抄的）：
 *
 *   CAN2（板载丝印）  MODULE_CAN1 + IfxCan_NodeId_3   P33.4 / P33.5   外挂 TCAN1044
 *   CAN1（本文件）    MODULE_CAN0 + IfxCan_NodeId_0   P33.8 / P33.7   SBC 内部
 *
 * 两边总线参数相同：仲裁段 500 kbit/s、数据段 2 Mbit/s、采样点 80%、SJW 15%、TDC 0。                  */
#define CAN1_NODE_ID                  IfxCan_NodeId_0
#define CAN1_ARB_BAUDRATE             500000u
#define CAN1_DATA_BAUDRATE            2000000u
#define CAN1_SAMPLE_POINT             8000u                                          /* 80.00 %                        */
#define CAN1_SYNC_JUMP_WIDTH          1500u                                          /* 15.00 %                        */
#define CAN1_TDC_OFFSET               0u

/* 总线环回。CAN2 调试时用过一次 1（不接总线也能收到自己发的帧），之后又恢复成 0。
 * 正常跑车上的总线时必须是 0：环回模式下 TX 引脚不出波形，报文只回到自己的接收路径里，
 * CANoe 一条都看不到。                                                                                  */
#define CAN1_BUS_LOOPBACK            0

/* CAN1 接收中断优先级。工程里已经占掉的：20 = RUN_LED，21 = CAN2 接收，22 = CAN2 自动发送定时。
 * 这里用 23，对应向量表槽位 0x17。                                                                     */
#define CAN1_RX_ISR_PRIORITY          23

/* 数据场按 64 字节 / 16 个字算。发送缓冲区和接收 FIFO0 的元素都按 64 字节数据配置，
 * 这样经典帧（<= 8 字节）和 CAN FD 长帧共用同一套元素尺寸。                                            */
#define CAN1_DATA_WORDS               16u
#define CAN1_DATA_BYTES               64u

/* 消息 RAM 的划分，单位是字节偏移（相对 CAN00 的消息 RAM 基地址 0xF0200000）。
 * 顺序和 CAN2 一样，只是 CAN1 的标准滤波表放了 10 条，所以后面的地址跟着顺延：
 *   0x0000  标准滤波表   10 条 x 4 字节 = 40 字节，到 0x0028
 *   0x0040  扩展滤波表   本通道不用
 *   0x0100  接收 FIFO0    16 个元素 x 72 字节（8 字节头 + 64 字节数据）
 *   0x0800  接收 FIFO1    不用
 *   0x0900  专用接收缓冲区 不用
 *   0x0A00  发送事件 FIFO 不用
 *   0x0B00  发送缓冲区   2 个元素 x 72 字节（8 字节头 + 64 字节数据）
 *
 * 注意：往这些地址写配置字必须用 32 位整字写。字节写 / 半字写会被静默丢弃（读回来还是旧值，
 * 不报总线错误、不置标志），CAN2 就是栽在这上面。具体处理见 can1.c 和 Can2/can.h。                    */
#define CAN1_RAM_STD_FILTER           0x0000u
#define CAN1_RAM_EXT_FILTER           0x0040u
#define CAN1_RAM_RX_FIFO0             0x0100u
#define CAN1_RAM_RX_FIFO1             0x0800u
#define CAN1_RAM_RX_BUFFERS           0x0900u
#define CAN1_RAM_TX_EVENT_FIFO        0x0A00u
#define CAN1_RAM_TX_BUFFERS           0x0B00u

#define CAN1_RX_FIFO0_SIZE            16u
#define CAN1_TX_BUFFER_COUNT          2u
#define CAN1_TX_BUFFER                IfxCan_TxBufferId_0

/*********************************************************************************************************************/
/*-------------------------------------------------Message IDs-------------------------------------------------------*/
/*********************************************************************************************************************/
/* 接收（PC -> MCU）要过滤波的 10 个标准帧 ID。没列进来的 ID 一律丢弃（非匹配帧配置成 reject），
 * 所以想让 MCU 多收一个 ID，就往这张表里加一条，并把 CAN1_RX_FILTER_COUNT 改对。                       */
#define CAN1_RX_FILTER_COUNT          10u
#define CAN1_RX_ID_01                 0x505u
#define CAN1_RX_ID_02                 0x7FFu
#define CAN1_RX_ID_03                 0x741u
#define CAN1_RX_ID_04                 0x055u
#define CAN1_RX_ID_05                 0x153u
#define CAN1_RX_ID_06                 0x495u
#define CAN1_RX_ID_07                 0x066u
#define CAN1_RX_ID_08                 0x225u
#define CAN1_RX_ID_09                 0x217u
#define CAN1_RX_ID_10                 0x065u

/* 发送（MCU -> PC）的 7 个标准帧 ID。自动发送按这张表的顺序轮着发。
 * 0x055 同时出现在收和发两张表里，这不冲突：收的滤波表只管收，MCU 自己发出去的帧不会回到
 * 自己的接收 FIFO（正常模式下 MCMCAN 不做自发自收）。                                                  */
#define CAN1_TX_ID_COUNT              7u
#define CAN1_TX_ID_01                 0x589u   /* SumZCU CANFD3Fr01 */
#define CAN1_TX_ID_02                 0x641u
#define CAN1_TX_ID_03                 0x055u   /* SumZCU CANFD3Fr01 */
#define CAN1_TX_ID_04                 0x1C0u
#define CAN1_TX_ID_05                 0x210u
#define CAN1_TX_ID_06                 0x525u
#define CAN1_TX_ID_07                 0x565u

/* 自动发送周期，毫秒。initCan1() 里换算成 STM0 的 tick 数存进 g_can1TxAutoPeriodTicks。
 * 不是独立的定时器中断，是在 can1Service() 里比 STM0 计数器，主循环约 200 us 一圈，抖动 <= 0.2 ms。 */
#define CAN1_AUTO_TX_PERIOD_MS        20u

/*********************************************************************************************************************/
/*-----------------------------------------------Debugger variables--------------------------------------------------*/
/*********************************************************************************************************************/
/* 调试器里直接看 / 直接改的全局变量。都是普通全局变量，不是宏，也不是位域打包，按网表 / 报文的名字起名。
 * 改 g_can1TxId 这类变量不需要重新编译，主循环下一圈就用新值。                                          */

/* --- 发送通道 ---------------------------------------------------------------------------------------- */
extern volatile uint32 g_can1TxRequest;      /* 写 1 = 请求发一帧（can1Service 发完会自己清 0）          */
extern volatile uint32 g_can1TxId;           /* 发的 ID，默认 0x589，自动发送时按表轮换                    */
extern volatile uint32 g_can1TxExtended;     /* 0 = 标准帧，1 = 扩展帧                                     */
extern volatile uint32 g_can1TxFd;           /* 0 = 经典帧，1 = CAN FD 且数据段加速，2 = CAN FD 不加速      */
extern volatile uint32 g_can1TxDlc;          /* 要发的字节数，8 / 12 / 16 / 20 / 24 / 32 / 48 / 64         */
extern volatile uint8  g_can1TxData[CAN1_DATA_BYTES]; /* 要发的数据，g_can1TxDlc 个字节有效                  */
extern volatile uint32 g_can1TxSubmitCount;  /* 成功交给驱动层的帧数                                       */
extern volatile uint32 g_can1TxDoneCount;    /* 发送缓冲区空了（发完了）的帧数                              */
extern volatile uint32 g_can1TxPending;      /* 1 = 发送缓冲区里还有一帧没发完                              */
extern volatile uint32 g_can1TxSendStatus;   /* 最近一次 IfxCan_Can_sendMessage 的返回值，0 = 成功          */

/* --- 接收通道 ---------------------------------------------------------------------------------------- */
extern volatile uint32 g_can1RxCount;        /* 收到的帧数（中断里加）                                     */
extern volatile uint32 g_can1RxId;           /* 最近一帧的 ID                                              */
extern volatile uint32 g_can1RxExtended;     /* 最近一帧是不是扩展帧                                       */
extern volatile uint32 g_can1RxFd;           /* 最近一帧的帧类型：0 经典 / 1 FD 加速 / 2 FD 不加速          */
extern volatile uint32 g_can1RxDlc;          /* 最近一帧的字节数（已经换算成 0..64，不是 DLC 码）           */
extern volatile uint8  g_can1RxData[CAN1_DATA_BYTES]; /* 最近一帧的数据                                      */
extern volatile uint32 g_can1RxLostCount;    /* 接收 FIFO 溢出丢帧的次数                                    */

/* --- 通道状态 ---------------------------------------------------------------------------------------- */
extern volatile uint32 g_can1InitDone;       /* initCan1() 跑完置 1；can1Service() 先看这一位               */
extern volatile uint32 g_can1ModuleFreqHz;   /* CAN 内核时钟频率（Hz），500k/2M 就是从这里算出来的           */
extern volatile uint32 g_can1Synced;         /* 1 = 节点已经和总线同步（收到过有效帧），0 = 总线上没人        */
extern volatile uint32 g_can1ServiceCount;   /* can1Service() 被调用的次数                                  */

/* --- 自动发送 ---------------------------------------------------------------------------------------- */
extern volatile uint32 g_can1TxAutoEnable;   /* 1 = 上电就按 20 ms 周期轮发那 7 个 ID，写 0 停                 */
extern volatile uint32 g_can1TxAutoTickCount;/* 自动发送的节拍计数，每 20 ms 加 1                             */
extern volatile uint32 g_can1TxAutoIndex;    /* 下一个要发的 ID 在表里的下标                                  */
extern volatile uint32 g_can1TxAutoPeriodTicks; /* 20 ms 对应的 STM0 tick 数                                 */
extern volatile uint32 g_can1TxAutoNextTick; /* 下一个节拍的 STM0 计数值                                      */

/*********************************************************************************************************************/
/*------------------------------------------------Function Prototypes------------------------------------------------*/
/*********************************************************************************************************************/
/* 初始化 CAN1：SBC 里的收发器 -> CAN00 内核 -> 节点 -> 10 条接收滤波。
 * 内部会先调 initSbcCan()，把 SBC 的 BUS_CTRL_0 设成 Normal（复位值是 000，收发器是关的）。
 * 必须在 initSbc() 之后调用。跑完 g_can1InitDone = 1。                                             */
void initCan1(void);

/* 主循环里调用，做三件事：更新状态、20 ms 到点就请求发一帧、有请求就真正发出去。
 * 不阻塞，最坏情况执行时间在微秒级。                                                               */
void can1Service(void);

#endif /* CAN1_H_ */
