#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SYSTEM_CLOCK_CYCLES_PER_SECOND 3686400 /* 3.6864 MHz */
#define SUB_CLOCK_CYCLES_PER_SECOND 32768 /* 32.768 KHz */

#define ENTER (1<<0)
#define LEFT (1<<2)
#define RIGHT (1<<4)

#define LCD_WIDTH 96
#define LCD_HEIGHT 64

void initWalker(); // Must be called once before the main loop
int runNextInstruction(uint64_t* cycleCount); // Must be called once every main loop iteration and given a cycleCount variable defined globally
void fillVideoBuffer(uint32_t* videoBuffer);
void setKeys(uint8_t input); // Must be called every time a key is pressed down. 'input' should be one of ENTER, LEFT or RIGHT
void quarterRTCInterrupt();// Must be called once every quarter second
