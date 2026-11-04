/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#ifndef QSPI_H_
#define QSPI_H_

#include "Ifx_Types.h"

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
/* 板载 PSI5 链路：QSPI3 -> 两颗 ELMOS E521.41A62CXX2 -> 7 路传感器。
 *
 *   trx1 = U360（下面那颗）  SIF1..SIF4 = Level_FR / Level_FL / Level_RR / Level_RL   片选 P02.8，ChID 1..4
 *   trx2 = U388（上面那颗）  SIF1..SIF3 = ACC_FR / ACC_FL / ACC_RR                     片选 P00.5，ChID 1..3
 *                                                                                       （trx2 的 ChID4 没接）
 *
 * 这 7 路是数字 PSI5 传感器，不是模拟输出，所以没有 ADC 这条路：传感器在 PSI5 总线上按时间槽
 * 把一帧数据发给收发器，E521 把每个通道的数据缓存在内部 SPI 缓冲里，MCU 用 QSPI 的
 * Get_Data_16b 命令把缓冲读出来。整条链是：
 *
 *   同步脉冲(SPI) -> 传感器在槽里回帧 -> E521 缓冲 -> QSPI 读 -> Level_* / ACC_* 全局变量
 *
 * SPI 参数（E521 数据手册）：16 位一帧、MSB 先、SCLK 空闲低、下降沿采样（SPI 模式 1：收发器在上升沿
 * 移出 SDO、下降沿采样 SDI，所以主机在上升沿换 MOSI、下降沿采 MISO）、
 * SCLK <= 5 MHz、帧间 NCS 高 >= 700 ns、NCS 提前 >= 600 ns、NCS 拖尾 >= 100 ns。            */
#define QSPI3_MAX_BAUDRATE            5000000.0f   /* 模块上限（iLLD 用它算 TQ）                    */
#define QSPI3_CHANNEL_BAUDRATE        4000000.0f   /* 通道实际速率，留了 1 MHz 余量                 */

/* SPI 命令字，最高 4 位是命令号。注意 1111（0xFFFF）是 SW_Reset，
 * 所有"填充帧"必须用 NOP 0xE000，不能拿 0xFFFF 凑数。                                          */
#define PSI5_CMD_WRITE_REG            0x1000u      /* Write_Register  0001                          */
#define PSI5_CMD_READ_REG             0x2000u      /* Read_Register   0010                          */
#define PSI5_CMD_SYNC                 0x3000u      /* Sync_Pulse      0011                          */
#define PSI5_CMD_GET_DATA_16B         0x4000u      /* Get_Data_16b    0100（16 bit/块缓冲）           */
#define PSI5_CMD_GET_DATA_48B         0x8000u      /* Get_Data_48b    1000（48 bit/块缓冲，本项目用） */
#define PSI5_CMD_NOP                  0xE000u      /* NOP             1110                          */
/* 多帧命令里的 stuff 帧（读命令的帧 n+1）。手册 Table 6.2.3.2-1 要求 Stuff[15:0] = 0，
 * 这里填 NOP 的话芯片会判成出错：帧 n+2 回全 0 + 取反 XCRC，低 10 位数据全丢。            */
#define PSI5_CMD_STUFF                0x0000u

/* !!! 一条 Write_Register / Read_Register 由连续几个 16 位帧组成（见数据手册 6.2.3.2 表）：
 *   写：帧 n = 0001|地址|D[15:10]，帧 n+1 = D[9:0]|000000，帧 n+2 = 下一条命令或 NOP
 *   读：帧 n = 0010|地址|000000，帧 n+1 = stuff，帧 n+2 = 下一条命令
 *
 * 关键：**每个 16 位帧各自一个 NCS 脉冲**，帧与帧之间 NCS 必须抬高，时间 >= t_ON_NCS
 * （手册 6.2.3.2 注解："The idle time between consecutive SPI frames has to fulfill
 * parameter ton_NCS"）。芯片自己按命令状态机把这些帧串起来，不靠片选。
 *
 * 如果帧间 NCS 空闲时间不够，两帧会连成一次多时钟传输 —— 那是 daisy-chain 的格式，
 * 一颗片子只取最前面 16 位，第二帧的数据整个丢掉。实测就是这个现象：
 *   帧间只隔 ~175 ns（QSPI 的 SLSO 拖尾+提前延迟）时，
 *   写 0x0C03 -> 读回 0x0C00（只进了命令字里的 D[15:10]=3）
 *   写 0x0003 -> 读回 0x0000（D[15:10]=0，D[9:0] 全丢）
 * 反过来把 3 帧压在同一个 NCS 低电平里（48 个时钟）也是同样结果，一样只取前 16 位。
 * 读命令不受影响，因为地址信息全在第一帧里，后两帧只是把数据移位出来。                */

