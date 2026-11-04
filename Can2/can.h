/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#ifndef CAN2_CAN_H_
#define CAN2_CAN_H_

#include "Ifx_Types.h"
#include "IfxCan.h"
#include "IfxCan_Can.h"

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
/* 板子的 "CAN2 通道" 在 iLLD 里叫 CAN13。这三套编号对不上就会配错模块：
 *
 *   板子 / 引脚表   CAN2 通道, CAN通讯模块2, Can2Tx / Can2Rx, 总线 CANH2 / CANL2
 *   网络名          OD_Can2TxCan13_M / ID_Can2RxCan13_M       <- "Can13" 这个线索嵌在这里
 *   iLLD / 芯片      MODULE_CAN1 + IfxCan_NodeId_3，引脚 IfxCan_TXD13_P33_4_OUT / IfxCan_RXD13B_P33_5_IN
 *
 * 芯片只有 CAN0 和 CAN1 两个 MCMCAN 模块，每个模块 4 个 node，iLLD 把 node 编成 00 01 02 03 / 10 11 12 13，
 * 所以 "13" 读作"模块 1、node 3"。名字里带 "CAN2" 的地方全是板子的叫法；iLLD 符号里一个 "CAN2" 都不会
 * 出现，因为芯片根本没有 CAN2 模块。
 *
 * 下面这两条不是从名字推的，是逐字段核对 Libraries/iLLD/TC37A/Tricore/_PinMap/IfxCan_PinMap.c 得到的：
 *   IfxCan_TXD13_P33_4_OUT = { &MODULE_CAN1, IfxCan_NodeId_3, { &MODULE_P33, 4 }, IfxPort_OutputIdx_alt7 }
 *   IfxCan_RXD13B_P33_5_IN = { &MODULE_CAN1, IfxCan_NodeId_3, { &MODULE_P33, 5 }, Ifx_RxSel_b }
 *
 * P33.4 -> T350 (CAN13_TXD)，P33.5 -> T320 (CAN13_RXDB)，与引脚表第 74/75 行一致（表里第 169 行的 T338
 * 属于 P10.1 的 QSPI1_MRSTA，不是这一路）。注意引脚表的"更正项"
 * 列写的是 CANH2，CANL2，说明这两个 TTL 脚后面还隔着一个收发器才到差分总线 —— 而那个收发器的型号在仓库里
 * 查不到（引脚表只管 MCU 侧，SBC 原理图只画了 CAN00 那一路）。后果见 CAN2_BUS_LOOPBACK 上面的说明。 */
#define CAN2_NODE_ID            IfxCan_NodeId_3

/* ---- 总线参数 --------------------------------------------------------------------------------------------------
 * 仲裁段 500 kbps，数据段 2 Mbps（FR-4）。位定时不自己算：nodeConfig.calculateBitTimingValues = TRUE 时
 * iLLD 会读实际模块时钟，自己填 NBTP / DBTP 两个寄存器。 */
#define CAN2_ARB_BAUDRATE       500000u          /* FR-1: 经典 CAN，也是 FD 的仲裁段 */
#define CAN2_DATA_BAUDRATE      2000000u         /* FR-4: FD 数据段 2 Mbps          */
#define CAN2_SAMPLE_POINT       8000u            /* 80.00%，单位是 1/10000          */
/* SJW 15.00%，单位**和采样点一样**是 1/10000，不是"几个 tq"。
 * 20 tq/位时 1500 恰好选出 3 tq，也就是这个宏字面上想要的那个数。
 *
 * 别写成 3：IfxCan_Node_setBitTiming() 的 SJW 搜索（IfxCan.c:516-526）拿 (tempSJW*10000)/bestTBAUD
 * 去比 syncJumpWidth，两边都是 1/10000 单位，写 3 就是"要 0.03%"，算法只好退回最小值 SJW=1 tq。
 * iLLD 自己的默认值也是 3（IfxCan_Can.c:635），照抄就会中这个招。
 * SJW 越小，容忍收发双方时钟漂移的能力越弱，且只会表现为随机偶发错误，很难查。 */
#define CAN2_SYNC_JUMP_WIDTH    1500u

/* FD 数据段的收发器延迟补偿 (TDC)。2 Mbps 时一个位时间 500 ns，收发器环回延迟通常小于一个位时间，先给 0
 * 也就是关掉。注意 tranceiverDelayOffset 是 iLLD 里的拼写，不是笔误。
 * 台架上如果"仲裁段一切正常、数据段报错"，第一个要动的就是它。 */
#define CAN2_TDC_OFFSET         0u

/* ---- 内部自环：台架保险，默认必须是 0 ---------------------------------------------------------------------------
 * 置 1 时发送在 node 内部环回给自己：不需要收发器、不需要第二个节点回 ACK，FR-1 ~ FR-6 全部能在没有总线的
 * 台子上先跑通。
 *
 * CAN 不是单方面能"发出去"的总线：一帧必须被至少一个别的节点回了 ACK 才算发完。这块板子的 CAN2 收发器
 * 型号未知（见上面那段），如果 T350 上挂个示波器、板子独立上电，那么波形会出现但**帧永远完成不了** ——
 * 表现就是 g_can2TxSubmitCount 涨而 g_can2TxDoneCount 不涨。这条开关是留给那种情况的第一道保险。
 *
 * 默认 0：开着它测出来的"通了"不能上总线。改它要重新编译。 */
#define CAN2_BUS_LOOPBACK       0


