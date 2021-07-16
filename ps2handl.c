#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/timer.h>

#include "serial.h"
#include "ps2handl.h"
#include "hr_timer.h"
#include "port_def.h"
//Use Tab width=2


//States definitions of PS/2 clock interrupt machine 
#define	PS2INT_RECEIVE										0x400
#define	PS2INT_SEND_COMMAND								0x401
#define	PS2INT_WAIT_FOR_COMMAND_ACK				0x402
#define	PS2INT_SEND_ARGUMENT							0x403
#define	PS2INT_WAIT_FOR_ARGUMENT_ACK			0x404

//PS/2 keyboard iteration constants
#define COMM_TYPE3_NO_REPEAT							0xF8	//Type 3 command
#define COMM_READ_ID											0xF2
#define COMM_SET_TYPEMATIC_RATEDELAY			0xF3	//Type 2 command
#define COMM_SET_RESET_LEDS								0xED
#define COMM_ECHO													0xEE
#define ARG_NO_ARG												0xFF
#define ARG_LOWRATE_LOWDELAY							0x7F	//Type 2 (Delay 1 second to repeat. 2cps repeat rate)
#define KB_ACKNOWLEDGE										0xFA
#define KB_FIRST_ID												0xAB
#define KB_SECOND_ID											0x83
#define KB_SUCCESSFULL_BAT								0xAA
#define KB_ERROR_BAT											0xFC


//Global Vars
extern bool update_ps2_leds, ps2numlockstate;			//Declared on msxmap.cpp
volatile uint16_t ps2int_state;
volatile uint8_t ps2int_TX_bit_idx;
volatile uint8_t ps2int_RX_bit_idx;
extern uint16_t state_overflow_tim2;							//Declared on hr_timer_delay.c
extern uint64_t TIM2_Update_Cnt;									//Declared on hr_timer_delay.c Overflow of time_between_ps2clk

volatile uint8_t command, argument;

volatile uint32_t prev_systicks;
extern uint32_t systicks;													//Declared on msxhid.cpp
extern uint64_t acctimeps2data0;									//Declared on hr_timer_delay.c
extern uint8_t keycode[4];												//declared on msxmap.cpp
extern uint64_t time_between_ps2clk;							//Declared on hr_timer_delay.c
extern uint16_t fail_count;												//declared on msxhid.cpp
volatile bool formerps2datapin;
volatile bool ps2_keyb_detected;
volatile bool command_ok;

volatile bool mount_keycode_OK;									 	//used on mount_keycode()
volatile bool ps2_keystr_e0 = false;
volatile bool ps2_keystr_e1 = false;
volatile bool ps2_keystr_f0 = false;
volatile uint8_t ps2_byte_received;
volatile uint8_t mount_keycode_count_status = 0;

//Need to stay as global to avoid creating different instancies
volatile uint8_t ps2_recv_buffer[PS2_RECV_BUFFER_SIZE];
volatile uint8_t ps2_recv_buff_put;
volatile uint8_t ps2_recv_buff_get;

//template<typename... Args>
//void debug_print(Args... args)
//{
	/*debug_count++;
	// printf("dbg: %03d ",debug_count);
	// printf(args...);*/
//}

//Prototypes not declared in ps2stdhandl.h
void init_ps2_recv_buffer(void);
bool available_ps2_byte(void);
uint8_t get_ps2_byte(volatile uint8_t*);
void send_start_bit_in(uint16_t);
void ps2_clock_send(bool);
void ps2_clock_receive(bool);
void ps2_send_command(uint8_t, uint8_t);
void reset_mount_keycode_machine(void);


//Power on PS/2 Keyboard and related pins setup
void power_on_ps2_keyboard()
{
	//Power pin control
	gpio_set_mode(ps2_power_ctr_port, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, ps2_power_ctr_pin);
	gpio_set(ps2_power_ctr_port, ps2_power_ctr_pin);

	// PS/2 keyboard Clock and Data pins
	gpio_set(ps2_clock_pin_port, ps2_clock_pin_id); //Hi-Z
	gpio_set_mode(ps2_clock_pin_port, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, ps2_clock_pin_id);
	gpio_port_config_lock(ps2_clock_pin_port, ps2_clock_pin_id);
	gpio_set(ps2_data_pin_port, ps2_data_pin_id);		//Hi-Z
	gpio_set_mode(ps2_data_pin_port, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, ps2_data_pin_id);
	gpio_port_config_lock(ps2_data_pin_port, ps2_data_pin_id);
	
	//Setup keyboard interrupt on pin change (not TIM2 CCR1):
	exti_select_source(ps2_clock_pin_exti, ps2_clock_pin_port);
	exti_set_trigger(ps2_clock_pin_exti, EXTI_TRIGGER_FALLING); //Int on fall transitions
	exti_reset_request(ps2_clock_pin_exti);
	exti_enable_request(ps2_clock_pin_exti);

	/*//PS/2 Clock interrupt
	// Enable EXTI4 interrupt. Used to handle ps2_clock_pin
	nvic_enable_irq(NVIC_EXTI4_IRQ);
	//Make sure that it has low priority than Y scan. It serves PS/2 Clock pin, through PB4
	nvic_set_priority(NVIC_EXTI4_IRQ, 20); */

	init_ps2_recv_buffer();
}

