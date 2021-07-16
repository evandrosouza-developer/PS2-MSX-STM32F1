/*
 * This file was part of the libopencm3 project, but highly modifyed.
 *
 * Copyright (C) 2013 Damian Miller <damian.m.miller@gmail.com>
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

#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/gpio.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "flash_intelhex.h"
#include "serial.h"


//Processor related sizes and adress:
#define FLASH_OPERATION_ADDRESS ((uint32_t)0x0800F400)	  //MSXTABLE in STM32F103C8T6 (Blue Pill)
#define FLASH_PAGE_NUM_MAX 63  														//STM32F103C8T6
#define FLASH_PAGE_SIZE 0x400															//1K in STM32F103C8T6

#define USART_ECHO_EN 1																		//All chars received in Intel Hex are echoed in serial routines
#define STRING_MOUNT_BUFFER_SIZE 128
#define FLASH_OPERATION_BUFFER_SIZE ((uint16_t)3*1024)		//in RAM
#define FLASH_WRONG_DATA_WRITTEN 0x80
#define RESULT_OK 0

//Prototype area:
/*usart operations*/
void usart_get_string_line(uint8_t*, uint16_t);
/*Intel Hex operations*/
void get_valid_intelhex_file(uint8_t*, uint16_t, uint8_t*);
void usart_get_intel_hex(uint8_t*, uint16_t, uint8_t*);
bool validate_intel_hex_record(uint8_t*, uint8_t*, uint8_t*, uint16_t*, uint8_t*);
/*flash operations*/
uint32_t flash_program_data(uint32_t, uint8_t*, uint16_t);
void flash_read_data(uint32_t, uint16_t, uint8_t*);

//Global var area:
bool error_intel_hex;
uint16_t flash_operation_max_size;
bool abort_intelhex_reception;


int flash_rw(void)	//was main. It is int to allow simulate as a single module
{
	uint32_t result = 0;
	uint8_t str_mount[STRING_MOUNT_BUFFER_SIZE];
	uint8_t flash_buffer_ram[FLASH_OPERATION_BUFFER_SIZE]; //Local variable, in aim to not consume resources in the main functional machine

	usart_send_string((uint8_t*)"Keyboard interface powered off.\r\n\nTo update the Conversion Table, please send the new file, in Intel Hex format!\r\nOr turn off now.");
	get_valid_intelhex_file(&str_mount[0], STRING_MOUNT_BUFFER_SIZE, &flash_buffer_ram[0]);
	
	result = flash_program_data(FLASH_OPERATION_ADDRESS, &flash_buffer_ram[0], FLASH_OPERATION_BUFFER_SIZE);

	switch(result)
	{
	case RESULT_OK: /*everything ok*/
		usart_send_string((uint8_t*)"\r\n\nVerification of written data...");
		//flash_read_data(FLASH_OPERATION_ADDRESS, FLASH_OPERATION_BUFFER_SIZE, flash_buffer_ram);
		// usart_send_string(flash_buffer_ram, FLASH_OPERATION_BUFFER_SIZE);
		usart_send_string((uint8_t*)"\r\nSuccessfull!\r\n\nNow, please TURN OFF to plug the PS/2 keyboard!");
		break;
	case FLASH_WRONG_DATA_WRITTEN: /*data read from Flash is different than written data*/
		usart_send_string((uint8_t*)"Wrong data written into flash memory\r\n");
		break;

	default: /*wrong flags' values in Flash Status Register (FLASH_SR)*/
		usart_send_string((uint8_t*)"\r\nWrong value of FLASH_SR: ");
		conv_uint32_to_8a_hex(result, str_mount);
		usart_send_string(&str_mount[0]);
		break;
	}
	/*send end_of_line*/
	usart_send_string((uint8_t*)"\r\n");
	return result;
}


