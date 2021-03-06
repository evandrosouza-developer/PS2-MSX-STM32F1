/*
 * This file is part of PS/2 to MSX keyboard adapter
 * This file was part of the libopencm3 project.
 *
 * Copyright (C) 2011 Piotr Esden-Tempski <piotr@esden.net>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
//Use Tab width=2

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/gpio.h>

#include "hr_timer.h"
#include "ps2handl.h"
#include "port_def.h"

volatile uint16_t state_overflow_tim2;
volatile uint64_t TIM2_Update_Cnt;								//Overflow of time_between_ps2clk
volatile uint64_t time_between_ps2clk ;
volatile uint64_t acctimeps2data0;


void tim2_setup(void)
{
	//Used to receive PS/2 Clock interrupt
	// Enable TIM2 clock
	rcc_periph_clock_enable(RCC_TIM2);

	//PS/2 Clock interrupt
	// Enable TIM2 interrupt.
	nvic_enable_irq(NVIC_TIM2_IRQ);
	nvic_set_priority(NVIC_TIM2_IRQ, 11);

	//Configuring PA15 as input and Alternate function of T2C1
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO15);	//Default config

	/* Reset TIM2 peripheral to defaults. */
	rcc_periph_reset_pulse(RST_TIM2);

	/* Timer global mode:
	 * - Prescaler = 72 (Prescler module=71)
	 * - Counter Enable
	 * - Direction up
	 */
	timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT, TIM_CR1_CEN, TIM_CR1_DIR_UP);

	/*
	 * Please take note that the clock source for STM32 timers
	 * might not be the raw APB1/APB2 clocks.  In various conditions they
	 * are doubled.  See the Reference Manual for full details!
	 * In our case, TIM2 on APB1 is running at double frequency
	 */
	timer_set_prescaler(TIM2, ((rcc_apb1_frequency * 2) / 1000000) - 1);

	// count full range
	timer_set_period(TIM2, 65535);
	//TIM_ARR(TIM2) = 0xFFFF;

	// Enable preload.
	timer_enable_preload(TIM2);

	/*
	TIM2_Update_Cnt = 0;
	time_between_ps2clk = 0;
	state_overflow_tim2 = TIME_CAPTURE;

	//. Select the active input: TIMx_CCR1 must be linked to the TI1 input, so write the CC1S
	//bits to 01 in the TIMx_CCMR1 register. As soon as CC1S becomes different from 00,
	//the channel is configured in input and the TIMx_CCR1 register becomes read-only.
	//Bits 1:0 CC1S: Capture/Compare 1 selection
	//This bit-field defines the direction of the channel (input/output) as well as the used input.
	//00: CC1 channel is configured as output.
	//01: CC1 channel is configured as input, IC1 is mapped on TI1.
	//10: CC1 channel is configured as input, IC1 is mapped on TI2.
	//11: CC1 channel is configured as input, IC1 is mapped on TRC. This mode is working only if
	//an internal trigger input is selected through TS bit (TIMx_SMCR register)
	//Note: CC1S bits are writable only when the channel is OFF (CC1E = 0 in TIMx_CCER)
	TIM_CCER(TIM2) = 0;//To zero TIM_CCER_CC1E and prepair to select active input
	TIM_CCMR1(TIM2) = TIM_CCMR1_CC1S_IN_TI1;
	//. Select the edge of the active transition on the TI1 channel by writing the CC1P bit to 1
	//in the TIMx_CCER register (falling edge in this case).
	//Bit 1 CC1P: Capture/Compare 1 configured as input:
	//This bit selects whether IC1 is used for trigger or capture operations.
	//0: non-inverted: capture is done on a rising edge of IC1. When used as external trigger, IC1 is non-inverted.
	//1: inverted: capture is done on a falling edge of IC1. When used as external trigger, IC1 is inverted.
	TIM_CCER(TIM2) = TIM_CCER_CC1P;
	//. Program the input prescaler. In our use, we wish the capture to be performed at
	//each valid transition, so the prescaler is disabled (write IC1PS bits to 00 in the
	//TIMx_CCMR1 register).
	//Bits 3:2 IC1PSC: Input capture 1 prescaler
	//This bit-field defines the ratio of the prescaler acting on CC1 input (IC1).
	//The prescaler is reset as soon as CC1E=0 (TIMx_CCER register).
	//00: no prescaler, capture is done each time an edge is detected on the capture input
	//01: capture is done once every 2 events
	//10: capture is done once every 4 events
	//11: capture is done once every 8 events
	TIM_CCMR1(TIM2) &= (TIM_CCMR1_IC1F_OFF | 0b11110011); //No prescaler and no filter, sampling is done at fDTS
	*/

	//Here there is no TIM2 run and/or interrupts enabled
	prepares_capture(TIM2);
}


