#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h>

#include "ps2handl.h"
#include "msxmap.h"
#include "port_def.h"
#include "serial.h"

//Use Tab width=2

//Variáveis globais: Visíveis por todo o contexto do programa
extern uint32_t systicks;											//Declared on sys_timer.cpp
volatile bool ps2numlockstate;
volatile bool update_ps2_leds;
volatile uint16_t msx_Y_scan;
//Place to store previous time for each Y last scan
volatile uint32_t previous_y_systick[ 16 ] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

//Variable used to store the values of the X for each Y scan - Each Y has its own image of BSSR register
volatile uint32_t x_bits[ 16 ] = 
			{0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00, 
			 0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00};

uint8_t y_dummy;															//Read from MSX Database to sinalize "No keys mapping"
volatile uint8_t keycode[4];									//O 1º é a quantidade de bytes;
volatile uint8_t formerkeycode[4];						//O 1º é a quantidade de bytes;
bool compatible_database;

extern bool enable_xon_xoff;									//Declared on serial.c

#define MSX_DISP_QUEUE_SIZE							16
volatile static uint8_t msx_dispatch_keys_queue[MSX_DISP_QUEUE_SIZE];
volatile uint8_t msx_dispatch_kq_put;
volatile uint8_t msx_dispatch_kq_get;

#define MAX_LINHAS_VARRIDAS								306	//To acommodate maximum size of 3K (3072 lines of 10 bytes each)
#define MAX_TIME_OF_IDLE_KEYSCAN_SYSTICKS	5		//30 / 5 = 6 times per second is the maximum sweep speed
#define	Y_SHIFT														6		//Shift colunm

#define PRESS_RELEASE_BIT_POSITION				3
#define PRESS_RELEASE_BIT									1 << PRESS_RELEASE_BIT_POSITION //8

//The original SW was compiled to a Sharp/Epcom MSX HB-8000 and a brazilian PS/2 keyboard (ID=275)
//But it is possible to update the table sending a Intel Hex File through serial 2 (3.3V only):
//PA2 as TX and PA3 as RX, using the following communication parameters:
//Speed: 38400 bps
//8 bits
//No parity
//1 Stop bit
//no hardware flow control

#define SCB_BASE_ADDR				0xE000E008
#define SCB_AIRCR_OFFSET		0x0C

