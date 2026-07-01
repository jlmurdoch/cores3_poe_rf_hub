/**
 * ESP32-S3 ULP-FSM RFM69HCW Continuous Data Reader
 *
 * This assembler routine reads DIO2, AKA DATA in continuous mode.
 *
 * Symbols are:
 * - Bit 0 = 500us high, 1000us low
 * - Bit 1 = 500us high, 2000us low
 * - Sync  = 500us high, 4000us low
 *
 * Pulse handling rules are:
 * - Ignore pulses < 500us
 * - Reset on sync bit
 * - Abort if significantly greater than 4000us
 *
 * Device data:
 *   value[0] 
 *      - id      - 8 bits
 *      - battery - 1 bit
 *      - button  - 1 bit
 *      - channel - 2 bits
 *      - tempmsb - 4 bits
 *   value[1]
 *      - tempmid - 4 bits
 *      - templsb - 4 bits
 *      - 0xF
 *      - humimsb - 4 bits
 *      - humilsb - 4 bits
 *
 * Registers:
 *   r0 - Working register
 *   r1 - Working register
 *   r2 - Last GPIO level [0-1]
 *   r3 - Bit counter [0-32]
 *   stage_cnt - Timer counter 
 */

#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"
#include "ulp_config.h"

    .bss

    .global state
state:
    .long 0             // 0 = no, in progress, 1 = RXOK, 2 = FAIL
    // 2 x 16-bit values
    .global value
value:  
    .long 0             // ulp_value[0]
    .long 0             // ulp_value[1]

last_val:               // Used for double-checking
    .long 0
    .long 0

    .text

    .global entry
entry:                  // (42 cycles)
                        // (8) Read GPIO low bank
    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S, 16)
    rsh r0, r0, DIO2    // (6) Shift to GPIO
    and r0, r0, 1       // (6) Is GPIO HIGH?
    add r1, r0, r2      // (6) Edge (r1 == 1) or same (r1 == 0 || 2)
    move r2, r0         // (6) Save level to r2
    and r1, r1, 1       // (6) Is it an edge (1)?     
    jump loop, eq       // (4) If not an edge, loop

edge:                   // Level change
    jumps flag, 240, GE // Timeout    (>4.8ms)
    jumps lead, 30,  LE // Lead pulse (<0.6ms)
    jumps data, 120, LE // Data pulse (<2.4ms)

sync:                   // Sync pulse (<4.8ms)
    move r0, r3         // Get bit count
    jumpr rxok, 36, EQ  // Quit if 36 bits then sync bit

reset:                  // Reset everything
    move r3, 0          // Reset bit count
    move r1, 0          // Reset all vars
    move r0, value      // r0 <- var address
    st r1, r0, 0        // r1 -> r0 data save
    move r0, value      // r0 <- var address
    st r1, r0, 4        // r1 -> r0 data save
    move r3, 0          // Reset bit count

lead:                   // Lead pulse - soft reset
    stage_rst           // Reset timer
    jump loop           // Continue

loop:                   // (20 cycles)
    wait 288            // (6) 288 + 20 + 42 = 350 cycles = 20ms
    stage_inc 1         // (6) Timer increment
    jumps flag, 240, GE // (4) break()
    jump entry          // (4) Read again

data:                   // Valid bit
    add r3, r3, 1       // Increment bit count
    move r0, r3         // Eval bit count
    jumpr msb, 16, LE   // MSB short [1-16]
    jumpr lsb, 24, LE   // LSB short [17-24]
    jumpr lsb, 29, GE   // LSB short [29-36]
    stage_rst           // Ignore the 0xF = 36 to 32 bits!
    jumpr loop, 28, LE  // Skip bits [25-28]

msb:                    // MSB short [0-15]
    move r1, value      // r1 <- var address
    ld r0, r1, 0        // r0 <- r1[0] data load
    lsh r1, r0, 1       // r1 = r0 << 1
    jumps msb0, 60, LT  // Add zero?
msb1:
    add r1, r1, 1       // Add one: r1++
msb0:
    move r0, value      // r0 <- var address
    st r1, r0, 0        // r1 -> r0[0] data save
    stage_rst           // Reset timer
    jump loop           // Continue

lsb:                    // LSB short [17-24, 29-36]
    move r1, value      // r1 <- var address
    ld r0, r1, 4        // r0 <- r1[1] data load
    lsh r1, r0, 1       // r1 = r0 << 1
    jumps lsb0, 60, LT  // Add zero?
lsb1:
    add r1, r1, 1       // Add one: r1++
lsb0:
    move r0, value      // r0 <- var address
    st r1, r0, 4        // r1 -> r0[1] data save
    stage_rst           // Reset timer
    jump loop           // Continue

store:
    move r0, value
    ld r1, r0, 0
    move r0, last_val
    st r1, r0, 0        // store value[0] to last_val[0]
    move r0, value
    ld r1, r0, 4
    move r0, last_val   // store value[1] to last_val[1]
    st r1, r0, 4
    jump entry

rxok:
    stage_rst           // Reset timer
    move r0, value
    ld r1, r0, 0
    move r0, last_val
    ld r2, r0, 0
    sub r0, r1, r2      // Compare value[0] with last value[0]
    jumpr store, 1, ge

    move r0, value
    ld r1, r0, 4
    move r0, last_val
    ld r2, r0, 4
    sub r0, r1, r2      // Compare value[1] with last value[1]
    jumpr store, 1, ge

coda:                   // loop of 3.7ms exactly
    wait 64734          // (6) 16 + 64734 = 64750 = 3.7.ms
    stage_inc 1         // (6) Increment counter
    jumps coda, 175, LE // (4) 175 x 3.7ms = 647.5ms = 92.5ms x 7 repeats
    move r1, 1          // Set state to RXOK
    move r0, state      // r0 <- var address
    st r1, r0, 0        // r1 -> r0 data save 

flag:                   // Notify main code of state
    // "Set to 1, then that pin i/o is routed to the RTC subsystem" 
    WRITE_RTC_REG(RTC_IO_TOUCH_PAD1_REG, RTC_IO_TOUCH_PAD1_MUX_SEL_S, 1, 1)

    // Allow low level GPIO setting
    WRITE_RTC_REG(RTC_GPIO_ENABLE_W1TS_REG, RTC_GPIO_ENABLE_W1TS_S + GPIO_RX_DONE, 1, 1)

    // ULP started flag via GPIO
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + GPIO_RX_DONE, 1, 1)

    move r0, state      // r0 <- var address
    ld r1, r0, 0        // r1 <- r0 data load
    and r1, r1, 1       // r1 == 1
    jump exit, eq        // If already RXOK, exit 

    move r0, state      // r0 <- var address
    move r1, 2          // Set state to FAIL
    st r1, r0, 0        // r1 -> r0 data save

exit:
    // Cancel sleep
    WRITE_RTC_FIELD(RTC_CNTL_ULP_CP_TIMER_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN, 0)
    // signal low to indicate a RX has been done
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + GPIO_RX_DONE, 1, 1)
    halt                // Stop