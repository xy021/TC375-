/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#ifndef SBC_CAN_H_
#define SBC_CAN_H_

#include "Ifx_Types.h"

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
/* This file configures the CAN transceiver inside the SBC (U104, TLE9471-3ES). It does not configure the MCU's own
 * CAN controller - that is a separate job, and the two are easy to confuse because both are called "CAN
 * configuration". The split is:
 *
 *   SBC side   BUS_CTRL_0, register 0x04 - which mode the transceiver is in. This file.
 *              Pins: U104 pin 7 TXDCAN  and pin 14/13 CANH/CANL, and VCAN on pin 1, which is an internal
 *              regulator output decoupled by C226 and not something software can switch.
 *
 *   MCU side   CAN00 module, and its two pins from the pin assignment table:
 *                row 77  P33.7  ID_Can1RxCan00_M  CAN00_RXDE  input   <- SBC's RXD
 *                row 78  P33.8  OD_Can1TxCan00_M  CAN00_TXD   output  -> SBC's TXD
 *              Nothing in this file touches those. Until the CAN00 module is set up and driving P33.8, the SBC is
 *              a transceiver with nothing on its TXD but the part's own internal pull up, which is recessive.
 *              That is a safe state - it cannot put a dominant level on the bus - so the transceiver can be
 *              configured before the controller exists.                                                    */

/* BUS_CTRL_0, 7 bit register address 0x04. The CAN mode is a THREE bit field in bits 2:0 for the TLE9471-3ES.
 * Note the revision trap: the older TLE9471ES uses a two bit field where 11B is Normal. Sending the older part's
 * 11B to this one would select "CAN Normal Mode with SWK", a different thing. The part on this board is the -3ES,
 * so the three bit table applies.                                                                             */
#define SBC_CAN_MASK          0x07u

#define SBC_CAN_MODE_OFF             0x00u   /* 000, transceiver off                                  */
#define SBC_CAN_MODE_WAKE            0x01u   /* 001, wake capable, no selective wake                  */
#define SBC_CAN_MODE_RECEIVE_ONLY    0x02u   /* 010, receiver on, transmitter off - cannot drive bus   */
#define SBC_CAN_MODE_NORMAL          0x03u   /* 011, normal transceiver operation                     */
#define SBC_CAN_MODE_OFF_ALT         0x04u   /* 100, off                                              */
#define SBC_CAN_MODE_WAKE_SWK        0x05u   /* 101, wake capable with selective wake                 */
#define SBC_CAN_MODE_RECEIVE_SWK     0x06u   /* 110, receive only with selective wake                 */
#define SBC_CAN_MODE_NORMAL_SWK      0x07u   /* 111, normal with selective wake                       */

/* The mode this board comes up in.
 *
 * Normal Mode, 011. Chosen deliberately over the alternatives:
 *   - CAN Off would make the whole file pointless.
 *   - The SWK variants need a whole additional configuration (bit timing, frame filters in BUS_CTRL_1/2/3 and the
 *     message RAM) before they mean anything; with none of that done they are just Normal Mode with extra ways to
 *     go wrong.
 *   - Receive Only is the safe choice if the CAN bus is ever shared with something that matters while the MCU's
 *     CAN00 is still unconfigured, because it cannot drive the bus at all. That is a one word change below.
 *
 * Normal Mode is safe here even before the MCU's CAN00 exists: TXD on this part has an internal pull up to VCC1,
 * so an undriven TXDCAN reads as recessive, which is bus idle. On power up BUS_CTRL_0 resets to 0000 0000B, so
 * the transceiver is off until this file runs.                                                              */
#define SBC_CAN_STARTUP_MODE   SBC_CAN_MODE_NORMAL

/*********************************************************************************************************************/
/*------------------------------------------------Global Variables---------------------------------------------------*/
/*********************************************************************************************************************/
/* What BUS_CTRL_0 read back as after the last write, masked to the CAN field. Watch this one, not the constant:
 * 0x03 means the transceiver took Normal Mode, 0x00 means the write did not land, and 0x07 means an older two bit
 * encoding was assumed somewhere. It is a plain global so it can be watched without touching the debugger.    */
extern volatile uint32 g_sbcCanMode;

/*********************************************************************************************************************/
/*------------------------------------------------Function Prototypes------------------------------------------------*/
/*********************************************************************************************************************/
/* Puts the SBC's CAN transceiver into SBC_CAN_STARTUP_MODE and records what it read back.
 *
 * Call it AFTER initSbc(). It writes a register, so it needs the SPI link and the SBC in Normal mode, and it must
 * not be allowed to stand between the board and its power - which is why it is a separate call and not folded into
 * initSbc(). A CAN problem may not keep VCC2 from coming up.                                                 */
void    initSbcCan(void);

/* Change the transceiver mode at any time. Only legal while the SBC is in Normal Mode; in Stop or Sleep the SBC
 * ignores the write. Also preserves whatever else is in BUS_CTRL_0.                                        */
void    sbcCanSetMode(uint8 mode);

#endif /* SBC_CAN_H_ */