/* 寄存器地址（6 位，写在命令字 bit11..6） */
#define PSI5_REG_ASIC_CNFG_1          0x00u
#define PSI5_REG_ASIC_CNFG_2          0x01u
#define PSI5_REG_ASIC_CNFG_3          0x02u

/* 配置 / 错误 / 诊断寄存器地址（数据手册 6.2.6.2 ~ 6.2.6.4）。
 * 这些只读出来看，不写 —— 用来在纯代码层面判断"传感器有没有回帧、线有没有电"。            */
/* 每个通道占 7 个连续寄存器（CFG1..CFG7）：
 *   CH1 = 0x03..0x09, CH2 = 0x0A..0x10, CH3 = 0x11..0x17, CH4 = 0x18..0x1E
 * 时间槽/帧长是**每通道独立**的，只写 CH1 的话 CH2..4 还是复位值（T1_LEN=4 -> 128 us）。 */
#define PSI5_REG_CH1_CFG1             0x03u
#define PSI5_REG_CH1_CFG7             0x09u
#define PSI5_REG_CH2_CFG1             0x0Au
#define PSI5_REG_CH2_CFG7             0x10u
#define PSI5_REG_CH3_CFG1             0x11u
#define PSI5_REG_CH3_CFG7             0x17u
#define PSI5_REG_CH4_CFG1             0x18u
#define PSI5_REG_CH4_CFG7             0x1Eu
#define PSI5_REG_ERROR_STATUS_1       0x25u   /* 全局 ASIC 错误（UART/SPI、过温、VBUS 过压） */
#define PSI5_REG_ERROR_STATUS_2       0x26u   /* 每通道错误汇总：bit0=CH1 ... bit3=CH4        */
#define PSI5_REG_ERROR_STATUS_3       0x27u   /* CH1 帧 1..4 的 MD 错误                       */
#define PSI5_REG_ERROR_STATUS_4       0x28u   /* CH1 帧 5/6 + 模拟错误（BUFF_ERR/OC/DIAG）     */
#define PSI5_REG_ERROR_STATUS_5       0x29u   /* CH2 帧 1..4 的 MD 错误                       */
#define PSI5_REG_ERROR_STATUS_6       0x2Au   /* CH2 帧 5/6 + 模拟错误                        */
#define PSI5_REG_ERROR_STATUS_7       0x2Bu   /* CH3 帧 1..4 的 MD 错误                       */
#define PSI5_REG_ERROR_STATUS_8       0x2Cu   /* CH3 帧 5/6 + 模拟错误                        */
#define PSI5_REG_ERROR_STATUS_9       0x2Du   /* CH4 帧 1..4 的 MD 错误                       */
#define PSI5_REG_ERROR_STATUS_10      0x2Eu   /* CH4 帧 5/6 + 模拟错误                        */
#define PSI5_REG_DIAG_ADC_3_4         0x30u   /* ADC：高字节 VSIF2，低字节 VSIF1               */
#define PSI5_REG_DIAG_ADC_5_6         0x31u   /* ADC：高字节 VSIF4，低字节 VSIF3               */
#define PSI5_REG_DIAG_ADC_7_8         0x32u   /* ADC：高字节 VSYNC，低字节 VBUS                */
#define PSI5_REG_DIAG_ADC_9_10        0x33u   /* ADC：低字节 VCP_GATE                          */
#define PSI5_REG_DIAG_IDAC_1_2        0x35u   /* Ibase 跟踪值：高 7 位 CH2，低 7 位 CH1        */
#define PSI5_REG_DIAG_IDAC_3_4        0x36u   /* Ibase 跟踪值：高 7 位 CH4，低 7 位 CH3        */