void delay_usec(uint16_t usec, int16_t next_state)
{
	//Here we init TIM2 to a single shot,
	//and have an interrupt at the overflow of the counting

	// Counter disable
	TIM_CR1(TIM2) &= ~TIM_CR1_CEN;

	//Disable capture from the counter into the capture register by setting the CC1E bit in the TIMx_CCER register.
	//Bit 0 CC1E: Capture/Compare 1 configured as input:
	//This bit determines if a capture of the counter value can actually be done into the input 
	//capture/compare register 1 (TIMx_CCR1) or not.
	//0: Capture disabled.
	//1: Capture enabled.
	TIM_CCER(TIM2) &= ~TIM_CCER_CC1E;

	// Bit 2 URS: Update request source
	// This bit is set and cleared by software to select the UEV event sources.
	// 0: Any of the following events generate an update interrupt or DMA request if enabled.
	// These events can be:
	// ??? Counter overflow/underflow
	// ??? Setting the UG bit
	// ??? Update generation through the slave mode controller
	// 1: Only counter overflow/underflow generates an update interrupt or DMA request if enabled.	
	TIM_CR1(TIM2) |= TIM_CR1_URS;

	//Now clear timer through set UG:
	//Generate a UEV (UG bit in the TIMx_EGR register)
	//Bit 0 UG: Update generation
	//This bit can be set by software, it is automatically cleared by hardware.
	//0: No action
	//1: Re-initialize the counter and generates an update of the registers. Note that the prescaler
	//counter is cleared too (anyway the prescaler ratio is not affected). The counter is cleared if
	//the center-aligned mode is selected or if DIR=0 (upcounting), else it takes the auto-reload
	//value (TIMx_ARR) if DIR=1 (downcounting).
	TIM_EGR(TIM2) |= TIM_EGR_UG;
	// Clear timer Update Interrupt Flag after set UG (Update Generation)
	//timer_clear_flag(TIM2, TIM_SR_UIF);
	TIM_SR(TIM2) &= ~TIM_SR_UIF;

	//timer_set_period(TIM2, 65535-usec-1);
	TIM_CNT(TIM2) = 65536 - usec;

	state_overflow_tim2 = next_state;

	//Enables OPM (One Pulse Mode)
	TIM_CR1(TIM2) |= TIM_CR1_OPM;

	// Counter enable
	//timer_enable_counter(TIM2);
	TIM_CR1(TIM2) |= TIM_CR1_CEN;

	//timer_enable_irq(TIM2, TIM_DIER_UIE);
	TIM_DIER(TIM2) |= TIM_DIER_UIE;
}


void prepares_capture(uint32_t timer_peripheral)
{
	// Dummy read the Input Capture value (to clear CC1IF flag)
	TIM_CCR1(timer_peripheral);
	//Clear TIM2 Capture compare interrupt pending bit
	//timer_clear_flag(timer_peripheral, TIM_SR_CC1IF);							//if the above dummy read didn`t do the task
	//timer_clear_flag(timer_peripheral, TIM_SR_CC1OF);
	TIM_SR(timer_peripheral) &= ~(TIM_SR_CC1IF | TIM_SR_CC1OF);					//if the above dummy read didn`t do the task

	//. Select the active input: TIMx_CCR1 must be linked to the TI1 input, so write the CC1S
	//bits to 01 in the TIMx_CCMR1 register. As soon as CC1S becomes different from 00,
	//the channel is configured in input and the TIMx_CCR1 register becomes read-only.
	//Bits 1:0 CC1S: Capture/Compare 1 selection
	//This bit-field defines the direction of the channel (input/output) as well as the used input.
	//00: CC1 channel is configured as output.
	//01: CC1 channel is configured as input, IC1 is mapped on TI1.
	//10: CC1 channel is configured as input, IC1 is mapped on TI2.
	//11: CC1 channel is configured as input, IC1 is mapped on TRC. This mode is working only if
	//an internal trigger input is selected through TS bit (TIMx_SMCR register)
	//Note: CC1S bits are writable only when the channel is OFF (CC1E = 0 in TIMx_CCER)
	TIM_CCER(timer_peripheral) = 0;//To zero TIM_CCER_CC1E and prepair to select active input
	TIM_CCMR1(timer_peripheral) = TIM_CCMR1_CC1S_IN_TI1;
	//. Select the edge of the active transition on the TI1 channel by writing the CC1P bit to 1
	//in the TIMx_CCER register (falling edge in this case).
	//Bit 1 CC1P: Capture/Compare 1 configured as input:
	//This bit selects whether IC1 is used for trigger or capture operations.
	//0: non-inverted: capture is done on a rising edge of IC1. When used as external trigger, IC1 is non-inverted.
	//1: inverted: capture is done on a falling edge of IC1. When used as external trigger, IC1 is inverted.
	TIM_CCER(timer_peripheral) = TIM_CCER_CC1P;
	//. Program the input prescaler. In our use, we wish the capture to be performed at
	//each valid transition, so the prescaler is disabled (write IC1PS bits to 00 in the
	//TIMx_CCMR1 register).
	//Bits 3:2 IC1PSC: Input capture 1 prescaler
	//This bit-field defines the ratio of the prescaler acting on CC1 input (IC1).
	//The prescaler is reset as soon as CC1E=0 (TIMx_CCER register).
	//00: no prescaler, capture is done each time an edge is detected on the capture input
	//01: capture is done once every 2 events
	//10: capture is done once every 4 events
	//11: capture is done once every 8 events
	TIM_CCMR1(timer_peripheral) &= (TIM_CCMR1_IC1F_OFF | 0b11110011); //No prescaler and no filter, sampling is done at fDTS

	//Reenable TIM2 as Capture Timer:
	//Enable capture from the counter into the capture register by setting the CC1E bit in the TIMx_CCER register.
	//Bit 0 CC1E: Capture/Compare 1 configured as input:
	//This bit determines if a capture of the counter value can actually be done into the input 
	//capture/compare register 1 (TIMx_CCR1) or not.
	//0: Capture disabled.
	//1: Capture enabled.
	TIM_CCER(timer_peripheral) |= TIM_CCER_CC1E;
	timer_set_period(timer_peripheral, 0xFFFF);
	//Disables OPM (One Pulse Mode)
	TIM_CR1(timer_peripheral) &= ~TIM_CR1_OPM;
	//Setup counters
	time_between_ps2clk = 0;
	acctimeps2data0 = 0;
	TIM2_Update_Cnt = 0;
	//Now points UIF (overflow int) to "normal" state: TIME_CAPTURE
	state_overflow_tim2 = TIME_CAPTURE;

	//Clear TIM2_CR1_URS;
	TIM_CR1(timer_peripheral) &= ~TIM_CR1_URS;

	//Counter enable
	//timer_enable_counter(timer_peripheral);
	TIM_CR1(timer_peripheral) |= TIM_CR1_CEN;

	timer_enable_irq(timer_peripheral, TIM_DIER_CC1IE | TIM_DIER_UIE);
}


