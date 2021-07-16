#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <libopencm3/cm3/nvic.h>

struct ring
{
	uint8_t *data;
	uint16_t put_ptr;
	uint16_t get_ptr;
};


void serial_setup(void);

// Put a char (uint8_t) on serial buffer.
// It returns true if there was room to put this on USART1 TX buffer.
// It is a non blocking function
bool serial_put_char(uint8_t ch);

// If there is an available char in USART2 RX ring, it returns true.
// It is a non blocking function
bool serial_available_get_char(void);

// If there is an available char in serial, it returns with an uint8_t.
// It is a non blocking function
uint8_t serial_get_char(void);

// Send a ASCIIZ string to serial (up to 127 chars).
// It is a non blocking function if there is room on TX Buffer
void usart_send_string(uint8_t*);

/*Functions to convert strings*/
// Convert a two byte string pointed by i into a binary byte. 
uint8_t conv_2a_hex_to_uint8(uint8_t*, int16_t);

// Convert a word (32 bit binary) to into a 8 char string. 
void conv_uint32_to_8a_hex(uint32_t, uint8_t*);

// Convert a half-word (16 bit binary) to into a 4 char string. 
void conv_uint16_to_4a_hex(uint16_t, uint8_t*);

// Convert a byte (8 bit binary) to into a 2 char string. 
void conv_uint8_to_2a_hex(uint8_t value, uint8_t *outstring);

#ifdef __cplusplus
}
#endif
