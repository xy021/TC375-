/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#ifndef RTC_H_
#define RTC_H_

#include "Ifx_Types.h"

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
/* 板载 RTC：Seiko Epson RA4000CE，4 线 SPI（Option D/E），挂在 QSPI1 上：
 *
 *   P10.1  ID_RtcDoQspi1_M   QSPI1_MRSTA   输入   RTC 的 DO -> MCU
 *   P10.2  OD_RtcClkQspi1_M  QSPI1_SCLKA   输出   SCLK
 *   P10.3  OD_RtcDiQspi1_M   QSPI1_MTSRA   输出   MCU -> RTC 的 DI
 *   P11.11 OS_RtcNcsQspi1_M  QSPI1_SLSO4   输出   CE（注意是高有效，不是低有效）
 *   P11.12 IS_RtcAwa_M       普通 GPIO     输入   RTC 的 /INT，开漏低有效
 *
 * SPI 参数（RA4000CE 数据手册）：8 位一帧、MSB 先、SCLK 空闲高、上升沿采样 / 下降沿移出
 * （SPI 模式 3）、SCLK <= 4 Mbit/s。片选 CE 高有效，CE 拉低会取消一次传输，芯片内部有下拉。
 *
 * 一次寄存器访问 = 地址字节 + 数据字节，CE 全程保持有效。地址字节 bit7 = 1 读 / 0 写，
 * 低 4 位地址在 burst 内自增（0xF 之后回到 0x0）。所以从 0x00 起连续读 7 字节就是
 * 秒 / 分 / 时 / 星期 / 日 / 月 / 年，一次读完是一个时间快照。
 *
 * 注意：本模块没有用 iLLD 的 IfxQspi_SpiMaster_write()。那个接口是逐字收发的，
 * 每个字之间会抬高片选，而 RA4000CE 要求一根片选从头到尾包住整个 burst。
 * 这里直接用 iLLD 底层的 BACON/DATA FIFO 接口做流式传输：
 *   BACONENTRY(LAST=0) -> data[0..n-2] -> BACONENTRY(LAST=1) -> data[n-1]
 * 前 n-1 个字发送时 LAST=0，片选保持有效；最后一个字发送时 LAST=1，片选在帧尾释放。 */
#define RTC_QSPI_MAX_BAUDRATE         4000000.0f   /* 模块上限（iLLD 用它算 TQ）                 */
#define RTC_QSPI_CHANNEL_BAUDRATE     1000000.0f   /* 通道实际速率，数据手册上限 4 Mbit/s        */
#define RTC_SPI_CS_DELAY              7u           /* 片选前/后延时，单位半个 SCLK = 3.5 us      */
#define RTC_SPI_TIMEOUT_US            1000u        /* 一次 burst 的等待上限（正常 8 字节 < 100 us） */
#define RTC_SPI_MAX_BURST             8u           /* 本模块一次最多搬 8 字节                     */

#define RTC_SPI_READ                  0x80u        /* 地址字节 bit7 = 1 -> 读                     */

/* 时钟 / 日历寄存器，都是 BCD 编码 */
#define RTC_REG_SEC                   0x00u        /* 秒 00..59                                   */
#define RTC_REG_MIN                   0x01u        /* 分 00..59，bit7 保留                        */
#define RTC_REG_HOUR                  0x02u        /* 时 00..23（24 小时制），bit7/6 保留         */
#define RTC_REG_WEEKDAY               0x03u        /* 星期，7 位 one-hot：bit0 = 周日 ... bit6 = 周六 */
#define RTC_REG_DAY                   0x04u        /* 日 01..31                                   */
#define RTC_REG_MONTH                 0x05u        /* 月 01..12                                   */
#define RTC_REG_YEAR                  0x06u        /* 年 00..99，只存后两位                       */

/* 定时器 / 唤醒定时器寄存器 */
#define RTC_REG_WTCNT_L               0x0Au        /* 唤醒计数器预置值低字节                      */
#define RTC_REG_WTCNT_M               0x0Bu
#define RTC_REG_WTCNT_H               0x0Cu
#define RTC_REG_TCTL                  0x0Du        /* FSEL[7:6] USEL0[5] TE[4] WADA[3] - TSEL[1:0] */
#define RTC_REG_INTF                  0x0Eu        /* 标志寄存器，写 0 清除，写 1 无效            */
#define RTC_REG_TSTP_INTE             0x0Fu        /* CSEL[7:6] UIE[5] TIE[4] AIE[3] EIE[2] - STOP[0] */
#define RTC_REG_WTICFG                0x38u        /* PINMUX[7:6] RSTOPT[5:4] WTONETIM[3] TSEL2[2] WTIOUT[1] UPDOWNMOD[0] */
#define RTC_REG_WTCTL                 0x39u        /* WTRST[7] EXWTRSTEN[6] WTRSTWIN[5:4] WTMODSEL[3] WTSTOPCTL[2] - WTSTOP[0] */

