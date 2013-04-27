/* stepper.h - stepper motor interface
 * This file is part of TinyG2 project
 *
 * Copyright (c) 2013 Alden S. Hart Jr.
 * Copyright (c) 2013 Robert Giseburt
 *
 * This file ("the software") is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 as published by the
 * Free Software Foundation. You should have received a copy of the GNU General Public
 * License, version 2 along with the software.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, you may use this file as part of a software library without
 * restriction. Specifically, if other files instantiate templates or use macros or
 * inline functions from this file, or you compile this file and link it with  other
 * files to produce an executable, this file does not by itself cause the resulting
 * executable to be covered by the GNU General Public License. This exception does not
 * however invalidate any other reasons why the executable file might be covered by the
 * GNU General Public License.
 *
 * THE SOFTWARE IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, BUT WITHOUT ANY
 * WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
 * SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/* 
 *	Coordinated motion (line drawing) is performed using a classic 
 *	Bresenham DDA as per reprap and grbl. A number of additional steps 
 *	are taken to optimize interpolation and pulse train accuracy.
 *
 *	- The DDA accepts and processes fractional motor steps. Steps are 
 *	  passed to the move queue as floats, and do not need to be integer
 *	  values. The DDA implements fractional steps and interpolation by 
 *	  extending the counter range downward using the DDA_SUBSTEPS setting. 
 *
 *	- The DDA is not used as a 'ramp' for acceleration management. Accel
 *	  is computed as 3rd order (controlled jerk) equations that generate 
 *	  accel/decel segments to the DDA in much the same way arc drawing
 *	  is approximated. The DDA runs at a constant rate for each segment,
 *	  up to a maximum of 50 Khz step rate.
 *
 *	- The DDA rate for a segment is set to an integer multiple of the 
 *	  step freqency of the fastest motor (major axis). This amount of 
 *	  overclocking is controlled by the DDA_OVERCLOCK value, typically 16x.
 *	  A minimum DDA rate is enforced that prevents overflowing the 16 bit 
 *	  DDA timer PERIOD value. The DDA timer always runs at 32 Mhz: the 
 *	  prescaler is not used. Various methods are used to keep the numbers 
 *	  in range for long lines. See _st_set_f_dda() for details.
 *
 *	- Pulse phasing is preserved between segments if possible. This makes
 *	  for smoother motion, particularly at very low speeds and short 
 *	  segment lengths (avoids pulse jitter). Phase continuity is achieved 
 *	  by simply not resetting the DDA counters across segments. In some 
 *	  cases the differences between timer values across segments are too 
 *	  large for this to work, and you risk motor stalls due to pulse 
 *	  starvation. These cases are detected and the counters are reset 
 *	  to prevent stalling.
 *
 *  - Pulse phasing is also helped by minimizing the time spent loading 
 *	  the next move segment. To this end as much as possible about that 
 *	  move is pre-computed during move execution. Also, all moves are 
 *	  loaded from the interrupt level, avoiding the need for mutual 
 *	  exclusion locking or volatiles (which slow things down).
 */

#ifndef stepper_h
#define stepper_h

void stepper_init(void);			// initialize stepper subsystem

void st_disable(void);				// stop the steppers (step the stoppers)
uint8_t st_isbusy(void);			// return TRUE is any axis is running (F=idle)
void st_set_polarity(const uint8_t motor, const uint8_t polarity);
void st_set_microsteps(const uint8_t motor, const uint8_t microstep_mode);

//uint8_t st_test_prep_state(void);
//void st_request_exec_move(void);
void st_prep_null(void);
void st_prep_dwell(float microseconds);
//uint8_t st_prep_line(float steps[], float microseconds);

magic_t st_get_st_magic(void);
magic_t st_get_sps_magic(void);

// handy macro
#define _f_to_period(f) (uint16_t)((float)F_CPU / (float)f)

/*
 * Stepper configs and constants
 */

/* Timer settings for stepper module. See hardware.h for overall timer assignments */

#define FREQUENCY_DDA	50000UL
#define TC_RC_DDA		(VARIANT_MCK / FREQUENCY_DDA / 2) // <--- divided by MCK divisor
#define TC_CMR_DDA		(TC_CMR_TCCLKS_TIMER_CLOCK1 | TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC)	// MCK/2, RC trigger (component_tc.h)
#define TC_IER_DDA		TC_IER_CPCS		// Interrupt enable value - RC compare

#define TC_CMR_DWELL	(TC_CMR_TCCLKS_TIMER_CLOCK1 | TC_CMR_WAVSEL_UP_RC)	// MCK/2, RC trigger (component_tc.h)
#define TC_IER_DWELL	TC_IER_CPCS		// Interrupt enable value - RC compare