// Initialize receive ringbuffer
void init_ps2_recv_buffer()
{
	ps2_recv_buff_put=0;
	ps2_recv_buff_get=0;
	for(uint8_t i=0; i<PS2_RECV_BUFFER_SIZE; ++i)
	{
		ps2_recv_buffer[i]=0;
	}
}

// Verify if there is an available ps2_byte_received on the receive ring buffer, but does not fetch this one
bool available_ps2_byte()
{
	uint8_t i = ps2_recv_buff_get;
	if(i == ps2_recv_buff_put)
		//No char in buffer
		return false;
	else
		return true;
}

// Fetches the next ps2_byte_received from the receive ring buffer
uint8_t get_ps2_byte(volatile uint8_t *buff)
{
	uint8_t i, result;

	i = ps2_recv_buff_get;
	if(i == ps2_recv_buff_put)
		//No char in buffer
		return 0;
	result = buff[i];
	i++;
	ps2_recv_buff_get = i & (uint16_t)(PS2_RECV_BUFFER_SIZE - 1); //if(i >= (uint16_t)SERIAL_RING_BUFFER_SIZE)	i = 0;
	return result;
}


bool ps2_keyb_detect(void)
{
	uint32_t systicks_start_command;	//Initial time mark
	uint8_t mountstring[16];					//Used in usart_send_string()
	
	/* List of most used commands to control the PS/2 Keyboard:
	* 0xED: Set/Reset LEDs.
	* 0xEE: Echo.
	* 0xF2: Read device type.  It musts responds with 0xFA, 0xAB, 0x83.
	* 0xF3: Set Typematic Rate/Delay 2 bytes command 0xF3 + 0x7F (2cps repeat rate + 1 second delay).
	* 0xF4: Enable device.
	* 0xFF: Reset command => Wait 700ms.
	*/

	//Wait for 2.5s to keyboard execute its own power on and BAT (Basic Assurance Test) procedure
	systicks_start_command = systicks;
	ps2_keyb_detected = false;
	//User messages
	usart_send_string((uint8_t*)"Waiting up to 2.5s to power up the keyboard and run BAT...\n");
	while ( ((systicks-systicks_start_command) < (25*3)) && (!available_ps2_byte()) )	//Wait 2500ms for keyboard power on
		prev_systicks = systicks;	//To avoid errors on keyboard power up BEFORE the first access
	//PS/2 keyboard might already sent its BAT result. Check it:
	ps2_byte_received = get_ps2_byte(&ps2_recv_buffer[0]);
	if(ps2_byte_received != 0)
	{
		if(ps2_byte_received == KB_SUCCESSFULL_BAT)
		{
			//User messages
			usart_send_string((uint8_t*)"BAT OK\r\n");
		}
		else
		{
			//User messages
			usart_send_string((uint8_t*)"Received 0x");
			conv_uint8_to_2a_hex(ps2_byte_received, &mountstring[0]);
			usart_send_string((uint8_t*)&mountstring[0]);
			usart_send_string((uint8_t*)"\r\n");
		}
	}
	
	//Send command 0xF2 (Read ID). It musts responds with 0xFA, 0xAB, 0x83
	// Wait clock line to be unactive for 100 ms (3 systicks)
	//usart_send_string((uint8_t*)"Sending Read ID comm\n");
	systicks_start_command = systicks;
	ps2_send_command(COMM_READ_ID, ARG_NO_ARG); //Read ID command.
	while (!command_ok && (systicks - systicks_start_command)<(3*3)) //Must be excecuted in less than 100ms
    __asm("nop");
	if (command_ok)
	{
		//usart_send_string((uint8_t*)"Waiting 0xAB\n");
		systicks_start_command = systicks;
		while(!available_ps2_byte()&& (systicks - systicks_start_command)<(1*3))
    __asm("nop");
		ps2_byte_received = get_ps2_byte(&ps2_recv_buffer[0]);
		if(ps2_byte_received == KB_FIRST_ID)
		{
			//usart_send_string((uint8_t*)"Waiting 0x83\n");
			systicks_start_command = systicks;
			while(!available_ps2_byte()&& (systicks - systicks_start_command)<(1*3))
			__asm("nop");
			ps2_byte_received = get_ps2_byte(&ps2_recv_buffer[0]);
			if(ps2_byte_received == KB_SECOND_ID)
			{
				ps2_keyb_detected = true;
				//User messages
				usart_send_string((uint8_t*)"PS/2 Keyboard detected\n");
			}
			else
			{
				//usart_send_string((uint8_t*)"Did not receive 0x83");
				return ps2_keyb_detected;
			}
		}
		else
		{
			//usart_send_string((uint8_t*)"Did not receive 0xAB");
			return ps2_keyb_detected;
		}
	}
	else
	{
		//User messages
		/*usart_send_string((uint8_t*)"PS/2 ReadID command not OK. Elapsed time was ");
		conv_uint32_to_8a_hex((systicks - systicks_start_command), &mountstring[0]);
		usart_send_string((uint8_t*)&mountstring[0];
		usart_send_string((uint8_t*)"\n"); */
		return ps2_keyb_detected;
	}

	//The objective of this block is to minimize the keyboard interruptions, to keep time to high priority MSX interrupts.
	//Send type 3 command 0xFA (Set Key Type Make/Break - This one only disables typematic repeat):
	//  If it does not receive "ack" (0xFA), then send type 2 command 0xF3 + 0x7F (2cps repeat rate + 1 second delay)
	//  It musts respond with an "ack" after the first byte, than with a second "ack" after the second byte.
	//User messages
	//usart_send_string((uint8_t*)"Type 2 sets typematic repeat 0xF3 0x7F requested\n");
	systicks_start_command = systicks;
	//Type 2 command: Set typematic rate to 2 cps and delay to 1 second.
	ps2_send_command(COMM_SET_TYPEMATIC_RATEDELAY, ARG_LOWRATE_LOWDELAY);
	while (!command_ok && (systicks - systicks_start_command)<(1*3)) //Must be excecuted in less than 100ms
		__asm("nop");
	if (command_ok)
	{
		//User messages
		usart_send_string((uint8_t*)"Type 2 (Delay 1 second to repeat. 2cps repeat rate) OK\n");
	}
	else
	{
		//User messages
		//usart_send_string((uint8_t*)"Type 3 Disables typematic repeat 0xFA requested\n");
		systicks_start_command = systicks;
		//Type 3 command: Set All Keys Make/Break: This one only disables typematic repeat and applies to all keys
		ps2_send_command(COMM_TYPE3_NO_REPEAT, ARG_NO_ARG);
		while (!command_ok && (systicks - systicks_start_command)<(1*3)) //Must be excecuted in less than 100ms
			__asm("nop");
		if (command_ok)
		{
			//User messages
			usart_send_string((uint8_t*)"Type 3 Disables typematic 0xFA repeat OK\n");
		}
	}
	return ps2_keyb_detected;
}