/*************************************************************************************************/
/*************************************************************************************************/
/******************************************* ISR's ***********************************************/
/*************************************************************************************************/
/*************************************************************************************************/
void tim2_isr(void)
{
	//Verify if Update Interrupt Flag
	if (timer_get_flag(TIM2, TIM_SR_UIF))
	{
		//Debug & performance measurement
		gpio_clear(INT_TIM2_port, TIM2UIF_pin_id); //Signs start of interruption

		// Clear timer Update Interrupt Flag
		timer_clear_flag(TIM2, TIM_SR_UIF);
		switch (state_overflow_tim2)
		{
			case TIME_CAPTURE:
			{
				TIM2_Update_Cnt++;
				break;
			}
			case SEND_ST_BIT:
			{
				send_start_bit_now();
				break;
			}	
			case SEND_ST_BIT_2:
			{
				send_start_bit2();
				break;
			}	
			case SEND_ST_BIT_3:
			{
				send_start_bit3();
				break;
			}	
			default:
			{
			}
		}
		//Debug & performance measurement
		gpio_set(INT_TIM2_port, TIM2UIF_pin_id); //Signs end of interruption
	}	//if (timer_get_flag(TIM2, TIM_SR_UIF))
	else if (timer_get_flag(TIM2, TIM_SR_CC1IF))
	{
		//When an input capture occurs:
		//. The TIMx_CCR1 register gets the value of the counter on the active transition.
		//. CC1IF flag is set (interrupt flag). CC1OF is also set if at least two consecutive captures
		//occurred whereas the flag was not cleared.
		//. An interrupt is generated depending on the CC1IE bit.
		//. A DMA request is generated depending on the CC1DE bit.
		//In order to handle the overcapture, it is recommended to read the data before the
		//overcapture flag. This is to avoid missing an overcapture which could happen after reading
		//the flag and before reading the data.
		//Note: IC interrupt and/or DMA requests can be generated by software by setting the
		//corresponding CCxG bit in the TIMx_EGR register.
    
		//Debug & performance measurement
		gpio_clear(INT_TIM2_port, TIM2CC1_pin_id); //Signs start of interruption

    // Clear TIM2 Capture compare interrupt pending bit
    //timer_clear_flag(TIM2, TIM_SR_CC1OF);
    //timer_clear_flag(TIM2, TIM_SR_CC1IF);
		TIM_SR(TIM2) &= ~(TIM_SR_CC1IF | TIM_SR_CC1OF);
		// Get the Input Capture value
		time_between_ps2clk = TIM2_Update_Cnt << 16 | TIM_CCR1(TIM2);
		//Clear Counter2 (Reset both CNT & PSC plus )
		TIM_CR1(TIM2) |= TIM_CR1_URS;
		TIM_EGR(TIM2) |= TIM_EGR_UG;
		TIM_SR(TIM2) &= ~TIM_SR_UIF;
		//Clear TIM2_CR1_URS;
		TIM_CR1(TIM2) &= ~TIM_CR1_URS;

		TIM2_Update_Cnt = 0;

		//This is the ISR of PS/2 clock pin. It jumps to ps2_clock_update.
		//It is an important ISR, but it does not require critical timming resources as MSX Y scan does.
		// EXTI4 & TIM2_CCR1
		bool ps2datapin_logicstate=gpio_get(ps2_data_pin_port, ps2_data_pin_id);
		ps2_clock_update(ps2datapin_logicstate);

		//Debug & performance measurement
		gpio_set(INT_TIM2_port, TIM2CC1_pin_id); //Signs end of TIM2 interruption. Default condition is "1"
	}
}