/* ASIC 寄存器初值：
 *   CNFG_1 = 0x0003  V_BUS[1:0] = 11 -> 内部 LDO 7.7 V
 *                    （VBUS 脚上只有测试点和电容，没有外部电源，必须用内部 LDO）
 *   CNFG_2 = 0x0000  复位默认值：PSI5 同步模式、125 kbps、300 uA
 *   CNFG_3 trx1 = 0x004F  EN_CP_SYNC + CH1..CH4 全开
 *          trx2 = 0x0047  EN_CP_SYNC + CH1..CH3 全开（第 4 路没接传感器）
 *
 * 写 CHx_CFG1/2 时只改时间槽/帧长那几位：bit15..11 是 Ibase 跟踪用的 IDAC 系数，
 * 必须保持复位值（CFG1 三个字段全 0，CFG2 的 DEC2/DEC1 = 01/01，即 CFG2 里 0x2800）。
 * CHx_CFG7 必须从复位的 0b11（16 位块）改成 0b00（48 位块），否则长帧根本装不进缓冲。 */
#define PSI5_CNFG1_VBUS_LDO_7V7       0x0003u
#define PSI5_CNFG2_DEFAULT            0x0000u
#define PSI5_CNFG3_TRX1               0x004Fu
#define PSI5_CNFG3_TRX2               0x0047u
#define PSI5_CNFG3_EN_MASK            0x000Fu   /* EN_CH4..EN_CH1，改 1/2 号寄存器前先清掉这四位     */
#define PSI5_CFG2_KEEP_IDAC           0x2800u   /* CHx_CFG2 bit14:11 = 01/01，Ibase 跟踪系数        */

/* 同步脉冲命令：0x3000 | ChT[3:0] << 8 | ChL[3:0] << 4 | 0
 * ChT 每一位对应一个通道，1 = 该通道发同步脉冲；ChL = 0 是短脉冲。
 * trx1 四路全发 = 0x3F00，trx2 前三路 = 0x3700。一个同步周期只能发一条 Sync 命令。         */
#define PSI5_SYNC_TRX1                0x3F00u
#define PSI5_SYNC_TRX2                0x3700u

/* 时间参数 */
#define PSI5_SERVICE_PERIOD_MS        2u      /* 同步周期，2 ms -> 500 Hz 刷新率                          */
#define PSI5_READ_DELAY_US            600u    /* 同步后等 600 us 再读（时间槽 1..3 最坏 448 us）           */
#define PSI5_FRAME_TIMEOUT_US         200u    /* 单个 16 位帧的等待上限（正常一帧约 7 us）                 */
#define PSI5_INTER_FRAME_US           2u      /* 帧间 NCS 空闲时间，必须 >= t_ON_NCS（手册要求 >=700 ns）  */
#define PSI5_NRES_LOW_US              50u     /* NRES 低电平保持时间（数据手册 >= 10 us）                  */
#define PSI5_OSC_STARTUP_MS           2u      /* NRES 释放后内部振荡器起振（数据手册 1 ms）                */
#define PSI5_BLANKING_MS              10u     /* 寄存器配完后等传感器稳定，再开始发同步                    */
#define PSI5_DIAG_PERIOD_MS           100u    /* 诊断寄存器读取周期；手册要求 ERROR_STATUS_x 间隔 >= 9 ms  */

/* ---- 传感器供电开关 Do01 ---------------------------------------------------------------------------------
 * 板上网表 OS_Do01_M，MCU 引脚 P10.0，高电平有效（引脚分配表第 168 行）。
 * 走向：P10.0 -> OS_Do01_M -> Q380 -> Q518A 栅极 -> 两颗 E521 的 VSUPPLY（第 14 脚）。
 *
 * 这一路是 PSI5 的模拟电源：VSUPPLY 不来，片内 VBUS LDO（ASIC_CNFG_1 的 V_BUS）就没有输入，
 * 同步脉冲电荷泵也没有输入，SIF 线上没有电压，传感器根本不上电、不回帧。
 * 数字部分 VDD 走的是 D5V，所以此时 SPI 读写寄存器一切正常、缓冲却永远是 Fid=7/Err=3。
 *
 * 原来的代码从未驱动过 P10.0 —— 这是整条链上唯一没有被写过的"寄存器"。                              */
