#pragma once

//Use Tab width=2

#ifdef __cplusplus
extern "C" {
#endif


//#include <stdint.h>
//#include <stdlib.h>
//#include <stdio.h>
//#include <libopencm3/stm32/gpio.h>
//#include <libopencm3/stm32/exti.h>

#define PS2_RECV_BUFFER_SIZE_POWER 6	//64 uint8_t positions
#define PS2_RECV_BUFFER_SIZE 64				//(2 << PS2_RECV_BUFFER_SIZE_POWER)

void power_on_ps2_keyboard(void);
bool ps2_keyb_detect(void);
void ps2_clock_update(bool);
void ps2_update_leds(bool, bool, bool);
bool mount_keycode(void);

void send_start_bit(void);
void send_start_bit2(void);
void send_start_bit3(void);

void general_debug_setup(void);

#ifdef __cplusplus
}
#endif
