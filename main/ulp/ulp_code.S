#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"

// OUTPUTS
#define D4  					10	// = RTC_GPIO_10 = GPIO_4
#define D15  					13	// = RTC_GPIO_13 = GPIO_15

#define ADD_TRACE_POINT(tracePoint)	or r3, r3, tracePoint
	
#define SET_HIGH(pin)				WRITE_RTC_REG(RTC_GPIO_OUT_REG, (14 + (pin)), 1, 1)
#define SET_LOW(pin)				WRITE_RTC_REG(RTC_GPIO_OUT_REG, (14 + (pin)), 1, 0)
#define UNHOLD_D4					WRITE_RTC_REG(RTC_IO_TOUCH_PAD0_REG, 31, 1, 0)
#define HOLD_D4						WRITE_RTC_REG(RTC_IO_TOUCH_PAD0_REG, 31, 1, 1)
#define UNHOLD_D15					WRITE_RTC_REG(RTC_IO_TOUCH_PAD3_REG, 31, 1, 0)
#define HOLD_D15					WRITE_RTC_REG(RTC_IO_TOUCH_PAD3_REG, 31, 1, 1)

#define READ_ANALOG_ON_D34_TO_R0	adc r0, 0, 7

	.global trace
trace: 
	.long 0

	.global initialize
initialize: 
	.long 0

	.global measurementsPerPublishment
measurementsPerPublishment: 
	.long 0

measurementIndex: 
	.long 0

	.global anemometerPulses
anemometerPulses:
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0

	.global directionVaneValues
directionVaneValues:
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0
	.long 0

	.text
	.global entry
entry:

	move r3, 0 										// trace = 0

	move r2, initialize
	ld r0, r2, 0
	jumpr read_counter_value, 0, eq 
	
init:

	ADD_TRACE_POINT(1)
	move r0, 0										// store 0 in initialize
	st r0, r2, 0
	
	move r2, measurementIndex						// store 0 in measurementIndex
	move r0, 0
	st r0, r2, 0

	UNHOLD_D4										// reset the counter (74HC590)
	SET_LOW(D4)
	SET_HIGH(D4)
	HOLD_D4

	SET_HIGH(D15)									// set 3-state output of counter (74HC590) to high impedance
	UNHOLD_D15
	HOLD_D15

	jump haltUlpProgram

read_counter_value:

	// 01234567890123456789012345678901		RTC_GPIO_IN_REG: [13:0] reserved, [31:14] RTC IN pins
	//               012345678901234567
	// --------------------0123----4567		bit order of digital inputs
	//
	// pins used: D25 (LSB), D26, D33, D32, D13, D12, D14, D27 (MSB)
	
	ADD_TRACE_POINT(2)

	SET_LOW(D15)									// tell counter (74HC590) to copy counter value into storage register
	UNHOLD_D15
	SET_HIGH(D15)
	SET_LOW(D15)									// set 3-state output of counter (74HC590) to output stored value
	
	READ_RTC_REG(RTC_GPIO_IN_REG, (14 + 6), 12)

	SET_HIGH(D15)									// set 3-state output of counter (74HC590) to high impedance
	HOLD_D15

	SET_LOW(D4)										// reset the counter (74HC590)
	UNHOLD_D4
	SET_HIGH(D4)
	HOLD_D4
	
	move r2, r0
	and  r2, r2, 0x000f
	and  r0, r0, 0x0f00
	rsh  r0, r0, 4
	or   r2, r2, r0
	move r0, r2
							
	move r2, measurementIndex						// store counter value in anemometerPulses[measurementIndex]
	ld r1, r2, 0
	move r2, anemometerPulses
	add r2, r2, r1
	st r0, r2, 0

read_direction_vane_value:

	// value range 12 bits -> [0, 4095]

	move r2, measurementIndex						// store direction value in directionVaneValues[measurementIndex]
	ld r1, r2, 0
	move r2, directionVaneValues
	add r2, r2, r1
	
	READ_ANALOG_ON_D34_TO_R0
	
	st r0, r2, 0

saveTrace:

	move r2, trace
    st r3, r2, 0

checkNumberOfMeasurements:
	move r2, measurementsPerPublishment
	ld r0, r2, 0
	move r2, measurementIndex
	ld r1, r2, 0
	add r1, r1, 1
	sub r0, r0, r1
	jumpr incrementMeasurementIndex, 0, gt 

isReadyForWakeup:									// Read RTC_CNTL_RDY_FOR_WAKEUP bit
	READ_RTC_FIELD(RTC_CNTL_LOW_POWER_ST_REG, RTC_CNTL_RDY_FOR_WAKEUP)
	and r0, r0, 1
	jump isReadyForWakeup, eq    					// Retry until the bit is set
	wake                          					// Trigger wake up
	REG_WR 0x006, 24, 24, 0       				    // Stop ULP timer (clear RTC_CNTL_ULP_CP_SLP_TIMER_EN)

incrementMeasurementIndex:
	move r2, measurementIndex
	ld r0, r2, 0
	add r0, r0, 1
	st r0, r2, 0

haltUlpProgram:
	halt