//Paste here the generated excel Conversion Database to make it with a default.
//Colar aqui Base de Dados de conversao gerada pelo excel para compilar com uma tabela default.
const uint8_t __attribute__((section("MSXDATABASE")))
	MSX_KEYB_DATABASE_CONVERSION[uint16_t (204)][uint8_t (10)] =
{
    {1, 0, 255, 255, 255, 255, 255, 255, 255, 255},
    {1, 0, 0, 244, 96, 112, 255, 255, 255, 255},
    {3, 0, 0, 244, 113, 255, 255, 255, 255, 255},
    {4, 0, 0, 244, 103, 255, 255, 255, 255, 255},
    {5, 0, 0, 244, 101, 255, 255, 255, 255, 255},
    {6, 0, 0, 244, 102, 255, 255, 255, 255, 255},
    {7, 0, 0, 244, 116, 255, 255, 255, 255, 255},
    {9, 0, 0, 244, 96, 113, 255, 255, 255, 255},
    {10, 0, 0, 244, 96, 103, 255, 255, 255, 255},
    {11, 0, 0, 244, 96, 101, 255, 255, 255, 255},
    {12, 0, 0, 244, 112, 255, 255, 255, 255, 255},
    {13, 0, 0, 244, 115, 255, 255, 255, 255, 255},
    {14, 0, 0, 244, 96, 22, 255, 255, 255, 255},
    {17, 0, 0, 244, 100, 255, 255, 255, 255, 255},
    {18, 0, 0, 244, 96, 255, 255, 255, 255, 255},
    {20, 0, 0, 244, 97, 255, 255, 255, 255, 255},
    {21, 0, 0, 244, 70, 255, 255, 255, 255, 255},
    {22, 0, 0, 244, 1, 255, 255, 255, 255, 255},
    {26, 0, 0, 244, 87, 255, 255, 255, 255, 255},
    {27, 0, 0, 244, 80, 255, 255, 255, 255, 255},
    {28, 0, 0, 244, 38, 255, 255, 255, 255, 255},
    {29, 0, 0, 244, 84, 255, 255, 255, 255, 255},
    {30, 0, 0, 244, 2, 255, 255, 255, 255, 255},
    {33, 0, 0, 244, 48, 255, 255, 255, 255, 255},
    {34, 0, 0, 244, 85, 255, 255, 255, 255, 255},
    {35, 0, 0, 244, 49, 255, 255, 255, 255, 255},
    {36, 0, 0, 244, 50, 255, 255, 255, 255, 255},
    {37, 0, 0, 244, 4, 255, 255, 255, 255, 255},
    {38, 0, 0, 244, 3, 255, 255, 255, 255, 255},
    {41, 0, 0, 244, 128, 255, 255, 255, 255, 255},
    {42, 0, 0, 244, 83, 255, 255, 255, 255, 255},
    {43, 0, 0, 244, 51, 255, 255, 255, 255, 255},
    {44, 0, 0, 244, 81, 255, 255, 255, 255, 255},
    {45, 0, 0, 244, 71, 255, 255, 255, 255, 255},
    {46, 0, 0, 244, 80, 255, 255, 255, 255, 255},
    {49, 0, 0, 244, 67, 255, 255, 255, 255, 255},
    {50, 0, 0, 244, 39, 255, 255, 255, 255, 255},
    {51, 0, 0, 244, 53, 255, 255, 255, 255, 255},
    {52, 0, 0, 244, 52, 255, 255, 255, 255, 255},
    {53, 0, 0, 244, 86, 255, 255, 255, 255, 255},
    {54, 0, 0, 254, 6, 255, 255, 255, 104, 22},
    {58, 0, 0, 244, 66, 255, 255, 255, 255, 255},
    {59, 0, 0, 244, 55, 255, 255, 255, 255, 255},
    {60, 0, 0, 244, 82, 255, 255, 255, 255, 255},
    {61, 0, 0, 244, 7, 255, 255, 255, 255, 255},
    {62, 0, 0, 244, 16, 255, 255, 255, 255, 255},
    {65, 0, 0, 254, 34, 255, 255, 255, 104, 37},
    {66, 0, 0, 244, 64, 255, 255, 255, 255, 255},
    {67, 0, 0, 244, 54, 255, 255, 255, 255, 255},
    {68, 0, 0, 244, 68, 255, 255, 255, 255, 255},
    {69, 0, 0, 244, 0, 255, 255, 255, 255, 255},
    {70, 0, 0, 244, 17, 255, 255, 255, 255, 255},
    {73, 0, 0, 254, 35, 255, 255, 255, 96, 37},
    {74, 0, 0, 254, 96, 34, 255, 255, 96, 35},
    {75, 0, 0, 244, 65, 255, 255, 255, 255, 255},
    {76, 0, 0, 244, 23, 255, 255, 255, 255, 255},
    {77, 0, 0, 244, 69, 255, 255, 255, 255, 255},
    {78, 0, 0, 244, 18, 255, 255, 255, 255, 255},
    {81, 0, 0, 244, 36, 255, 255, 255, 255, 255},
    {82, 0, 0, 244, 22, 255, 255, 255, 255, 255},
    {84, 0, 0, 244, 21, 255, 255, 255, 255, 255},
    {85, 0, 0, 244, 19, 255, 255, 255, 255, 255},
    {88, 0, 0, 244, 99, 255, 255, 255, 255, 255},
    {89, 0, 0, 244, 96, 255, 255, 255, 255, 255},
    {90, 0, 0, 244, 119, 255, 255, 255, 255, 255},
    {91, 0, 0, 254, 33, 255, 255, 255, 100, 85},
    {93, 0, 0, 254, 96, 33, 255, 255, 100, 85},
    {97, 0, 0, 244, 20, 255, 255, 255, 255, 255},
    {102, 0, 0, 244, 117, 255, 255, 255, 255, 255},
    {105, 0, 0, 244, 1, 255, 255, 255, 255, 255},
    {107, 0, 0, 253, 104, 4, 104, 116, 255, 255},
    {108, 0, 0, 253, 104, 7, 104, 113, 255, 255},
    {109, 0, 0, 244, 35, 255, 35, 255, 255, 255},
    {112, 0, 0, 253, 104, 0, 104, 114, 255, 255},
    {113, 0, 0, 253, 104, 34, 104, 115, 255, 255},
    {114, 0, 0, 253, 104, 2, 104, 118, 255, 255},
    {115, 0, 0, 244, 5, 255, 255, 255, 255, 255},
    {116, 0, 0, 253, 104, 6, 104, 116, 255, 255},
    {117, 0, 0, 253, 104, 16, 104, 117, 255, 255},
    {118, 0, 0, 244, 114, 255, 255, 255, 255, 255},
    {119, 0, 0, 244, 95, 255, 95, 255, 255, 255},
    {121, 0, 0, 244, 96, 19, 255, 255, 255, 255},
    {122, 0, 0, 244, 3, 255, 255, 255, 255, 255},
    {123, 0, 0, 244, 18, 255, 255, 255, 255, 255},
    {124, 0, 0, 244, 96, 16, 255, 255, 255, 255},
    {125, 0, 0, 244, 17, 255, 255, 255, 255, 255},
    {126, 0, 0, 244, 100, 255, 255, 255, 255, 255},
    {131, 0, 0, 244, 96, 103, 255, 255, 255, 255},
    {224, 17, 0, 244, 100, 255, 255, 255, 255, 255},
    {224, 20, 0, 244, 97, 255, 255, 255, 255, 255},
    {224, 31, 0, 244, 98, 255, 255, 255, 255, 255},
    {224, 39, 0, 244, 98, 255, 255, 255, 255, 255},
    {224, 47, 0, 244, 118, 255, 255, 255, 255, 255},
    {224, 74, 0, 244, 36, 255, 255, 255, 255, 255},
    {224, 90, 0, 244, 119, 255, 255, 255, 255, 255},
    {224, 107, 0, 244, 132, 255, 255, 255, 255, 255},
    {224, 108, 0, 244, 129, 255, 255, 255, 255, 255},
    {224, 112, 0, 244, 130, 255, 255, 255, 255, 255},
    {224, 113, 0, 244, 131, 255, 255, 255, 255, 255},
    {224, 114, 0, 244, 134, 255, 255, 255, 255, 255},
    {224, 116, 0, 244, 135, 255, 255, 255, 255, 255},
    {224, 117, 0, 244, 133, 255, 255, 255, 255, 255},
    {224, 240, 17, 244, 108, 255, 255, 255, 255, 255},
    {224, 240, 20, 244, 105, 255, 255, 255, 255, 255},
    {224, 240, 31, 244, 106, 255, 255, 255, 255, 255},
    {224, 240, 39, 244, 106, 255, 255, 255, 255, 255},
    {224, 240, 47, 244, 126, 255, 255, 255, 255, 255},
    {224, 240, 74, 244, 44, 255, 255, 255, 255, 255},
    {224, 240, 90, 244, 127, 255, 255, 255, 255, 255},
    {224, 240, 107, 244, 140, 255, 255, 255, 255, 255},
    {224, 240, 108, 244, 137, 255, 255, 255, 255, 255},
    {224, 240, 112, 244, 138, 255, 255, 255, 255, 255},
    {224, 240, 113, 244, 139, 255, 255, 255, 255, 255},
    {224, 240, 114, 244, 142, 255, 255, 255, 255, 255},
    {224, 240, 116, 244, 143, 255, 255, 255, 255, 255},
    {224, 240, 117, 244, 141, 255, 255, 255, 255, 255},
    {225, 20, 119, 244, 116, 255, 255, 255, 255, 255},
    {240, 1, 0, 244, 104, 120, 255, 255, 255, 255},
    {240, 3, 0, 244, 121, 255, 255, 255, 255, 255},
    {240, 4, 0, 244, 111, 255, 255, 255, 255, 255},
    {240, 5, 0, 244, 109, 255, 255, 255, 255, 255},
    {240, 6, 0, 244, 110, 255, 255, 255, 255, 255},
    {240, 7, 0, 244, 124, 255, 255, 255, 255, 255},
    {240, 9, 0, 244, 104, 121, 255, 255, 255, 255},
    {240, 10, 0, 244, 104, 111, 255, 255, 255, 255},
    {240, 11, 0, 244, 104, 109, 255, 255, 255, 255},
    {240, 12, 0, 244, 120, 255, 255, 255, 255, 255},
    {240, 13, 0, 244, 123, 255, 255, 255, 255, 255},
    {240, 14, 0, 244, 104, 30, 255, 255, 255, 255},
    {240, 17, 0, 244, 108, 255, 255, 255, 255, 255},
    {240, 18, 0, 244, 104, 255, 255, 255, 255, 255},
    {240, 20, 0, 244, 105, 255, 255, 255, 255, 255},
    {240, 21, 0, 244, 78, 255, 255, 255, 255, 255},
    {240, 22, 0, 244, 9, 255, 255, 255, 255, 255},
    {240, 26, 0, 244, 95, 255, 255, 255, 255, 255},
    {240, 27, 0, 244, 88, 255, 255, 255, 255, 255},
    {240, 28, 0, 244, 46, 255, 255, 255, 255, 255},
    {240, 29, 0, 244, 92, 255, 255, 255, 255, 255},
    {240, 30, 0, 244, 10, 255, 255, 255, 255, 255},
    {240, 33, 0, 244, 56, 255, 255, 255, 255, 255},
    {240, 34, 0, 244, 93, 255, 255, 255, 255, 255},
    {240, 35, 0, 244, 57, 255, 255, 255, 255, 255},
    {240, 36, 0, 244, 58, 255, 255, 255, 255, 255},
    {240, 37, 0, 244, 12, 255, 255, 255, 255, 255},
    {240, 38, 0, 244, 11, 255, 255, 255, 255, 255},
    {240, 41, 0, 244, 136, 255, 255, 255, 255, 255},
    {240, 42, 0, 244, 91, 255, 255, 255, 255, 255},
    {240, 43, 0, 244, 59, 255, 255, 255, 255, 255},
    {240, 44, 0, 244, 89, 255, 255, 255, 255, 255},
    {240, 45, 0, 244, 79, 255, 255, 255, 255, 255},
    {240, 46, 0, 244, 88, 255, 255, 255, 255, 255},
    {240, 49, 0, 244, 75, 255, 255, 255, 255, 255},
    {240, 50, 0, 244, 47, 255, 255, 255, 255, 255},
    {240, 51, 0, 244, 61, 255, 255, 255, 255, 255},
    {240, 52, 0, 244, 60, 255, 255, 255, 255, 255},
    {240, 53, 0, 244, 94, 255, 255, 255, 255, 255},
    {240, 54, 0, 254, 14, 255, 255, 255, 104, 30},
    {240, 58, 0, 244, 74, 255, 255, 255, 255, 255},
    {240, 59, 0, 244, 63, 255, 255, 255, 255, 255},
    {240, 60, 0, 244, 90, 255, 255, 255, 255, 255},
    {240, 61, 0, 244, 15, 255, 255, 255, 255, 255},
    {240, 62, 0, 244, 24, 255, 255, 255, 255, 255},
    {240, 65, 0, 254, 42, 255, 255, 255, 104, 45},
    {240, 66, 0, 244, 72, 255, 255, 255, 255, 255},
    {240, 67, 0, 244, 62, 255, 255, 255, 255, 255},
    {240, 68, 0, 244, 76, 255, 255, 255, 255, 255},
    {240, 69, 0, 244, 8, 255, 255, 255, 255, 255},
    {240, 70, 0, 244, 25, 255, 255, 255, 255, 255},
    {240, 73, 0, 254, 43, 255, 255, 255, 104, 45},
    {240, 74, 0, 254, 104, 42, 255, 255, 104, 43},
    {240, 75, 0, 244, 73, 255, 255, 255, 255, 255},
    {240, 76, 0, 244, 31, 255, 255, 255, 255, 255},
    {240, 77, 0, 244, 77, 255, 255, 255, 255, 255},
    {240, 78, 0, 244, 26, 255, 255, 255, 255, 255},
    {240, 81, 0, 244, 44, 255, 255, 255, 255, 255},
    {240, 82, 0, 244, 30, 255, 255, 255, 255, 255},
    {240, 84, 0, 244, 29, 255, 255, 255, 255, 255},
    {240, 85, 0, 244, 27, 255, 255, 255, 255, 255},
    {240, 88, 0, 244, 107, 255, 255, 255, 255, 255},
    {240, 89, 0, 244, 104, 255, 255, 255, 255, 255},
    {240, 90, 0, 244, 127, 255, 255, 255, 255, 255},
    {240, 91, 0, 254, 41, 255, 255, 255, 108, 93},
    {240, 93, 0, 254, 104, 41, 255, 255, 108, 93},
    {240, 93, 0, 244, 28, 255, 255, 255, 255, 255},
    {240, 102, 0, 244, 125, 255, 255, 255, 255, 255},
    {240, 105, 0, 244, 9, 255, 255, 255, 255, 255},
    {240, 107, 0, 253, 104, 12, 104, 124, 255, 255},
    {240, 108, 0, 253, 104, 15, 104, 121, 255, 255},
    {240, 109, 0, 244, 43, 255, 43, 255, 255, 255},
    {240, 112, 0, 253, 104, 8, 104, 122, 255, 255},
    {240, 113, 0, 253, 104, 42, 104, 123, 255, 255},
    {240, 114, 0, 253, 104, 10, 104, 126, 255, 255},
    {240, 115, 0, 244, 13, 255, 255, 255, 255, 255},
    {240, 116, 0, 253, 104, 14, 104, 124, 255, 255},
    {240, 117, 0, 253, 104, 24, 104, 125, 255, 255},
    {240, 118, 0, 244, 122, 255, 255, 255, 255, 255},
    {240, 119, 0, 244, 95, 255, 95, 255, 255, 255},
    {240, 121, 0, 244, 104, 27, 255, 255, 255, 255},
    {240, 122, 0, 244, 11, 255, 255, 255, 255, 255},
    {240, 123, 0, 244, 26, 255, 255, 255, 255, 255},
    {240, 124, 0, 244, 104, 24, 255, 255, 255, 255},
    {240, 125, 0, 244, 25, 255, 255, 255, 255, 255},
    {240, 126, 0, 244, 108, 255, 255, 255, 255, 255},
    {240, 131, 0, 244, 104, 111, 255, 255, 255, 255},
};