/* 寄存器位定义 */
#define RTC_TCTL_TSEL_MASK            0x03u        /* TSEL[1:0]：00 = 1024 Hz（唤醒定时器用）     */
#define RTC_TCTL_TE                   0x10u        /* 定时器使能                                  */
#define RTC_INTF_PORF                 0x80u        /* 上电复位标志                                */
#define RTC_INTF_OSCSTPF              0x40u        /* 振荡停止标志                                */
#define RTC_INTF_UF                   0x20u        /* 时间更新标志                                */
#define RTC_INTF_TF                   0x10u        /* 唤醒定时器标志，写 0 清除                   */
#define RTC_INTF_AF                   0x08u        /* 报警标志                                    */
#define RTC_INTF_EVF                  0x04u        /* 事件标志                                    */
#define RTC_INTF_VLF                  0x02u        /* 电压低标志，上电后要写 0 清掉才能配置寄存器 */
#define RTC_INTF_VTMPLF               0x01u        /* 温度低标志                                  */
#define RTC_TSTP_INTE_TIE             0x10u        /* 定时器中断使能                              */
#define RTC_TSTP_INTE_STOP            0x01u        /* 0 = 走时                                    */
#define RTC_WTICFG_WTONETIM           0x08u        /* /INT 保持低直到软件清掉 TF                  */
#define RTC_WTICFG_TSEL2              0x04u
#define RTC_WTICFG_WTIOUT             0x02u
#define RTC_WTICFG_UPDOWNMOD          0x01u        /* 1 = 加法计数                                */
#define RTC_WTCTL_WTSTOP              0x01u        /* 0 = 唤醒定时器运行                          */

/* 唤醒定时器：源时钟 1024 Hz，周期 = 预置值 / 1024 秒。
 * 预置值是 24 位，范围 1..16777215，不能为 0，所以整数秒最大 16383 s。
 * 默认 15 s -> 预置 15 * 1024 = 15360 = 0x3C00。                                          */
#define RTC_WAKEUP_SOURCE_HZ          1024u
#define RTC_WAKEUP_PERIOD_DEFAULT_S   15u
#define RTC_WAKEUP_PERIOD_MAX_S       16383u

#define RTC_POWER_ON_DELAY_MS         40u          /* 上电到寄存器可访问，数据手册 tCL = 40 ms   */
#define RTC_CLOCK_REFRESH_MS          100u         /* 主循环里读时钟 / 查唤醒标志的周期           */
#define RTC_CLOCK_BURST               8u           /* 0x00 起地址字节 + 7 个数据字节              */

/*********************************************************************************************************************/
/*-----------------------------------------------Debugger variables--------------------------------------------------*/
/*********************************************************************************************************************/
/* 调试器里直接看的全局变量。都是普通全局变量，不是宏、也不是位域打包，名字按网表 / 信号名起。 */

/* --- 板级网络 ------------------------------------------------------------------------------------------- */
extern volatile uint32 IS_RtcAwa_M;           /* P11.12，RTC 的 /INT，0 = 有唤醒（低有效）              */

/* --- 状态 / 计数 ---------------------------------------------------------------------------------------- */
extern volatile uint32 g_rtcInitDone;         /* initRtc() 跑完置 1；rtcService() 先看这一位            */
extern volatile uint32 g_rtcQspiStatus;       /* IfxQspi_SpiMaster_initChannel 的返回值，0 = ok          */
extern volatile uint32 g_rtcRealBaudrate;     /* 通道实际速率（iLLD 算出来的，单位 Hz）                  */
extern volatile uint32 g_rtcCommOk;           /* 最近一次 SPI 访问是否成功                               */
extern volatile uint32 g_rtcOscOk;            /* INTF.VLF / OSCSTPF 清掉后没有再次置位                    */
extern volatile uint32 g_rtcClockValid;       /* 读回来的时间在合法范围内                                */
extern volatile uint32 g_rtcServiceCount;     /* rtcService() 被调用的次数                               */
extern volatile uint32 g_rtcClockReadCount;   /* 成功读到日历的次数（每 100 ms 加 1）                    */
extern volatile uint32 g_rtcWakeupCount;      /* 唤醒定时器到期的次数（每 15 s 加 1）                    */
extern volatile uint32 g_rtcTimeoutCount;     /* SPI FIFO 等待超时的次数                                 */
extern volatile uint32 g_rtcErrorCount;       /* 参数 / 状态错误的次数                                   */