#define FREQUENCY_DWELL	1000UL
#define FREQUENCY_DWELL	1000UL

//#define TC_CHANNEL_DDA 
#define F_DDA 		(float)10000	// DDA frequency in hz.
//#define F_DDA 		(float)50000	// DDA frequency in hz.
#define F_DWELL		(float)10000	// Dwell count frequency in hz.
#define SWI_PERIOD		100			// cycles you have to shut off SW interrupt
#define TIMER_PERIOD_MIN (20)		// used to trap bad timer loads

/* DDA substepping
 * 	DDA_SUBSTEPS sets the amount of fractional precision for substepping.
 *	Substepping is kind of like microsteps done in software to make
 *	interpolation more accurate.
 *
 *	Set to 1 to disable, but don't do this or you will lose a lot of accuracy.
 */
//#define DDA_SUBSTEPS 1000000		// 100,000 accumulates substeps to 6 decimal places
#define DDA_SUBSTEPS 100000		// 100,000 accumulates substeps to 6 decimal places

/* DDA overclocking
 * 	Overclocking multiplies the step rate of the fastest axis (major axis) 
 *	by an integer value up to the DDA_OVERCLOCK value. This makes the 
 *	interpolation of the non-major axes more accurate than simply setting
 *	the DDA to the speed of the major axis; and allows the DDA to run at 
 *	less than the max frequency when possible.
 *
 *	Set to 0 to disable.
 *
 *	NOTE: TinyG doesn't use tunable overclocking any more. It just overclocks
 *	at the fastest sustainable step rate which is about 50 Khz for the xmega.
 *	This minimizes the aliasing on minor axes at minimal impact to the major 
 *	axis. The DDA overclock setting and associated code are left in for historical
 *	purposes and in case we ever want to go back to pure overclocking.
 *
 *	Setting this value to 0 has the effect of telling the optimizer to take out
 *	entire code regions that are not called if this value is zero. So they are 
 *	left in for historical purposes and not commented out. These regions are noted.
 */
//#define DDA_OVERCLOCK 16		// doesn't have to be a binary multiple
#define DDA_OVERCLOCK 0			// Permanently disabled. See above NOTE

/* Counter resets
 * 	You want to reset the DDA counters if the new ticks value is way less 
 *	than previous value, but otherwise you should leave the counters alone.
 *	Preserving the counter value from the previous segment aligns pulse 
 *	phasing between segments. However, if the new counter is going to be 
 *	much less than the old counter you must reset it or risk motor stalls. 
 */
#define COUNTER_RESET_FACTOR 2	// amount counter range can safely change

/* DDA minimum operating frequency
 *	This is the minumum value the DDA time can run with a fixed 32 Mhz 
 *	clock. Anything lower will overflow the 16 bit PERIOD register.
 */
//#define F_DDA_MIN (float)489	// hz
#define F_DDA_MIN (float)500	// hz - is 489 Hz with some margin

// Timer setups
/*
#define STEP_TIMER_TYPE		TC0_struct // stepper subsubstem uses all the TC0's
#define STEP_TIMER_DISABLE 	0		// turn timer off (clock = 0 Hz)
#define STEP_TIMER_ENABLE	1		// turn timer clock on (F_CPU = 32 Mhz)
#define STEP_TIMER_WGMODE	0		// normal mode (count to TOP and rollover)

#define TIMER_DDA_ISR_vect	TCC0_OVF_vect	// must agree with assignment in hardware.h
#define TIMER_DWELL_ISR_vect TCD0_OVF_vect	// must agree with assignment in hardware.h
#define TIMER_LOAD_ISR_vect	TCE0_OVF_vect	// must agree with assignment in hardware.h
#define TIMER_EXEC_ISR_vect	TCF0_OVF_vect	// must agree with assignment in hardware.h

#define TIMER_OVFINTLVL_HI	3		// timer interrupt level (3=hi)
#define	TIMER_OVFINTLVL_MED 2;		// timer interrupt level (2=med)
#define	TIMER_OVFINTLVL_LO  1;		// timer interrupt level (1=lo)

#define TIMER_DDA_INTLVL 	TIMER_OVFINTLVL_HI
#define TIMER_DWELL_INTLVL	TIMER_OVFINTLVL_HI
#define TIMER_LOAD_INTLVL	TIMER_OVFINTLVL_HI
#define TIMER_EXEC_INTLVL	TIMER_OVFINTLVL_LO
*/

#endif