#define PSI5_DO01_PORT                (&MODULE_P10)
#define PSI5_DO01_PIN                 0u
#define PSI5_SUPPLY_SETTLE_MS         20u     /* 供电打开后等传感器上电，再释放 NRES                 */

#define PSI5_CHANNEL_COUNT            7u

/* Get_Data_16b 的 BID（缓冲号）。CHx_CFG7 的复位值把 6 个缓冲对应时间槽 1..6，
 * 每根 SIF 线上只挂一个传感器、发在槽 1，所以默认读 BID0。
 * 调不通时把 g_qspiReadBid 改成 0..5 挨个试，看哪个 BID 的 Fid/Err 不是 111/11。              */
#define PSI5_DEFAULT_READ_BID         0u

/* ---- 帧格式：实测已确定，开机直接用，不再扫 ---------------------------------------------------------------
 * 实测（上板验证过）：189 kbps + 25 bit 帧。25 = 2 起始 + 20 位数据 + 3 位 CRC。
 * 数据字低 6 位恒为 0，测量值左对齐，真实值是 D[19:6]。
 * 启动时直接套用这一档并把 g_qspiScanLocked 置 1；要重新扫一遍，在调试器里把它改回 0 即可。
 * 扫描档位 = PSI5_SCAN_RATE_COUNT × 9 种帧长 = 18 档，每 PSI5_SCAN_HOLD_CYCLES 个同步周期换一档。       */
#define PSI5_SCAN_FIXED_INDEX         15u     /* rate=1（189 kbps）, 帧长 25 bit（第 7 个候选）        */
/* trx2 用的是另一档，2026-11-03 扫出来的：22 = 189 kbps / 短同步脉冲 / 21 bit。
 * 21 bit = 2 起始 + **16 数据** + 3 CRC（CRC 在 D[18:16]），所以数据就是 D[15:0]。
 * 注意：**加速度不是 25 bit**，不能套高度那套 D[19:4] 取法。                      */
#define PSI5_TRX2_FIXED_INDEX         22u
#define PSI5_SCAN_HOLD_CYCLES         25u     /* 每档观察 25 个同步周期 = 50 ms                      */
#define PSI5_SCAN_RATE_COUNT          2u      /* 125 kbps / 189 kbps                                 */

/*********************************************************************************************************************/
/*-----------------------------------------------Debugger variables--------------------------------------------------*/
/*********************************************************************************************************************/
/* 调试器里直接看的全局变量。都是普通全局变量，不是宏、也不是位域打包，名字按网表 / 信号名起。 */

/* --- 高度（悬架位置），trx1 的 4 路（25 bit 帧，数据取 D[19:4]）---------------------------------------- */
extern volatile uint16 Level_FR;              /* 右前，trx1 SIF1（U360 第 1 路）                          */
extern volatile uint16 Level_FL;              /* 左前，trx1 SIF2                                          */
extern volatile uint16 Level_RR;              /* 右后，trx1 SIF3                                          */
extern volatile uint16 Level_RL;              /* 左后，trx1 SIF4                                          */

/* --- 加速度，trx2 的 3 路，**无符号原码**（21 bit 帧 = 2 起始 + 16 数据 + 3 CRC）------------------------
 *   现在不做任何换算，灵敏度系数确认后再做。带外的大数（例如 0x7D00 = 32000）
 *   是传感器的状态码，不是加速度                                                            */
extern volatile uint16 ACC_FR;                /* 右前，trx2 SIF1（U388 第 1 路）                          */
extern volatile uint16 ACC_FL;                /* 左前，trx2 SIF2                                          */
extern volatile uint16 ACC_RR;                /* 右后，trx2 SIF3                                          */