/* --- 调试旋钮 ------------------------------------------------------------------------------------------- */
/* 唤醒周期（秒），默认 15。调试器里直接改这个值：rtcService() 每 100 ms 检查一次，
 * 发现和已经写进芯片的值不一样，就按新周期重新配置一遍唤醒定时器（计数器从新预置值重新开始）。 */
extern volatile uint32 g_rtcWakeupPeriodS;

/* --- 唤醒定时器 ----------------------------------------------------------------------------------------- */
extern volatile uint32 g_rtcWakeupPreset;     /* 实际写进 WTCNT_L/M/H 的 24 位预置值                    */
extern volatile uint32 g_rtcWakeupPeriodApplied; /* 已经写进芯片的周期（秒）                            */
extern volatile uint32 g_rtcIntf;             /* 最近一次读到的 INTF                                    */
extern volatile uint32 g_rtcIntfInit;         /* initRtc() 里第一遍读到的 INTF（PORF / OSCSTPF / VLF） */
extern volatile uint32 g_rtcIntfAfterClear;   /* 清完标志后读回来的 INTF                                 */
extern volatile uint32 g_rtcWticfgReadback;   /* 唤醒配置完成后回读的 WTICFG                            */

/* --- 时间（BCD 已经解成十进制） ------------------------------------------------------------------------- */
extern volatile uint8  g_rtcSecond;           /* 0..59                                                  */
extern volatile uint8  g_rtcMinute;           /* 0..59                                                  */
extern volatile uint8  g_rtcHour;             /* 0..23                                                  */
extern volatile uint8  g_rtcWeekday;          /* 1 = 周日 ... 7 = 周六                                  */
extern volatile uint8  g_rtcDay;              /* 1..31                                                  */
extern volatile uint8  g_rtcMonth;            /* 1..12                                                  */
extern volatile uint8  g_rtcYear;             /* 0..99（20xx 年的后两位）                               */

/* --- 原始寄存器值（BCD） -------------------------------------------------------------------------------- */
extern volatile uint8  g_rtcSecRaw;
extern volatile uint8  g_rtcMinRaw;
extern volatile uint8  g_rtcHourRaw;
extern volatile uint8  g_rtcWeekdayRaw;       /* one-hot 原始值                                         */
extern volatile uint8  g_rtcDayRaw;
extern volatile uint8  g_rtcMonthRaw;
extern volatile uint8  g_rtcYearRaw;

/*********************************************************************************************************************/
/*------------------------------------------------Function Prototypes------------------------------------------------*/
/*********************************************************************************************************************/
/* 初始化 QSPI1 + RA4000CE：P11.12 -> 等 40 ms -> 配置 QSPI 模块和通道 -> 清 INTF 自监控标志
 * -> 配置唤醒定时器（默认 15 s）-> 读一次时间。只在启动时跑一次，跑完 g_rtcInitDone = 1。       */
void initRtc(void);

/* 主循环里调用，非阻塞。每 100 ms 做三件事：
 *   1. 调试器改了 g_rtcWakeupPeriodS 就按新周期重新配置唤醒定时器；
 *   2. 读一遍日历寄存器，刷新 g_rtcSecond ... g_rtcYear；
 *   3. 读 INTF，TF 置位说明唤醒定时器到期：g_rtcWakeupCount 加 1 并清 TF（/INT 随之释放）。
 * 每次调用都会刷新 IS_RtcAwa_M（P11.12 电平，0 = /INT 正在拉低）。                         */
void rtcService(void);

/* 底层寄存器访问：地址 bit7 = 1 读 / 0 写，一次 2 字节（地址 + 数据），CE 全程有效。
 * initRtc() 之后才能调用；返回 FALSE 表示 SPI 没通（g_rtcCommOk = 0）。                      */
boolean rtcReadRegister(uint8 address, uint8 *value);
boolean rtcWriteRegister(uint8 address, uint8 value);

/* 读 / 写整套时间。写的时候 year 传 0..99，weekday 传 1（周日）..7（周六），
 * 函数内部会转成 BCD 和星期 one-hot；写完自动回读一次。                                     */
boolean rtcReadClock(void);
boolean rtcSetClock(uint8 year, uint8 month, uint8 day, uint8 weekday, uint8 hour, uint8 minute, uint8 second);

/* 按数据手册的 9 步流程配置唤醒定时器，periodSeconds 会被限制到 1..16383。
 * 成功后会更新 g_rtcWakeupPreset / g_rtcWakeupPeriodApplied / g_rtcWakeupPeriodS。          */
boolean rtcApplyWakeupPeriod(uint32 periodSeconds);

#endif /* RTC_H_ */