void msxmap::msx_interface_setup(void)
{
	//Not the STM32 default: Default MSX state - Release MSX keys;
	gpio_set(X7_port,
	X7_pin_id | X6_pin_id | X5_pin_id | X4_pin_id | X3_pin_id | X2_pin_id | X1_pin_id | X0_pin_id);

	//Init output port B15:8
	gpio_set_mode(X7_port, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, 
	X7_pin_id | X6_pin_id | X5_pin_id | X4_pin_id | X3_pin_id | X2_pin_id | X1_pin_id | X0_pin_id);

	//Init startup state of BSSR image to each Y scan
	for(uint8_t i = 0; i < 16; i++)
		x_bits[ i ] = X7_SET_OR | X6_SET_OR | X5_SET_OR | X4_SET_OR | X3_SET_OR | X2_SET_OR | X1_SET_OR | X0_SET_OR;
	
	// Initialize receive ringbuffer
	msx_dispatch_kq_put=0;
	msx_dispatch_kq_get=0;
	for(uint8_t i=0; i<MSX_DISP_QUEUE_SIZE; ++i)
	{
		msx_dispatch_keys_queue[i]=0;
	}
	
	// GPIO pins for MSX keyboard Y scan (PC3:0 of the MSX 8255 - PC3 MSX 8255 Pin 17)
	gpio_set_mode(Y3_port, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, Y3_pin_id); // PC3 (MSX 8255 Pin 17)
	gpio_set(Y3_port, Y3_pin_id); //pull up resistor
	exti_select_source(Y3_exti, Y3_port);
	exti_set_trigger(Y3_exti, EXTI_TRIGGER_BOTH); //Int on both raise and fall transitions
	exti_reset_request(Y3_exti);
	exti_enable_request(Y3_exti);
	gpio_port_config_lock(Y3_port, Y3_pin_id);

	// GPIO pins for MSX keyboard Y scan (PC3:0 of the MSX 8255 - PC2 MSX 8255 Pin 16)
	gpio_set_mode(Y2_port, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, Y2_pin_id); // PC2 (MSX 8255 Pin 16)
	gpio_set(Y2_port, Y2_pin_id); //pull up resistor
	exti_select_source(Y2_exti, Y2_port);
	exti_set_trigger(Y2_exti, EXTI_TRIGGER_BOTH); //Int on both raise and fall transitions
	exti_reset_request(Y2_exti);
	exti_enable_request(Y2_exti);
	gpio_port_config_lock(Y2_port, Y2_pin_id);

	// GPIO pins for MSX keyboard Y scan (PC3:0 of the MSX 8255 - PC1 MSX 8255 Pin 15)
	gpio_set_mode(Y1_port, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, Y1_pin_id); // PC1 (MSX 8255 Pin 15)
	gpio_set(Y1_port, Y1_pin_id); //pull up resistor
	exti_select_source(Y1_exti, Y1_port);
	exti_set_trigger(Y1_exti, EXTI_TRIGGER_BOTH); //Int on both raise and fall transitions
	exti_reset_request(Y1_exti);
	exti_enable_request(Y1_exti);
	gpio_port_config_lock(Y1_port, Y1_pin_id);

	// GPIO pins for MSX keyboard Y scan (PC3:0 of the MSX 8255 - PC0 MSX 8255 Pin 14)
	gpio_set_mode(Y0_port, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, Y0_pin_id); // PC0 (MSX 8255 Pin 14)
	gpio_set(Y0_port, Y0_pin_id); //pull up resistor
	exti_select_source(Y0_exti, Y0_port);
	exti_set_trigger(Y0_exti, EXTI_TRIGGER_BOTH); //Int on both raise and fall transitions
	exti_reset_request(Y0_exti);
	exti_enable_request(Y0_exti);
	gpio_port_config_lock(Y0_port, Y0_pin_id);

	// GPIO pins for MSX keyboard Y scan (PC3:0 of the MSX 8255 - PC0 MSX 8255 Pin 14),
	// CAP_LED and KANA/Cyrillic_LED (mapped to scroll lock) to replicate in PS/2 keyboard
	// Set to input and enable internal pulldown

	// CAPS_LED
	gpio_set_mode(CAPSLOCK_port, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, CAPSLOCK_pin_id); // CAP_LED (MSX 8255 Pin 11)
	gpio_set(CAPSLOCK_port, CAPSLOCK_pin_id); //pull up resistor
	gpio_port_config_lock(CAPSLOCK_port, CAPSLOCK_pin_id);

	// Kana LED
	gpio_set_mode(KANA_port, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, KANA_pin_id); // KANA_LED - Mapeado para Scroll Lock
	gpio_set(KANA_port, KANA_pin_id); //pull up resistor
	gpio_port_config_lock(KANA_port, KANA_pin_id);

	// Enable EXTI9_5 interrupt. (for Y - bits 0 and 1)
	nvic_enable_irq(NVIC_EXTI9_5_IRQ);

	// Enable EXTI15_10 interrupt.  (for Y - bits 0 and 1)
	nvic_enable_irq(NVIC_EXTI15_10_IRQ);
	
	//Highest priority to avoid interrupt Y scan
	nvic_set_priority(NVIC_EXTI9_5_IRQ, 10); 		//Y0 and Y1
	nvic_set_priority(NVIC_EXTI15_10_IRQ, 10);	//Y2 and Y3

	//Check the Database version, get y_dummy, ps2numlockstate and enable_xon_xoff
	compatible_database = true;
	//Locate: MSX_KEYB_DATABASE_CONVERSION[0][3]
	if ((MSX_KEYB_DATABASE_CONVERSION[0][0]!= 1) || (MSX_KEYB_DATABASE_CONVERSION[0][1]!= 0))
	{
		//Incompatible Database
		compatible_database = false;
		usart_send_string((uint8_t*)"Incompatible Database version. Please update it!\r\n\n");
	}
	y_dummy = MSX_KEYB_DATABASE_CONVERSION[0][3] & 0x0F; //Low nibble
	ps2numlockstate = ((MSX_KEYB_DATABASE_CONVERSION[0][3] & 0x10) != 0); //Bit 4
	enable_xon_xoff = ((MSX_KEYB_DATABASE_CONVERSION[0][3] & 0x20) != 0); //Bit 5
	update_ps2_leds = true;
}