/* --- 每路的诊断信息 -------------------------------------------------------------------------------------
 *   _Raw  收到的 D[15:0]，无符号，看位型用。加速度这一路它就是完整的 16 位数据；
 *         高度那一路只用 D[19:4]（D[19:16] 在 _Raw 之上，不在里面）
 *   _Fid  帧号（时间槽号），111 = 这个缓冲还没有被写过
 *   _Err  错误码，11 = 这个缓冲还没有被写过
 * 某一路的 Fid/Err 一直是 7/3，说明收发器没在对应时间槽里收到这一路的数据
 * （传感器没接 / 没上电 / 同步没发出去）。                                                              */
extern volatile uint16 Level_FR_Raw;
extern volatile uint16 Level_FL_Raw;
extern volatile uint16 Level_RR_Raw;
extern volatile uint16 Level_RL_Raw;
extern volatile uint16 ACC_FR_Raw;
extern volatile uint16 ACC_FL_Raw;
extern volatile uint16 ACC_RR_Raw;
extern volatile uint8  Level_FR_Fid;
extern volatile uint8  Level_FL_Fid;
extern volatile uint8  Level_RR_Fid;
extern volatile uint8  Level_RL_Fid;
extern volatile uint8  ACC_FR_Fid;
extern volatile uint8  ACC_FL_Fid;
extern volatile uint8  ACC_RR_Fid;
extern volatile uint8  Level_FR_Err;
extern volatile uint8  Level_FL_Err;
extern volatile uint8  Level_RR_Err;
extern volatile uint8  Level_RL_Err;
extern volatile uint8  ACC_FR_Err;
extern volatile uint8  ACC_FL_Err;
extern volatile uint8  ACC_RR_Err;

/* --- 状态 / 计数 ---------------------------------------------------------------------------------------- */
extern volatile uint32 g_qspiInitDone;        /* initQspi() 跑完置 1；qspiService() 先看这一位            */
extern volatile uint32 g_qspiRealBaudrate;    /* 通道实际速率（iLLD 算出来的，单位 Hz）                    */
extern volatile uint32 g_qspiServiceCount;    /* qspiService() 被调用的次数                               */
extern volatile uint32 g_qspiSyncCount;       /* 发出去的同步周期数（每 2 ms 加 1）                       */
extern volatile uint32 g_qspiReadCount;       /* 成功读到响应的通道数（每路每周期最多加 1）                */
extern volatile uint32 g_qspiFrameCount;      /* 收发的 16 位帧总数                                       */
extern volatile uint32 g_qspiNoDataCount;     /* 缓冲为空（Fid=7, Err=3）的次数                           */
extern volatile uint32 g_qspiBadRespCount;    /* 回显对不上的次数（片选 / 接线 / 供电有问题）              */
extern volatile uint32 g_qspiTimeoutCount;    /* FIFO 等待超时的次数                                      */

/* --- 调试旋钮 ------------------------------------------------------------------------------------------- */
extern volatile uint32 g_qspiReadBid;         /* 读哪个 SPI 缓冲（0..5），默认 0，改完不用重新编译          */
extern volatile uint16 g_qspiSyncCmdTrx1;     /* trx1 每周期发的同步命令，默认 0x3F00（短脉冲，全通道）      */
extern volatile uint16 g_qspiSyncCmdTrx2;     /* trx2 每周期发的同步命令，默认 0x3700（短脉冲，CH1..CH3）    */

/* --- 帧格式自动扫描状态（只读） ------------------------------------------------------------------------- */
extern volatile uint16 g_qspiScanIndex;       /* 当前第几档 0..17                                            */
extern volatile uint16 g_qspiScanFlen;        /* 当前档的帧长（bit）                                         */
extern volatile uint16 g_qspiScanRate;        /* 0 = 125 kbps，1 = 189 kbps                                  */
extern volatile uint16 g_qspiScanLocked;      /* 1 = 已经收到过有效帧，扫描停止                              */

/* --- trx2（加速度三路）的帧格式档位 ----------------------------------------------------------------------
 * 0xFFFF = 自动扫描中（当前默认）。0..35 = 锁定档位。拆法：
 *   rate  = idx / 18         0 = 125 kbps，1 = 189 kbps
 *   pulse = (idx % 18) / 9   0 = 短同步脉冲，1 = 长同步脉冲
 *   flen  = 帧长表 [(idx % 18) % 9] = {11,13,15,19,21,23,25,29,33}
 * 例：24 = 189 kbps / 短脉冲 / 25 bit（trx1 高度那四路用的档）。
 * 扫描每 50 ms 换一档，任意一路 Fid != 7 就锁住并把编号留在这个变量里。               */
