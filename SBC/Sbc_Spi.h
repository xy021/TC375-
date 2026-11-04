/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#ifndef SBC_SPI_H_
#define SBC_SPI_H_

#include "Ifx_Types.h"

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
/* The TLE9471-3ES sits on QSPI4. The four nets, from the schematic (SBC原理图.jpg, U104) and from the pin assignment
 * table rows 95..98:
 *
 *   P22.0   OD_SbcSdiQspi4_M   QSPI4_MTSR    U104 pin 4   SDI      MCU -> SBC
 *   P22.1   ID_SbcSdoQspi4_M   QSPI4_MRSTB   U104 pin 6   SDO      SBC -> MCU
 *   P22.2   OS_SbcNcsQspi4_M   QSPI4_SLSO3   U104 pin 3   nCS      MCU -> SBC
 *   P22.3   OD_SbcClkQspi4_M   QSPI4_SCLK    U104 pin 5   CLK      MCU -> SBC
 *
 * Driven as four plain GPIOs rather than through the QSPI4 peripheral. The bus is only touched during startup and
 * once per second for the watchdog, so its speed is irrelevant, and bit banging removes every assumption about an
 * alternate function coming out of this part - this board already has one pin whose alternate function silently
 * produces nothing while the same pin works as a GPIO (see the note in Driver.h about P20.14). Four GPIOs that are
 * known to work are a better trade than one peripheral that may not.                                          */
#define SBC_SPI_PORT        (&MODULE_P22)
#define SBC_SPI_SDI_PIN     0                                                  /* P22.0, U104 pin 4, MCU -> SBC */
#define SBC_SPI_SDO_PIN     1                                                  /* P22.1, U104 pin 6, SBC -> MCU */
#define SBC_SPI_NCS_PIN     2                                                  /* P22.2, U104 pin 3, active low */
#define SBC_SPI_CLK_PIN     3                                                  /* P22.3, U104 pin 5, idle low   */

/* ---- SPI frame --------------------------------------------------------------------------------------------------
 * One command is exactly 16 bits, LSB first in both directions, and the SBC supervises the frame: 16 clocks and 16
 * clocks only, with CLK low whenever nCS moves. The layout is
 *
 *   bit  0..6   register address
 *   bit  7      0 = read, 1 = write (for a status register, 1 = read and clear)
 *   bit  8..15  data byte
 *
 * so the word handed to the wire is  (data << 8) | (write << 7) | address,  and it goes out bit 0 first.       */
#define SBC_SPI_READ        0u
#define SBC_SPI_WRITE       1u

/* ---- Clock ------------------------------------------------------------------------------------------------------
 * CPOL = 0, CPHA = 1. Idle low; the SBC takes SDI on the falling edge and moves SDO on the rising edge, so the MCU
 * presents a bit on the rising edge and reads the reply after the falling edge. Half a bit time is the loop below
 * run SBC_SPI_HALF_BIT_LOOPS times, roughly 0.5 us at 200 MHz, which puts SCLK near 1 MHz. The exact figure is not
 * critical - the SBC has a maximum clock but no minimum, and this is far under the maximum.                  */
#define SBC_SPI_HALF_BIT_LOOPS   20u

/* ---- TLE9471-3ES registers. Addresses are the 7 bit field of the frame above. ---------------------------------- */
#define SBC_REG_M_S_CTRL      0x01u   /* mode and supply control                                        */
#define SBC_REG_HW_CTRL_0     0x02u   /* charge pump, fail-safe output, soft reset                      */
#define SBC_REG_WD_CTRL       0x03u   /* watchdog                                                       */
#define SBC_REG_BUS_CTRL_0    0x04u   /* CAN transceiver mode - see Sbc_Can.c                           */
#define SBC_REG_BUS_CTRL_1    0x05u   /* further CAN and wake configuration                             */
#define SBC_REG_WK_CTRL_0     0x06u   /* wake inputs, and watchdog deactivation bit 1                   */
#define SBC_REG_WK_CTRL_1     0x07u   /* wake inputs, filter time                                       */
#define SBC_REG_WK_PUPD_CTRL  0x08u   /* wake input pull up / pull down                                 */
#define SBC_REG_BUS_CTRL_3    0x0Bu   /* further CAN configuration                                      */
#define SBC_REG_BUS_STAT      0x0Cu   /* CAN bus status, read to clear                                  */

/* M_S_CTRL data byte. This is the register that decides what the board's rails do.                              */
#define SBC_MS_MODE_NORMAL    0x00u                                            /* bits 7:6, 00 = SBC Normal     */
#define SBC_MS_MODE_SLEEP     0x40u                                            /* bits 7:6, 01                  */
#define SBC_MS_MODE_STOP      0x80u                                            /* bits 7:6, 10                  */
#define SBC_MS_MODE_SOFT_RESET 0xC0u                                           /* bits 7:6, 11                  */
/* VCC2 is the board net A5V. This is the only switch for it: no other register, no other pin, and no way to
 * turn A5V on from the MCU side. A5V is the EVADC's reference (VAREF1, VAREF2 and the MCU's VDDM pin, pin
 * table rows 52..55), so VCC2 off means every ADC reading on this board is noise. Reading it as "VCC2" hides
 * that - the debugger flag that follows it is named after the net, see g_sbcA5V_on below.
 *
 * 01 (on in Normal) is what this board runs with and what has been confirmed on the bench. 11 (always on)
 * would survive the SBC dropping out of Normal mode, but it is a bit pattern this project has never had
 * confirmed and the datasheet behind it is a web mirror - switching to it is an experiment, not a fix.  */