// Verify if there is an available ps2_byte_received on the receive ring buffer, but does not fetch this one
//Input: void
//Output: True if there is char available in input buffer or False if none
bool msxmap::available_msx_disp_keys_queue_buffer(void)
{
	uint8_t i = msx_dispatch_kq_get;
	if(i == msx_dispatch_kq_put)
		//No char in buffer
		return false;
	else
		return true;
}


// Fetches the next ps2_byte_received from the receive ring buffer
//Input: void
//Outut: Available byte read
uint8_t msxmap::get_msx_disp_keys_queue_buffer(void)
{
	uint8_t i, result;

	i = msx_dispatch_kq_get;
	if(i == msx_dispatch_kq_put)
		//No char in buffer
		return 0;
	result = msx_dispatch_keys_queue[i];
	i++;
	msx_dispatch_kq_get = i & (uint8_t)(MSX_DISP_QUEUE_SIZE- 1); //if(i) >= (uint16_t)MSX_DISP_QUEUE_SIZE)	i = 0;
	return result;
}


/*Put a MSX Key (byte) into a buffer to get it mounted to x_bits
* Input: uint8_t as parameter
* Output: Total number of bytes in buffer, or ZERO if buffer was already full.*/
uint8_t msxmap::put_msx_disp_keys_queue_buffer(uint8_t data_word)
{
	uint8_t i, i_next;
	
	i = msx_dispatch_kq_put;
	i_next = i + 1;
	i_next &= (uint8_t)(MSX_DISP_QUEUE_SIZE - 1);
	if (i_next != msx_dispatch_kq_get)
	{
		msx_dispatch_keys_queue[i] = data_word;
		msx_dispatch_kq_put = i_next;
		return (uint8_t)(MSX_DISP_QUEUE_SIZE - msx_dispatch_kq_get + msx_dispatch_kq_put) & (uint8_t)(MSX_DISP_QUEUE_SIZE - 1);
	}
	else 
		return 0;
}