extern volatile uint16 g_qspiTrx2Format;
/* 同步命令格式 = 0x3000 | ChT[3:0]<<8 | ChL[3:0]<<4。
 * ChT 的 bit0..3 对应 CH1..CH4，1 = 这一路发同步；ChL 对应位 = 1 表示长脉冲。
 * 例：0x3990 = CH1/CH4 长脉冲；0x3F00 = 四路短脉冲。运行中直接改变量即可，下一周期生效。       */

/* 传感器供电开关 Do01（P10.0，网表 OS_Do01_M）。变量名就是网表名，和 Driver.h 里五个电源开关
 * 的写法一致：写 1 = P10.0 拉高 = 供电开（默认），写 0 = 拉低。
 * 下一轮 qspiService() 会把改动写到引脚上，不用重新编译 —— 万一板上这一级实际低有效，
 * 在调试器里写成 0 就行。g_qspiDo01On 是最近一次真正写到引脚上的电平，只读。                    */
extern volatile uint32 OS_Do01_M;             /* P10.0 原始电平，1 = 高 = 传感器供电开         */
extern volatile uint32 g_qspiDo01On;          /* 已经写到引脚上的电平，观察用                  */

/* --- 收发器配置寄存器的"待写入值" ------------------------------------------------------------------------
 * 改这三个变量，qspiService() 下一轮就把新值写进芯片，不用重新编译。初值 = 初始化时写进去的值，
 * 所以不动它们什么都不会发生。写完之后 g_qspiTrx1Cnfg1/2/3 的读数会跟着变。
 *
 *   ASIC_CNFG_1（协议 / 模拟档位）
 *     0x0003  默认：V_BUS=7.7V，VSYNC_V3=4.8V(common)，ΔIS=26mA
 *     0x003F  只把 ΔIS 改小到 13 mA（低功耗型传感器）
 *     0x03FF  ΔIS 13 mA + VSYNC_V3 3.7V（整套低功耗档）
 *     注意 V_BUS[1:0] 不能写 00，那是把 LDO 关掉。
 *   ASIC_CNFG_2（速率 / 同步异步）
 *     0x0000  默认：同步模式、125 kbps（8 us 位时间）
 *     0x00F0  同步模式、189 kbps（5.3 us 位时间），四位通道一起改
 *     0x000F  四位 ASYNC_CHx=1 是异步模式，PSI5 传感器不用这个
 *   ASIC_CNFG_3（通道开关）
 *     0x004F / 0x0047  初始化值：EN_CP_SYNC + CH1..4 / CH1..3
 *     VBUS 过压、过温、过流都会让芯片自己把 EN 位清 0，诊断读数里看见变了就把它写回去。
 *
 * 手册建议通道使能之后不要再改通道配置；这里允许改是为了在板上直接试，改的瞬间可能丢一帧。      */
extern volatile uint16 g_qspiCnfg1WriteTrx1;  /* 默认 PSI5_CNFG1_VBUS_LDO_7V7（0x0003）        */
extern volatile uint16 g_qspiCnfg1WriteTrx2;
extern volatile uint16 g_qspiCnfg2WriteTrx1;  /* 默认 PSI5_CNFG2_DEFAULT（0x0000 = 125 kbps）  */
extern volatile uint16 g_qspiCnfg2WriteTrx2;
extern volatile uint16 g_qspiCnfg3WriteTrx1;  /* 默认 PSI5_CNFG3_TRX1（0x004F）                */
extern volatile uint16 g_qspiCnfg3WriteTrx2;  /* 默认 PSI5_CNFG3_TRX2（0x0047）                */

