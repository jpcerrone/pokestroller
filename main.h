#pragma once
#include <stdint.h>

struct RegRef8{
	 int idx;
	 char loOrHiReg;
	 uint8_t* ptr;
};

struct RegRef16{
	 int idx;
	 char loOrHiReg;
	 uint16_t* ptr;
};

struct RegRef32{
	 int idx;
	 uint32_t* ptr;
};


// -- EEPROM
const static int EEPROM_PAGE_SIZE = 128;

enum EEPROM_STATES{
	EEPROM_EMPTY,
	EEPROM_READING_HI,
	EEPROM_READING_LO,
	EEPROM_WRITING_BYTES
};

struct Eeprom_t{
	uint8_t* memory;
	uint8_t status;
	struct buffer_t{
		uint8_t hiAddress;
		uint8_t loAddress;
		enum EEPROM_STATES state;
		uint8_t offset;
	} buffer;
};
// --

// -- Accelerometer