//The objective of this routine is implement a smooth typing.
//The usage is to put a byte key (bit 7:4 represents Y, bit 3 is the release=1/press=0, bits 2:0 are the X )
//F = 15Hz. This routine is called from Ticks interript routine
void msxmap::msxqueuekeys(void)
{
	uint8_t x_local, y_local, readkey;
	bool x_local_setb;
	if (available_msx_disp_keys_queue_buffer())
	{
		//This routine is allways called AFTER the mapped condition has been tested 
		readkey = get_msx_disp_keys_queue_buffer();
		y_local = (readkey & 0xF0) >> 4;
		x_local = readkey & 0x07;
		x_local_setb = ((readkey & PRESS_RELEASE_BIT) >> PRESS_RELEASE_BIT_POSITION) == (uint8_t)1;
		// Compute x_bits of ch and verifies when Y was last updated,
		// with aim to update MSX keys, no matters if the MSX has the interrupts stucked. 
		compute_x_bits_and_check_interrupt_stuck(y_local, x_local, x_local_setb);
	}
}


void msxmap::convert2msx()
{
	volatile uint16_t linhavarrida;
	volatile bool shiftstate;

	if (
	(keycode[0] == (uint8_t)1) 		&&
	(keycode[1] == (uint8_t)0x77) )
	{
		//NumLock Pressed. Toggle NumLock status
		ps2numlockstate = !ps2numlockstate;
		update_ps2_leds = true;  //this will force update_leds at main loop => To save interrupt time
		return;
	}

	if (
	( keycode[0] == (uint8_t)1)	  &&
	((keycode[1] == (uint8_t)0x12) || (keycode[1] == (uint8_t)0x59)) )
	{
		//Shift Pressed.
		shiftstate = true;
		return;
	}

	if (
	( keycode[0] == (uint8_t)2)		&&
	( keycode[1] == (uint8_t)0xF0) &&
	((keycode[2] == (uint8_t)0x12) || (keycode[2]== (uint8_t)0x59)) )
	{
		//Shift Released (Break code).
		shiftstate = false;
		return;
	}

	//Now searches for PS/2 scan code in MSX Table to match. First search first colunm
	linhavarrida = 1;
	while ( 
	(MSX_KEYB_DATABASE_CONVERSION[linhavarrida][0] != keycode[1]) &&
	(linhavarrida < MAX_LINHAS_VARRIDAS) )
	{
		linhavarrida++;
	}
	if (
	(keycode[0] == (uint8_t)1) && 
	(keycode[1]  == MSX_KEYB_DATABASE_CONVERSION[linhavarrida][0]) && 
	(linhavarrida < MAX_LINHAS_VARRIDAS) )
	{
		//1 byte key
		msx_dispatch(linhavarrida, shiftstate);
		return;
	}
	if ((keycode[0] >= (uint8_t)1) &&
	(linhavarrida < MAX_LINHAS_VARRIDAS) )
	{
		//2 bytes key, then now search match on second byte of keycode
		while ( 
		(MSX_KEYB_DATABASE_CONVERSION[linhavarrida][1] != keycode[2]) &&
		(linhavarrida < MAX_LINHAS_VARRIDAS))
		{
			linhavarrida++;
		}
		if(
		(keycode[0] == (uint8_t)2) && 
		(keycode[1] == MSX_KEYB_DATABASE_CONVERSION[linhavarrida][0]) && 
		(keycode[2] == MSX_KEYB_DATABASE_CONVERSION[linhavarrida][1]) )
		{
			//Ok: Matched code
			msx_dispatch(linhavarrida, shiftstate);
			return;
		}
		//3 bytes key, then now search match on third byte of keycode
		while (
		(MSX_KEYB_DATABASE_CONVERSION[linhavarrida][2] != keycode[3]) &&
		(linhavarrida < MAX_LINHAS_VARRIDAS) )
		{
			linhavarrida++;
		}
		if( 
		keycode[0] == 3 && 
		keycode[1] == MSX_KEYB_DATABASE_CONVERSION[linhavarrida][0] && 
		keycode[2] == MSX_KEYB_DATABASE_CONVERSION[linhavarrida][1] &&
		keycode[3] == MSX_KEYB_DATABASE_CONVERSION[linhavarrida][2] )
		{
			//Ok: Matched code
			msx_dispatch(linhavarrida, shiftstate);
			return;
		}
	}
}

	/*
	The structure of the Database is:
		The  three first columns of each line are the mapped scan codes;
		The 4th column is The Control Byte, detailed bellow:
		CONTROL BYTE:
			High nibble is Reserved;
			(bit 3) Combined Shift;
			(bit 2) Reserved-Not used;
			(bits 1-0) Modifyer Type:
			.0 - Default mapping
			.1 - NumLock Status+Shift changes
			.2 - PS/2 Shift
			.3 - Reserved-Not used
		
		This table has 3 modifyers: Up two MSX keys are considered to each mapping behavior modifying:
		
		5th and 6th columns have the mapping ".0 - Default mapping";
		7th e 8th columns have the mapping ".1 - NumLock Status+Shift changes";
		9th and 10th columns have the mapping ".2 - PS/2 Shift", where I need to
		release the sinalized Shift in PS/2 to the MSX and put the coded key, and so,
		release them, reapplying the Shift key, deppending on the initial state;
		
		
		Each column has a MSX coded key, with the following structure:
		(Bit 7:4) MSX Y PPI 8255 PC3:0 is send to an OC outputs BCD decoder, for example:
							In the case of Hotbit HB8000, the keyboard scan is done as a 9 columns scan, CI-16 7445N 08 to 00;
							If equals to 1111 (Y=15), there is no MSX key mapped.
		(Bit 3)		0: keypress
							1: key release;
		(Bit 2:0) MSX X, ie, which bit will carry the key, to be read by PPI 8255 PB7:0.
	
	
	Now in my native language: Português
	 Primeiramente verifico se este scan, que veio em keycode[n], está referenciado na tabela MSX_KEYB_DATABASE_CONVERSION.
	 Esta tabela, montada em excel, está pronta para ser colada
	 A tabela já está com sort, para tornar possível executar uma pesquisa otimizada.
	
	 As três primeiras posições (colunas) de cada linha são os scan codes mapeados;
	 A 4ª coluna é o controle, e tem a seguinte estrutura:
		CONTROL BYTE:
		High nibble is Reserved; 
		(bit 3) Combined Shift;
		(bit 2) Reserved-Not used;
		(bits 1-0) Modifyer Type:
		.0 - Default mapping
		.1 - NumLock Status+Shift changes
		.2 - PS/2 Shift
		.3 - Reserved-Not used
	
	Esta tabela contém 3 modificadores: São codificadas até 2 teclas do MSX para cada modificador de mapeamento:
	
	 5ª e 6ª colunas contém o mapeamento ".0 - Default mapping";
	 7ª e 8ª colunas contém o mapeamento ".1 - NumLock Status+Shift changes";
	 9ª e 10ª colunas contém o mapeamento ".2 - PS/2 Shift", onde necessito
	 liberar o Shift sinalizado no PS/2 para o MSX e colocar as teclas codificadas,
	 e liberá-las, reinserindo o Shift, se aplicável;
	
	
	 Cada coluna contém uma tecla codificada para o MSX, com a seguinte estrutura:
	 (Bit 7:4) MSX Y PPI 8255 PC3:0 é enviada à um decoder BCD com OC outputs.
						 MSX Y PPI 8255 PC3:0. No caso do Hotbit, o scan é feito em 9 colunas, CI-16 7445N 08 a 00;
						 Se 1111 (Y=15), não há tecla MSX mapeada.
	 (Bit 3)	 0: keypress
						 1: key release;
	 (Bit 2:0) MSX X, ou seja, qual bit levará a informação da tecla, a ser lida pela PPI 8255 PB7:0.
	
	*/