/* --- 最近一次读回来的 ASIC 寄存器（0xFFFF = 读失败） ---------------------------------------------------- */
extern volatile uint16 g_qspiTrx1Cnfg1;       /* trx1 ASIC_CNFG_1，写的是 0x0003                          */
extern volatile uint16 g_qspiTrx1Cnfg2;       /* trx1 ASIC_CNFG_2，写的是 0x0000（125 kbps 同步）           */
extern volatile uint16 g_qspiTrx1Cnfg3;       /* trx1 ASIC_CNFG_3，写的是 0x004F                          */
extern volatile uint16 g_qspiTrx2Cnfg1;       /* trx2 ASIC_CNFG_1，写的是 0x0003                          */
extern volatile uint16 g_qspiTrx2Cnfg2;       /* trx2 ASIC_CNFG_2，写的是 0x0000                          */
extern volatile uint16 g_qspiTrx2Cnfg3;       /* trx2 ASIC_CNFG_3，写的是 0x0047                          */
extern volatile uint16 g_qspiTrx1Ch1Cfg1;     /* trx1 CH1_CFG1，复位值应为 0x0034（P10P，12 数据位）        */
extern volatile uint16 g_qspiTrx1Ch1Cfg7;     /* trx1 CH1_CFG7，复位值应为 0x0C00（6 个 16 位缓冲）         */
extern volatile uint16 g_qspiTrx2Ch1Cfg1;     /* trx2 CH1_CFG1，复位值应为 0x0034                          */
extern volatile uint16 g_qspiTrx2Ch1Cfg7;     /* trx2 CH1_CFG7，复位值应为 0x0C00                          */

/* --- 诊断寄存器（每 100 ms 用 SPI 读一轮，不需要示波器 / 测试点） ----------------------------------------
 *
 * 错误寄存器是 clear-on-read：这里读到的是最近 100 ms 里出现过、还没被清掉的错误。
 * 0xFFFF = 这一条 SPI 读失败（回显对不上），不是寄存器值。
 *
 *   ERROR_STATUS_2  bit0..3 = CH1..CH4 有没有接口错误（MD / 缓冲 / 过流 / 线束任一都算）
 *   ERROR_STATUS_3  bit2 = 时间槽 1 没收到帧   bit1 = 帧错误   bit0 = 校验错
 *   ERROR_STATUS_4  bit11 = 缓冲配置错  bit10 = 过流  bit9:8 = 线束诊断  bit12 = 反向电流
 *
 * VBUS / VSYNC / VSIFx 是 8 位 ADC 码值，不是伏特，只看量级：
 *   0            = 线上没有电压
 *   和 VBUS 同量级 = 线上有电（传感器在、供电到了）
 * VCP_GATE 应该和 VBUS 同量级。trx2 的 VSIF4 没接传感器，值无意义。                              */
extern volatile uint16 g_qspiTrx1ErrStatus1;  /* 0x25 全局 ASIC 错误                          */
extern volatile uint16 g_qspiTrx1ErrStatus2;  /* 0x26 每通道错误汇总（bit0=CH1 ... bit3=CH4）  */
extern volatile uint16 g_qspiTrx1ErrStatus3;  /* 0x27 CH1 帧 1..4                             */
extern volatile uint16 g_qspiTrx1ErrStatus4;  /* 0x28 CH1 帧 5/6 + 模拟错误                    */
extern volatile uint16 g_qspiTrx2ErrStatus1;  /* 0x25 全局 ASIC 错误                          */
extern volatile uint16 g_qspiTrx2ErrStatus2;  /* 0x26 每通道错误汇总（bit0=CH1 ... bit2=CH3）  */
extern volatile uint16 g_qspiTrx2ErrStatus3;  /* 0x27 CH1 帧 1..4                             */
extern volatile uint16 g_qspiTrx2ErrStatus4;  /* 0x28 CH1 帧 5/6 + 模拟错误                    */

