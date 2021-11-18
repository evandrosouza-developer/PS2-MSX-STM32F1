#pragma once

//Use Tab width=2

#ifdef __cplusplus
extern "C" {
#endif

#define X7_port							GPIOB
#define X7_pin_id						GPIO9
#define X6_port							GPIOB
#define X6_pin_id						GPIO8
#define X5_port							GPIOB
#define X5_pin_id						GPIO10
#define X4_port							GPIOB
#define X4_pin_id						GPIO15
#define X3_port							GPIOB
#define X3_pin_id						GPIO11
#define X2_port							GPIOB
#define X2_pin_id						GPIO14
#define X1_port							GPIOB
#define X1_pin_id						GPIO13
#define X0_port							GPIOB
#define X0_pin_id						GPIO12
#define Y3_port							GPIOA
#define Y3_pin_id						GPIO12		//port A10 (Y2) broken
#define Y3_exti							EXTI12
#define Y2_port							GPIOA		//port A10 (Y2) broken
//#define Y2_pin_id						GPIO10		//port A10 (Y2) broken
#define Y2_pin_id						GPIO11		//port A10 (Y2) broken
#define Y2_exti							EXTI4
#define Y1_port							GPIOA
#define Y1_pin_id						GPIO9
#define Y1_exti							EXTI9
#define Y0_port							GPIOA
#define Y0_pin_id						GPIO8
#define Y0_exti							EXTI8
#define CAPSLOCK_port				GPIOA
#define CAPSLOCK_pin_id			GPIO12
#define CAPSLOCK_exti				EXTI12
#define KANA_port						GPIOB
#define KANA_pin_id					GPIO6
#define KANA_exti						EXTI6
//SERIAL2_port							GPIOA (Pre-defined)
//SERIAL2_TX_pin_id					GPIO2 (Pre-defined)
//SERIAL2_RX_pin_id					GPIO3 (Pre-defined)


#define ps2_data_pin_port		GPIOB
#define ps2_data_pin_id			GPIO7
#define ps2_clock_pin_port	GPIOA
#define ps2_clock_pin_id		GPIO15
#define ps2_power_ctr_port	GPIOB	
#define ps2_power_ctr_pin		GPIO1	


//Para debug
#define SYSTICK_port				GPIOA
#define SYSTICK_pin_id			GPIO0
#define TIM2CC1_pin_id			GPIO1
#define BIT0_pin_port				GPIOA
#define BIT0_pin_id					GPIO4
#define INT_TIM2_port				GPIOA
#define TIM2UIF_pin_id			GPIO5
#define Dbg_Yint_port				GPIOA
#define Dbg_Yint0e1_pin_id	GPIO6
#define Dbg_Yint2e3_pin_id	GPIO7

#ifdef __cplusplus
}
#endif