/* ---- 接收中断 --------------------------------------------------------------------------------------------------
 * 轮询扛不住。主循环一圈约 200 us，而 500 kbps 的 8 字节帧只有约 230 us、2 Mbps 的 64 字节 FD 帧约 300 us，
 * 一圈跟不上就丢帧，"不丢帧"就无从谈起。所以走 Rx FIFO0 + 新报文中断，中断里把 FIFO 一次抽干。
 *
 * 优先级 21 的理由：全工程只有一个中断 —— CCU_Clock.c:65 的 RUN_LED_ISR_PRIORITY = 20（STM0 比较器 0，
 * CPU0）。SRPN 是 8 位、0..255、**数值越大优先级越高、0 = 关**，所以 21 撞不上。
 *
 * 不带 u 后缀是故意的：IFX_INTERRUPT 会把它塞进 TASKING 的 __interrupt(prio)，而
 * __interrupt 是编译器属性不是普通表达式。现有的 RUN_LED_ISR_PRIORITY 也是不带后缀的 20。 */
#define CAN2_RX_ISR_PRIORITY    21

/* ---- 数据宽度 --------------------------------------------------------------------------------------------------
 * FR-5 要 64 字节 payload。这里有两个单位，别混：
 *
 * **调试变量是字节** —— g_can2TxData[64] / g_can2RxData[64]，**一个元素就是总线上的一个字节**。
 * 填 g_can2TxData[0] = 0x11 就是数据域的第 1 个字节，不用自己把 4 个字节拼成一个 32 位数。
 *
 * **iLLD 是字** —— 它收发数据都用 uint32* 并且按字计（IfxCan_Node_readData 的循环是 data[i] = src[i]，
 * 上限来自 IfxCan_Node_getDataLength，而那个函数返回的是**字**不是字节：IfxCan.c:90 的
 * return (numBytes + 3) >> 2）。所以 can.c 里收发各有一层字节<->字的转换，用 uint8* 打在数组上按地址
 * 走，不手拼位移 —— 于是调试变量的第几个元素就是总线上的第几个字节，跟端序无关。 */
#define CAN2_DATA_WORDS         16u     /* 64 字节 = 16 个字，iLLD 侧的单位 */
#define CAN2_DATA_BYTES         64u     /* 调试变量的单位：一个元素一个字节 */

/* 发送快照里记几个字节。8 是经典帧的上限，也是"组帧对不对"最常见的那种帧长，
 * 记 8 个够用了；要看更多字节就直接看 g_can2TxData。 */
#define CAN2_TX_DEBUG_BYTES     8u

/* ---- Message RAM 布局 ------------------------------------------------------------------------------------------
 * 单位是**字节**偏移，不是字。这一点是查实的：iLLD 写寄存器时做 address >> 2（IfxCan.h:2327 FLSSA、
 * :2157 FLESA、:2255 F0SA、:2393 TBSA），而算元素地址时做 ramBase + address（IfxCan.c:241），两边都按
 * 字节走，所以配置里给字节偏移是自洽的。
 *
 * RAM 起点就是模块基址：MODULE_CAN1 = 0xF0210000，而 CAN0_RAM 的定义也是 0xF0200000 == MODULE_CAN0，
 * 寄存器在 +0x8000 之上（IfxCan_reg.h:70/71/80/83）。所以 nodeConfig 里那句
 * .baseAddress = (uint32)(can->can) 是对的，不用动它。
 *
 * iLLD 的默认布局（0x0 / 0x80 / 0x100 / 0x200 / 0x300 / 0x400 / 0x440）每个区只留了 0x100 字节，是按
 * 8 字节数据域排的。而 FD 长帧要求数据域按**最大帧**配（不能按当前帧配），一个 64 字节数据域的元素是
 * 8 + 64 = 72 字节，默认那点地方放不下几条，所以这里覆盖默认值把各区铺开：
 *
 *   0x0000  标准滤波表      4 条 x  4 字节      =   16 字节  -> 0x0010
 *   0x0040  扩展滤波表      1 条 x  8 字节      =    8 字节  -> 0x0048
 *   0x0100  Rx FIFO0       16 条 x (8+64) 字节  = 1152 字节  -> 0x0580
 *   0x0800  Rx FIFO1        不用（size 0）
 *   0x0900  Rx 专用缓冲     不用（rxMode 不含 dedicatedBuffers）
 *   0x0A00  Tx Event FIFO   不用（size 0）
 *   0x0B00  Tx buffer       2 条 x (8+64) 字节  =  144 字节  -> 0x0B90
 *
 * 元素里那 8 字节是 MCMCAN 每个元素的头（两个 32 位字），后面才是数据 —— IfxCan.c:236/239 里
 * numOfconfigBytes = 8 就是它。
 *
 * 最高用到 0x0B90。CAN1 的 message RAM 是 16 KB（CAN1_RAM_SIZE = 0x4000，IfxCan_reg.h:1811），
 * 4 个 node 均分是每 node 0x1000；本工程只用 node 3，但把布局压在 0x1000 以内，就不依赖"其他 node 没用"。
 *
 * 别把 CAN0 的数拿过来用：CAN0_RAM_SIZE = 0x8000（32 KB）是 CAN0 独一份的规格，
 * 两个模块的 RAM 大小不一样，照抄 32 KB 会算出一个并不存在的余量。 */
#define CAN2_RAM_STD_FILTER     0x0000u
#define CAN2_RAM_EXT_FILTER     0x0040u
#define CAN2_RAM_RX_FIFO0       0x0100u
#define CAN2_RAM_RX_FIFO1       0x0800u
#define CAN2_RAM_RX_BUFFERS     0x0900u
#define CAN2_RAM_TX_EVENT_FIFO  0x0A00u
#define CAN2_RAM_TX_BUFFERS     0x0B00u

/* Rx FIFO0 深度。每个元素 72 字节，16 条 = 1152 字节，见上面那张账。
 * 16 条在 2 Mbps 下是约 4.6 ms 的缓冲，而中断每次进来都会把 FIFO 抽干，余量足够。 */
#define CAN2_RX_FIFO0_SIZE      16u

/* 发送用专用 Tx buffer（storeInTxFifoQueue = FALSE），发的是几号 buffer 是确定的，行为最好预测。
 * 2 条是 iLLD 的默认值，这里留一条备用。 */