void msxmap::msx_dispatch(volatile uint16_t linhavarrida, volatile bool shiftstate)
{
	volatile uint8_t y_local, x_local;
	volatile bool x_local_setb;

	//Qual o tipo de Mapeamento?
	
	switch(MSX_KEYB_DATABASE_CONVERSION[linhavarrida][3] & 0x03)
	{
		case 0:
		{	// .0 - Mapeamento default (Colunas 4 e 5)
			// Tecla 1:
			y_local = (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][4] & (uint8_t)0xF0) >> 4;
			// Verifica se está mapeada
			if (y_local != y_dummy)
			{
				x_local = MSX_KEYB_DATABASE_CONVERSION[linhavarrida][4] & (uint8_t)0x07;
				x_local_setb = ((MSX_KEYB_DATABASE_CONVERSION[linhavarrida][4] & (uint8_t)PRESS_RELEASE_BIT) >> PRESS_RELEASE_BIT_POSITION) == (uint8_t)1;
				// Calcula x_bits da Tecla 1 e verifica se o tempo em que foi atualizado o dado de Linha X da Coluna Y,
				// com a finalidade de atualizar teclas mesmo sem o PPI ser atualizado. 
				compute_x_bits_and_check_interrupt_stuck(y_local, x_local, x_local_setb);
			}
			// Tecla 2:
			y_local = (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][5] & (uint8_t)0xF0) >> 4;
			// Verifica se está mapeada
			if (y_local != y_dummy)
			{
				put_msx_disp_keys_queue_buffer(MSX_KEYB_DATABASE_CONVERSION[linhavarrida][5]);
			}
			break;
		} // .0 - Mapeamento default (Colunas 4 e 5)

		case 1:
		{	// .1 - Mapeamento NumLock (Colunas 6 e 7)
			if (ps2numlockstate ^ shiftstate)
			{
				//numlock e Shift tem estados diferentes
				// Tecla 1 (PS/2 NumLock ON (Default)):
				y_local = (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][4] & (uint8_t)0xF0)  >> 4;
				// Verifica se está mapeada
				if (y_local != y_dummy)
				{
					x_local = MSX_KEYB_DATABASE_CONVERSION[linhavarrida][4] & (uint8_t)0x07;
					x_local_setb = ((MSX_KEYB_DATABASE_CONVERSION[linhavarrida][4] & PRESS_RELEASE_BIT) >> PRESS_RELEASE_BIT_POSITION) == (uint8_t)1;
					// Calcula x_bits da Tecla 1 e verifica se o tempo em que foi atualizado o dado de Linha X da Coluna Y,
					// com a finalidade de atualizar teclas mesmo sem o PPI ser atualizado. 
					compute_x_bits_and_check_interrupt_stuck(y_local, x_local, x_local_setb);
				}
				// Tecla 2 (PS/2 NumLock ON (Default)):
				y_local = (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][5] & (uint8_t)0xF0) >> 4;
				// Verifica se está mapeada
				if (y_local != y_dummy)
				{
					put_msx_disp_keys_queue_buffer(MSX_KEYB_DATABASE_CONVERSION[linhavarrida][5]);
				}
			}
			else //if (ps2numlockstate ^ shiftstate)
			{
				//numlock e Shift tem mesmo estado
				// Verifica se ha teclas mapeadas
				// Tecla 1 (PS/2 NumLock OFF):
				y_local = (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][6] & (uint8_t)0xF0)  >> 4;
				// Verifica se está mapeada
				if (y_local != y_dummy)
				{
					x_local = MSX_KEYB_DATABASE_CONVERSION[linhavarrida][6] & (uint8_t)0x07;
					x_local_setb = ((MSX_KEYB_DATABASE_CONVERSION[linhavarrida][6] & (uint8_t)PRESS_RELEASE_BIT) >> PRESS_RELEASE_BIT_POSITION) == (uint8_t)1;
					// Calcula x_bits da Tecla 1 e verifica se o tempo em que foi atualizado o dado de Linha X da Coluna Y,
					// com a finalidade de atualizar teclas mesmo sem o PPI ser atualizado. 
					compute_x_bits_and_check_interrupt_stuck(y_local, x_local, x_local_setb);
				}
				// Tecla 2 (PS/2 NumLock OFF):
				y_local = (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][7] & (uint8_t)0xF0) >> (uint8_t)4;
				// Verifica se está mapeada
				if (y_local != y_dummy)
				{
					put_msx_disp_keys_queue_buffer(MSX_KEYB_DATABASE_CONVERSION[linhavarrida][7]);
				}
			} //if (ps2numlockstate ^ shiftstate)
			break;
		}  // .1 - Mapeamento NumLock (Colunas 6 e 7)

		case 2:
		{	// .2 - Mapeamento alternativo (PS/2 Left and Right Shift)  (Colunas 8 e 9)
			if (!shiftstate)
			{
				// Tecla 1 (PS/2 NumLock ON (Default)):
				y_local = (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][4] & (uint8_t)0xF0)  >> 4;
				// Verifica se está mapeada
				if (y_local != y_dummy)
				{
					x_local = MSX_KEYB_DATABASE_CONVERSION[linhavarrida][4] & (uint8_t)0x07;
					x_local_setb = ((MSX_KEYB_DATABASE_CONVERSION[linhavarrida][4] & (uint8_t)PRESS_RELEASE_BIT) >> PRESS_RELEASE_BIT_POSITION) == (uint8_t)1;
					// Calcula x_bits da Tecla 1 e verifica se o tempo em que foi atualizado o dado de Linha X da Coluna Y,
					// com a finalidade de atualizar teclas mesmo sem o PPI ser atualizado. 
					compute_x_bits_and_check_interrupt_stuck(y_local, x_local, x_local_setb);
				}
				// Tecla 2 (PS/2 NumLock ON (Default)):
				y_local = (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][5] & (uint8_t)0xF0) >> 4;
				// Verifica se está mapeada
				if (y_local != y_dummy)
				{
					put_msx_disp_keys_queue_buffer(MSX_KEYB_DATABASE_CONVERSION[linhavarrida][5]);
				}
			}
			else //if (!shiftstate) => now, after this else, shiftstate is true (ON)
			{
				// Verifica se ha teclas mapeadas
				// Tecla 1 (PS/2 Left and Right Shift):
				y_local = (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][8] & (uint8_t)0xF0) >> 4;
				// Verifica se está mapeada
				if (y_local != y_dummy)
				{
					if (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][8] == (uint8_t)0x64) //if CODE key is to be pressed
					{
						x_bits[Y_SHIFT] = (x_bits[Y_SHIFT] | X0_SET_OR) & X0_SET_AND;		//then Release MSX Shift key
						//verify if the Y interrupts are stucked 
						if (systicks - previous_y_systick[Y_SHIFT] > MAX_TIME_OF_IDLE_KEYSCAN_SYSTICKS)
						{
							//MSX is not updating Y, so updating keystrokes by interrupts is not working
							//Verify the actual hardware Y_SCAN
							// First I have to disable Y_SCAN interrupts, to avoid misspelling due to updates
							exti_disable_request(Y3_exti | Y2_exti | Y1_exti | Y0_exti);
							// Read the MSX keyboard Y scan through GPIO pins A11:A8, mask to 0 other bits and rotate right 8
							msx_Y_scan = (gpio_port_read(GPIOA)>>8) & 0x0F; //Read Y3:0 pins
							if (msx_Y_scan == Y_SHIFT)
							{
								GPIOB_BSRR = x_bits[Y_SHIFT]; //Atomic GPIOB update => Release and press MSX keys for this column
								//update time marker for previous_y_systick[Y_SHIFT]
								previous_y_systick[Y_SHIFT] = systicks;
							}
							//Than reenable Y_SCAN interrupts
							exti_enable_request(Y0_exti | Y1_exti | Y2_exti | Y3_exti);
						}
					}  //if (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][8] == 0x64) //if CODE key pressed
					if (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][8] == (uint8_t)0x6C) //if CODE key released
					{
						x_bits[Y_SHIFT] = (x_bits[Y_SHIFT] & X0_CLEAR_AND) | X0_CLEAR_OR;		//then Press MSX Shift key
						if (systicks - previous_y_systick[Y_SHIFT] > MAX_TIME_OF_IDLE_KEYSCAN_SYSTICKS)
						{
							//MSX is not updating Y, so updating keystrokes by interrupts is not working
							//Verify the actual hardware Y_SCAN
							// First I have to disable Y_SCAN interrupts, to avoid misspelling due to updates
							exti_disable_request(Y3_exti | Y2_exti | Y1_exti | Y0_exti);
							msx_Y_scan = (gpio_port_read(GPIOA)>>8) & 0x0F; //Read Y3:0 pins
							if (msx_Y_scan == Y_SHIFT)
							{
								GPIOB_BSRR = x_bits[Y_SHIFT];
								//update time marker for previous_y_systick[Y_SHIFT]
								previous_y_systick[Y_SHIFT] = systicks;
							}
							//Than reenable Y_SCAN interrupts
							exti_enable_request(Y0_exti | Y1_exti | Y2_exti | Y3_exti);
						}
					}	//if (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][8] == 0x6C) //if CODE key released
					//Process Code key 
					put_msx_disp_keys_queue_buffer(MSX_KEYB_DATABASE_CONVERSION[linhavarrida][8]);
				}
				// Tecla 2 (PS/2 Left and Right Shift):
				y_local = (MSX_KEYB_DATABASE_CONVERSION[linhavarrida][9] & (uint8_t)0xF0) >> 4;
				// Verifica se está mapeada
				if (y_local != y_dummy)
				{
					put_msx_disp_keys_queue_buffer(MSX_KEYB_DATABASE_CONVERSION[linhavarrida][9]);
				}
			} //if (shiftstate)
		}  // .2 - Mapeamento alternativo  (PS/2 Left  and Right Shift)  (Colunas 8 e 9)
	}
}

