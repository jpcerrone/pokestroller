#pragma once
#include <stdint.h>
#include <stdbool.h>

#define SYSTEM_CLOCK_CYCLES_PER_SECOND 3686400 /* 3.6864 MHz */
//#define SYSTEM_CLOCK_CYCLES_PER_SECOND 1843200/* 3.6864 MHz */
#define SUB_CLOCK_CYCLES_PER_SECOND 32768 /* 32.768 KHz */

#define STARTING_WATTS 50

static const size_t MEM_SIZE = 64 * 1024;
static const size_t EEPROM_SIZE = 64 * 1024;

static bool sleep;

#define ENTER (1<<0)
#define LEFT (1<<2)
#define RIGHT (1<<4)

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
#define EEPROM_PIN 0x4

enum EEPROM_STATES{
	EEPROM_EMPTY,
	EEPROM_GETTING_STATUS_REGISTER,
	EEPROM_GETTING_ADDRESS_HI,
	EEPROM_GETTING_ADDRESS_LO,
	EEPROM_GETTING_BYTES
};

struct Eeprom_t{
	uint8_t* memory;
	uint8_t status;
	struct buffer_t{
		uint8_t hiAddress;
		uint8_t loAddress;
		enum EEPROM_STATES state;
		uint16_t offset;
	} buffer;
};
// --

// -- Accelerometer
#define ACCEL_PIN 0x1
enum ACCEL_STATES{
	ACCEL_GETTING_ADDRESS,
	ACCEL_GETTING_BYTES,
};

struct Accelerometer_t{
	uint8_t* memory;
	struct accelBuffer_t{
		uint8_t address;
		uint8_t offset;
		enum ACCEL_STATES state;
	} buffer;
};

// -- LCD
#define LCD_PIN 0x1
#define LCD_DATA_PIN 0x2
#define LCD_WIDTH 96
#define LCD_BUFFER_SEPARATION 16
#define LCD_BYTES_PER_STRIPE 2
#define LCD_HEIGHT 64
#define LCD_MEM_WIDTH 128 // TODO: figure out way to traverse the memory without needing to simulate the bigger RAM size
#define LCD_MEM_HEIGHT 176
static const int LCD_MEM_SIZE = LCD_MEM_WIDTH * LCD_MEM_HEIGHT / 4; // /4 cuz in 2B we get 8px ( 4px per byte)
enum LCD_STATES{
	LCD_EMPTY,
	LCD_READING_CONTRAST,
};
struct Lcd_t{
	bool powerSave;
	bool displayOn;
	uint8_t* memory;
	uint8_t contrast;
	enum LCD_STATES state;
	uint8_t currentColumn;
	uint8_t currentPage;
	uint8_t currentByte;
	bool currentBuffer;

};

static bool shouldRedrawScreen;
void initWalker();
int runNextInstruction(uint64_t* cycleCount);
void fillVideoBuffer(uint32_t* videoBuffer);
void setKeys(uint8_t);
void setRTCQuarterBit();
void wake();
