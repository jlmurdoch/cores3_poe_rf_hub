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

    .global state       // 0 = ONGOING, 1 = RXOK, 2 = FAIL
state:
    .long 0             
    
    .global value       // 2 x 16-bit values
value:  
    .long 0x0           // ulp_value[0]
    .long 0x0           // ulp_value[1]

value_cache:            // Used for double-checking
    .long 0x0
    .long 0x0

    .text

/*
 * Entry point and reset
 */
    .global entry
entry:                      // Reset everything
    stage_rst               // Reset timer counter
    move r3, 0              // Reset bit count

    move r1, 0xFFFF         // Reset value[0] and value[1]
    move r0, value          // r0 <- var address
    st r1, r0, 0            // r1 -> r0 data save
    move r0, value          // r0 <- var address
    st r1, r0, 4            // r1 -> r0 data save

/*
 * Read GPIO and edge detection. Time sensitive.
 */
read:                       // (42 cycles)
                            // (8) Read GPIO low bank
    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S, 16)
    rsh r0, r0, DIO2        // (6) Shift to GPIO
    and r0, r0, 1           // (6) Is GPIO HIGH?
    add r1, r0, r2          // (6) Edge (r1 == 1) or same (r1 == 0 || 2)
    move r2, r0             // (6) Save level to r2
    and r1, r1, 1           // (6) Is it an edge (1)?     
    jump loop, eq           // (4) If not an edge, loop

/*
 * Edge processing: Use stage counter to determine time elapsed 
 */
edge:                       // Level change
    jumps lead, 30,  LE     // Lead pulse (<0.6ms)
    jumps data, 120, LE     // Data pulse (<2.4ms)
    move r0, r3             // Get the number of bits
    jumpr rxok, 36, EQ      // If long and 36 bits processed, go to RX OK
    jump entry              // Restart if anything else

/*
 * Data pulse procedure: Determine if zero and one (or ignore)
 */
data:                       // Valid bit
    and r0, r2, 1           // Quit if level is 1 - data should be 0
    jump entry, EQ          // Quit
    add r3, r3, 1           // Increment bit count
    move r0, r3             // Eval bit count
    jumpr msb, 16, LE       // MSB short [1-16]
    jumpr lsb, 24, LE       // LSB short [17-24]
    jumpr lsb, 29, GE       // LSB short [29-36]

    jumps entry, 60, LT     // Reset if data not one [25-28]
    jump lead               // Ignore like a lead pulse for bits [25-28]

/*
 * Add bit to value[0] (MSB)
 */
msb:                        // MSB short [0-15]
    move r1, value          // r1 <- var address
    ld r0, r1, 0            // r0 <- r1[0] data load
    lsh r1, r0, 1           // r1 = r0 << 1
    jumps msb0, 60, LT      // Add zero?
msb1:
    add r1, r1, 1           // Add one: r1++
msb0:
    move r0, value          // r0 <- var address
    st r1, r0, 0            // r1 -> r0[0] data save
    stage_rst               // Reset timer counter
    jump loop               // Continue

/*
 * Add bit to value[0] (LSB)
 */
lsb:                        // LSB short [17-24, 29-36]
    move r1, value          // r1 <- var address
    ld r0, r1, 4            // r0 <- r1[1] data load
    lsh r1, r0, 1           // r1 = r0 << 1
    jumps lsb0, 60, LT      // Add zero?
lsb1:
    add r1, r1, 1           // Add one: r1++
lsb0:
    move r0, value          // r0 <- var address
    st r1, r0, 4            // r1 -> r0[1] data save

/*
 * Lead pulse procedure: Do nothing other than reset the timer
 */ 
lead:                       // Lead pulse - soft reset
    stage_rst               // Reset timer counter, fall through

/*
 * Looping procedure: Spins the ULP for a number of cycles. Also detects timeouts. Time sensitive.
 */ 
loop:                       // (20 cycles)
    wait 300                // (6) changed from 288 to 300 for SX127x (sensitivity?)
    stage_inc 1             // (6) Timer increment
    jumps timeout, 254, GE  // (4) break() if time is too long for a reasonable pulse
    jump read               // (4) Read again

/*
 * Cache procedure: Move both values into value_cache
 */ 
cache:
    move r0, value          // store value[0] to value_cache[0]
    ld r1, r0, 0
    move r0, value_cache
    st r1, r0, 0        

    move r0, value          // store value[1] to value_cache[1]
    ld r1, r0, 4
    move r0, value_cache   
    st r1, r0, 4
    
    jump entry              // Continue, looking for new set of values

/*
 * Timeout procedure: flag failure and do a hard exit (no waiting)
 */
timeout:
    move r1, 2              // Set state to TIMEOUT
    move r0, state          // r0 <- var address
    st r1, r0, 0            // r1 -> r0 data save 
    jump exit               // Premature exit

/*
 * RX OK procedure: check if last two valid values match
 */
rxok:
    move r0, value          // Match value[0] with cached value[0]
    ld r1, r0, 0
    move r0, value_cache
    ld r2, r0, 0
    and r0, r1, r2      
    jump cache, EQ          // No match, cache value[0] and value[1]

    move r0, value          // Match value[1] with cached value[1]
    ld r1, r0, 4
    move r0, value_cache
    ld r2, r0, 4
    and r0, r1, r2          // No match, cache value[0] and value[1]
    jump cache, EQ

    stage_rst               // Reset timer counter, fall through

/*
 * Wait out the remaining transmission.
 */
coda:                       // loop of 3.7ms exactly
    wait 64734              // (6) 16 + 64734 = 64750 = 3.7.ms
    stage_inc 1             // (6) Increment counter
    jumps coda, 250, LE     // (4) 200 x 3.7ms = 740ms stop recurring false-positives caused by tail

    move r1, 1              // Set state to RXOK
    move r0, state          // r0 <- var address
    st r1, r0, 0            // r1 -> r0 data save

/*
 * Clean shutdown, flagging RX complete (good or not)
 */
exit:                       // Notify main code of state
    // "Set to 1, then that pin i/o is routed to the RTC subsystem" 
    WRITE_RTC_REG(RTC_IO_TOUCH_PAD1_REG, RTC_IO_TOUCH_PAD1_MUX_SEL_S, 1, 1)

    // Allow low level GPIO setting
    WRITE_RTC_REG(RTC_GPIO_ENABLE_W1TS_REG, RTC_GPIO_ENABLE_W1TS_S + GPIO_RX_DONE, 1, 1)

    // ULP started flag via GPIO
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_W1TS_S + GPIO_RX_DONE, 1, 1)

    // Cancel sleep
    WRITE_RTC_FIELD(RTC_CNTL_ULP_CP_TIMER_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN, 0)
    // signal low to indicate a RX has been done
    WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_W1TC_S + GPIO_RX_DONE, 1, 1)
    halt                    // Stop