#define CAN2_TX_BUFFER_COUNT    2u
#define CAN2_TX_BUFFER          IfxCan_TxBufferId_0

/* ---- 接收滤波表 (FR-3) -----------------------------------------------------------------------------------------
 * ID 取自 CANoe 工程实际加载的那份 DBC：桌面上 Canoe15_500K_EMC_DXN_B 的 NVS_Project01.cfg:37 指向
 * DBC\Can2\NVB_can2_202607281059.dbc，里面 BU_: NVB Tester —— NVB 是这块板子，Tester 是 CANoe。
 * Tester -> NVB 的四条就是我们要收的：
 *
 *   BO_ 517 RXMESSAGE_01: 16 Tester      0x205
 *   BO_ 518 RXMESSAGE_02: 16 Tester      0x206
 *   BO_ 519 RXMESSAGE_03:  8 Tester      0x207
 *   BO_ 520 RXMESSAGE_04:  8 Tester      0x208
 *
 * 反方向（NVB -> Tester）是 514/515/516/532..537/544，正好拿来当"无关 ID"的反例：CANoe 发 514 或 532，
 * g_can2RxCount 必须**不动**，FR-3 才算证死。
 *
 * 这份 DBC 里没有 29 位扩展帧，而 FR-2 要求扩展帧收发，所以扩展帧这条只能另外定一个 ID，同样进滤波表，
 * 否则 FR-2 的接收方向没东西可测。 */
#define CAN2_RX_ID_01           0x205u
#define CAN2_RX_ID_02           0x206u
#define CAN2_RX_ID_03           0x207u
#define CAN2_RX_ID_04           0x208u
#define CAN2_RX_ID_EXT          0x18FF0001u      /* DBC 里没有扩展帧，这个值是本项目自定的 */

/*********************************************************************************************************************/
/*------------------------------------------------Global Variables---------------------------------------------------*/
/*********************************************************************************************************************/
/* 全部是 plain global、volatile、具名到它是什么，可以在 Expressions 里直接打名字看 —— 不进宏、不进位域。
 *
 * ---- TX 组：你在调试器里写，代码读 ----------------------------------------------------------------------------
 * g_can2TxRequest   置 1 发一帧。上电不发，只做调试器触发。
 *                   **一次性消耗品**：can2Service() 的门在调用 can2SendOne() 之前就把它清 0，之后
 *                   不管成功失败都不还回来。所以它自己会回到 0，你要看结果看 g_can2TxSendStatus。
 *                   想再发一次就再置 1 —— 和第一次走的是同一条路。
 *                   （它**不会**被代码自动重触发：失败时重触发会构成满速重试环，把调试器冻住，
 *                     详见 can.c 里 can2SendOne() 结尾那段注释。）
 * g_can2TxId        帧 ID。默认 0x202 = 514，就是 DBC 里 NVB -> Tester 的第一条 TXMESSAGE_01。
 * g_can2TxExtended  0 = 11 位标准帧，1 = 29 位扩展帧（FR-2）。
 * g_can2TxFd        0 = 经典帧，1 = FD 帧数据段 2 Mbps（FR-4），2 = FD 长帧但数据段不加速。
 *                   刻意不用 IfxCan_FrameMode 的数值 —— 那个枚举里 1 是 fdLong、2 是 fdLongAndFast，
 *                   正好和直觉反着。
 * g_can2TxDlc       字节数。合法值只有 0..8 / 12 / 16 / 20 / 24 / 32 / 48 / 64，填别的会被收到最近的合法值，
 *                   **代码会把实际用上的字节数写回这个变量**，免得变量上写着 15 而实际发了 12。
 *                   注意跟 g_can2TxFd 的搭配：经典帧（g_can2TxFd = 0）的数据域最多 8 字节，所以
 *                   fd=0 时填 12/16/64 会被再夹到 8 —— 写回的就是 8，不是 12。要发长帧必须 fd=1 或 2。
 * g_can2TxData      64 字节 payload，**按字节填**：一个元素 = 总线上的一个字节，g_can2TxData[0] 就是
 *                   数据域的第 1 个字节。FR-5 用它。
 * g_can2TxSubmitCount / g_can2TxDoneCount
 *                   提交了几帧 / 真正发出去（拿到 ACK）几帧。这两个分开记是整块代码里最有用的诊断：
 *                   **只涨 Submit 不涨 Done，就是"MCU 侧没问题、总线侧没通"的签名** —— 没有第二个节点回
 *                   ACK，或者收发器那一段有问题。 */
extern volatile uint32 g_can2TxRequest;
extern volatile uint32 g_can2TxId;
extern volatile uint32 g_can2TxExtended;
extern volatile uint32 g_can2TxFd;
extern volatile uint32 g_can2TxDlc;
extern volatile uint8  g_can2TxData[CAN2_DATA_BYTES];
extern volatile uint32 g_can2TxSubmitCount;
extern volatile uint32 g_can2TxDoneCount;

/* g_can2TxPending   1 = 已提交、还没被证实发出去。can2Service() 只在它为 0 时才接受新请求，
 *                   所以**它一直是 1 就是"门关着"**：g_can2TxRequest 会停在 1、Submit 不再涨、
 *                   总线上只剩上一帧 —— "触发了却只看到历史报文"就是这个。
 *                   唯一直接说明门为什么关着的量，所以是普通全局，不是 static。 */
extern volatile uint32 g_can2TxPending;