//Insert a delay before run send_start_bit()
void send_start_bit_in(uint16_t x_usec)
{
	delay_usec(x_usec, SEND_ST_BIT); //wait x_usec and go to send_start_bit on TIM2 overflow interrupt
}


//This three functions are the split of Transmit Initiator, to avoid stuck inside an interrupt due to 120u and 20usec
void send_start_bit(void)
{
	//exti_disable_request(ps2_clock_pin_exti); //Disable keyboard int on clock pin change
	timer_disable_irq(TIM2, TIM_DIER_CC1IE);	// Disable interrupt on Capture/Compare1, but keeps on overflow
	command_ok = false; //Here command_OK is initialized
	gpio_set(ps2_data_pin_port, ps2_data_pin_id);
	gpio_clear(ps2_clock_pin_port, ps2_clock_pin_id);
	//if keyboard interrupt was not disabled, it would be interrupted here, pointing to something not coded
	//Something was wrong with original delay, so I decided to use TIM2 Capture/Compare interrupt
	// See hr_timer_delay.c file
	//now insert a 120us delay and run step 2 of send_start_bit function
	gpio_clear(BIT0_pin_port, BIT0_pin_id);	//debug (Start bit)
	delay_usec(120, SEND_ST_BIT_2); //wait 120usec and go to send_start_bit2 on TIM2 overflow interrupt
}


void send_start_bit2(void) //Second part of send_start_bit
{
	gpio_clear(ps2_data_pin_port, ps2_data_pin_id); //this is the start bit
	//now insert a 20us delay and run step 3 of send_start_bit function
	delay_usec(20, SEND_ST_BIT_3); /*wait 20usec and go to send_start_bit3 on TIM2 overflow interrupt*/
}


void send_start_bit3(void) //Third part of send_start_bit
{
	prev_systicks = systicks;
	//Rise clock edge starts the PS/2 device to receive command/argument
	gpio_set(ps2_clock_pin_port, ps2_clock_pin_id);

	ps2int_TX_bit_idx = 1;

	prepares_capture(TIM2);
}


/*************************************************************************************************/
/******************************  Support to other ISR's ******************************************/
/*************************************************************************************************/

