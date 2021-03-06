# STM32: PS/2 to USB Converter

This code facilitates a STM32 chip to convert between PS/2 and MSX computer. It is meant to connect a keyboard, which only provides a PS/2 connector, to a MSX (or any one that have a up to 15 active columns and reads zeroes througth 8 bits - up to 15 x 8 matrix) computer.
This code powers and has been tested on the following Windows keyboards with brazilian 275 layout:
- Compaq RT235BTWBR;
- Clone KE11090749;
- Clone #09100.

The original code was originally developed based on:
- Cherry G80-3000LSMDE;

## Dependencies

- `arm-none-eabi-gcc`
- `arm-none-eabi-gdb`
- `arm-none-eabi-binutils`
- `arm-none-eabi-newlib`
- `stlink`
- `openocd (if you wnat to debug)`
- `mpfr`

## Preparations

After cloning the repository you need to make the following preparations:

```
git submodule init
git submodule update
cd libopencm3
make
cd ..
make
```

## Hardware and Setup

You will obviously need a STM32F103C8T6 chip. I have used a chinese blue pill. The software was made thinking in use of compatible processors, like GD32 for example. The software was made considering 8.000Mhz oscillator crystal, to clock the STM32F103C8T6 microcontroller chip at 72MHz. The connections are:

1) PS/2 Keyboard:
- Clock Pin PA12 - Connect to PS/2 mini-din 45322 pin 5 throught R16;
- Data Pin PA9 - Connect to PS/2 mini-din pin 1 throught R18;
- +5V PS/2 power - Connect collector of Q2 to mini-din pin 4;
- GND - Connect GNDD (Digital Ground) to mini-din pin 3.

2) MSX computer:
Obs.: You have to access PPI Ports B0 to B7 (Lines X0 to X7), Port C0 to C3 (Y0 to Y3), Port C6 (Caps Lock, pin 11 of DIP package), and for Russian and Japanese Computers (Cyrillic and Kana alphabets), you have to get access to YM2149 IOB7, pin 6 of DIP package.
- Y3 - Connect to MSX PPI 8255 Signal PC3;
- Y2 - Connect to MSX PPI 8255 Signal PC2;
- Y1 - Connect to MSX PPI 8255 Signal PC1;
- Y0 - Connect to MSX PPI 8255 Signal PC0;
- X7 - Connect to MSX PPI 8255 Signal PB7 (Hotbit HB-8000 CI-15 Pin 25);
- X6 - Connect to MSX PPI 8255 Signal PB6 (Hotbit HB-8000 CI-15 Pin 24);
- X5 - Connect to MSX PPI 8255 Signal PB5 (Hotbit HB-8000 CI-15 Pin 23);
- X4 - Connect to MSX PPI 8255 Signal PB4 (Hotbit HB-8000 CI-15 Pin 22);
- X3 - Connect to MSX PPI 8255 Signal PB3 (Hotbit HB-8000 CI-15 Pin 21);
- X2 - Connect to MSX PPI 8255 Signal PB2 (Hotbit HB-8000 CI-15 Pin 20);
- X1 - Connect to MSX PPI 8255 Signal PB1 (Hotbit HB-8000 CI-15 Pin 19);
- X0 - Connect to MSX PPI 8255 Signal PB0 (Hotbit HB-8000 CI-15 Pin 18);
- Caps LED - Connect to MSX PPI 8255 Signal PC6 (Hotbit HB-8000 CI-15 Pin 11 / Expert XP-800 CI-4);
- Kana LED - Connect to MSX YM2149 IOB7, pin 6 of DIP package. If the MSX doesn't have this, you can leave it open, as it already has an internal pull-up connection.

3) Serial connection: Needed only to update internal PS/2 to MSX key mapping Database. To create this Intel Hex file, better to use the Macro based Excel file, so you have to trust and enable macro excecution in excel app.
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
	
	
	Each column has a MSX coded key, with the sollwing structure:
	(Bit 7:4) MSX Y PPI 8255 PC3:0 is send to an OC outputs BCD decoder, for example:
					 In the case of Hotbit HB8000, the keyboard scan is done as a 9 columns scan, CI-16 7445N 08 to 00;
					 If equals to 1111 (Y=15), there is no MSX key mapped.
	(Bit 3)	 		 0: keypress
					 1: key release;
	(Bit 2:0) MSX X, ie, which bit will carry the key, to be read by PPI 8255 PB7:0.
	

Use a ST-Link v2 Programmer (or similar) to flash the program using `make flash` onto the STM32.

# PS2-MSX-STM32F1
# PS2-MSX-STM32F1