/* ---- 发送诊断组：本次发送的现场快照 ---------------------------------------------------------------------------
 * 只在 can2SendOne() 里更新，**每次尝试发送都刷新一遍**（包括失败的那次）。全部初值 0xFFFFFFFF = "还没送过"，
 * 好让"没更新过"和"更新成了 0"分得开（ID 真的可以是 0）。
 *
 * g_can2TxSendStatus         IfxCan_Can_sendMessage 的返回值原样存下来。**它只有两种可能**：
 *                              0x00 = IfxCan_Status_ok           提交成功
 *                              0x20 = IfxCan_Status_notSentBusy  这个 Tx buffer 的 TXBRP 位还立着
 *                            依据是 IfxCan_Can.c:869-926 的函数体：整个函数里只有 871 行 `status = ok` 和
 *                            886 行 `status = notSentBusy` 两处给 status 赋值，之后不再改。所以
 *                            IfxCan_Status 枚举里另外那几个值（busOff = 0x10、notInitialised = 0x01、
 *                            receiveEmpty = 0x40 ...，见 IfxCan.h:412-424）**这个函数永远不会返回**，
 *                            读到它们只能是没人写过或者被谁改过 —— 别拿它们的字面含义去解释现场。
 *
 *                            特别要读懂 0x20 的意思：它**不是**"这一帧发失败"，而是"**上一帧还占着
 *                            buffer 没走掉**"。iLLD 自己在 IfxCan_Can.c:885 写了原因可能是总线忙、
 *                            BUS-OFF 或其它。所以看到 0x20 要立刻回头看 g_can2TxDebugBufferPending。
 *
 * g_can2TxDebugBufferPending 发送前那一刻 TXBRP[0] 的**硬件原值**：1 = 上一个请求还挂在 buffer 里。
 *                            它就是 g_can2TxSendStatus == 0x20 的判据本身（IfxCan_Can.c:884），
 *                            而且是直接问寄存器，不经过 g_can2TxPending 那个软件影子变量 ——
 *                            手工把 g_can2TxPending 清成 0 之后，这个位照样是 1。这一对是用来区分
 *                            "软件以为能发"和"硬件说不能发"的。0xFFFFFFFF = 还没采过。
 *
 * g_can2TxDebugId / Dlc / Extended / Fd / Data[8]
 *                            实际交进 IfxCan_Can_sendMessage 的参数快照，采在调用**之前**。
 *                            Id / Extended / Fd 取自组好的 message 结构体（不是原始全局），
 *                            Dlc 是**字节数**（和 g_can2TxDlc 同一套单位，不是 DLC 编码值），
 *                            Data 是数据数组前 8 个字节的**实际内存内容**。
 *
 *                            为什么要单独一组：g_can2TxId 是**你写进去的**，g_can2TxDebugId 是**代码交
 *                            出去的**。两边不一致 = 中间那层换算出了问题，这正好是 g_can2TxDlc 被写回
 *                            时会掩盖掉的那类错（经典帧配 Dlc=12，写回的是 8，看上去像"你说要 12 就是
 *                            12"，而 DebugDlc 让你看到夹这一刀确实生效了）。两边一致也**只证明 MCU 侧
 *                            组帧没错**，证明不了总线上有东西。 */
extern volatile uint32 g_can2TxSendStatus;
extern volatile uint32 g_can2TxDebugId;
extern volatile uint32 g_can2TxDebugDlc;
extern volatile uint32 g_can2TxDebugExtended;
extern volatile uint32 g_can2TxDebugFd;
extern volatile uint8  g_can2TxDebugData[CAN2_TX_DEBUG_BYTES];
extern volatile uint32 g_can2TxDebugBufferPending;

/* g_can2TxDebugBusOff        节点是不是被踢出总线了。读 PSR.BO（IfxCan.h:2492 的 IfxCan_Node_getBusOffStatus）：
 *                            0 = 没进，1 = 已经 Bus-Off，0xFFFFFFFF = 还没采过。
 *                            在 can2Service() 里**每圈采一次**（和 g_can2Synced 一个采法），因为它是状态不是事件。
 *
 *                            这一条是为了把 TXBRP 不清的两大来源分开：**没有节点回 ACK** 和 **节点自己 Bus-Off 了**。
 *                            两者的排查方向完全不同 —— 前者查线、查终端电阻、查对面波特率；后者要问错误计数
 *                            为什么一直涨。没有它的话，g_can2TxSendStatus == 0x20 在这两种情况下长得一模一样。
 *                            它和 g_can2Synced 也不是一回事：同步了不等于没 Bus-Off。 */
extern volatile uint32 g_can2TxDebugBusOff;

/* g_can2TxDebugBufferPendingAfterSend
 *                            调完 IfxCan_Can_sendMessage **立刻**再读一次同一个 TXBRP[0]：
 *                            1 = 还挂着，0 = 已经不挂了，0xFFFFFFFF = 还没采过。
 *                            和上面那个发送前的值配成一对看：发送前 1 / 发送后 0 = 硬件当真处理了这次请求。
 *
 *                            **读到 0 不等于"发出去了"**，这一点必须记住：sendMessage 只写一下 TXBAR 就返回，
 *                            硬件受理要几个时钟——这一行紧跟其后执行，读到的可能是受理前的旧值，也可能是
 *                            受理后的新值。它单独出现毫无意义，只有当 g_can2TxDoneCount 也跟着涨时才对得上。 */