/*  Enter point of PS/2 clock line, called from interrupt handled by msxhid  */
/*  Here is decided if the int is going to be treated as send or receive*/
void ps2_clock_update(bool ps2datapin_logicstate)
{
	if (!ps2datapin_logicstate && (ps2datapin_logicstate == formerps2datapin))
	{
		//State low is repeated
		acctimeps2data0 += time_between_ps2clk;
		if (acctimeps2data0 >= 200000) //.2s
		{
			gpio_set(ps2_data_pin_port, ps2_data_pin_id);
			ps2int_state = PS2INT_RECEIVE;
			ps2int_RX_bit_idx = 0;
		}
	}
	else
		acctimeps2data0 = 0;
	formerps2datapin = ps2datapin_logicstate;
	//uint8_t mountstring[16]; //Used in usart_send_string()
	/*Any keyboard interrupt that comes after 900 micro seconds means an error condition,
	but I`m considering it as an error for about 100 ms, to acommodate this to power on, to answer 
	to Read ID command. I observed this behavior on my own PS/2 keyboard. It is huge!*/
	if( ((ps2int_state == PS2INT_SEND_COMMAND) || (ps2int_state == PS2INT_SEND_ARGUMENT))
				&& ((systicks - prev_systicks) > 1*3) )
	{	//reset to PS/2 receive condition
		//User messages
		/*usart_send_string((uint8_t*)"ps2_clock_sent reseted - Timeout ");
		conv_uint32_to_8a_hex((systicks - prev_systicks), &mountstring[0]);
		usart_send_string((uint8_t*)&mountstring[0];
		usart_send_string((uint8_t*)"\n"); */
		ps2int_state = PS2INT_RECEIVE;
		ps2int_RX_bit_idx = 0;
	}
	
	if( (ps2int_state == PS2INT_SEND_COMMAND) || (ps2int_state == PS2INT_SEND_ARGUMENT) )
	{
		ps2_clock_send(ps2datapin_logicstate);
	}
	else
	{
		ps2_clock_receive(ps2datapin_logicstate);
	}
}


void ps2_clock_send(bool ps2datapin_logicstate)
{
	gpio_set(BIT0_pin_port, BIT0_pin_id);//Debug Start bit ended. Set BIT0
	//uint8_t mountstring[16]; //Used in usart_send_string()
	prev_systicks = systicks;
	//Time check - The same for all bits
	if (time_between_ps2clk > 10000) // time >10ms
	{
		usart_send_string((uint8_t*)"Time > 10ms on TX");
	}
	//|variável| = `if`(condição) ? <valor1 se true> : <valor2 se false>;:
	//Only two TX states of send: ps2_send_command & send_argument
	uint8_t data_byte = (ps2int_state == PS2INT_SEND_COMMAND) ? command : argument;
	if( (ps2int_TX_bit_idx>=1) && (ps2int_TX_bit_idx<9) )
	{
		bool bit = data_byte&(1 << (ps2int_TX_bit_idx - 1));
		//User messages
		/*usart_send_string((uint8_t*)"sent bit #");
		conv_uint8_to_2a_hex(ps2int_TX_bit_idx, &mountstring[0]);
		usart_send_string((uint8_t*)&mountstring[0];
		usart_send_string((uint8_t*)"as "); //This print continues below*/
		if(bit)
		{
			gpio_set(ps2_data_pin_port, ps2_data_pin_id);
			//User messages
			//usart_send_string((uint8_t*)"(1)\n");
		}
		else
		{
			gpio_clear(ps2_data_pin_port, ps2_data_pin_id);
			//User messages
			//usart_send_string((uint8_t*)"(0)\n");
		}
		ps2int_TX_bit_idx++;
	}
	else if(ps2int_TX_bit_idx == 9)
	{//parity
		bool parity =! __builtin_parity(data_byte);
		//User messages
		//usart_send_string((uint8_t*)"sent parity \n"); //This print continues below
		if(parity)
		{
			gpio_set(ps2_data_pin_port, ps2_data_pin_id);
			//User messages
			//usart_send_string((uint8_t*)"(1)\n");
		}
		else
		{
			gpio_clear(ps2_data_pin_port, ps2_data_pin_id);
			//User messages
			//usart_send_string((uint8_t*)"(0)\n");
		}
		ps2int_TX_bit_idx = 10;
	}
	else if(ps2int_TX_bit_idx == 10)
	{//stop  bit
		gpio_set(ps2_data_pin_port, ps2_data_pin_id);
		ps2int_TX_bit_idx = 11;
		//User messages
		//usart_send_string((uint8_t*)"sent stop\n");
	}
	else if(ps2int_TX_bit_idx >= 11)
	{
		if(ps2datapin_logicstate == false) // ACK bit ok
		{
			; // Ignore ACK
			//User messages
			/*usart_send_string((uint8_t*)"Data sent OK: 0x");
			conv_uint8_to_2a_hex(data_byte, &mountstring[0]);
			usart_send_string((uint8_t*)&mountstring[0];
			usart_send_string((uint8_t*)"\n");
			*/
		}
		else
		{
			gpio_set(ps2_data_pin_port, ps2_data_pin_id);
			//User messages
			/*usart_send_string((uint8_t*)"Error clock_updating 0x");
			conv_uint8_to_2a_hex(data_byte, &mountstring[0]);
			usart_send_string((uint8_t*)&mountstring[0];
			usart_send_string((uint8_t*)" to keyboard. NACK\n"); */
			ps2int_state = PS2INT_RECEIVE; //force to receive_status in absence of something better
			ps2int_RX_bit_idx = 0;
		}
		ps2int_TX_bit_idx = 0;	//Prepares for the next send

		if(ps2int_state == PS2INT_SEND_COMMAND)
		{
			//For me, after commmand sent, you have to wait the PS/2 Acknowlodge from the command,
			//but the original logic pointed to state=PS2INT_RECEIVE.
			ps2int_state = PS2INT_WAIT_FOR_COMMAND_ACK;
		}
		else if(ps2int_state == PS2INT_SEND_ARGUMENT)
		{
			ps2int_state = PS2INT_WAIT_FOR_ARGUMENT_ACK;
		}
		else
		{
			gpio_set(ps2_data_pin_port, ps2_data_pin_id);
			ps2int_state = PS2INT_RECEIVE;
			ps2int_RX_bit_idx =  0;
		}
	}
}


