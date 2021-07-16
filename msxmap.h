#ifndef msxmap_h
#define msxmap_h

#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/exti.h>

//Use Tab width=2


#define X7_SET_AND   0xFDFFFFFF
#define X6_SET_AND   0xFEFFFFFF
#define X5_SET_AND   0xFBFFFFFF
#define X4_SET_AND   0x7FFFFFFF
#define X3_SET_AND   0xF7FFFFFF
#define X2_SET_AND   0xBFFFFFFF
#define X1_SET_AND   0xDFFFFFFF
#define X0_SET_AND   0xEFFFFFFF
#define X7_CLEAR_OR  0x02000000
#define X6_CLEAR_OR  0x01000000
#define X5_CLEAR_OR  0x04000000
#define X4_CLEAR_OR  0x80000000
#define X3_CLEAR_OR  0x08000000
#define X2_CLEAR_OR  0x40000000
#define X1_CLEAR_OR  0x20000000
#define X0_CLEAR_OR  0x10000000
#define X7_CLEAR_AND 0xFFFFFDFF
#define X6_CLEAR_AND 0xFFFFFEFF
#define X5_CLEAR_AND 0xFFFFFBFF
#define X4_CLEAR_AND 0xFFFF7FFF
#define X3_CLEAR_AND 0xFFFFF7FF
#define X2_CLEAR_AND 0xFFFFBFFF
#define X1_CLEAR_AND 0xFFFFDFFF
#define X0_CLEAR_AND 0xFFFFEFFF
#define X7_SET_OR    0x00000200
#define X6_SET_OR    0x00000100
#define X5_SET_OR    0x00000400
#define X4_SET_OR    0x00008000
#define X3_SET_OR    0x00000800
#define X2_SET_OR    0x00004000
#define X1_SET_OR    0x00002000
#define X0_SET_OR    0x00001000


class msxmap
{
private:


public:
	void msx_interface_setup(void);
	void msxqueuekeys(void);
	void convert2msx(void);
	void msx_dispatch(volatile uint16_t linhavarrida, volatile bool shiftstate);
	bool available_msx_disp_keys_queue_buffer(void);
	uint8_t put_msx_disp_keys_queue_buffer(uint8_t);
	uint8_t get_msx_disp_keys_queue_buffer(void);
	void compute_x_bits_and_check_interrupt_stuck ( 
					volatile uint8_t y_local, volatile uint8_t x_local, volatile bool x_local_setb);
};


#endif