/* ---- 协议层状态：说明"为什么发不出去"的一组，can2Service() 里每圈采一次（只读，无副作用）-----------------------
 *
 * g_can2TxDebugLec          最后错误码 PSR.B.LEC（IfxCan.h:2474，枚举 IfxCan.h:284-292）。这一组里信息量最大的一个：
 *                             0 = noError        上一次传输是成功的
 *                             3 = ackError       **没有别的节点回 ACK** —— 帧确实上了总线，只是没人应答
 *                             4 = bit1Error      发隐性读回显性 —— 有别的节点在总线上压着我们
 *                             5 = bit0Error      发显性读回隐性 —— **发出去的东西没回到总线上**
 *                             7 = noCANBusEvent  总线上什么都没发生
 *                            它只在"传输成功"时被清零，所以错误码会一直挂着，停机时读得到。
 *
 * g_can2TxDebugDlec         数据段最后错误码 PSR.B.DLEC（IfxCan.h:2504）。只有 FD 帧的数据段用得上，取值同上。
 *                            它的针对性作用是：**仲裁段正常、数据段报错** = 收发器延迟补偿（TDC）没配的典型签名。
 *
 * g_can2TxDebugTec / Rec    发送/接收错误计数器（ECR，IfxCan_regdef.h:334-341）。**这两个是唯一直接说明
 *                            "节点到底在不在重试"的量**，比上面那两个"最后一次"更硬：
 *                              TEC 一直涨 / 停在 128 以上  -> 节点确实在发、一直发不出去、在重试
 *                              TEC 恒为 0                 -> 节点一个错误帧都没发过，它压根没上总线
 *                              TEC 到 256 后掉回 0        -> 被踢出总线了（配合 g_can2TxDebugBusOff 看）
 *                              REC 在涨                   -> 总线上确实有帧
 *
 *                            看的时候**要看变化趋势，不要只看一个静止的数**：停一次、继续跑、再停一次对比。
 *                            另外 iLLD 在 IfxCan.h 里没有提供错误计数器的访问函数，这两个是直接读寄存器的，
 *                            是 can2 里唯一两处出现 MCMCAN 寄存器名的地方。 */
extern volatile uint32 g_can2TxDebugBufferPendingAfterSend;

/* g_can2TxDebugStage          执行流走到 can2SendOne() 的哪一步了。**这个量是用来终结"到底卡在哪一行"这场猜谜的。**
 *                             它和上面那些的区别：那些是"发出去之后的结果"，这个是"过程走到哪"。
 *                             代码自己每步盖一个章，所以你读到的值**和调试器高亮的那一行无关**——
 *                             哪一步没盖上，就是哪一段没走完：
 *                               0           从没进过 can2SendOne()（初值 0xFFFFFFFF，同义）
 *                               1           已进入
 *                               2           64 字节数据搬完
 *                               3           DLC 换算返回 —— **停在 2 上不去 3，就是卡在 DLC 那个 16 次循环里**
 *                               4           发送前快照采完
 *                               5           IfxCan_Can_sendMessage() 已返回 —— **停在 4 上不去 5，就是卡在那次调用里**
 *                               6           正常走完
 *                             判读只有一句话：**它停在哪一级，就是哪一段没走完。** */
extern volatile uint32 g_can2TxDebugStage;

extern volatile uint32 g_can2TxDebugLec;
extern volatile uint32 g_can2TxDebugDlec;
extern volatile uint32 g_can2TxDebugTec;
extern volatile uint32 g_can2TxDebugRec;

/* g_can2TxDebugServiceCount  can2Service() 跑过的圈数，只回答一个问题：**主循环还活着吗**。
 *                            上面那些量只要被写过一次就留在那儿，所以"读到一个合理值"证明不了循环还在转。
 *                            这个数在你盯着看的这几秒里必须**一直在涨**；停住了，后面所有结论都是在读一份死现场。 */
extern volatile uint32 g_can2TxDebugServiceCount;

/* ---- RX 组：只读，接收中断里更新 -------------------------------------------------------------------------------
 * g_can2RxCount     收到的帧数，只统计通过滤波表、真的进了 Rx FIFO0 的。
 * g_can2RxId / Extended / Fd / Dlc / Data
 *                   最近一帧的 ID、长度、是不是扩展帧、是不是 FD（取值含义同 g_can2TxFd）、DLC 和 64 字节数据。
 *                   短帧时 Data 里超出 DLC 的部分是 0，不是上一帧的残留。
 * g_can2RxLostCount FIFO 满导致丢帧的计数，由硬件 IR.RF0L 置位。**这个数必须一直是 0**，否则"没丢帧"
 *                   就是假的 —— 有了它，FR-6 的结论才是证据而不是感觉。
 *
 * 一个已知的不精确：接收中断在写 g_can2RxData[64] 时主循环若正好在读，可能读到半新半旧。台架上在调试器
 * 停机时看是安全的，实时读不保证。这里不额外加锁。 */
extern volatile uint32 g_can2RxCount;
extern volatile uint32 g_can2RxId;
extern volatile uint32 g_can2RxExtended;
extern volatile uint32 g_can2RxFd;
extern volatile uint32 g_can2RxDlc;
extern volatile uint8  g_can2RxData[CAN2_DATA_BYTES];
extern volatile uint32 g_can2RxLostCount;

/* ---- 状态量：没它就没法定位 -----------------------------------------------------------------------------------
 * g_can2ModuleFreqHz  iLLD 实际拿来算位定时的 fMCAN，单位 Hz。**期望 80000000**。
 *                     这个值是从 IfxScu_cfg.h 的宏链推出来的，不是量出来的：PLL1 = 320 MHz、
 *                     PLL1DIVDIS = 0、MCANDIV = 2、CLKSELMCAN = 1 -> 320/2/2 = 80 MHz。80 MHz 给
 *                     2 Mbps 数据段的是两倍余量：iLLD 找预分频时会一直减到每比特刚好 20 tq
 *                     （IfxCan.c:446 的跳出条件是 <= 20），所以仲裁段 NBRP = 7（÷8）、数据段
 *                     DBRP = 1（÷2），两档都是 20 tq/bit、80% 采样点。推导见 CAN2配置详解.md §2。
 *                     读出 0 就是 PERPLL 没起来，后面全是白测。
 * g_can2InitDone      initCan2() 走完置 1。
 * g_can2Synced        PSR.ACT 不为 0。ACT 是 **2 位**的活动状态字段（IfxCan_regdef.h:646 "ACT:2 [4:3]"），
 *                     不是一位的是非位：
 *                         00 = Synchronizing   还没同步上总线
 *                         01 = Idle            同步上了，空闲
 *                         10 = Receiver        11 = Transmitter
 *                     iLLD 的 IfxCan_Node_isNodeSynchronized() 判的是 != 0（IfxCan.h:2038），所以这个全局
 *                     真正的含义是"**节点已经同步上总线**"。总线上没有第二个节点也一样：RX 上读到 11 个
 *                     隐性位就会走到 01。反过来说它是 **RX 那一根的探针，但只探一半**：读 0 说明 RX 被
 *                     按在显性上下不来（T320 对地短路，或 U330 一直驱低）；**读 1 并不证明收发器
 *                     活着** —— U330 的 RXD 是推挽，它不在、没供电或者没接的时候那个脚是高阻，而 RX 这
 *                     一侧配了内部上拉（见 CAN2解析.md §5），读到的是隐性，节点照样算同步。收发器在不在，
 *                     只能靠 Submit/Done 那一对计数器分。
 *
 *                     初始化里**不忙等**它：同步要等总线上的隐性位，等多久不由我们决定，而 SBC 的看门狗
 *                     在 initSbc() 之前只有 200 ms，忙等就是把板子挂在初始化里，从外面看就是块死板。
 *                     所以改成在 can2Service() 里每次进来采一次。 */
