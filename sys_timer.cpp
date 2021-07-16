#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h>

#include "sys_timer.h"
#include "serial.h"
#include "hr_timer.h"
#include "ps2handl.h"
#include "msxmap.h"
#include "port_def.h"
//#include "flash_intelhex.h"
//Use Tab width=2


//Global vars
volatile uint32_t systicks, ticks;
volatile bool ticks_keys;
volatile uint16_t last_ps2_fails=0;
volatile uint16_t fail_count;
extern bool ps2_keyb_detected;										//Declared on ps2stdhandl.c
extern bool update_ps2_leds, ps2numlockstate;			//Declared on msxmap.cpp
extern uint8_t keycode[4];												//Declared on msxmap.cpp
extern uint8_t formerkeycode[4];									//Declared on msxmap.cpp
extern bool mount_keycode_OK; 										//Declared on ps2stdhandl.c


void systick_setup(void)
{
	//Make sure systick doesn't interrupt PS/2 protocol bitbang action
	nvic_set_priority(NVIC_SYSTICK_IRQ, 100);

	/* 72MHz / 8 => 9000000 counts per second */
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	/* 9000000 / 30(ints per second) = 300000 systick reload - every 33,3ms one interrupt */
	/* SysTick interrupt every N clock pulses: set reload to N-1 */
	systick_set_reload((72000000/8/30)-1);
	
	systicks = 0; //systick_clear
	ticks = 0;
	ticks_keys = false;

	systick_interrupt_enable();

	/* Start counting. */
	systick_counter_enable();
}

/*************************************************************************************************/
/*************************************************************************************************/
/******************************************* ISR's ***********************************************/
/*************************************************************************************************/
/*************************************************************************************************/
void sys_tick_handler(void) // f=30Hz (Each 33,33ms)
{
	bool kana_state, caps_state;
	static bool kana_former, caps_former;

	//Debug & performance measurement
	gpio_clear(SYSTICK_port, SYSTICK_pin_id); //Signs start of interruption

	systicks++;

	ticks++;
	if(ps2_keyb_detected==true && (ticks>=(10*3)))
	{ /*C13 LED blinks each 2s (2*1 s) on keyboard detected */
		gpio_toggle(GPIOC, GPIO13);
		ticks=0;
	} else if (ps2_keyb_detected==false && ticks >= 7)
	{ /*C13 LED blinks each 467 ms (2*233.3 ms) on keyboard absence*/
		gpio_toggle(GPIOC, GPIO13);
		ticks=0;
	}

	if (ps2_keyb_detected == true)
	{
		//Queue keys processing:
		ticks_keys = !ticks_keys;	//Toggle ticks_keys
		if (ticks_keys)
		{
			msxmap objeto;
			objeto.msxqueuekeys();

			//Check CAPS Led update
			caps_state = gpio_get(CAPSLOCK_port, CAPSLOCK_pin_id);
			if (caps_state != caps_former)
				update_ps2_leds = true;
			caps_former = caps_state;
			//Check Kana Led update
			kana_state = gpio_get(KANA_port, KANA_pin_id);
			if (kana_state != kana_former)
				update_ps2_leds = true;
			kana_former = kana_state;
		}

		for (uint16_t i=0; i<2; i++)	//Capable of 2 keystrokes per systicks interrupt
		{
			//New key processing:
			if (mount_keycode())
			{
				//At this point, we already have the assembled compound keys code, so
				//to avoid unnecessary processing at convert2msx, check if this last keycode
				//is different from the former one.
				if (
				(keycode[0] != formerkeycode[0]) ||
				(keycode[1] != formerkeycode[1]) ||
				(keycode[2] != formerkeycode[2]) ||
				(keycode[3] != formerkeycode[3]) )
				{
					//Serial message the keyboard change
					uint8_t mountstring[3];
					usart_send_string((uint8_t*)"Bytes qty=");
					conv_uint8_to_2a_hex(keycode[0], &mountstring[0]);
					usart_send_string(&mountstring[0]);
					usart_send_string((uint8_t*)"; Key codes=");
					conv_uint8_to_2a_hex(keycode[1], &mountstring[0]);
					usart_send_string(&mountstring[0]);
					usart_send_string((uint8_t*)"; ");
					conv_uint8_to_2a_hex(keycode[2], &mountstring[0]);
					usart_send_string(&mountstring[0]);
					usart_send_string((uint8_t*)"; ");
					conv_uint8_to_2a_hex(keycode[3], &mountstring[0]);
					usart_send_string(&mountstring[0]);
					usart_send_string((uint8_t*)"\n");
					//Toggle led each keyboard change (both new presses and releases).
					gpio_toggle(GPIOC, GPIO13);
					// Do the MSX search and conversion
					msxmap objeto;
					objeto.convert2msx();
				}
				//update former keystoke
				formerkeycode[0] = keycode[0];
				formerkeycode[1] = keycode[1];
				formerkeycode[2] = keycode[2];
				formerkeycode[3] = keycode[3];
				//Now we can reset to prepair to do a new mount_keycode (Clear to read a new key press or release)
				keycode[0] = 0;
				keycode[1] = 0;
				keycode[2] = 0;
				keycode[3] = 0;
				mount_keycode_OK = false;
			}
		}
	}

	if(fail_count!=last_ps2_fails)
	{
		// printf("PS/2 failure count: %03d\n", fail_count);
		/*uint8_t mountstring[3];
		usart_send_string((uint8_t*)"PS/2 failure count: ");
		conv_uint8_to_2a_hex(fail_count, &mountstring[0]);
		usart_send_string(&mountstring[0]);
		usart_send_string((uint8_t*)"\n");*/

		last_ps2_fails=fail_count;
	}
	//Debug & performance measurement
	gpio_set(SYSTICK_port, SYSTICK_pin_id); //Signs end of systicks interruption. Default condition is "1"
}