void ps2_clock_receive(bool ps2datapin_logicstate)
{
	static uint8_t data_word, stop_bit, parity_bit;
	//uint8_t mountstring[16]; //Used in usart_send_string()

	//Verify RX timeout, that is quite restricted, if compared to Send Command/Argument
	if ( (ps2int_RX_bit_idx != 0) && (time_between_ps2clk > 120) )  //because if RX_bit_idx == 0 will be the reset
	{	
		//usart_send_string((uint8_t*)"ps2_clock_receive - Timeout\n");
		ps2int_RX_bit_idx = 0;
	}
	prev_systicks = systicks;

	if(ps2int_RX_bit_idx == 0)
	{
		//Force this interface to put data line in Hi-Z to avoid unspected behavior in case of errors
		gpio_set(ps2_data_pin_port, ps2_data_pin_id);
		//gpio_set(ps2_clock_pin_port, ps2_clock_pin_id);
		data_word = 0;
		stop_bit = 0xff;
		parity_bit = 0xff;
		ps2int_RX_bit_idx = 1; //Points to the next bit
		bool start_bit = ps2datapin_logicstate;
		if(start_bit)
		{
			fail_count++;
  		ps2int_RX_bit_idx = 0; //reset
		}
	}
	else if( (ps2int_RX_bit_idx>0) && (ps2int_RX_bit_idx<9) ) // collect bits 1 to 8 (D0 to D7)
	{
		data_word |= (ps2datapin_logicstate << (ps2int_RX_bit_idx - 1));
		ps2int_RX_bit_idx++;
	}
	else if(ps2int_RX_bit_idx == 9)
	{
		parity_bit = ps2datapin_logicstate;
		ps2int_RX_bit_idx++;
	}
	else if(ps2int_RX_bit_idx == 10)
	{	 // start + 8 + stop + parity (but started with 0)
		ps2int_RX_bit_idx = 0;	//next (reset) PS/2 receive condition

		stop_bit = ps2datapin_logicstate;
		bool parity_ok = __builtin_parity((data_word<<1)|parity_bit);
		//User messages
		/*usart_send_string((uint8_t*)"RX Data: 0x");
		conv_uint8_to_2a_hex(data_byte, &mountstring[0]);
		usart_send_string((uint8_t*)&mountstring[0];
		usart_send_string((uint8_t*)", parity ");
		mountstring = parity_bit ? "1" : "0";
		usart_send_string((uint8_t*)&mountstring[0];
		usart_send_string((uint8_t*)", Stop ");
		mountstring = stop_bit ? "1" : "0";
		usart_send_string((uint8_t*)&mountstring[0];
		usart_send_string((uint8_t*)"\n"); */

		if(parity_ok && (stop_bit == 1) ) //start bit condition was already tested above
		{	/* ps2int_status receive procesing block (begin) */
			if (ps2int_state == PS2INT_RECEIVE)
			{
				//this put routine is new
				uint8_t i = ps2_recv_buff_put;
				uint8_t i_next = i + 1;
				i_next &= (uint8_t)(PS2_RECV_BUFFER_SIZE - 1);
				if (i_next != ps2_recv_buff_get)
				{
					ps2_recv_buffer[i] = data_word;
					ps2_recv_buff_put = i_next;
				}
			}

			else if (ps2int_state == PS2INT_WAIT_FOR_COMMAND_ACK)
			{
				if(data_word == KB_ACKNOWLEDGE) //0xFA is Acknowledge from PS/2 keyboard
				{
					if(argument == ARG_NO_ARG) //0xFF is an empty argument
					{
						//no argument: set to receive
						ps2int_state = PS2INT_RECEIVE;
						ps2int_RX_bit_idx = 0;//reset PS/2 receive condition
						command_ok = true;
					}
					else
					{
						//argument is not empty (!=ARG_NO_ARG). Send it
						ps2int_state = PS2INT_SEND_ARGUMENT;
						send_start_bit_in(150);
					}
				}
				else if(data_word == 0xfe) //0xFE is Resend
				{
					ps2int_state = PS2INT_SEND_COMMAND;
					send_start_bit_in(150); //Send BOTH command and argument
				}	//if(data_word==0xfe) //0xFE is Resend
				else
				{
					//User messages
					/*usart_send_string((uint8_t*)"Got unexpected command response: 0x");
					conv_uint8_to_2a_hex(data_word, &mountstring[0]);
					usart_send_string((uint8_t*)&mountstring[0];
					usart_send_string((uint8_t*)"\n"); */
					ps2int_state = PS2INT_RECEIVE;
					ps2int_RX_bit_idx = 0;//reset PS/2 receive condition
					fail_count++;
				}	//else if(data_word==0xfe) //0xFE is Resend
			}

			else if (ps2int_state == PS2INT_WAIT_FOR_ARGUMENT_ACK)
			{
				if(data_word == KB_ACKNOWLEDGE) //Acknowledge from PS/2 keyboard
				{
					//Acknowledge received => set to receive
					ps2int_state = PS2INT_RECEIVE;
					ps2int_RX_bit_idx = 0;//Prepares for the next PS/2 receive condition
					command_ok = true;
				}
				else if(data_word == 0xfe) //0xFE is Resend
				{
					ps2int_state = PS2INT_SEND_COMMAND; //Resend BOTH command AND argument
					send_start_bit_in(150);
				}
				else
				{
					ps2int_state = PS2INT_RECEIVE;
					ps2int_RX_bit_idx = 0;//reset PS/2 receive condition
					//User messages
					/*usart_send_string((uint8_t*)"Got unexpected command response: 0x");
					conv_uint8_to_2a_hex(data_word, &mountstring[0]);
					usart_send_string((uint8_t*)&mountstring[0];
					usart_send_string((uint8_t*)"\n"); */
				}
			}		/* ps2int_status receive procesing block (wnd) */
		}	//if(start_bit==0 && stop_bit==1 && parity_ok)
		else
		{
			//User messages
			/*usart_send_string((uint8_t*)"Framming Error. RX Data: 0x");
			conv_uint8_to_2a_hex(data_byte, &mountstring[0]);
			usart_send_string((uint8_t*)&mountstring[0];
			usart_send_string((uint8_t*)", parity ");
			mountstring = parity_bit ? "1" : "0";
			usart_send_string((uint8_t*)&mountstring[0];
			usart_send_string((uint8_t*)", Stop ");
			mountstring = stop_bit ? "1" : "0";
			usart_send_string((uint8_t*)&mountstring[0];
			usart_send_string((uint8_t*)"\n"); */
			fail_count++;
			ps2int_RX_bit_idx = 0;
		}
	}	//else if(ps2int_RX_bit_idx==10)
}