extern volatile uint32 g_can2ModuleFreqHz;
extern volatile uint32 g_can2InitDone;
extern volatile uint32 g_can2Synced;

/*********************************************************************************************************************/
/*-----------------------------------------------Filter ID Correction-------------------------------------------------*/
/*********************************************************************************************************************/
/* 更正一段旧说法：之前这里写的是"can.c 把 ID 左移了 21 位，SFID1 变成 0"——那是误读，can.c 没有移位。
 *
 * 证据（can.src = can.c 的编译产物，can.c 本身是密文）：
 *   can.src:1490  .rodata can2StdFilterIds = .word 517,518,519,520        <- 就是 0x205..0x208，没移位
 *   can.src:1496  .rodata can2ExtFilterIds = .word 419364865              <- 0x18FF0001，没移位
 *   can.src:891   insert d15,d15,#0,#11,#21   <- 这是"保留低 11 位"的掩码，不是左移 21 位
 *                （TriCore 的 insert 是 insert d[c],d[a],value,position,width，第 4 个操作数是位置）
 *
 * 所以 can2InitFilters() 交给 iLLD 的 id1/id2 本来就是对的。这一段包装保留，理由只剩一个：
 * 把滤波 ID 集中到本文件的 CAN2_RX_ID_xx 宏，改 ID 只改一处。它是幂等的——填的值和 can.c 传的一样，
 * 留着、删掉都不影响功能（删的话记得把 can2SetStandardFilterCorrected 里对 can2FilterStdId 的调用一起去掉）。 */
static inline uint32 can2FilterStdId(uint8 number)
{
    uint32 id = CAN2_RX_ID_01;

    if (number == 1u) { id = CAN2_RX_ID_02; }
    else if (number == 2u) { id = CAN2_RX_ID_03; }
    else if (number == 3u) { id = CAN2_RX_ID_04; }

    return id & 0x7FFu;             /* SFID1 / SFID2 是 11 位字段，iLLD 内部也只取低 11 位 */
}

/* ---- 滤波器元素也要整字重写 -------------------------------------------------------------------------------------
 * 和发送侧 T1 同一个病：iLLD 写滤波器元素用的是 st.h / st.b
 * （IfxCan_Can.src:2649、2658、2667、2674、2681），落在 MCMCAN message RAM 上不落地。
 * 结果是滤波器元素根本没写进去，message RAM 里还是复位后的残留值。实测（修之前读 0xF0210000，滤波器 0 的 S0）
 * 得到 0xB8A8B0BA：里面没有 0x205，拆开是 SFT=classic / SFEC=7（存进 Rx Buffer），和配置的 dualId / 存进 FIFO0
 * 完全不沾边。GFC 又是 reject，于是 0x205 一个字节都进不了 Rx FIFO0：RF0N 不置位、RX 中断不触发、
 * g_can2RxCount 永远 0。总线侧一切正常（帧被 ACK），从外面看不出来。
 *
 * 元素布局按 IfxCan_regdef.h：
 *   标准滤波器元素 = 1 个字（Ifx_CAN_STDMSG 只有 S0）：SFT[31:30] SFEC[29:27] SFID1[26:16] SFID2[15:0]
 *   扩展滤波器元素 = 2 个字：F0 = EFEC[31:29] | EFID1[28:0]，F1 = EFT[31:30] | EFID2[28:0]
 * iLLD 的填法是 SFID1 = id2、SFID2 = id1（标准），F0 用 id1、F1 用 id2（扩展），这里照抄；
 * type / elementConfiguration 直接用 can.c 传进来的值，不自己编。 */
static inline void can2WriteStandardFilterElement(IfxCan_Can_Node *node, const IfxCan_Filter *filter)
{
    Ifx_CAN_STDMSG *element;
    uint32          s0;

    element = IfxCan_Node_getStandardFilterElementAddress(node->messageRAM.baseAddress,
                                                          node->messageRAM.standardFilterListStartAddress,
                                                          (uint8)filter->number);

    s0  = ((uint32)filter->type                 & 0x3u) << 30;
    s0 |= ((uint32)filter->elementConfiguration & 0x7u) << 27;
    s0 |= (filter->id2 & 0x7FFu) << 16;
    s0 |= (filter->id1 & 0x7FFu);

    *(volatile uint32 *)element = s0;
}