void usart_get_string_line(uint8_t *ser_inp_line, uint16_t str_max_size)
{ //Reads until CR and sends as a ASCIIZ on *ser_inp_line
	uint8_t sign = 0;
	uint16_t iter = 0;

redohere:
	while(iter < str_max_size)
	{
		//wait until next char is available
		while (!serial_available_get_char())
			__asm("nop");
		sign = serial_get_char();

#if USART_ECHO_EN == 1
		if (sign == 3)
		{
			while (!serial_put_char('^'))
				__asm("nop");
			while (!serial_put_char('C'))
				__asm("nop");
		}
		else if (sign != '\r')  //if sign == '\r' do nothing
		{
			if (sign == '\n')
			{
				while (!serial_put_char('\r'))
					__asm("nop");
				while (!serial_put_char('\n'))
					__asm("nop");
			}
			else
				while (!serial_put_char(sign))
					__asm("nop");
		}
#endif

		if(sign == 3)
			abort_intelhex_reception = true;
		else
		{
			//only accepts char between '0' ~ ':', 'A' ~ 'F', 'a' ~ 'f'
			if( ( ((sign > ('/')) && (sign < (':'+1))) ||
					(((sign&0x5F) > ('A'-1)) && ((sign&0x5F) < ('G'))))	)
				//assembling the record
				ser_inp_line[iter++] = sign;
			else if( (iter > 10) && ((sign == '\n') || (sign == '\r')) )	//	'\r' or '\n'
			{	//End of record. Close the ASCIIZ.
				ser_inp_line[iter++] = 0;
				break;
			}
			else
			{	
				//The minimum valid record is 11 characters, so redo.
				iter = 0;
				goto redohere;
			}
		}
	} //while(iter < str_max_size)
	// Indicates that we received new line data, and it is going to be validated.
	gpio_toggle(GPIOC, GPIO13);	//Blinks a cycle each two lines
}


void get_valid_intelhex_file(uint8_t *ser_inp_line, uint16_t str_max_size, uint8_t *ram_buffer)
{
	error_intel_hex = true;
	abort_intelhex_reception = false;
	while (error_intel_hex == true)
	{
		usart_get_intel_hex(ser_inp_line, str_max_size, ram_buffer);
		if (error_intel_hex == true)
		{
			usart_send_string((uint8_t*)"\r\n\n\n\nERROR in Intel Hex. ERROR\r\n\nPlease resend the Intel Hex...");
		}
		else if (abort_intelhex_reception)
		{
			usart_send_string((uint8_t*)"\r\n\n\n\n!!!!!INTERRUPTED!!!!!\r\n\nPlease resend the Intel Hex...");
		}
		else
			return;
	}
}