void msxmap::compute_x_bits_and_check_interrupt_stuck (
					volatile uint8_t y_local, volatile uint8_t x_local, volatile bool x_local_setb)
{
	switch (x_local)
	{
		case 0:
			if (x_local_setb)	//key release
			{
				x_bits[y_local] = (x_bits[y_local] | X0_SET_OR) & X0_SET_AND;
 				break;
			}
			else 							//keypress
			{
				x_bits[y_local] = (x_bits[y_local] & X0_CLEAR_AND) | X0_CLEAR_OR;
				break;
			}
		case 1:
			if (x_local_setb)	//key release
			{
				x_bits[y_local] = (x_bits[y_local] | X1_SET_OR) & X1_SET_AND;
				break;
			}
			else							//keypress
			{
				x_bits[y_local] = (x_bits[y_local] & X1_CLEAR_AND) | X1_CLEAR_OR;
				break;
			}
		case 2:
			if (x_local_setb)
			{
				x_bits[y_local] = (x_bits[y_local] | X2_SET_OR) & X2_SET_AND;
				break;
			}
			else
			{
				x_bits[y_local] = (x_bits[y_local] & X2_CLEAR_AND) | X2_CLEAR_OR;
				break;
			}
		case 3:
			if (x_local_setb)
			{
				x_bits[y_local] = (x_bits[y_local] | X3_SET_OR) & X3_SET_AND;
				break;
			}
			else
			{
				x_bits[y_local] = (x_bits[y_local] & X3_CLEAR_AND) | X3_CLEAR_OR;
				break;
			}
		case 4:
			if (x_local_setb)
			{
				x_bits[y_local] = (x_bits[y_local] | X4_SET_OR) & X4_SET_AND;
				break;
			}
			else
			{
				x_bits[y_local] = (x_bits[y_local] & X4_CLEAR_AND) | X4_CLEAR_OR;
				break;
			}
		case 5:
			if (x_local_setb)
			{
				x_bits[y_local] = (x_bits[y_local] | X5_SET_OR) & X5_SET_AND;
				break;
			}
			else
			{
				x_bits[y_local] = (x_bits[y_local] & X5_CLEAR_AND) | X5_CLEAR_OR;
				break;
			}
		case 6:
			if (x_local_setb)
			{
				x_bits[y_local] = (x_bits[y_local] | X6_SET_OR) & X6_SET_AND;
				break;
			}
			else
			{
				x_bits[y_local] = (x_bits[y_local] & X6_CLEAR_AND) | X6_CLEAR_OR;
				break;
			}
		case 7:
			if (x_local_setb)
			{
				x_bits[y_local] = (x_bits[y_local] | X7_SET_OR) & X7_SET_AND;
				break;
			}
			else
			{
				x_bits[y_local] = (x_bits[y_local] & X7_CLEAR_AND) | X7_CLEAR_OR;
				break;
			}
	}

	//ver se o tempo em que foi atualizado o dado de Linha X da Coluna Y, com a finalidade de atualizar teclas mesmo sem o PPI ser atualizado. 
	if (systicks - previous_y_systick[y_local] > MAX_TIME_OF_IDLE_KEYSCAN_SYSTICKS)
	{
		//MSX is not updating Y, so updating keystrokes by interrupts is not working
		//Verify the actual hardware Y_SCAN
		// First I have to disable Y_SCAN interrupts, to avoid misspelling due to updates
		exti_disable_request(Y3_exti | Y2_exti | Y1_exti | Y0_exti);
		// Read the MSX keyboard Y scan through GPIO pins A11:A8, mask to 0 other bits and rotate right 8
		msx_Y_scan = (gpio_port_read(GPIOA)>>8) & 0x0F; //Read Y3:0 pins
		if (msx_Y_scan == y_local)
		{
			GPIOB_BSRR = x_bits[msx_Y_scan]; //Atomic GPIOB update => Release and press MSX keys for this column
			//update time marker for previous_y_systick[y_local]
			//previous_y_systick[y_local] = systicks;
		}
		//Than reenable Y_SCAN interrupts
		exti_enable_request(Y0_exti | Y1_exti | Y2_exti | Y3_exti);
	}
}