static inline void can2WriteExtendedFilterElement(IfxCan_Can_Node *node, const IfxCan_Filter *filter)
{
    Ifx_CAN_EXTMSG *element;
    uint32          f0;
    uint32          f1;

    element = IfxCan_Node_getExtendedFilterElementAddress(node->messageRAM.baseAddress,
                                                          node->messageRAM.extendedFilterListStartAddress,
                                                          (uint8)filter->number);

    f0  = ((uint32)filter->elementConfiguration & 0x7u) << 29;
    f0 |= (filter->id1 & 0x1FFFFFFFu);

    f1  = ((uint32)filter->type & 0x3u) << 30;
    f1 |= (filter->id2 & 0x1FFFFFFFu);

    *(volatile uint32 *)element       = f0;
    *((volatile uint32 *)element + 1) = f1;
}

static inline void can2SetStandardFilterCorrected(IfxCan_Can_Node *node, IfxCan_Filter *filter)
{
    IfxCan_Filter fixed = *filter;

    fixed.id1 = can2FilterStdId(filter->number);
    fixed.id2 = fixed.id1;          /* 类型是 dualId（SF1ID 或 SF2ID），两个 ID 都填成同一个 */
    IfxCan_Can_setStandardFilter(node, &fixed);
    can2WriteStandardFilterElement(node, &fixed);
}

static inline void can2SetExtendedFilterCorrected(IfxCan_Can_Node *node, IfxCan_Filter *filter)
{
    IfxCan_Filter fixed = *filter;

    fixed.id1 = CAN2_RX_ID_EXT & 0x1FFFFFFFu;
    fixed.id2 = fixed.id1;
    IfxCan_Can_setExtendedFilter(node, &fixed);
    can2WriteExtendedFilterElement(node, &fixed);
}

/* 两个包装函数的定义在宏定义**之前**，所以函数体里那两行调用是真的 iLLD 函数（预处理不会在一个宏自己的
 * 展开里再展开它）。can.c 第一次用到这两个名字是在 can2InitFilters() 的调用处，那时宏已经生效。 */
#define IfxCan_Can_setStandardFilter(node, filter)      can2SetStandardFilterCorrected((node), (filter))
#define IfxCan_Can_setExtendedFilter(node, filter)      can2SetExtendedFilterCorrected((node), (filter))

/*********************************************************************************************************************/
/*-------------------------------------Tx API Boundary Probe + T1 Correction-------------------------------------------*/
/*********************************************************************************************************************/
/* 这个包装做两件事：
 *
 * 1) 【只读快照】在**调用 IfxCan_Can_sendMessage() 的那一瞬间**，把真正传进去的 IfxCan_Message 逐字段抄到
 *    一组全局里。这是回答"发出去的到底是不是远程帧"最直接的证据 —— 不经过 CANoe、不经过 DBC，也不依赖
 *    can.c 里的中间量。为什么不用断点：自动发是 20 ms 一帧，断点每 20 ms 停一次根本没法看；而且
 *    Can2\can.c 在本机是加密的，里面那个 msg 是栈上的局部变量，调试器里也取不到。这组全局是每帧覆盖一次的
 *    "最后一帧快照"，停机时读就行。
 *
 * 2) 【补 T1】iLLD 的 IfxCan_Can_sendMessage() 只对 T0（ID）和数据区用 32 位写，而 DLC / FDF / BRS / EFC / MM
 *    全在 T1 里，它偏偏都写成 st.b 字节写。实测：0xF0210B00（T0）跟着 g_can2TxId 变（0x08080000 → 0x0AA80000），
 *    0xF0210B04（T1）却一直是 0 —— 连 fdLongAndFast 本该置上的 FDF/BRS 都没留下，说明这些字节写没有落到
 *    message RAM，于是每帧都以"经典帧 + DLC 0 + 无数据"发出。这里在真函数之前，用一次 32 位写把 T1 整个写好。
 *
 *    为什么不会被驱动盖掉：驱动后面写 T1 的每一处都是 st.b（本就不生效）；万一哪天换了编译器/芯片、字节写是
 *    好的，它写的也是同样的值，结果一致。T0 那边驱动是"读回来 → insert ID → 32 位写回"，[31:29] 从读回来的
 *    值里保留，所以先写进去的 XTD/RTR/ESI 也会被它带过去。
 *
 *    缓冲区还挂在 TXBRP 上（上一帧没走完）时不写：驱动这时会直接返回 32，不该去动在途的那一帧。
 *
 * 和上面 initNode / 滤波那两个包装同一个套路：函数定义在宏之前，函数体里那行调用是真的 iLLD 函数
 * （预处理不会在一个宏自己的展开里再展开它）。返回值原样透传。 */
extern volatile uint32 g_can2TxApiCount;        /* 这个接口被调用过几次 —— 它不涨，下面几个都是旧的 */
extern volatile uint32 g_can2TxApiId;           /* message->messageId，已按标准/扩展掩码过 */
extern volatile uint32 g_can2TxApiRtr;          /* message->remoteTransmitRequest，0 = 数据帧，1 = 远程帧 */
extern volatile uint32 g_can2TxApiDlc;          /* message->dataLengthCode，**原始 DLC 码 0..15**，不是字节数 */
extern volatile uint32 g_can2TxApiIdLen;        /* message->messageIdLength，0 = 11 位，1 = 29 位 */
extern volatile uint32 g_can2TxApiFrameMode;    /* message->frameMode，0 = 经典，1 = fdLong，2 = fdLongAndFast */
extern volatile uint32 g_can2TxApiData0;        /* data[0]，即 payload 第 0..3 字节（小端，低字节是 data[0]） */
extern volatile uint32 g_can2TxApiData1;        /* data[1]，即 payload 第 4..7 字节 */

