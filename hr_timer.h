#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//Definitions of TIM2 stateful machine
#define TIME_CAPTURE	1056	//0x420 //"Normal" state
#define SEND_ST_BIT		1057	//0x421
#define SEND_ST_BIT_2	1058	//0x421
#define SEND_ST_BIT_3	1059	//0x422

void tim2_setup(void);

void delay_usec(uint16_t, int16_t);

void prepares_capture(uint32_t timer_peripheral);

#ifdef __cplusplus
}
#endif