/*************************************************************************************************/
/*************************************************************************************************/
/******************************************* ISR's ***********************************************/
/*************************************************************************************************/
/*************************************************************************************************/
void exti9_5_isr(void) // PC0 and PC1 - It works like interrupt on change of each one of Y connected pins
{
	//Debug & performance measurement
	gpio_clear(Dbg_Yint_port, Dbg_Yint0e1_pin_id); //Signs start of interruption

	// Read the MSX keyboard Y scan through GPIO pins A11:A8, mask to 0 other bits and rotate right 8
	msx_Y_scan = (gpio_port_read(GPIOA)>>8) & 0x0F; //Read bits B7:3 (B5 zeroed)

	GPIOB_BSRR = x_bits[msx_Y_scan]; //Atomic GPIOB update => Release and press MSX keys for this column
    
	// Clear interrupt Y Scan flags, including those not used on this ISR
	// if(exti_get_flag_status(EXTI7), (EXTI6), (EXTI4) and (EXTI3))
	exti_reset_request(Y0_exti | Y1_exti | Y2_exti |Y3_exti);
    
	//Update systicks (time stamp) for this Y
	previous_y_systick[msx_Y_scan]  = systicks;

	//Debug & performance measurement
	gpio_set(Dbg_Yint_port, Dbg_Yint0e1_pin_id); //Signs end of interruption. Default condition is "1"
}


void exti15_10_isr(void) // PC2 and PC3 - It works like interrupt on change of each one of Y connected pins
{
	//Debug & performance measurement
	gpio_clear(Dbg_Yint_port, Dbg_Yint2e3_pin_id); //Signs start of interruption

	// Read the MSX keyboard Y scan through GPIO pins A11:A8, mask to 0 other bits and rotate right 8
	msx_Y_scan = (gpio_port_read(GPIOA)>>8) & 0x0F; //Read bits B7:3 (B5 zeroed)

	GPIOB_BSRR = x_bits[msx_Y_scan]; //Atomic GPIOB update => Release and press MSX keys for this column
    
	// Clear interrupt Y Scan flags, including those not used on this ISR
	// if(exti_get_flag_status(EXTI7), (EXTI6), (EXTI4) and (EXTI3))
	exti_reset_request(Y0_exti | Y1_exti | Y2_exti |Y3_exti);
    
	//Update systicks (time stamp) for this Y
	previous_y_systick[msx_Y_scan]  = systicks;

	//Debug & performance measurement
	gpio_set(Dbg_Yint_port, Dbg_Yint2e3_pin_id); //Signs end of interruption. Default condition is "1"
}