#define SBC_MS_VCC2_OFF       0x00u                                            /* bits 4:3, 00 = VCC2 off       */
#define SBC_MS_VCC2_NORMAL    0x08u                                            /* bits 4:3, 01 = on in Normal   */
#define SBC_MS_VCC2_STOP      0x10u                                            /* bits 4:3, 10 = on in Normal+Stop */
#define SBC_MS_VCC2_ALWAYS    0x18u                                            /* bits 4:3, 11 = always on      */
#define SBC_MS_VCC1_OV_RST    0x04u                                            /* bit 2, restart on VCC1 over voltage */

/* HW_CTRL_0 data byte. */
#define SBC_HW_SOFT_RESET_RST 0x40u                                            /* bit 6                         */
#define SBC_HW_FO_ON          0x20u                                            /* bit 5, fail-safe output on    */
#define SBC_HW_CP_EN          0x04u                                            /* bit 2, charge pump enable     */
#define SBC_HW_CFG1           0x01u                                            /* bit 0                         */

/* WD_CTRL data byte. Bit 7 is not a field, it is a check sum: the eight bits of the data byte must contain an even
 * number of ones or the SBC ignores the command and raises SPI_FAIL. sbcWatchdogData() fills it in.             */
#define SBC_WD_WIN_WINDOW     0x20u                                            /* bit 5, 1 = window watchdog    */
#define SBC_WD_TIMER_10MS     0x00u                                            /* bits 2:0                      */
#define SBC_WD_TIMER_20MS     0x01u
#define SBC_WD_TIMER_50MS     0x02u
#define SBC_WD_TIMER_100MS    0x03u
#define SBC_WD_TIMER_200MS    0x04u                                            /* the POR default               */
#define SBC_WD_TIMER_500MS    0x05u
#define SBC_WD_TIMER_1000MS   0x06u
#define SBC_WD_TIMER_10000MS  0x07u                                            /* the longest the part offers   */

/*********************************************************************************************************************/
/*------------------------------------------------Global Variables---------------------------------------------------*/
/*********************************************************************************************************************/
/* All of these are plain globals named after what they are, so they can be watched in the debugger.
 *
 * g_sbcSpiFrames   climbs once per 16 bit frame on the wire. It is the only proof that the bus is moving at all:
 *                  a register that did not take, a chip that is not answering and a command that was never sent
 *                  all look the same from the outside, and this tells the three apart.
 * g_sbcReadback    the data byte the last sbcReadRegister() got back; 0xFF means nothing answered on SDO.
 * g_sbcWrites      number of write frames that have gone out since reset.
 * g_sbcWatchdogMs  the watchdog period the last successful WD_CTRL write asked for, in milliseconds. 200 is the
 *                  value the part comes up with, 10000 is what initSbc() asks for.                      */
extern volatile uint32 g_sbcSpiFrames;
extern volatile uint32 g_sbcReadback;
extern volatile uint32 g_sbcWrites;
extern volatile uint32 g_sbcWatchdogMs;

/* 1 while M_S_CTRL says VCC2 is on, which is the same statement as A5V being up. Decoded out of the register
 * rather than left as one, because a flag named after the net is what can be watched directly - 0x08 in
 * g_sbcReadback means the same thing but has to be decoded by hand every time.
 *
 * Refreshed once per second alongside the watchdog feed, not just at init: a value read once at startup would
 * keep claiming A5V is on long after the SBC dropped out of Normal mode and took VCC2 down with it. If this
 * ever reads 0, the ADC has no reference and IA_Vddv_M and IA_M_LsCurSv1 are both meaningless.          */
extern volatile uint32 g_sbcA5V_on;

/* 1 while HW_CTRL_0 says the charge pump is on - and the charge pump is what drives the gates of the two main supply
 * FETs, so this one bit is both of the board's rails. Refreshed the same way g_sbcA5V_on is: sbcServiceChargePump()
 * re-writes the register every second and decodes what comes back.
 *
 * A5V good and this good are two separate statements, and keeping them separate is the point. On a cold start A5V
 * came up and this did not, which is the whole of "the LED breathes and nothing else works": VCP = 0 holds Q105 and
 * Q106 off, so Vddv and Vddm both sit at the ~1.5 V of leakage that looks exactly like a hardware fault.       */
extern volatile uint32 g_sbcCpOn;

/*********************************************************************************************************************/
/*------------------------------------------------Function Prototypes------------------------------------------------*/
/*********************************************************************************************************************/
/* Brings the SBC up: pins, then Normal mode with VCC2 on, then the charge pump, then the watchdog. Call it as
 * early as the clock allows - before it runs, the SBC is free to reset the MCU at any moment. See the long note in
 * Sbc_Spi.c for what "free to reset" means.                                                                    */
void    initSbc(void);

/* Feeds the watchdog. Non blocking and safe to call every pass of the main loop: it decides for itself whether
 * enough time has passed.                                                                                      */
void    sbcServiceWatchdog(void);

/* One register access. TRUE means the frame was sent; it is not a claim that the SBC acted on it.              */
boolean sbcWriteRegister(uint8 address, uint8 data);
boolean sbcReadRegister(uint8 address, uint8 *data);

/* Read, change the bits in mask, write back. For registers that hold fields this file does not own - any bit
 * outside the mask is preserved. BUS_CTRL_0 is the one that needs it.                                          */
boolean sbcModifyRegister(uint8 address, uint8 mask, uint8 value);

/* Adds the even-parity check sum bit that WD_CTRL needs. Exposed because Sbc_Can.c and anything else that writes
 * WD_CTRL has to use it - a WD_CTRL write without it is silently dropped.                                     */
uint8   sbcWatchdogData(uint8 data);

#endif /* SBC_SPI_H_ */