void usart_get_intel_hex(uint8_t *ser_inp_line, uint16_t str_max_size, uint8_t *ram_buffer)
{
	uint8_t intel_hex_localreg_data[STRING_MOUNT_BUFFER_SIZE / 2], intel_hex_numofdatabytes, intel_hex_type;
	uint16_t i, count_rx_intelhex_bytes, intel_hex_address, first_data_address_intel_hex, shifting;
	bool seek_first_intel_hex_address_for_data = true;
	
	intel_hex_numofdatabytes = 0; //Force init of uint8_t here
	intel_hex_type = 0; //Force init of uint8_t here
	count_rx_intelhex_bytes = 0; //Force init of uint16_t here
	intel_hex_address = 0; //Force init of uint16_t here
	first_data_address_intel_hex = 0; //Force init of uint16_t here
	error_intel_hex = false;
	seek_first_intel_hex_address_for_data = true;

	//Init RAM Buffer
	for(i=0 ; i < (3*1024) ; i++)
	{
		ram_buffer[i] = (uint8_t)0xFF;
	}

	while (intel_hex_type != 1) //Run until intel_hex_type == 1: means end-of-file record
	{
		usart_get_string_line(ser_inp_line, str_max_size);
		if (!abort_intelhex_reception)
		{
			//Is it a valid Intel Hex record?
			if (validate_intel_hex_record(ser_inp_line, &intel_hex_numofdatabytes, &intel_hex_type,
					&intel_hex_address, &intel_hex_localreg_data[0]))
			{
				//":llaaaatt[dd...]cc"	tt is the field that represents the HEX record type, which may be one of the following:
				//											00 - data record
				//											01 - end-of-file record
				//											02 - extended segment address record
				//											04 - extended linear address record
				//											05 - start linear address record (MDK-ARM only)

				if (intel_hex_type == 0)  //00 - data record
				{
					if (seek_first_intel_hex_address_for_data)
					{
						first_data_address_intel_hex = intel_hex_address;
						seek_first_intel_hex_address_for_data = false;
					}
					//Now fill in RAM Buffer with data record
					for (i = 0; i < intel_hex_numofdatabytes; i++)
					{
						shifting = ((uint16_t)intel_hex_address - (uint16_t)first_data_address_intel_hex);
						ram_buffer[shifting + i] = intel_hex_localreg_data[i];
						count_rx_intelhex_bytes++;
					}
				}
				else if (intel_hex_type == 1) //01 - end-of-file record
				{
					return;
				}
				else if (intel_hex_type == 2)  //02 - extended segment address record
				{
					__asm("nop"); //not useful in this code
				}
				else if (intel_hex_type == 4) //04 - extended linear address record
				{
					if (intel_hex_numofdatabytes != 2) //intel_hex_numofdatabytes must be 2;
					{
						error_intel_hex = true;
					}
					if (intel_hex_localreg_data[0] != 0x08) //First data must be 0x08
					{
						error_intel_hex = true;
					}
					if (intel_hex_localreg_data[1] != 0x00) //Second data must be 0x00
					{
						error_intel_hex = true;
					}
				} //if (intel_hex_type = 4) //04 - extended linear address record
				else if (intel_hex_type == 5)  //05 - start linear address record (MDK-ARM only)
				{
					__asm("nop"); //not useful in this code
				}
			}
			else	//if (validate_intel_hex_record(ser_inp_line, intel_hex_numofdatabytes, intel_hex_type, intel_hex_address, intel_hex_localreg_data))
			{
				//Invalid record received
				usart_send_string((uint8_t*)" Error: Invalid");
				error_intel_hex = true;
			}
		} //if (validate_intel_hex_record(ser_inp_line, &intel_hex_numofdatabytes, &intel_hex_type, &intel_hex_address, &intel_hex_localreg_data[0]))
	} //while (intel_hex_type != 0) //Run until case 1: means end-of-file record
}


bool validate_intel_hex_record(uint8_t *ser_inp_line, uint8_t *intel_hex_numofdatabytes,
		 uint8_t *intel_hex_type, uint16_t *intel_hex_address, uint8_t *intel_hex_localreg_data)
{
	uint8_t checksum_read, checksum_calc;
	int16_t i;

	//Record Intel Hex ":llaaaatt[dd...]cc"

	//":llaaaatt[dd...]cc" : is the colon that starts every Intel HEX record
	if (ser_inp_line[0] != ':')
	{
		//Error: Not started with ":"
		error_intel_hex = true;
		return false;
	}
	
	//":llaaaatt[dd...]cc" ll is the record-length field that represents the number of data bytes (dd) in the record
	*intel_hex_numofdatabytes = conv_2a_hex_to_uint8(ser_inp_line, 1);

	//":llaaaatt[dd...]cc" aaaa is the address field that represents the starting address for subsequent data in the record
	*intel_hex_address = (conv_2a_hex_to_uint8(ser_inp_line, 3) << 8) + (conv_2a_hex_to_uint8(ser_inp_line, 5));
	
	//":llaaaatt[dd...]cc"	tt is the field that represents the HEX record type, which may be one of the following:
	//											00 - data record
	//											01 - end-of-file record
	//											02 - extended segment address record
	//											04 - extended linear address record
	//											05 - start linear address record (MDK-ARM only)
	*intel_hex_type = conv_2a_hex_to_uint8(ser_inp_line, 7);
	
	//":llaaaatt[dd...]cc"	dd is a data field that represents one byte of data. A record may have multiple data bytes. The number of data bytes in the record must match the number specified by the ll field
	if (*intel_hex_numofdatabytes > 0)
	{
		for(i = 0; i < *intel_hex_numofdatabytes; i ++) //2 ASCII bytes per each byte of data in the record
		{
			intel_hex_localreg_data[i] = conv_2a_hex_to_uint8(ser_inp_line, 2 * i + 9);
		}
	}

	//":llaaaatt[dd...]cc"	cc is the checksum field that represents the checksum of the record. The checksum is calculated by summing the values of all hexadecimal digit pairs in the record modulo 256 and taking the two's complement
	checksum_read = conv_2a_hex_to_uint8(ser_inp_line, 2 * (*intel_hex_numofdatabytes) + 9);
	//Now compute Checksum
	checksum_calc = *intel_hex_numofdatabytes; // Checksum init
	for(i = 0; i < *intel_hex_numofdatabytes; i++)
		checksum_calc += intel_hex_localreg_data[i];
	checksum_calc += *intel_hex_type;
	checksum_calc += (uint8_t)((*intel_hex_address & 0xFF00) >> 8) + (uint8_t)(*intel_hex_address & 0x00FF);
	checksum_calc += checksum_read;
	//And check the computed Checksum
	if (checksum_calc != 0)
		error_intel_hex = true;
	return !error_intel_hex;
}