static inline IfxCan_Status can2SendMessageCorrected(IfxCan_Can_Node *node, IfxCan_Message *message, uint32 *data)
{
    IfxCan_TxBufferId  buffer;

    buffer = (IfxCan_TxBufferId)message->bufferNumber;

    g_can2TxApiCount     = g_can2TxApiCount + 1u;
    g_can2TxApiId        = message->messageId;
    g_can2TxApiRtr       = (message->remoteTransmitRequest) ? 1u : 0u;
    g_can2TxApiDlc       = (uint32)message->dataLengthCode;
    g_can2TxApiIdLen     = (uint32)message->messageIdLength;
    g_can2TxApiFrameMode = (uint32)message->frameMode;
    g_can2TxApiData0     = data[0];
    g_can2TxApiData1     = data[1];

    /* 用和驱动完全相同的函数算元素地址，不自己编偏移：base + txBuffersStartAddress + n * (8 + 数据域长度)。
       元素里 T0 在 +0、T1 在 +4，数据从 +8 开始。 */
    if (!IfxCan_Can_isTxBufferRequestPending(node, buffer))
    {
        Ifx_CAN_TXMSG   *element;
        volatile uint32 *t0Word;
        volatile uint32 *t1Word;
        uint32           t0;
        uint32           t1;

        element = IfxCan_Node_getTxBufferElementAddress(node->node,
                                                        node->messageRAM.baseAddress,
                                                        node->messageRAM.txBuffersStartAddress,
                                                        buffer);
        t0Word  = (volatile uint32 *)element;
        t1Word  = (volatile uint32 *)element + 1u;

        /* T1：DLC[19:16] / BRS[20] / FDF[21] / EFC[23] / MM[31:24]，其余位保持 0。
           位定义见 IfxCan_regdef.h 的 Ifx_CAN_TXMSG_T1_Bits。 */
        t1  = ((uint32)message->dataLengthCode & 0xFu) << 16;
        t1 |= ((uint32)message->bufferNumber & 0xFFu) << 24;
        if (message->txEventFifoControl)                          { t1 |= (1u << 23); }
        if (message->frameMode != IfxCan_FrameMode_standard)      { t1 |= (1u << 21); }
        if (message->frameMode == IfxCan_FrameMode_fdLongAndFast) { t1 |= (1u << 20); }

        /* T0：ID[28:0] / RTR[29] / XTD[30] / ESI[31]。ID 驱动会用 32 位写自己盖一遍，这里跟着写是为了让
           驱动那次「读回来 → insert → 写回」把 [31:29] 带上；扩展帧没了 XTD 会被当成 11 位 ID 发出去。 */
        t0  = ((message->messageIdLength != IfxCan_MessageIdLength_standard)
                   ? (message->messageId & 0x1FFFFFFFu)
                   : (message->messageId & 0x7FFu));
        if (message->messageIdLength == IfxCan_MessageIdLength_extended) { t0 |= (1u << 30); }
        if (message->remoteTransmitRequest)                              { t0 |= (1u << 29); }
        if (message->errorStateIndicator)                                { t0 |= (1u << 31); }

        *t0Word = t0;
        *t1Word = t1;
    }

    return IfxCan_Can_sendMessage(node, message, data);
}

#define IfxCan_Can_sendMessage(node, message, data)     can2SendMessageCorrected((node), (message), (data))

/*********************************************************************************************************************/
/*---------------------------------------------20 ms Automatic Transmit-----------------------------------------------*/
/*********************************************************************************************************************/
/* 下载完就自动发，不用调试器触发 —— 20 ms 一帧，帧内容还是 g_can2TxId / g_can2TxDlc / g_can2TxData 那一组，
 * 上电默认全 0（can.c 里那组 .data 本来就是清零的，ID 是 514 = 0x202）。
 *
 * 节拍来自 STM0 比较器 1 的中断，实现在 Can2\can2_auto.c。中断里只把 g_can2TxRequest 置 1，发帧仍然走
 * can2Service() -> can2SendOne()，所以调试器里改 ID/DLC/数据、手动置 g_can2TxRequest 全部照旧有效。
 * 想停掉自动发就把 g_can2TxAutoEnable 清 0。
 *
 * 为什么要包装 IfxCan_Can_initNode()：Can2\can.c 在本机是加密文件（89 7D 1C .. 打头，读出来是密文），改不动；
 * can.h 是明文，而 initCan2() 里"node 刚初始化完"的调用点只有这一个。包装函数定义在宏之前，所以函数体里
 * 那行调用是真的 iLLD 函数（预处理不会在一个宏自己的展开里再展开它）—— 和上面两个滤波包装是同一个套路。 */
extern volatile uint32 g_can2TxAutoEnable;
extern volatile uint32 g_can2TxAutoTickCount;
extern volatile uint32 g_can2TxAutoPeriodTicks;

void can2AutoTxStart(void);

static inline boolean can2InitNodeAndStartAutoTransmit(IfxCan_Can_Node *node, const IfxCan_Can_NodeConfig *config)
{
    boolean result = IfxCan_Can_initNode(node, config);

    can2AutoTxStart();

    return result;
}

#define IfxCan_Can_initNode(node, config)   can2InitNodeAndStartAutoTransmit((node), (config))

/*********************************************************************************************************************/
/*------------------------------------------------Function Prototypes------------------------------------------------*/
/*********************************************************************************************************************/
/* 配好 CAN2：模块、node、位定时、滤波表、接收中断、引脚。全部调官方函数，这个文件里不出现 MCMCAN 寄存器名。
 *
 * 调用位置在 Cpu0_Main.c 的 initSbc() **之后** —— SBC 是总闸，200 ms 看门狗没喂上之前任何初始化都可能
 * 被打断。这个函数不忙等任何位，所以不会把板子挂住。 */
void initCan2(void);

/* 主循环里调，非阻塞、不自节流。干两件事：
 *   1. 上一帧发出去了没有（TXBRP 由硬件在拿到 ACK 时清零）-> 维护 Done 计数
 *   2. g_can2TxRequest 置 1 就发一帧
 * 顺带采一次 g_can2Synced。 */
void can2Service(void);

#endif /* CAN2_CAN_H_ */