void ps2_send_command(uint8_t cmd, uint8_t argm)
{
	command =  cmd;
	argument = argm;
	command_ok = false;
	ps2int_state = PS2INT_SEND_COMMAND;
	send_start_bit();
}


void ps2_update_leds(bool num, bool caps, bool scroll)
{
	command = 0xED;
	argument = (scroll<<0)|(num<<1)|(caps<<2);
	command_ok = false;
	ps2int_state = PS2INT_SEND_COMMAND;
	send_start_bit();
}


void reset_mount_keycode_machine()
{
	mount_keycode_count_status = 0;
	ps2_keystr_e0 = false;
	ps2_keystr_e1 = false;
	ps2_keystr_f0 = false;
}


bool mount_keycode()
{
	// static uint16_t prev_state_index=0;
	if (!mount_keycode_OK)
	{	
		while(available_ps2_byte() && !mount_keycode_OK)
		{
			ps2_byte_received = get_ps2_byte(&ps2_recv_buffer[0]);
			//User messages
			/*usart_send_string((uint8_t*)"Mount_keycode RX Ch=");
			conv_uint8_to_2a_hex(ps2_byte_received, &mountstring[0]);
			usart_send_string((uint8_t*)&mountstring[0];
			usart_send_string((uint8_t*)"\n"); */
			switch (mount_keycode_count_status)
			{
			//if (mount_keycode_count_status == 0)  //Está lendo o primeiro byte do ps2_byte_received
			case 0:
			{
				if((ps2_byte_received > 0) && (ps2_byte_received < 0xE0)) //Se até 0xDF cai aqui
				{
					//Concluído. Keycode com apenas 1 byte.
					keycode[1] = ps2_byte_received;
					//Conclui scan
					keycode[0] = 1;
					mount_keycode_OK = true;
					reset_mount_keycode_machine();
					return true;
				}
				if(ps2_byte_received == 0xE0)
				{
					//0xE0 + (Any != 0xF0) são dois bytes
					//0xE0 + 0xF0 + (Any) <=  são três bytes
					ps2_keystr_e0 = true;
					keycode[1] = ps2_byte_received;
					keycode[0] = 1;
					mount_keycode_count_status = 1; //points to next case
					break;
				}
				if(ps2_byte_received == 0xE1)
				{
					// Pause/Break key: São 8 bytes (0xE1 + 7 bytes). Ler apenas os 3 iniciais e desprezar os demais
					ps2_keystr_e1 = true;
					keycode[1] = ps2_byte_received;
					keycode[0] = 1;
					mount_keycode_count_status = 1; //points to next case
					break;
				}
				if(ps2_byte_received == 0xF0)
				{
					//São sempre dois bytes 0xF0 + (Any)
					ps2_keystr_f0 = true;
					keycode[1] = ps2_byte_received;
					keycode[0] = 1;
					mount_keycode_count_status = 1; //points to next case
					break;
				}
 				break;	//case syntax suggested
			}	//case 0:
			//if (mount_keycode_count_status == 1)  //Está lendo o segundo byte do ps2_byte_received
			case 1:
			{
				if (ps2_keystr_e0 == true)
				{
					if(ps2_byte_received != 0xF0)
					{
						if(ps2_byte_received != 0x12)
						{
							//São 2 bytes e este ps2_byte_received é != 0xF0 e != 0x12, logo, terminou
							keycode[2] = ps2_byte_received;
							keycode[0] = 2;
							//Conclui scan
							mount_keycode_OK = true;
							reset_mount_keycode_machine();
							return true;
						}
						else
						{
							//Discard E0 12 here
							reset_mount_keycode_machine();
							break;
						}
					}
					else //if(ps2_byte_received == 0xF0)
					{
							//São 3 bytes, mas estou lendo o segundo byte (E0 F0)
							keycode[2] = ps2_byte_received;
							keycode[0] = 2;
							mount_keycode_count_status = 2; //points to next case
							break;
					}
				}
				if (ps2_keystr_e1)  //Break key (8 bytes)
				{
					//São 8 bytes (0xE1 + 7 bytes). Ler apenas os 3 iniciais e desconsiderar os demais. Estou lendo o segundo byte
					keycode[2] = ps2_byte_received;
					keycode[0] = 2;
					mount_keycode_count_status = 2; //points to next case
					break;
				}
				if (ps2_keystr_f0 == true)
				{
					//São 2 bytes, logo, terminou
					// Exception is the PrintScreen key: It will be returned as two 2 bytes ps2_byte_received presses:
					// E0 7C, but this key is not present on MSX.
					// If you want to map this key, fix it in the excel origin file.
					keycode[2] = ps2_byte_received;
					keycode[0] = 2;
					//Conclui scan
					mount_keycode_OK = true;
					reset_mount_keycode_machine();
					return true;
				}
 				break;	//case syntax suggested
			}	//case 1:
			case 2:
			//if (mount_keycode_count_status == 2)  //Está lendo o terceiro byte do ps2_byte_received
			{
				if (ps2_keystr_e0 == true)
				{
					//São 3 bytes, e estou lendo o terceiro byte, logo, terminou.
					// Exception is the PrintScreen break: It will be returned as one 3 bytes ps2_byte_received releases:
					// E0 F0 7C (and E0 F0 12 is dicarded), but this key is not present on MSX.
					// If you want to map this key, fix it in excel file and click on the black keyboard to rerun macro
					if(ps2_byte_received != 0x12)
					{
						keycode[3] = ps2_byte_received;
						keycode[0] = 3;
						//Conclui scan
						mount_keycode_OK = true;
						reset_mount_keycode_machine();
						return true;
					}
					else
					{
						//Discard E0 F0 12 here
						reset_mount_keycode_machine();
						break;
					}
				}
				else if (ps2_keystr_e1 == true)  //Break key (8 bytes)
				{
					//São 8 bytes (0xE1 + 7 bytes). Ler apenas os 3 iniciais e desprezar os demais.
					//Estou lendo o terceiro byte.
					keycode[3] = ps2_byte_received;
					keycode[0] = 3;
					mount_keycode_count_status = 3; //points to next case
					break;
				}  	
 				break;	//case syntax suggested
			}	//case 2:
			//if (mount_keycode_count_status == 3)  //Está lendo o quarto byte do ps2_byte_received Pause/Break
			case 3:
			{
				if (ps2_keystr_e1 == true)  //Break key (8 bytes)
				{
					//São 8 bytes (0xE1 + 7 bytes). Ler apenas os 3 iniciais e desprezar os demais. 
					//Estou lendo o quarto byte. Não o armazeno.
					mount_keycode_count_status = 4; //points to next case
					break;
				}	   	
 				break;	//case syntax suggested
			}	//case 3:
			//if (mount_keycode_count_status == 4)  //Está lendo o quinto byte do ps2_byte_received Pause/Break
			case 4:
			{
				if (ps2_keystr_e1 == true)  //Break key (8 bytes)
				{
					//São 8 bytes (0xE1 + 7 bytes). Ler apenas os 3 iniciais e desprezar os demais. 
					//Estou lendo o quinto byte. Não o armazeno.
					mount_keycode_count_status = 5; //points to next case
					break;
				}	   	
 				break;	//case syntax suggested
			}		//case 4:
			//if (mount_keycode_count_status == 5)  //Está lendo o sexto byte do ps2_byte_received Pause/Break
			case 5:
			{
				if (ps2_keystr_e1 == true)  //Break key (8 bytes)
				{
					//São 8 bytes (0xE1 + 7 bytes). Ler apenas os 3 iniciais e desprezar os demais. 
					//Estou lendo o sexto byte. Não o armazeno.
					mount_keycode_count_status = 6; //points to next case
					break;
				}	   	
 				break;	//case syntax suggested
			}	//case 5
			//if (mount_keycode_count_status == 6)  //Está lendo o sétimo byte do ps2_byte_received Pause/Break
			case 6:
			{
				if (ps2_keystr_e1 == true)  //Break key (8 bytes)
					{
						//São 8 bytes (0xE1 + 7 bytes). Ler apenas os 3 iniciais e desprezar os demais. 
						//Estou lendo o sétimo byte. Não o armazeno.
						mount_keycode_count_status = 7; //points to next case
						break;
					}	   	
 				break;	//case syntax suggested
			}	//case 6
			//if (mount_keycode_count_status == 7)
			case 7:
			  //Está lendo o oitavo byte do ps2_byte_received Pause/Break
			{
				//São 8 bytes (0xE1 + 7 bytes). Ler apenas os 3 iniciais e desprezar os demais. 
				//Estou lendo o oitavo byte e não o armazeno, logo, terminou
				//Conclui scan
				mount_keycode_OK = true;
				reset_mount_keycode_machine();
				return true;
			}
			default:
				break;
			}	//switch (mount_keycode_count_status)
		} //while((ps2_byte_received=get_ps2_byte(&ps2_recv_buffer[0]))!=0 && !mount_keycode_OK)
		return false;
	}//if (!mount_keycode_OK)
	return false;
}