uint32_t flash_program_data(uint32_t start_address, uint8_t *fl_buffer_ram, uint16_t num_elements)
{
	uint16_t iter;
	uint32_t current_address = start_address;
	uint32_t page_address = start_address;
	uint32_t flash_status = 0;
	uint8_t mountstr[80];

	/*check if start_address is in proper range*/
	if((start_address - FLASH_BASE) >= (FLASH_PAGE_SIZE * (FLASH_PAGE_NUM_MAX+1)))
		return 1;

	/*calculate current page address*/
	if(start_address % FLASH_PAGE_SIZE)
		page_address -= (start_address % FLASH_PAGE_SIZE);
		
	flash_unlock();
	flash_wait_for_last_operation();

	usart_send_string((uint8_t*)"\r\n\nErasing flash memory...\r\n\nBase Address  Size  Status\r\n");

	/*Erasing 3 pages*/
	for(iter=0 ; iter < 3 ; iter++)
	{
		//Information to user
		conv_uint32_to_8a_hex((page_address + iter * FLASH_PAGE_SIZE), &mountstr[0]);
		usart_send_string((uint8_t*)" 0x");
		usart_send_string(&mountstr[0]);
		conv_uint16_to_4a_hex((uint16_t)FLASH_PAGE_SIZE, &mountstr[0]);
		usart_send_string((uint8_t*)"  0x");
		usart_send_string(&mountstr[0]);

		flash_erase_page(page_address + iter * FLASH_PAGE_SIZE);
		flash_wait_for_last_operation();
		flash_status = flash_get_status_flags();
		if(flash_status != FLASH_SR_EOP)
			return flash_status;
		
		//Information to user - continued
		usart_send_string((uint8_t*)"  Done\r\n");
	}
	
	/*programming flash memory*/
	for(iter=0; iter<num_elements; iter += 4)
	{
		/*programming word data*/
		flash_program_word(current_address+iter, *((uint32_t*)(fl_buffer_ram + iter)));
		flash_wait_for_last_operation();
		flash_status = flash_get_status_flags();
		if(flash_status != FLASH_SR_EOP)
			return flash_status;

		/*verify if correct data is programmed*/
		if(*((uint32_t*)(current_address+iter)) != *((uint32_t*)(fl_buffer_ram + iter)))
			return FLASH_WRONG_DATA_WRITTEN;
	}
	return 0;
}


void flash_read_data(uint32_t start_address, uint16_t num_elements, uint8_t *output_data)
{
	uint16_t iter;
	uint32_t *memory_ptr= (uint32_t*)start_address;

	for(iter=0; iter<num_elements/4; iter++)
	{
		*(uint32_t*)output_data = *(memory_ptr + iter);
		output_data += 4;
	}
}