/* 从 ERROR_STATUS_3 / _4 里拆出来的单 bit 标志，调试器里直接看 0 / 1 就行 */
extern volatile uint8  g_qspiTrx1Ch1NoFrame;  /* 时间槽 1 没收到帧（传感器没回 / 没供电 / 没同步） */
extern volatile uint8  g_qspiTrx1Ch1FrameErr; /* 收到帧但帧错误（位数 / 速率 / MC 编码不对）       */
extern volatile uint8  g_qspiTrx1Ch1ParityErr;/* 收到帧但奇偶 / CRC 错                             */
extern volatile uint8  g_qspiTrx1Ch1BuffErr;  /* 数据放不进 16 位缓冲（帧比配置长）                 */
extern volatile uint8  g_qspiTrx1Ch1OverCur;  /* 过流                                             */
extern volatile uint8  g_qspiTrx1Ch1DiagCode; /* 线束诊断：0 正常 / 1 对地漏电 / 2 对电源或开路     */
extern volatile uint8  g_qspiTrx1Ch1RevCur;   /* 反向电流                                         */
extern volatile uint8  g_qspiTrx2Ch1NoFrame;
extern volatile uint8  g_qspiTrx2Ch1FrameErr;
extern volatile uint8  g_qspiTrx2Ch1ParityErr;
extern volatile uint8  g_qspiTrx2Ch1BuffErr;
extern volatile uint8  g_qspiTrx2Ch1OverCur;
extern volatile uint8  g_qspiTrx2Ch1DiagCode;
extern volatile uint8  g_qspiTrx2Ch1RevCur;

/* 收发器内部 ADC 的原始码值 */
extern volatile uint8  g_qspiTrx1Vbus;        /* VBUS 电压                         */
extern volatile uint8  g_qspiTrx1Vsync;       /* VSYNC（同步脉冲维持电压）          */
extern volatile uint8  g_qspiTrx1Vsif1;       /* SIF1 线电压（= 传感器供电 / 回帧） */
extern volatile uint8  g_qspiTrx1Vsif2;
extern volatile uint8  g_qspiTrx1Vsif3;
extern volatile uint8  g_qspiTrx1Vsif4;
extern volatile uint8  g_qspiTrx1VcpGate;     /* 电荷泵栅极电压                     */
extern volatile uint8  g_qspiTrx2Vbus;
extern volatile uint8  g_qspiTrx2Vsync;
extern volatile uint8  g_qspiTrx2Vsif1;
extern volatile uint8  g_qspiTrx2Vsif2;
extern volatile uint8  g_qspiTrx2Vsif3;
extern volatile uint8  g_qspiTrx2Vsif4;       /* trx2 的 SIF4 没接线，值无意义       */
extern volatile uint8  g_qspiTrx2VcpGate;

/* Ibase 跟踪值（复位约 0x64）：偏高 = 对地漏电，偏低 = 对电源漏电 / 开路 */
extern volatile uint8  g_qspiTrx1Idac1;
extern volatile uint8  g_qspiTrx1Idac2;
extern volatile uint8  g_qspiTrx1Idac3;
extern volatile uint8  g_qspiTrx1Idac4;
extern volatile uint8  g_qspiTrx2Idac1;
extern volatile uint8  g_qspiTrx2Idac2;
extern volatile uint8  g_qspiTrx2Idac3;
extern volatile uint8  g_qspiTrx2Idac4;

extern volatile uint32 g_qspiDiagCount;       /* 诊断寄存器已经读了几轮 */

/*********************************************************************************************************************/
/*------------------------------------------------Function Prototypes------------------------------------------------*/
/*********************************************************************************************************************/
/* 初始化 QSPI3 和两颗 E521.41：供电 Do01 -> 控制脚 -> QSPI 模块 -> 两个通道 -> 释放 NRES -> 写寄存器 -> 读回。
 * 内部有几段毫秒级的等待（供电建立 20 ms + 振荡器起振 2 ms + 稳定 10 ms），总共约 32 ms，只在启动时跑一次。
 * 跑完 g_qspiInitDone = 1。                                                                 */
void initQspi(void);

/* 主循环里调用，非阻塞。两阶段状态机：
 *   阶段 0：每 2 ms 给两颗收发器各发一条同步命令，然后算出 600 us 后的读取时刻；
 *   阶段 1：到点把 7 路缓冲读一遍，解码进 Level_* / ACC_*，然后回到阶段 0。
 * 第一帧数据在 initQspi() 之后的第一次调用后约 600 us 出现。                                */
void qspiService(void);

#endif /* QSPI_H_ */