void general_debug_setup(void)
{
	gpio_set_mode(SYSTICK_port, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, SYSTICK_pin_id);
	gpio_set(SYSTICK_port, SYSTICK_pin_id); //Default condition is "1"

	gpio_set_mode(BIT0_pin_port, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, BIT0_pin_id);
	gpio_set(BIT0_pin_port, BIT0_pin_id); //Default condition is "1"

	gpio_set_mode(INT_TIM2_port, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TIM2CC1_pin_id);
	gpio_set(INT_TIM2_port, TIM2CC1_pin_id); //Default condition is "1"

	gpio_set_mode(FLASH_RW_port, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TIM2UIF_pin_id); // PC3 (MSX 8255 Pin 17)
	gpio_set(FLASH_RW_port, TIM2UIF_pin_id); //Default condition is "1"
	
	gpio_set_mode(Dbg_Yint_port, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, Dbg_Yint2e3_pin_id); // PC2 e 3 (MSX 8255 Pin 17)
	gpio_set(Dbg_Yint_port, Dbg_Yint2e3_pin_id); //Default condition is "1"
	
	gpio_set_mode(Dbg_Yint_port, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, Dbg_Yint0e1_pin_id); // PC2 (MSX 8255 Pin 17)
	gpio_set(Dbg_Yint_port, Dbg_Yint0e1_pin_id); //Default condition is "1"
}


/*************************************************************************************************/
/*************************************************************************************************/
/******************************************* ISR's ***********************************************/
/*************************************************************************************************/
/*************************************************************************************************/
void exti4_isr(void) // PS/2 Clock
{
	//Now starts PS2Clk interrupt handler
	//Debug & performance measurement
	//gpio_clear(BIT0_port, BIT0_pin_id); //Signs start of interruption
	//This was the ISR of PS/2 clock pin. It jumps to ps2_clock_update.
	//It is an important ISR, but it does not require critical timming resources as MSX Y scan does.
	//if(exti_get_flag_status(ps2_clock_pin_exti))  // EXTI4
	//{
	//	bool ps2datapin_logicstate=gpio_get(ps2_data_pin_port, ps2_data_pin_id);
	//	ps2_clock_update(ps2datapin_logicstate);
	//	exti_reset_request(ps2_clock_pin_exti);
	//}
	//Debug & performance measurement
	//gpio_set(BIT0_port, BIT0_pin_id); //Signs end of interruption. Default condition is "1"
}
