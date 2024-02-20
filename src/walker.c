#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>
#include <string.h>
#include <stdarg.h>

#include <assert.h>

#include "walker.h"
#include "utils.c"
/*
enum Mode{
	STEP,
	RUN
};

static enum Mode mode;
*/

static uint64_t subClockCyclesEllapsed;

uint8_t clearBit8(uint8_t operand, int bit){
	return operand & ~(1 << bit);			
}

static int PORT1 = 0xFFD4;
static int PORT9 = 0xFFDC;

// Timers
#define TMB_AUTORELOAD (1<<7)
#define TMB_COUNTING (1<<6)
struct TimerB_t{
	bool on;
	uint8_t TLBvalue;
	uint8_t* TMB1;
	uint8_t* TCB1;
};
static struct TimerB_t TimerB;
#define VECTOR_TIMER_B1 0x06fa
#define VECTOR_TIMER_W 0x3a4a

#define CTS (1<<7)
#define CCLR (1<<7)
struct TimerW_t{
	bool on;
	uint8_t* TMRW;
	uint8_t* TCRW;
	uint8_t* TIERW;
	uint8_t* TSRW;
	uint8_t* TIOR0;
	uint8_t* TIOR1;
	uint16_t* TCNT;
	uint16_t* GRA;
	uint16_t* GRB;
	uint16_t* GRC;
	uint16_t* GRD;
};

static struct TimerW_t TimerW;

static uint8_t* CKSTPR1; // Clock halt register 1
#define TB1CKSTP (1<<2) /* Timer B1 Module Standby */

static uint8_t* CKSTPR2; // Clock halt register 2
#define TWCKSTP (1<<6) /* Timer W Module Standby */

// CCR Condition Code Register
// I UI H U N Z V C
struct Flags{
	bool I; // Interrupt mask bit
	bool UI; // User bit
	bool H; // Half carry flag
	bool U; // User bit
	bool N; // Negative flag
	bool Z; // Zero flag
	bool V; // Overflow flag
	bool C; // Carry flag
};	
static struct Flags flags;

// Interrupts
static uint8_t* IRQ_IENR2; // Interrupt enable register 2
#define IENTB1 (1<<2) /* Timer B1 Interrupt Request Enable */
static uint8_t* IRQ_IRR2; // Interrupt flag register 2
#define IRRTB1 (1<<2) /* Timer B1 Interrupt Request Flag */
static uint16_t interruptSavedAddress;
static struct Flags interruptSavedFlags;

// General purpose registers
static uint32_t* ER[8];
static uint16_t* R[8];
static uint16_t* E[8];
static uint8_t* RL[8];
static uint8_t* RH[8];

static uint32_t* SP;

void setFlags(uint8_t value){
	flags.C = value & (1<<0);
	flags.V = value & (1<<1);
	flags.Z = value & (1<<2);
	flags.N = value & (1<<3);
	flags.U = value & (1<<4);
	flags.H = value & (1<<5);
	flags.UI = value & (1<<6);
	flags.I = value & (1<<7);
}

static uint8_t* memory;

static struct Accelerometer_t accel;
static struct Eeprom_t eeprom;
static struct Lcd_t lcd;

struct RegRef8 getRegRef8(uint8_t operand){
	struct RegRef8 newRef;
	newRef.idx = operand & 0b0111;
	newRef.loOrHiReg = (operand & 0b1000) ? 'l' : 'h';
	newRef.ptr = (newRef.loOrHiReg == 'l') ? RL[newRef.idx] : RH[newRef.idx];
	return newRef;
}

struct RegRef16 getRegRef16(uint8_t operand){
	struct RegRef16 newRef;
	newRef.idx = operand & 0b0111;
	newRef.loOrHiReg = (operand & 0b1000) ? 'e' : 'r';
	newRef.ptr = (newRef.loOrHiReg == 'r') ? R[newRef.idx] : E[newRef.idx];
	return newRef;
}

struct RegRef32 getRegRef32(uint8_t operand){
	struct RegRef32 newRef;
	newRef.idx = operand & 0b0111;
	newRef.ptr = ER[newRef.idx];
	return newRef;
}

void printRegistersState(){
#ifdef PRINT_STATE
	for(int i=0; i < 8; i++){
		printf("ER%d: [0x%08X], ", i, *ER[i]); 
	}
	printf("\n");
	printf("I: %d, H: %d, N: %d, Z: %d, V: %d, C: %d ", flags.I, flags.H, flags.N, flags.Z, flags.V, flags.C);
	printf("\n\n");
#endif

}

void printMemory(uint32_t address, int byteCount){
#ifdef PRINT_STATE
	address = address & 0x0000ffff; // Keep lower 16 bits only
	for(int i = 0; i < byteCount; i++){ 
		printf("MEMORY - 0x%04x -> %02x\n", address + i, memory[address + i]);
	}
#endif
}

void printInstruction(const char* format, ...){
#ifdef PRINT_STATE
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
#endif
}

#define GRAY_0 0x00333333
#define GRAY_1 0x00666666
#define GRAY_2 0x00999999
#define GRAY_3 0x00CCCCCC
const static uint32_t palette[4] = {GRAY_3, GRAY_2, GRAY_1, GRAY_0};

void fillVideoBuffer(uint32_t* videoBuffer){
	//FILE* lcdFile;
	//fopen_s(&lcdFile, "lcdDump.bin","wb");
	//fwrite(lcd.memory, 1, LCD_MEM_SIZE, lcdFile);
	for(int y = 0; y < LCD_HEIGHT; y++){
		for(int x = 0; x < LCD_WIDTH; x++){
			int yOffsetStripe = y%8;
			uint8_t firstByteForX = (lcd.memory[2*x + (y/8)*LCD_WIDTH*LCD_BYTES_PER_STRIPE] & (1<<yOffsetStripe)) >> yOffsetStripe;
			uint8_t secondByteForX = (lcd.memory[2*x + (y/8)*LCD_WIDTH*LCD_BYTES_PER_STRIPE + 1] & (1<<yOffsetStripe)) >> yOffsetStripe;
			int paletteIdx = firstByteForX + secondByteForX;
			uint32_t color = palette[paletteIdx];
			videoBuffer[y*LCD_WIDTH + x] = color;
		}
	}
	//fclose(lcdFile);

}
// With masking here we're ignoring the 0x00XX0000 part of the address for this emulator, as we have one big memory block that goes up to 0xFFFF
void setMemory8(uint32_t address, uint8_t value){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	memory[address] = value; 
}

void setMemory16(uint32_t address, uint16_t value){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	memory[address] = value >> 8; 
	memory[address + 1] = value & 0xFF; 
}

void setMemory32(uint32_t address, uint32_t value){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	memory[address] = value >> 24; 
	memory[address + 1] = (value >> 16) & 0xFF; 
	memory[address + 2] = (value >> 8) & 0xFF; 
	memory[address + 3] = value & 0xFF; 
}

uint16_t getMemory8(uint32_t address){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	return (uint8_t)(memory[address]);
}

uint16_t getMemory16(uint32_t address){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	return (uint16_t)((memory[address] << 8) | (memory[address + 1]));
}

uint32_t getMemory32(uint32_t address){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	return (uint32_t)((memory[address] << 24) | (memory[address + 1] << 16) | (memory[address + 2] << 8) | memory[address + 3]);
}

// Note: I considered using signed parameters here, but they get sign extended and screw up the carry calculations.
void setFlagsADD(uint32_t value1, uint32_t value2, int numberOfBits){
	// TODO: might be greately simplified, see setFlagsINC
	uint32_t maxValue;
	uint32_t maxValueLo;
	uint32_t negativeFlag;
	uint32_t halfCarryFlag;
	switch(numberOfBits){
		case 8:{
			maxValue = 0xFF;
			maxValueLo = 0xF;
			negativeFlag = 0x80;
			halfCarryFlag = 0x8;

			// TODO: maybe we can just cast to a signed int here and not have to use the flags
			flags.Z = (uint8_t)(value1 + value2) == 0x0;  
			flags.N = (uint8_t)(value1 + value2) & negativeFlag;  

		}break;
		case 16:{
			maxValue = 0xFFFF;
			maxValueLo = 0xFF;
			negativeFlag = 0x8000;
			halfCarryFlag = 0x100;

			flags.Z = (uint16_t)(value1 + value2) == 0x0;  
			flags.N = (uint16_t)(value1 + value2) & negativeFlag;  

		}break;
		case 32:{
			maxValue = 0xFFFFFFFF;
			maxValueLo = 0xFFFF;
			negativeFlag = 0x80000000;
			halfCarryFlag = 0x10000;

			flags.Z = (uint32_t)(value1 + value2) == 0x0;  
			flags.N = (uint32_t)(value1 + value2) & negativeFlag;  


		}break;
	}

	flags.V = ~(value1 ^ value2) & ((value1 + value2) ^ value1) & negativeFlag; // If both operands have the same sign and the results is from a different sign, overflow has occured.
	flags.C = (value1 & negativeFlag) && !(value2 & negativeFlag) && !((value1 + value2) & negativeFlag);
	flags.H = (((value1 & maxValueLo) + (value2 & maxValueLo) & halfCarryFlag) == halfCarryFlag) ? 1 : 0; 
}

void setFlagsSUB(uint32_t value1, uint32_t value2, int numberOfBits){
	// TODO: might be greately simplified, see setFlagsINC
	uint32_t maxValueLo;
	uint32_t negativeFlag;
	uint32_t halfCarryFlag;
	switch(numberOfBits){
		case 8:{
			maxValueLo = 0xF;
			negativeFlag = 0x80;
			halfCarryFlag = 0x8;

			// TODO: maybe we can just cast to a signed int here and not have to use the flags
			flags.N = (uint8_t)(value1 - value2) & negativeFlag;  

		}break;
		case 16:{
			maxValueLo = 0xFF;
			negativeFlag = 0x8000;
			halfCarryFlag = 0x100;

			flags.N = (uint16_t)(value1 - value2) & negativeFlag;  

		}break;
		case 32:{
			maxValueLo = 0xFFFF;
			negativeFlag = 0x80000000;
			halfCarryFlag = 0x10000;

			flags.N = (uint32_t)(value1 - value2) & negativeFlag;  


		}break;
	}

	flags.Z = (value1 - value2) == 0x0;  
	flags.V = ((value1 ^ value2) & negativeFlag) && (~((value1 - value2) ^ value2) & negativeFlag); // If both operands have a different sign and the results is from the same sing as the 2nd op, overflow has occured.
	flags.C = value2 > value1;
	flags.H = (value2 & maxValueLo) > (value1 & maxValueLo); 
}

void setFlagsINC(uint32_t value1, uint32_t value2, int numberOfBits){
	uint32_t negativeFlag = (1 << (numberOfBits-1));
	flags.N = (value1 + value2) & negativeFlag;  
	flags.Z = ((value1 + value2) == 0) ? true : false;
	flags.V = ~(value1 ^ value2) & ((value1 + value2) ^ value1) & negativeFlag; // If both operands have the same sign and the results is from a different sign, overflow has occured.
}

void setFlagsMOV(uint32_t value, int numberOfBits){
	flags.V = 0;
	flags.Z = (value == 0x0);
	switch(numberOfBits){
		case 8:{
			flags.N = value & 0x80;
		}break;
		case 16:{
			flags.N = value & 0x8000;
		}break;
		case 32:{
			flags.N = value & 0x80000000;
		}break;
	}
}

struct SSU_t{
	uint8_t* SSCRH; // Control register H
	uint8_t* SSCRL; // Control register L
	uint8_t* SSMR; // Mode register
	uint8_t* SSER; // Enable register
	uint8_t* SSSR; // Status register
	uint8_t* SSRDR; // Recieve data register
	uint8_t* SSTDR; // Transmit data register.
	uint8_t SSTRSR; // Shift register.
};
static struct SSU_t SSU;

static const uint8_t RDRF = (1 << 1); // Recieve Data Register Full
static const uint8_t TDRE = (1 << 2); // Transmit Data Register Empty
static const uint8_t TEND = (1 << 3); // Transmit End
static const uint8_t TE = 0x80; // Transmission Enabled
static const uint8_t RE = 0x40; // Reception Enabled

static uint16_t pc;
static int instructionsToStep;


void setKeys(bool enter, bool left, bool right){
	uint8_t enterDown = enter ? (1<<0) : 0;
	uint8_t leftDown = left ? (1<<2) : 0;
	uint8_t rightDown = right ? (1<<4) : 0;
	uint8_t value = enterDown | leftDown | rightDown;
	setMemory8(0xffde, value);
}

void runSubClock(){
	// Timer handling
	if (TimerB.on && ((subClockCyclesEllapsed % 256) == 0)){ // TODO(custom ROMs): parameterize frequency
		if(++(*TimerB.TCB1) == 0){
			*IRQ_IRR2 |= IRRTB1;
			*TimerB.TCB1 = TimerB.TLBvalue;
		}
	}
	if (TimerW.on){ // Count every subclock
		*TimerW.TCNT += 1;
		if ((*TimerW.TCNT == *TimerW.GRA) && (*TimerW.TCRW & CCLR)){
			*TimerW.TCNT = 0;
			*TimerW.TSRW |= 0x1; // IMFA
			if (*TimerW.TIERW & 0x1){ // IMIEA - Interrupt enabled A
				if (!flags.I){
					interruptSavedAddress = pc;
					interruptSavedFlags = flags;
					flags.I = true;
					pc = VECTOR_TIMER_W;
				}
			}
		}
	}

	// have to do this so that we don't count the instructions from the first cycle the timer is on
	// since it should count in parallell to CPU execution
	TimerB.on = (*CKSTPR1 & TB1CKSTP) && (*TimerB.TMB1 & TMB_COUNTING);
	TimerW.on = (*CKSTPR2 & TWCKSTP) && (*TimerW.TMRW & CTS);
}

int runNextInstruction(bool* redrawScreen, uint64_t* cycleCount){
	// Skip certain instructions
	if (pc == 0x336){ // Factory Tests
		pc += 4;
		printInstruction("SKIP 0336 jsr factoryTestPerformIfNeeded:24\n");
		return 0;
	} if (pc == 0x350){ // Check battery
		pc += 4;
		printInstruction("SKIP 350 jsr checkBatteryForBelowGivenLevel:24\n");
		*RL[0] = 0;
		return 0;
	}
	/*
	if (pc == 0x0822){ // Skip IR stuff for now
		pc = 0x0828;
		printInstruction("0x0880 Skip IR stuff for now\n");
		return 0;

	}
	*/
	/*
	if (pc == 0x08d6){ // Skip IR stuff for now
		pc = 0x0a74;
		printInstruction("0x0886 Skip IR stuff for now\n");
		return 0;
	}
	*/

	uint16_t* currentInstruction = (uint16_t*)(memory + pc);
	// IMPROVEMENT: maybe just use pointers to the ROM, left this way cause it seems cleaner
	uint16_t ab = (*currentInstruction << 8) | (*currentInstruction >> 8); // 0xbHbL aHaL -> aHaL bHbL

	uint8_t a = ab >> 8;
	uint8_t aH = (a >> 4) & 0xF; 
	uint8_t aL = a & 0xF;

	uint8_t b = ab & 0xFF;
	uint8_t bH = (b >> 4) & 0xF;
	uint8_t bL = b & 0xF;

	uint16_t cd = (*(currentInstruction + 1) << 8) | (*(currentInstruction + 1) >> 8);
	uint8_t c = cd >> 8;
	uint8_t cH = (c >> 4) & 0xF; 
	uint8_t cL = c & 0xF;

	uint8_t d = cd & 0xFF;
	uint8_t dH = (d >> 4) & 0xF;
	uint8_t dL = d & 0xF;

	uint16_t ef = (*(currentInstruction + 2) << 8) | (*(currentInstruction + 2) >> 8);
	uint8_t e = ef >> 8;
	uint8_t eH = (e >> 4) & 0xF; 
	uint8_t eL = e & 0xF;

	uint8_t f = ef & 0xFF;
	uint8_t fH = (f >> 4) & 0xF;
	uint8_t fL = f & 0xF;

	uint32_t cdef = cd << 16 | ef;                     
	if (pc == 0x79bc) { // Breakpoint for debugging
		setMemory8(0xf7b5, 0x1); // common_bit_flags - RTC 1/4 second, without this the normal mode loop doesnt draw
		int x = 3;
	}
	if (pc == 0x6a08) { // Breakpoint for debugging
		/*
		dumpArrayToFile(memory, MEM_SIZE, "mem_dump");
		dumpArrayToFile(eeprom.memory, EEPROM_SIZE, "eeprom_dump");
		*/
		int x = 3;
	}
	if (pc == 0x6a0c) { // Breakpoint for debugging
		/*
		dumpArrayToFile(memory, MEM_SIZE, "mem_dump_after");
		dumpArrayToFile(eeprom.memory, EEPROM_SIZE, "eeprom_dump_after");
		*/
		int x = 3;
	}
	switch(aH){
		case 0x0:{
			switch(aL){
				case 0x0:{
					printInstruction("%04x - NOP\n", pc);
				}break;
				case 0x1:{
					switch(bH){ // NOTE. we're ignoring bL here, might not be necesary
						case 0x0:{ // Lots of MOV.l type instructions and push.l + pop.l
							switch(c){
								case 0x6B:{
									switch(dH){
										case 0x0:{ // MOV.l @aa:16, Rd
											uint32_t address = (ef & 0x0000FFFF) | 0x00FF0000; 
											uint32_t value = getMemory32(address);

											struct RegRef32 Rd = getRegRef32(dL);

											setFlagsMOV(value, 32);
											*Rd.ptr = value;

											printInstruction("%04x - MOV.l @%x:16, ER%d\n", pc, address, Rd.idx); 
											printRegistersState();

										}break;

										case 0x8:{ // MOV.l Rs, @aa:16 
											uint32_t address = (cdef & 0x0000FFFF) | 0x00FF0000; 

											struct RegRef32 Rs = getRegRef32(dL);

											uint32_t value = *Rs.ptr;
											setFlagsMOV(value, 32);
											setMemory32(address, value);

											printInstruction("%04x - MOV.l ER%d,@%x:16 \n", pc, Rs.idx, address); 
											printMemory(address, 4);
											printRegistersState();

										}break;
										default:{
											return 1;
										} break;
									}
									pc += 4;


								}break;
								case 0x6D:{ // MOV.l @ERs+, ERd --- MOV.l ERs, @-ERd
									char incOrDec = (dH & 0b1000) ? '-' : '+';

									if (incOrDec == '+'){
										struct RegRef32 Rs = getRegRef32(dH);
										struct RegRef32 Rd = getRegRef32(dL);

										uint32_t value = getMemory32(*Rs.ptr);

										*Rs.ptr += 4;

										setFlagsMOV(value, 32);
										*Rd.ptr = value;

										printInstruction("%04x - MOV.l @ER%d+, ER%d\n", pc, Rs.idx, Rd.idx); 

									} else{
										struct RegRef32 Rs = getRegRef32(dL);
										struct RegRef32 Rd = getRegRef32(dH);

										*Rd.ptr -= 4;

										uint32_t value = *Rs.ptr;
										setMemory32(*Rd.ptr, value);
										setFlagsMOV(value, 32);

										printInstruction("%04x - MOV.l ER%d, @-ER%d, \n", pc, Rs.idx, Rd.idx); 
										printMemory(*Rd.ptr, 4);

									}
									printRegistersState();
									pc +=2;


								} break;
								case 0x6F:{ 
									uint16_t disp = ef;
									bool msbDisp = disp & 0x8000;
									uint32_t signExtendedDisp = msbDisp ? (0xFFFF0000 | disp) : disp;

									if (!(dH & 0b1000)){ // From memory @(d:16, ERs), ERd
										struct RegRef32 Rs = getRegRef32(dH);
										struct RegRef32 Rd = getRegRef32(dL);

										uint32_t value = getMemory32(*Rs.ptr + signExtendedDisp); 
										*Rd.ptr = value;
										setFlagsMOV(value, 32);

										printInstruction("%04x - MOV.l @(%d:16, ER%d), ER%d\n", pc, disp, Rs.idx, Rd.idx); 

									} else{ // To memory  ERs, @(d:16,ERd) 
										struct RegRef32 Rs = getRegRef32(dL);
										struct RegRef32 Rd = getRegRef32(dH);

										uint32_t value = *Rs.ptr;
										setFlagsMOV(value, 32);


										setMemory32(*Rd.ptr + signExtendedDisp, value);
										printInstruction("%04x - MOV.l ER%d,@(%d:16, ER%d)\n", pc, Rs.idx, disp, Rd.idx); 
										printMemory(*Rd.ptr + signExtendedDisp, 4);
									}
									printRegistersState();
									pc+=4;
								} break;
								case 0x69:{ 
									if (!(dH & 0x80)){ // MOV.L @ERs, ERd
										struct RegRef32 Rs = getRegRef32(dH);
										struct RegRef32 Rd = getRegRef32(dL);

										uint32_t value = getMemory32(*Rs.ptr);

										setFlagsMOV(value, 32);
										*Rd.ptr = value;

										printInstruction("%04x - MOV.l @ER%d, ER%d\n", pc, Rs.idx, Rd.idx ); 
										printRegistersState();
									} else{ // MOV.l ERs, @ERd 
										struct RegRef32 Rs = getRegRef32(bL);
										struct RegRef32 Rd = getRegRef32(bH);
										uint32_t value = *Rs.ptr;
										setFlagsMOV(value, 32);
										setMemory32(*Rd.ptr, value);
										printInstruction("%04x - MOV.l ER%d, @ER%d, \n", pc, Rs.idx, Rd.idx);
										printMemory(*Rd.ptr, 4);
									}
									pc += 2;
								}break;
								case 0x66:{ // AND.L Rs, ERd
									struct RegRef32 Rs = getRegRef32(dH);
									struct RegRef32 Rd = getRegRef32(dL);

									uint32_t value = *Rs.ptr;
									uint32_t newValue = value & *Rd.ptr;

									setFlagsMOV(newValue, 32);
									*Rd.ptr = newValue;

									printInstruction("%04x - AND.l R%d, ER%d\n", pc, Rs.idx, Rd.idx ); 
									printRegistersState();

									pc += 2;
								}break;
								case 0x64:{ // OR.L Rs, ERd
									struct RegRef32 Rs = getRegRef32(dH);
									struct RegRef32 Rd = getRegRef32(dL);

									uint32_t value = *Rs.ptr;
									uint32_t newValue = value | *Rd.ptr;

									setFlagsMOV(newValue, 32);
									*Rd.ptr = newValue;

									printInstruction("%04x - OR.l R%d, ER%d\n", pc, Rs.idx, Rd.idx ); 
									printRegistersState();

									pc += 2;
								}break;
								case 0x65:{ // XOR.L Rs, ERd
									struct RegRef32 Rs = getRegRef32(dH);
									struct RegRef32 Rd = getRegRef32(dL);

									uint32_t value = *Rs.ptr;
									uint32_t newValue = value ^ *Rd.ptr;

									setFlagsMOV(newValue, 32);
									*Rd.ptr = newValue;

									printInstruction("%04x - XOR.l R%d, ER%d\n", pc, Rs.idx, Rd.idx ); 
									printRegistersState();

									pc += 2;
								}break;
								default:{
									return 1;
								} break;


							}

						}break;
						case 0x4:{
							pc+=2;
							if (bL == 0x0 && cH == 0x6){
								switch(cL){
									case 0x9:
									case 0xB:
									case 0xD:
									case 0xF:{
										uint8_t mostSignificantBit = dH >> 7;
										if (mostSignificantBit == 0x1){
											printInstruction("%04x - STC\n", pc);// Unused in the ROM
										}else{
											printInstruction("%04x - LDC\n", pc); // Unused in the ROM
										}
									}break;
									default:{
										return 1;
									} break;

								}
							}
						}break;
						case 0x8:{
							printInstruction("%04x - SLEEP\n", pc); // Not sleeping for now
						}break;
						case 0xC:{
							if (bL == 0x0 && cH == 0x5){
								switch(cL){
									case 0x0:{ // MULXS B Rs, Rd
										struct RegRef8 Rs = getRegRef8(dH);
										struct RegRef16 Rd = getRegRef16(dL);
										int8_t lowerBitsRd = *Rd.ptr & 0x00FF;
										*Rd.ptr = (int16_t)*Rs.ptr * (int16_t)lowerBitsRd; 
										flags.Z = (*Rd.ptr == 0) ? 1 : 0;
										flags.N = (*Rd.ptr & 0x8000) ? 1 : 0;

										printInstruction("%04x - MULXS B r%d%c, %c%d\n", pc, Rs.idx, Rs.loOrHiReg, Rd.loOrHiReg, Rd.idx);
										printRegistersState();
										pc += 2;
									} break;
									case 0x2:{
										// MULXS W Rs, Rd
										struct RegRef16 Rs = getRegRef16(dH);
										struct RegRef32 Rd = getRegRef32(dL);
										int16_t lowerBitsRd = *Rd.ptr & 0x0000FFFF;
										*Rd.ptr = (int32_t)*Rs.ptr * (int32_t)lowerBitsRd;
										flags.Z = (*Rd.ptr == 0) ? 1 : 0;
										flags.N = (*Rd.ptr & 0x80000000) ? 1 : 0;

										printInstruction("%04x - MULXS W %c%d, er%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.idx);
										printRegistersState();
										pc += 2;
									}break;
									default:{
										return 1;
									} break;

								}
							};
						}break;
						case 0xD:{
							if (bL == 0x0 && cH == 0x5){
								switch(cL){ // TODO replace with if, and see if merging it with C & F makes it more readable
									case 0x1:{ // DIVXS B Rs, Rd
										struct RegRef8 Rs = getRegRef8(dH);
										struct RegRef16 Rd = getRegRef16(dL);
										int8_t quotient = (int16_t)*Rd.ptr / (int8_t)*Rs.ptr;
										int8_t remainder = (int16_t)*Rd.ptr % (int8_t)*Rs.ptr; // Following C99 rules for the sign of quotient and remainder, the actual behaviour isnt really documented in the H800 manual
										*Rd.ptr = (remainder << 8) | quotient;

										flags.Z = (*Rs.ptr == 0) ? 1 : 0;
										flags.N = (((int16_t)quotient) > 0) ? 0 : 1;

										printInstruction("%04x - DIVXS B r%d%c, %c%d\n", pc, Rs.idx, Rs.loOrHiReg, Rd.loOrHiReg, Rd.idx);
										printRegistersState();
										pc += 2;
									} break;
									case 0x3:{ // DIVXS W Rs, Rd
										struct RegRef16 Rs = getRegRef16(dH);
										struct RegRef32 Rd = getRegRef32(dL);
										int16_t quotient = (int32_t)*Rd.ptr / (int16_t)*Rs.ptr;
										int16_t remainder = (int32_t)*Rd.ptr % (int16_t)*Rs.ptr; 
										*Rd.ptr = (remainder << 16) | quotient;

										flags.Z = (*Rs.ptr == 0) ? 1 : 0;
										flags.N = (quotient & 0x80000000) ? 1 : 0;

										printInstruction("%04x - DIVXU W %c%d, er%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.idx);
										printRegistersState();
										pc += 2;
									}break;
									default:{
										return 1;
									} break;

								}
							};
						}break;
						case 0xF:{
							pc+=2;
							if (bL == 0x0 && cH == 0x6){
								switch(cL){ // TODO replace with if, and see if merging it with C & F makes it more readable
									case 0x4:{
										printInstruction("%04x - OR\n", pc);
										return 1; // UNIMPLEMENTED
									}break;
									case 0x5:{
										printInstruction("%04x - XOR\n", pc);
										return 1; // UNIMPLEMENTED
									}break;
									case 0x6:{
										printInstruction("%04x - AND\n", pc);
										return 1; // UNIMPLEMENTED
									}break;
									default:{
										return 1;
									} break;

								}
							};
						}break;

						default:{
							printInstruction("???\n");
							return 1; // UNIMPLEMENTED
						}break;

					}
				}break;
				case 0x2:{
					printInstruction("%04x - STC\n", pc); // Unused in the ROM
				}break;
				case 0x3:{ // LDC.B Rs, CCR
					struct RegRef8 Rs = getRegRef8(bL);
					uint8_t value = *Rs.ptr;
					setFlags(value);
					printInstruction("%04x - LDC.B r%d%c, CCR\n", pc, Rs.idx, Rs.loOrHiReg);
					printRegistersState();
				}break;
				case 0x4:{
					printInstruction("%04x - ORC\n", pc);
					return 1; // UNIMPLEMENTED
				}break;
				case 0x5:{
					printInstruction("%04x - XORC\n", pc);
					return 1; // UNIMPLEMENTED
				}break;
				case 0x6:{
					printInstruction("%04x - ANDC\n", pc);
					return 1; // UNIMPLEMENTED
				}break;
				case 0x7:{ // LDC.B #xx:8, CCR
					uint8_t value = b;
					setFlags(value);
					printInstruction("%04x - LDC.B #%x:8, CCR\n", pc, value);
					printRegistersState();
				}break;
				case 0x8:{ // ADD.B Rs, Rd
					struct RegRef8 Rs = getRegRef8(bH);
					struct RegRef8 Rd = getRegRef8(bL);

					setFlagsADD(*Rd.ptr, *Rs.ptr, 8);
					*Rd.ptr += *Rs.ptr;

					printInstruction("%04x - ADD.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
					printRegistersState();
				}break;
				case 0x9:{ // ADD.W Rs, Rd
					struct RegRef16 Rs = getRegRef16(bH);
					struct RegRef16 Rd = getRegRef16(bL);

					setFlagsADD(*Rd.ptr, *Rs.ptr, 16);

					*Rd.ptr += *Rs.ptr;
					printInstruction("%04x - ADD.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
					printRegistersState();

				}break;
				case 0xA:{
					switch(bH){
						case 0x0:{ // INC.b Rd
							struct RegRef8 Rd = getRegRef8(bL);
							setFlagsINC(*Rd.ptr, 1, 8);
							*Rd.ptr += 1;
							printInstruction("%04x - INC.b r%d%c\n", pc, Rd.idx, Rd.loOrHiReg);
							printRegistersState();
						}break;
						case 0x8:
						case 0x9:
						case 0xA:
						case 0xB:
						case 0xC:
						case 0xD:
						case 0xE:
						case 0xF:{ // ADD.l ERs, ERd
							struct RegRef32 Rs = getRegRef32(bH);
							struct RegRef32 Rd = getRegRef32(bL);

							setFlagsADD(*Rd.ptr, *Rs.ptr, 32);

							*Rd.ptr += *Rs.ptr;
							printInstruction("%04x - ADD.l ER%d, ER%d\n", pc, Rs.idx,  Rd.idx); 
							printRegistersState();
						}break;
						default:{
							return 1;
						} break;
					}
				}break;
				case 0xB:{ // ADDS and INC
					switch(bH){
						case 0x0:{ // ADDS.l #1, ERd
							struct RegRef32 Rd = getRegRef32(bL);
							*Rd.ptr += 1;
							printInstruction("%04x - ADDS.l #1, ER%d\n", pc, Rd.idx);
						}break;
						case 0x8:{ // ADDS.l #2, ERd
							struct RegRef32 Rd = getRegRef32(bL);
							*Rd.ptr += 2;
							printInstruction("%04x - ADDS.l #2, ER%d\n", pc, Rd.idx);
						}break;
						case 0x9:{ // ADDS.l #4, ERd
							struct RegRef32 Rd = getRegRef32(bL);
							*Rd.ptr += 4;
							printInstruction("%04x - ADDS.l #4, ER%d\n", pc, Rd.idx);
						}break;
						case 0x5:{ // INC.w #1, Rd
							struct RegRef16 Rd = getRegRef16(bL);
							setFlagsINC(*Rd.ptr, 1, 16); 
							*Rd.ptr += 1;
							printInstruction("%04x - INC.w #1, %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
						}break;
						case 0x7:{ // INC.l #1, ERd
							struct RegRef32 Rd = getRegRef32(bL);
							setFlagsINC(*Rd.ptr, 1, 32);
							*Rd.ptr += 1;
							printInstruction("%04x - INC.l #1, ER%d\n", pc, Rd.idx);
						} break;
						case 0xD:{ // INC.w #2, Rd
							struct RegRef16 Rd = getRegRef16(bL);
							setFlagsINC(*Rd.ptr, 2, 16);
							*Rd.ptr += 2;
							printInstruction("%04x - INC.w #2, %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
						}break;
						case 0xF:{ // INC.l #2, ERd
							struct RegRef32 Rd = getRegRef32(bL);
							setFlagsINC(*Rd.ptr, 2, 32);
							*Rd.ptr += 2;
							printInstruction("%04x - INC.l #2, ER%d\n", pc, Rd.idx);
						}break;
						default:{
							return 1;
						} break;
					}
					printRegistersState();
				}break;
				case 0xC:{ // MOV.B Rs, Rd
					struct RegRef8 Rs = getRegRef8(bH);
					struct RegRef8 Rd = getRegRef8(bL);

					setFlagsMOV(*Rs.ptr, 8);
					*Rd.ptr = *Rs.ptr;

					printInstruction("%04x - MOV.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
					printRegistersState();

				}break;
				case 0xD:{ // MOV.W Rs, Rd
					struct RegRef16 Rs = getRegRef16(bH);
					struct RegRef16 Rd = getRegRef16(bL);

					setFlagsMOV(*Rs.ptr, 16);

					*Rd.ptr = *Rs.ptr;
					printInstruction("%04x - MOV.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
					printRegistersState();

				}break;
				case 0xE:{
					printInstruction("%04x - ADDX\n", pc);
					return 1; // UNIMPLEMENTED
				}break;
				case 0xF:{
					switch(bH){
						case 0x0:{
							printInstruction("%04x - DAA\n", pc);
							return 1; // UNIMPLEMENTED
						}break;
						case 0x8:
						case 0x9:
						case 0xA:
						case 0xB:
						case 0xC:
						case 0xD:
						case 0xE:
						case 0xF:{// MOV.l ERs, ERd
							struct RegRef32 Rs = getRegRef32(bH);
							struct RegRef32 Rd = getRegRef32(bL);

							setFlagsMOV(*Rs.ptr, 32);

							*Rd.ptr = *Rs.ptr;
							printInstruction("%04x - MOV.l ER%d, ER%d\n", pc, Rs.idx,  Rd.idx); 
							printRegistersState();
						}break;
						default:{
							return 1;
						} break;
					}
				}break;
				default:{
					return 1;
				} break;

			}
		}break;
		case 0x1:{
			switch(aL){
				case 0x0:{
					switch(bH){
						case 0x0:{ // SHLL.b Rd
							struct RegRef8 Rd = getRegRef8(bL);
							flags.C = *Rd.ptr & 0x80;
							*Rd.ptr = (*Rd.ptr << 1);
							setFlagsMOV(*Rd.ptr, 8);
							printInstruction("%04x - SHLL.b r%d%c\n", pc, Rd.idx, Rd.loOrHiReg);
						}break;
						case 0x1:{ // SHLL.w Rd
							struct RegRef16 Rd = getRegRef16(bL);
							flags.C = *Rd.ptr & 0x8000;
							*Rd.ptr = (*Rd.ptr << 1);
							setFlagsMOV(*Rd.ptr, 16);
							printInstruction("%04x - SHLL.w %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
						} break;
						case 0x3:{ // SHLL.l Rd
							struct RegRef32 Rd = getRegRef32(bL);
							flags.C = *Rd.ptr & 0x80000000;
							*Rd.ptr = (*Rd.ptr << 1);
							setFlagsMOV(*Rd.ptr, 32);
							printInstruction("%04x - SHLL.l er%d\n", pc, Rd.idx);
						}break;
						case 0x8:{ // SHAL.b Rd -- These differ in their treatment of the V flag
							struct RegRef8 Rd = getRegRef8(bL);
							flags.C = *Rd.ptr & 0x80;
							*Rd.ptr = (*Rd.ptr << 1);
							setFlagsMOV(*Rd.ptr, 8);
							flags.V = flags.C && !(*Rd.ptr & 0x80);
							printInstruction("%04x - SHAL.b r%d%c\n", pc, Rd.idx, Rd.loOrHiReg);
						}break;
						case 0x9:{ // SHAL.w Rd
							struct RegRef16 Rd = getRegRef16(bL);
							flags.C = *Rd.ptr & 0x8000;
							*Rd.ptr = (*Rd.ptr << 1);
							setFlagsMOV(*Rd.ptr, 16);
							flags.V = flags.C && !(*Rd.ptr & 0x8000);
							printInstruction("%04x - SHAL.w %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
						}break;
						case 0xB:{ // SHAL.l Rd
							struct RegRef32 Rd = getRegRef32(bL);
							flags.C = *Rd.ptr & 0x80000000;
							*Rd.ptr = (*Rd.ptr << 1);
							setFlagsMOV(*Rd.ptr, 32);
							flags.V = flags.C && !(*Rd.ptr & 0x80000000);
							printInstruction("%04x - SHAL.l er%d\n", pc, Rd.idx);
						}break;
						default:{
							return 1;
						} break;
					}
					printRegistersState();
				}break;
				case 0x1:{
					switch(bH){
						case 0x0:{ // SHLR.b Rd
							struct RegRef8 Rd = getRegRef8(bL);
							flags.C = *Rd.ptr & 0x1;
							*Rd.ptr = (*Rd.ptr >> 1);
							setFlagsMOV(*Rd.ptr, 8);
							printInstruction("%04x - SHLR.b r%d%c\n", pc, Rd.idx, Rd.loOrHiReg);
						}break;
						case 0x1:{ // SHLR.w Rd
							struct RegRef16 Rd = getRegRef16(bL);
							flags.C = *Rd.ptr & 0x1;
							*Rd.ptr = (*Rd.ptr >> 1);
							setFlagsMOV(*Rd.ptr, 16);
							printInstruction("%04x - SHLR.w %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
						} break;
						case 0x3:{ // SHLR.l Rd
							struct RegRef32 Rd = getRegRef32(bL);
							flags.C = *Rd.ptr & 0x1;
							*Rd.ptr = (*Rd.ptr >> 1);
							setFlagsMOV(*Rd.ptr, 32);
							printInstruction("%04x - SHLR.l er%d\n", pc, Rd.idx);
						}break;
						case 0x8:{ // SHAR.b Rd - Unused in the ROM
							return 1;
						}break;
						case 0x9:{ // SHAR.w Rd
							struct RegRef16 Rd = getRegRef16(bL);
							flags.C = *Rd.ptr & 0x1;
							*Rd.ptr = (*Rd.ptr >> 1) | (*Rd.ptr & 0x8000);
							setFlagsMOV(*Rd.ptr, 16);
							printInstruction("%04x - SHAR.w %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
						}break;
						case 0xB:{ // SHAR.l Rd
							struct RegRef32 Rd = getRegRef32(bL);
							flags.C = *Rd.ptr & 0x1;
							*Rd.ptr = (*Rd.ptr >> 1) | (*Rd.ptr & 0x80000000);
							setFlagsMOV(*Rd.ptr, 32);
							printInstruction("%04x - SHAR.l er%d\n", pc, Rd.idx);
						}break;
						default:{
							return 1;
						} break;
					}
					printRegistersState();
				}break;
				case 0x2:{
					switch(bH){
						case 0x0:{ // ROTXL.b Rd
							struct RegRef8 Rd = getRegRef8(bL);
							bool oldCarry = flags.C;
							flags.C = *Rd.ptr & 0x80;
							*Rd.ptr = (*Rd.ptr << 1) | oldCarry;
							setFlagsMOV(*Rd.ptr, 8);
							printInstruction("%04x - ROTXL.b r%d%c\n", pc, Rd.idx, Rd.loOrHiReg);
						} break;
						case 0x1:{ // ROTXL.w Rd
							struct RegRef16 Rd = getRegRef16(bL);
							bool oldCarry = flags.C;
							flags.C = *Rd.ptr & 0x8000;
							*Rd.ptr = (*Rd.ptr << 1) | oldCarry;
							setFlagsMOV(*Rd.ptr, 16);
							printInstruction("%04x - ROTXL.w %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
						} break;
						case 0x3:{ // ROTXL.l Rd
							struct RegRef32 Rd = getRegRef32(bL);
							bool oldCarry = flags.C;
							flags.C = *Rd.ptr & 0x80000000;
							*Rd.ptr = (*Rd.ptr << 1) | oldCarry;
							setFlagsMOV(*Rd.ptr, 32);
							printInstruction("%04x - ROTXL.l er%d\n", pc, Rd.idx);
						}break;
						case 0x8:{ // ROTL.b Rd
							struct RegRef8 Rd = getRegRef8(bL);
							flags.C = *Rd.ptr & 0x80;
							*Rd.ptr = (*Rd.ptr << 1) | flags.C;
							setFlagsMOV(*Rd.ptr, 8);
							printInstruction("%04x - ROTL.b r%d%c\n", pc, Rd.idx, Rd.loOrHiReg);
						} break;
						case 0x9:{ // ROTL.w Rd
							struct RegRef16 Rd = getRegRef16(bL);
							flags.C = *Rd.ptr & 0x8000;
							*Rd.ptr = (*Rd.ptr << 1) | flags.C;
							setFlagsMOV(*Rd.ptr, 16);
							printInstruction("%04x - ROTL.w %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
						} break;
						case 0xB:{ // ROTL.l Rd
							struct RegRef32 Rd = getRegRef32(bL);
							flags.C = *Rd.ptr & 0x80000000;
							*Rd.ptr = (*Rd.ptr << 1) | flags.C;
							setFlagsMOV(*Rd.ptr, 32);
							printInstruction("%04x - ROTL.l er%d\n", pc, Rd.idx);
						}break;
						default:{
							return 1;
						} break;
					}
					printRegistersState();
				}break;
				case 0x3:{ // ROTR and ROTXR - Unused in the ROM
					return 1;
				}break;
				case 0x4:{ // OR.B Rs, Rd
					struct RegRef8 Rs = getRegRef8(bH);
					struct RegRef8 Rd = getRegRef8(bL);

					uint8_t newValue = *Rs.ptr | *Rd.ptr;

					setFlagsMOV(newValue, 8);
					*Rd.ptr = newValue;

					printInstruction("%04x - OR.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
					printRegistersState();
				}break;
				case 0x5:{ // XOR.B Rs, Rd
					struct RegRef8 Rs = getRegRef8(bH);
					struct RegRef8 Rd = getRegRef8(bL);

					uint8_t newValue = *Rs.ptr ^ *Rd.ptr;

					setFlagsMOV(newValue, 8);
					*Rd.ptr = newValue;

					printInstruction("%04x - XOR.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
					printRegistersState();
				}break;
				case 0x6:{ // AND.B Rs, Rd
					struct RegRef8 Rs = getRegRef8(bH);
					struct RegRef8 Rd = getRegRef8(bL);

					uint8_t newValue = *Rs.ptr & *Rd.ptr;

					setFlagsMOV(newValue, 8);
					*Rd.ptr = newValue;

					printInstruction("%04x - AND.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
					printRegistersState();
				}break;
				case 0x7:{
					switch(bH){
						case 0x0:
						case 0x1:
						case 0x3:{
							printInstruction("%04x - NOT\n", pc);
							return 1; // UNIMPLEMENTED
						}break;
						case 0x5:{ // EXTU.w Rd
							struct RegRef16 Rd = getRegRef16(bL);
							*Rd.ptr = *Rd.ptr & 0x00FF;
							setFlagsMOV(*Rd.ptr, 16);
							printInstruction("%04x - EXTU.w %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
							printRegistersState();
						} break;
						case 0x7:{ // EXTU.l Rd
							struct RegRef32 Rd = getRegRef32(bL);
							*Rd.ptr = *Rd.ptr & 0x0000FFFF;
							setFlagsMOV(*Rd.ptr, 32);
							printInstruction("%04x - EXTU.l er%d\n", pc, Rd.idx);
							printRegistersState();
						}break;
						case 0x8:{ // NEG.b Rd -- TODO: Untested
							struct RegRef8 Rd = getRegRef8(bL);

							setFlagsSUB(0, *Rd.ptr, 8);
							if (*Rd.ptr != 0x80){
								*Rd.ptr = (int8_t)0 - (int8_t)*Rd.ptr;
							}
							printInstruction("%04x - NEG.b r%d%c\n", pc, Rd.idx, Rd.loOrHiReg); 
							printRegistersState();

						} break;
						case 0x9:{ // NEG.w Rd
							struct RegRef16 Rd = getRegRef16(bL);

							setFlagsSUB(0, *Rd.ptr, 16);
							if (*Rd.ptr != 0x8000){
								*Rd.ptr = (int16_t)0 - (int16_t)*Rd.ptr;
							}
							printInstruction("%04x - NEG.w %c%d\n", pc, Rd.loOrHiReg, Rd.idx); 
							printRegistersState();

						} break;
						case 0xB:{ // NEG.l Rd
							printInstruction("%04x - NEG.l\n", pc);
							return 1; // Unused in the ROM
						}break;
						case 0xD:{ // EXTS.w Rd
							struct RegRef16 Rd = getRegRef16(bL);
							bool sign = *Rd.ptr & 0x80;
							if (sign == 0){
								*Rd.ptr = *Rd.ptr & 0x00FF;
							} else{
								*Rd.ptr = (*Rd.ptr & 0x00FF) | 0xFF00;
							}
							setFlagsMOV(*Rd.ptr, 16);
							printInstruction("%04x - EXTS.w %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
							printRegistersState();
						} break;
						case 0xF:{ // EXTS.l Rd
							struct RegRef32 Rd = getRegRef32(bL);
							bool sign = *Rd.ptr & 0x8000;
							if (sign == 0){
								*Rd.ptr = *Rd.ptr & 0x0000FFFF;
							} else{
								*Rd.ptr = (*Rd.ptr & 0x0000FFFF) | 0xFFFF0000;
							}
							setFlagsMOV(*Rd.ptr, 32);
							printInstruction("%04x - EXTS.l er%d\n", pc, Rd.idx);
							printRegistersState();
						}break;
						default:{
							return 1;
						} break;

					}
				}break;
				case 0x8:{ // SUB.b Rs, Rd
					struct RegRef8 Rs = getRegRef8(bH);
					struct RegRef8 Rd = getRegRef8(bL);

					setFlagsSUB(*Rd.ptr, *Rs.ptr, 8);
					*Rd.ptr -= *Rs.ptr;

					printInstruction("%04x - SUB.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
					printRegistersState();

				}break;
				case 0x9:{ // SUB.W Rs, Rd

					struct RegRef16 Rs = getRegRef16(bH);
					struct RegRef16 Rd = getRegRef16(bL);

					setFlagsSUB(*Rd.ptr, *Rs.ptr, 16);

					*Rd.ptr -= *Rs.ptr;
					printInstruction("%04x - SUB.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
					printRegistersState();

				}break;
				case 0xA:{
					switch(bH){
						case 0x0:{ // DEC.b Rd
							struct RegRef8 Rd = getRegRef8(bL);
							setFlagsINC(*Rd.ptr, -1, 8);
							*Rd.ptr -= 1;
							printInstruction("%04x - DEC.b r%d%c\n", pc, Rd.idx, Rd.loOrHiReg);
							printRegistersState();
						}break;
						case 0x8:
						case 0x9:
						case 0xA:
						case 0xB:
						case 0xC:
						case 0xD:
						case 0xE:
						case 0xF:{ // SUB.l ERs, ERd
							struct RegRef32 Rs = getRegRef32(bH);
							struct RegRef32 Rd = getRegRef32(bL);

							setFlagsSUB(*Rd.ptr, *Rs.ptr, 32);

							*Rd.ptr -= *Rs.ptr;
							printInstruction("%04x - SUB.l ER%d, ER%d\n", pc, Rs.idx,  Rd.idx); 
							printRegistersState();
						}break;
						default:{
							return 1;
						} break;

					}
				}break;
				case 0xB:{
					switch(bH){
						case 0x0:{ // SUBS #1, ERd
							struct RegRef32 Rd = getRegRef32(bL);

							*Rd.ptr -= 1;
							printInstruction("%04x - SUBS #1, ER%d\n", pc, Rd.idx); 
							printRegistersState();
						}break;
						case 0x8:{ // SUBS #2, ERd
							struct RegRef32 Rd = getRegRef32(bL);

							*Rd.ptr -= 2;
							printInstruction("%04x - SUBS #2, ER%d\n", pc, Rd.idx); 
							printRegistersState();
						}break;
						case 0x9:{ // SUBS #4, ERd
							struct RegRef32 Rd = getRegRef32(bL);

							*Rd.ptr -= 4;
							printInstruction("%04x - SUBS #4, ER%d\n", pc, Rd.idx); 
							printRegistersState();
						}break;
						case 0x5:{ // DEC.w #1, Rd
							struct RegRef16 Rd = getRegRef16(bL);
							setFlagsINC(*Rd.ptr, -1, 16);
							*Rd.ptr -= 1;
							printInstruction("%04x - DEC.w #1, %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
							printRegistersState();
						}break;
						case 0x7:{ // DEC.l #1, ERd
							struct RegRef32 Rd = getRegRef32(bL);
							setFlagsINC(*Rd.ptr, -1, 32);
							*Rd.ptr -= 1;
							printInstruction("%04x - DEC.l #1, ER%d\n", pc, Rd.idx);
							printRegistersState();
						}break;
						case 0xD:{ // DEC.w #2, Rd
							struct RegRef16 Rd = getRegRef16(bL);
							setFlagsINC(*Rd.ptr, -2, 16);
							*Rd.ptr -= 2;
							printInstruction("%04x - DEC.w #2, %c%d\n", pc, Rd.loOrHiReg, Rd.idx);
							printRegistersState();
						}break;
						case 0xF:{ // DEC.l #2, ERd
							struct RegRef32 Rd = getRegRef32(bL);
							setFlagsINC(*Rd.ptr, -2, 32);
							*Rd.ptr -= 2;
							printInstruction("%04x - DEC.l #2, ER%d\n", pc, Rd.idx);
							printRegistersState();
						}break;
						default:{
							return 1;
						} break;
					}
				}break;
				case 0xC:{ // CMP.b Rs, Rd
					struct RegRef8 Rs = getRegRef8(bH);
					struct RegRef8 Rd = getRegRef8(bL);

					setFlagsSUB(*Rd.ptr, *Rs.ptr, 8);

					printInstruction("%04x - SUB.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
					printRegistersState();

				}break;
				case 0xD:{ // CMP.W Rs, Rd
					struct RegRef16 Rs = getRegRef16(bH);
					struct RegRef16 Rd = getRegRef16(bL);
					setFlagsSUB(*Rd.ptr, *Rs.ptr, 16);

					printInstruction("%04x - CMP.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
					printRegistersState();
				}break;
				case 0xE:{
					printInstruction("%04x - SUBX\n", pc);
					return 1; // UNIMPLEMENTED
				}break;
				case 0xF:{
					switch(bH){
						case 0x0:{
							printInstruction("%04x - DAS\n", pc);
							return 1; // UNIMPLEMENTED
						}break;
						case 0x8:
						case 0x9:
						case 0xA:
						case 0xB:
						case 0xC:
						case 0xD:
						case 0xE:
						case 0xF:{ // CMP.l ERs, ERd
							struct RegRef32 Rs = getRegRef32(bH);
							struct RegRef32 Rd = getRegRef32(bL);

							setFlagsSUB(*Rd.ptr, *Rs.ptr, 32);

							printInstruction("%04x - CMP.l ER%d, ER%d\n", pc, Rs.idx,  Rd.idx); 
							printRegistersState();
						}break;
						default:{
							return 1;
						} break;
					}
				}break;
				default:{
					return 1;
				} break;
			}
		}break;

		case 0x2:{ // MOV.B @aa:8, Rd
			uint32_t address = (b & 0x000000FF) | 0x00FFFF00; // Upper 16 bits assumed to be 1
			uint8_t value = getMemory8(address);

			struct RegRef8 Rd = getRegRef8(aL);
			setFlagsMOV(value, 8);
			*Rd.ptr = value;

			printInstruction("%04x - MOV.b @%x:8, R%d%c\n", pc, address, Rd.idx, Rd.loOrHiReg); 
			printRegistersState();


		}break;
		case 0x3:{ // MOV.B Rs, @aa:8 
			uint32_t address = (b & 0x000000FF) | 0x00FFFF00; // Upper 16 bits assumed to be 1

			struct RegRef8 Rs = getRegRef8(aL);
			uint8_t value = *Rs.ptr;
			setFlagsMOV(value, 8);
			setMemory8(address, value);

			printInstruction("%04x - MOV.b R%d%c,@%x:8 \n", pc, Rs.idx, Rs.loOrHiReg, address); 
			printMemory(address, 1);
			printRegistersState();



		}break;
		case 0x4:{
			int8_t disp = b;

			switch(aL){
				case 0x0:{ // BRA d:8
					printInstruction("%04x - BRA %d:8\n", pc, disp);
					pc += disp; 
				}break;
				case 0x1:{ // Unused in the ROM
					printInstruction("%04x - BRN %d:8\n", pc, disp);
					return 1; // UNIMPLEMENTED
				}break;
				case 0x2:{
					printInstruction("%04x - BHI %d:8\n", pc, disp);
					if(!(flags.C | flags.Z)){
						pc += disp;
					}
				}break;
				case 0x3:{
					printInstruction("%04x - BLS %d:8\n", pc, disp);
					if((flags.C | flags.Z)){
						pc += disp;
					}
				}break;
				case 0x4:{
					printInstruction("%04x - BCC %d:8\n", pc, disp);
					if(!(flags.C)){
						pc += disp;
					}
				}break;
				case 0x5:{
					printInstruction("%04x - BCS %d:8\n", pc, disp);
					if(flags.C){
						pc += disp;
					}
				}break;
				case 0x6:{
					printInstruction("%04x - BNE %d:8\n", pc, disp);
					if(!(flags.Z)){
						pc += disp;
					}
				}break;
				case 0x7:{
					printInstruction("%04x - BEQ %d:8\n", pc, disp);
					if(flags.Z){
						pc += disp;
					}
				}break;
				case 0x8:{
					printInstruction("%04x - BVC %d:8\n", pc, disp);
					if(!(flags.V)){
						pc += disp;
					}
				}break;
				case 0x9:{
					printInstruction("%04x - BVS %d:8\n", pc, disp);
					if(flags.V){
						pc += disp;
					}
				}break;
				case 0xA:{
					printInstruction("%04x - BPL %d:8\n", pc, disp);
					if(!(flags.N)){
						pc += disp;
					}
				}break;
				case 0xB:{
					printInstruction("%04x - BMI %d:8\n", pc, disp);
					if(flags.N){
						pc += disp;
					}
				}break;
				case 0xC:{
					printInstruction("%04x - BGE %d:8\n", pc, disp);
					if(!(flags.N ^ flags.V)){
						pc += disp;
					}
				}break;
				case 0xD:{
					printInstruction("%04x - BLT %d:8\n", pc, disp);
					if((flags.N ^ flags.V)){
						pc += disp;
					}

				}break;
				case 0xE:{
					printInstruction("%04x - BGT %d:8\n", pc, disp);
					if(!(flags.Z | (flags.N ^ flags.V))){
						pc += disp;
					}

				}break;
				case 0xF:{
					printInstruction("%04x - BLE %d:8\n", pc, disp);
					if(flags.Z | (flags.N ^ flags.V)){
						pc += disp;
					}
				}break;
				default:{
					return 1;
				} break;
			}
		}break;
		case 0x5:{
			switch(aL){
				case 0x0:{ // MULXU B Rs, Rd
					struct RegRef8 Rs = getRegRef8(bH);
					struct RegRef16 Rd = getRegRef16(bL);
					uint8_t lowerBitsRd = *Rd.ptr & 0x00FF;
					*Rd.ptr = *Rs.ptr * lowerBitsRd;
					printInstruction("%04x - MULXU B r%d%c, %c%d\n", pc, Rs.idx, Rs.loOrHiReg, Rd.loOrHiReg, Rd.idx);
					printRegistersState();
				} break;
				case 0x2:{// MULXU W Rs, Rd
					struct RegRef16 Rs = getRegRef16(bH);
					struct RegRef32 Rd = getRegRef32(bL);
					uint16_t lowerBitsRd = *Rd.ptr & 0x0000FFFF;
					*Rd.ptr = *Rs.ptr * lowerBitsRd;

					printInstruction("%04x - MULXU W %c%d, er%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.idx);
					printRegistersState();
				}break;
				case 0x1:{ // DIVXU B Rs, Rd
					struct RegRef8 Rs = getRegRef8(bH);
					struct RegRef16 Rd = getRegRef16(bL);
					uint8_t quotient = *Rd.ptr / *Rs.ptr;
					uint8_t remainder = *Rd.ptr % *Rs.ptr; 
					*Rd.ptr = (remainder << 8) | quotient;

					flags.Z = (*Rs.ptr == 0) ? 1 : 0;
					flags.N = (*Rs.ptr & 0x8000) ? 1 : 0;

					printInstruction("%04x - DIVXU B r%d%c, %c%d\n", pc, Rs.idx, Rs.loOrHiReg, Rd.loOrHiReg, Rd.idx);
					printRegistersState();


				} break;
				case 0x3:{ // DIVXU W Rs, Rd
					struct RegRef16 Rs = getRegRef16(bH);
					struct RegRef32 Rd = getRegRef32(bL);
					uint16_t quotient = *Rd.ptr / *Rs.ptr;
					uint16_t remainder = *Rd.ptr % *Rs.ptr; 
					*Rd.ptr = (remainder << 16) | quotient;

					flags.Z = (*Rs.ptr == 0) ? 1 : 0;
					flags.N = (*Rs.ptr & 0x80000000) ? 1 : 0;

					printInstruction("%04x - DIVXU W %c%d, er%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.idx);
					printRegistersState();

				}break;
				case 0x4:{ // RTS
					printInstruction("%04x - RTS\n", pc);
					pc = getMemory16(*SP) - 2;
					*SP += 2;
					printRegistersState();
				}break;
				case 0x5:{ // BSR d:8
					int8_t disp = b;
					printInstruction("%04x - BSR @%d:8\n", pc, disp);
					*SP -= 2;
					setMemory16(*SP, pc + 2);

					pc = pc + disp; // No need to increment pc by 2 since we're always doing that at the end of the loop

					printMemory(*SP, 2);
					printRegistersState();
				}break;
				case 0xC:{ // BSR d:16
					int16_t disp = cd; 
					printInstruction("%04x - BSR @%d:16\n", pc, disp);
					*SP -= 2;
					setMemory16(*SP, pc + 4);

					pc = pc + 2 + disp; // Increment pc by 2 since we need the next instruction (pc + 4) and we're already adding 2 at the end of the loop.

					printMemory(*SP, 2);
					printRegistersState();
				}break;
				case 0x6:{
					pc = interruptSavedAddress - 2; // -2?
					flags = interruptSavedFlags;
					printInstruction("%04x - RTE\n", pc);
				}break;
				case 0x7:{
					printInstruction("%04x - TRAPA\n", pc);
					return 1; // UNIMPLEMENTED
				}break;
				case 0x8:{
					int16_t disp = cd;
					switch(bH){
						case 0x0:{
							printInstruction("%04x - BRA %d:16\n", pc, disp);
							pc += 2 + disp;
						}break;
						case 0x1:{ // Unused in the ROM
							printInstruction("%04x - BRN %d:16\n", pc, disp);
						}break;
						case 0x2:{
							printInstruction("%04x - BHI %d:16\n", pc, disp);
							if(!(flags.C | flags.Z)){
								pc += 2 + disp;
							}else{
								pc += 2;
							}
						}break;
						case 0x3:{
							printInstruction("%04x - BLS %d:16\n", pc, disp);
							if(flags.C | flags.Z){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0x4:{
							printInstruction("%04x - BCC %d:16\n", pc, disp);
							if(!flags.C){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0x5:{
							printInstruction("%04x - BCS %d:16\n", pc, disp);
							if(flags.C){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0x6:{
							printInstruction("%04x - BNE %d:16\n", pc, disp);
							if(!flags.Z){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0x7:{
							printInstruction("%04x - BEQ %d:16\n", pc, disp);
							if(flags.Z){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0x8:{
							printInstruction("%04x - BVC %d:16\n", pc, disp);
							if(!flags.V){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0x9:{
							printInstruction("%04x - BVS %d:16\n", pc, disp);
							if(flags.V){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0xA:{
							printInstruction("%04x - BPL %d:16\n", pc, disp);
							if(!flags.N){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0xB:{
							printInstruction("%04x - BMI %d:16\n", pc, disp);
							if(flags.N){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0xC:{
							printInstruction("%04x - BGE %d:16\n", pc, disp);
							if(!(flags.N ^ flags.V)){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0xD:{
							printInstruction("%04x - BLT %d:16\n", pc, disp);
							if(flags.N ^ flags.V){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0xE:{
							printInstruction("%04x - BGT %d:16\n", pc, disp);
							if(!(flags.Z | (flags.N ^ flags.V))){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						case 0xF:{
							printInstruction("%04x - BLE %d:16\n", pc, disp);
							if((flags.Z | (flags.N ^ flags.V))){
								pc += 2 + disp;
							}else{
								pc += 2;
							}

						}break;
						default:{
							return 1;
						} break;
					}				
				}break;
				case 0x9:{ // JMP @ERn
					struct RegRef32 Er = getRegRef32(bH);
					printInstruction("%04x - JMP @ER%d\n", pc, Er.idx);
					pc = (*Er.ptr & 0x0000FFFF) - 2; // Sub 2 cause we're incrementing 2 at the end of the loop
				}break;
				case 0xA:{ // JMP @aa:24
					uint32_t address = (b << 16) | cd;
					printInstruction("%04x - JMP @0x%04x:24\n", pc, address);
					pc = address - 2; // Sub 2 cause we're incrementing 2 at the end of the loop
				}break;
				case 0xB:{ // JMP @@aa:8 - UNUSED IN THE ROM, left unimplemented.
					printInstruction("%04x - ????\n", pc);
				}break;
				case 0xD:{ // JSR @ERn
					struct RegRef32 Er = getRegRef32(bH);

					*SP -= 2;
					setMemory16(*SP, pc + 2);

					printInstruction("%04x - JSR @ER%d\n", pc, Er.idx);
					pc = (*Er.ptr & 0x0000FFFF) - 2; // Sub 2 cause we're incrementing 2 at the end of the loop

					printMemory(*SP, 2);
					printRegistersState();
				}break;
				case 0xE:{ // JSR @aa:24
					uint32_t address = (b << 16) | cd;

					*SP -= 2;
					setMemory16(*SP, pc + 4);

					printInstruction("%04x - JSR @0x%04x:24\n", pc, address);
					pc = address - 2; // Sub 2 cause we're incrementing 2 at the end of the loop

					printMemory(*SP, 2);
					printRegistersState();

				}break;
				case 0xF:{ // JSR @@aa:24 - UNUSED IN THE ROM, left unimplemented.
					printInstruction("%04x - ????\n", pc);
				}break;
				default:{
					return 1;
				} break;

			}
		}break;
		case 0x6:{
			switch(aL){
				case 0x0:{ // BSET Rn, Rd

					struct RegRef8 Rd = getRegRef8(bL);		
					struct RegRef8 Rn = getRegRef8(bH);		
					int bitToSet = *Rn.ptr;

					*Rd.ptr = *Rd.ptr | (1 << bitToSet);

					printInstruction("%04x - BSET r%d%c, r%d%c\n", pc, Rn.idx, Rn.loOrHiReg, Rd.idx, Rd.loOrHiReg);
					printRegistersState();
				}break;
				case 0x1:{
					printInstruction("%04x - BNOT\n", pc);
					return 1; // UNUSED IN THE ROM
				}break;
				case 0x2:{ // BCLR Rn, Rd
					struct RegRef8 Rd = getRegRef8(bL);		
					struct RegRef8 Rn = getRegRef8(bH);		
					int bitToClear = *Rn.ptr;

					*Rd.ptr = *Rd.ptr & ~(1 << bitToClear);

					printInstruction("%04x - BCLR r%d%c, r%d%c\n", pc, Rn.idx, Rn.loOrHiReg, Rd.idx, Rd.loOrHiReg);
					printRegistersState();
				}break;
				case 0x3:{
					printInstruction("%04x - BTST\n", pc);
					return 1; // UNIMPLEMENTED
				}break;
				case 0x4:{ // OR.w Rs, Rd
					struct RegRef16 Rd = getRegRef16(bL);
					struct RegRef16 Rs = getRegRef16(bH);
					uint16_t newValue = *Rs.ptr | *Rd.ptr;
					setFlagsMOV(newValue, 16);
					*Rd.ptr = newValue;

					printInstruction("%04x - OR.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
					printRegistersState();
				}break;
				case 0x5:{ // XOR.w Rs, Rd
					struct RegRef16 Rd = getRegRef16(bL);
					struct RegRef16 Rs = getRegRef16(bH);
					uint16_t newValue = *Rs.ptr ^ *Rd.ptr;
					setFlagsMOV(newValue, 16);
					*Rd.ptr = newValue;

					printInstruction("%04x - XOR.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
					printRegistersState();
				}break;
				case 0x6:{ // AND.w Rs, Rd
					struct RegRef16 Rd = getRegRef16(bL);
					struct RegRef16 Rs = getRegRef16(bH);
					uint16_t newValue = *Rs.ptr & *Rd.ptr;
					setFlagsMOV(newValue, 16);
					*Rd.ptr = newValue;

					printInstruction("%04x - AND.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
					printRegistersState();
				}break;
				case 0x7:{ // BST #xx:3, Rd
					uint8_t mostSignificantBit = bH >> 7;
					if (mostSignificantBit == 0x1){
						printInstruction("%04x - BIST\n", pc);
					}else{
						uint8_t bitToSet = bH;
						struct RegRef8 Rd = getRegRef8(bL);
						if (flags.C == 0){
							setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) & ~(1 << bitToSet));
						} else{
							setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) | (1 << bitToSet));
						}
						printInstruction("%04x - BST #%d, R%d%c\n", pc, bitToSet, Rd.idx, Rd.loOrHiReg);
					}					
				}break;
				case 0x8:{ 
					if(!(b & 0x80)){ // MOV.B @ERs, Rd
						struct RegRef32 Rs = getRegRef32(bH);
						struct RegRef8 Rd = getRegRef8(bL);

						uint8_t value = getMemory8(*Rs.ptr);

						setFlagsMOV(value, 8);
						*Rd.ptr = value;

						printInstruction("%04x - MOV.b @ER%d, R%d%c\n", pc, Rs.idx, Rd.idx, Rd.loOrHiReg); 
					} else{// MOV.B Rs, @ERd 
						struct RegRef8 Rs = getRegRef8(bL);
						struct RegRef32 Rd = getRegRef32(bH);

						uint8_t value = *Rs.ptr;

						setFlagsMOV(value, 8);
						setMemory8(*Rd.ptr, value);
						printInstruction("%04x - MOV.b R%d%c, @ER%d, \n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx);
						printMemory(*Rd.ptr, 1);
					}
					printRegistersState();
				}break;
				case 0x9:{ 
					if(!(b & 0x80)){ // MOV.w @ERs, Rd
						struct RegRef32 Rs = getRegRef32(bH);
						struct RegRef16 Rd = getRegRef16(bL);
						uint16_t value = getMemory16(*Rs.ptr);
						setFlagsMOV(value, 16);
						*Rd.ptr = value;
						printInstruction("%04x - MOV.w @ER%d, %c%d\n", pc, Rs.idx, Rd.loOrHiReg, Rd.idx ); 
					} else{ // MOV.w Rs, @ERd 
						struct RegRef16 Rs = getRegRef16(bL);
						struct RegRef32 Rd = getRegRef32(bH);
						uint16_t value = *Rs.ptr;
						setFlagsMOV(value, 16);
						setMemory16(*Rd.ptr, value);
						printInstruction("%04x - MOV.w R%d%c, @ER%d, \n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx);
						printMemory(*Rd.ptr, 2);
					}
					printRegistersState();

				} break;
				case 0xA:{ 
					switch(bH){
						case 0x0:{ // MOV.B @aa:16, Rd
							uint32_t address = (cd & 0x0000FFFF) | 0x00FF0000; // Upper 16 bits assumed to be 1
							uint8_t value = getMemory8(address);

							struct RegRef8 Rd = getRegRef8(bL);

							setFlagsMOV(value, 8);
							*Rd.ptr = value;


							if(address == 0xfff0e9){ // SSSRDR
								*SSU.SSSR = clearBit8(*SSU.SSSR, 1);
							}

							printInstruction("%04x - MOV.b @%x:16, R%d%c\n", pc, address, Rd.idx, Rd.loOrHiReg); 
							printRegistersState();

						}break;

						case 0x8:{ // MOV.B Rs, @aa:16 
							uint32_t address = (cd & 0x0000FFFF) | 0x00FF0000; // Upper 16 bits assumed to be 1

							struct RegRef8 Rs = getRegRef8(bL);

							uint8_t value = *Rs.ptr;
							setFlagsMOV(value, 8);
							setMemory8(address, value);

							if(address == 0xfff0eb){ // SSSTDR
								*SSU.SSSR = clearBit8(*SSU.SSSR, 2); // TDRE
								*SSU.SSSR = clearBit8(*SSU.SSSR, 3); // TEND
							} else if(address == 0xfff0d1){ // TMRB_TCB1_TLB1
								TimerB.TLBvalue = value; // TODO (if handling custom ROMs) add these checks in the other MOVs 
							}

							printInstruction("%04x - MOV.b R%d%c,@%x:16 \n", pc, Rs.idx, Rs.loOrHiReg, address); 
							printMemory(address, 1);
							printRegistersState();

						}break;
						default:{
							return 1;
						} break;
					}
					pc+=2;

				}break;
				case 0xB:{
					switch(bH){
						case 0x0:{ // MOV.w @aa:16, Rd
							uint32_t address = (cd & 0x0000FFFF) | 0x00FF0000; // Upper 16 bits assumed to be 1
							uint16_t value = getMemory16(address);

							struct RegRef16 Rd = getRegRef16(bL);

							setFlagsMOV(value, 16);
							*Rd.ptr = value;

							printInstruction("%04x - MOV.w @%x:16, %c%d\n", pc, address, Rd.loOrHiReg, Rd.idx); 
							printRegistersState();

						}break;

						case 0x8:{ // MOV.w Rs, @aa:16 
							uint32_t address = (cd & 0x0000FFFF) | 0x00FF0000; // Upper 16 bits assumed to be 1

							struct RegRef16 Rs = getRegRef16(bL);

							uint16_t value = *Rs.ptr;
							setFlagsMOV(value, 16);
							setMemory16(address, value);

							printInstruction("%04x - MOV.w %c%d,@%x:16 \n", pc, Rs.loOrHiReg, Rs.idx, address); 
							printMemory(address, 2);
							printRegistersState();

						}break;
						default:{
							return 1;
						} break;
					}
					pc+=2;

				}break;
				case 0xC:{ // MOV.B @ERs+, Rd --- MOV.B Rs, @-ERd
					char incOrDec = (bH & 0b1000) ? '-' : '+';

					if (incOrDec == '+'){
						struct RegRef32 Rs = getRegRef32(bH);
						struct RegRef8 Rd = getRegRef8(bL);

						uint8_t value = getMemory8(*Rs.ptr);

						*Rs.ptr += 1;

						setFlagsMOV(value, 8);
						*Rd.ptr = value;

						printInstruction("%04x - MOV.b @ER%d+, R%d%c\n", pc, Rs.idx, Rd.idx, Rd.loOrHiReg); 

					} else{
						struct RegRef32 Rd = getRegRef32(bH);
						struct RegRef8 Rs = getRegRef8(bL);

						*Rd.ptr -= 1;


						uint8_t value = *Rs.ptr;
						setMemory8(*Rs.ptr, value);
						setFlagsMOV(value, 8);

						printInstruction("%04x - MOV.b R%d%c, @-ER%d, \n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx); 
						printMemory(*Rd.ptr, 1);

					}
					printRegistersState();



				}break;
				case 0xD:{ // MOV.w @ERs+, Rd --- MOV.w Rs, @-ERd
					char incOrDec = (bH & 0b1000) ? '-' : '+';

					if (incOrDec == '+'){
						struct RegRef32 Rs = getRegRef32(bH);
						struct RegRef16 Rd = getRegRef16(bL);

						uint16_t value = getMemory16(*Rs.ptr);

						*Rs.ptr += 2;

						setFlagsMOV(value, 16);
						*Rd.ptr = value;

						printInstruction("%04x - MOV.w @ER%d+, %c%d\n", pc, Rs.idx, Rd.loOrHiReg, Rd.idx); 

					} else{
						struct RegRef32 Rd = getRegRef32(bH);
						struct RegRef16 Rs = getRegRef16(bL);

						*Rd.ptr -= 2;

						uint16_t value = *Rs.ptr;
						setMemory16(*Rd.ptr, value);
						setFlagsMOV(value, 16);

						printInstruction("%04x - MOV.w %c%d, @-ER%d, \n", pc, Rs.loOrHiReg, Rs.idx, Rd.idx); 
						printMemory(*Rd.ptr, 2);

					}
					printRegistersState();



				} break;
				case 0xE:{ 
					struct RegRef8 Rd = getRegRef8(bL);
					struct RegRef32 Rs = getRegRef32(bH);

					uint16_t disp = cd;
					bool msbDisp = disp & 0x8000;
					uint32_t signExtendedDisp = msbDisp ? (0xFFFF0000 | disp) : disp;

					if (!(bH & 0b1000)){ // From memory MOV.B @(d:16, ERs), Rd
						uint8_t value = getMemory8(*Rs.ptr + signExtendedDisp);
						*Rd.ptr = value;
						setFlagsMOV(value, 8);

						printInstruction("%04x - MOV.b @(%d:16, ER%d), R%d%c\n", pc, disp, Rs.idx, Rd.idx, Rd.loOrHiReg); 

					} else{ // To memory MOV.B Rs, @(d:16, ERd)
						uint8_t value = *Rd.ptr;
						setFlagsMOV(value, 8);
						setMemory8(*Rs.ptr + signExtendedDisp, value);
						printInstruction("%04x - MOV.b R%d%c, @(%d:16, ER%d), \n", pc, Rd.idx, Rd.loOrHiReg, disp, Rs.idx); 
						printMemory(*Rs.ptr + signExtendedDisp, 1);
					}
					printRegistersState();
					pc+=2;


				}break;
				case 0xF:{ 
				struct RegRef16 Rd = getRegRef16(bL);
					struct RegRef32 Rs = getRegRef32(bH);
					uint16_t disp = cd;
					bool msbDisp = disp & 0x8000;
					uint32_t signExtendedDisp = msbDisp ? (0xFFFF0000 | disp) : disp;

					if (!(bH & 0b1000)){ // From memory MOV.W @(d:16, ERs), Rd
						uint16_t value = getMemory16(*Rs.ptr + signExtendedDisp);
						*Rd.ptr = value;
						setFlagsMOV(value, 16);

						printInstruction("%04x - MOV.w @(%d:16, ER%d), %c%d\n", pc, disp, Rs.idx, Rd.loOrHiReg, Rd.idx); 

					} else{ // To memory MOV.W Rs, @(d:16, ERd)
						uint16_t value = *Rd.ptr;
						setFlagsMOV(value, 16);
						setMemory16(*Rs.ptr + signExtendedDisp, value);
						printInstruction("%04x - MOV.w %c%d, @(%d:16, ER%d), \n", pc, Rd.loOrHiReg, Rd.idx, disp, Rs.idx); 
						printMemory(*Rs.ptr + signExtendedDisp, 2);
					}
					printRegistersState();
					pc+=2;


				}break;
				default:{
					return 1;
				} break;
			}
		}break;
		case 0x7:{
			uint8_t mostSignificantBit = bH >> 7;
			switch(aL){
				case 0x0:{ // BSET #xx:3, Rd

					struct RegRef8 Rd = getRegRef8(bL);		

					int bitToSet = bH;

					*Rd.ptr = *Rd.ptr | (1 << bitToSet);

					printInstruction("%04x - BSET #%d, r%d%c\n", pc, bitToSet, Rd.idx, Rd.loOrHiReg);
					printRegistersState();
				}break;
				case 0x1:{
					return 1; // UNUSED IN THE ROM
					printInstruction("%04x - BNOT\n", pc);
				}break;
				case 0x2:{ // BCLR #xx:3, Rd

					struct RegRef8 Rd = getRegRef8(bL);		

					int bitToClear = bH;

					*Rd.ptr = *Rd.ptr & ~(1 << bitToClear);

					printInstruction("%04x - BCLR #%d, r%d%c\n", pc, bitToClear, Rd.idx, Rd.loOrHiReg);
					printRegistersState();
				}break;
				case 0x3:{
					printInstruction("%04x - BTST\n", pc);
				}break;
				case 0x4:{
					if (mostSignificantBit == 0x1){
						printInstruction("%04x - BIOR\n", pc);
					}else{
						printInstruction("%04x - BOR\n", pc);
					}
				}break;
				case 0x5:{
					if (mostSignificantBit == 0x1){
						printInstruction("%04x - BIXOR\n", pc);
					}else{
						printInstruction("%04x - BXOR\n", pc);
					}
				}break;
				case 0x6:{
					if (mostSignificantBit == 0x1){
						printInstruction("%04x - BIAND\n", pc);
					}else{
						printInstruction("%04x - BAND\n", pc);
					}
				}break;
				case 0x7:{
					if (mostSignificantBit == 0x1){
						printInstruction("%04x - BILD\n", pc);
					}else{ // BLD #xx:3, Rd
						struct RegRef8 Rd = getRegRef8(bL);		

						int bitToLoad = bH;

						flags.C = *Rd.ptr & (1 << bitToLoad);

						printInstruction("%04x - BLD #%d, r%d%c\n", pc, bitToLoad, Rd.idx, Rd.loOrHiReg);
						printRegistersState();
					}					
				}break;
				case 0x8:{
					printInstruction("%04x - MOV\n", pc);
					return 1; // UNIMPLEMENTED
				}break;
				case 0x9:{ // XXX.w #xx:16, Rd
					struct RegRef16 Rd = getRegRef16(bL);
					switch(bH){ 
						case 0x0:{ // MOV.w #xx:16, Rd
							setFlagsMOV(cd, 16);
							*Rd.ptr = cd;
							printInstruction("%04x - MOV.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
						}break;
						case 0x1:{ // ADD.w #xx:16, Rd
							setFlagsADD(*Rd.ptr, cd, 16);
							*Rd.ptr += cd;
							printInstruction("%04x - ADD.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
						}break;
						case 0x2:{ // CMP.w #xx:16, Rd
							setFlagsSUB(*Rd.ptr, cd, 16);
							printInstruction("%04x - CMP.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
						}break;
						case 0x3:{ // SUB.w #xx:16, Rd
							setFlagsSUB(*Rd.ptr, cd, 16);
							*Rd.ptr -= cd;
							printInstruction("%04x - SUB.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
						}break;
						case 0x4:{ // OR.w #xx:16, Rd
							uint16_t value = cd;
							uint16_t newValue = cd | *Rd.ptr;
							setFlagsMOV(newValue, 16);
							*Rd.ptr = newValue;
							printInstruction("%04x - OR.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
						}break;
						case 0x5:{ // XOR.w #xx:16, Rd
							uint16_t value = cd;
							uint16_t newValue = cd ^ *Rd.ptr;
							setFlagsMOV(newValue, 16);
							*Rd.ptr = newValue;
							printInstruction("%04x - XOR.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
						}break;
						case 0x6:{ // AND.w #xx:16, Rd
							uint16_t value = cd;
							uint16_t newValue = cd & *Rd.ptr;
							setFlagsMOV(newValue, 16);
							*Rd.ptr = newValue;
							printInstruction("%04x - AND.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
						}break;
						default:{
							return 1;
						} break;
					}
					printRegistersState();
					pc+=2;
				}break;
				case 0xA:{ // XXX.l #xx:32, ERd
					struct RegRef32 Rd = getRegRef32(bL);
					switch(bH){ 
						case 0x0:{ // MOV.l #xx:32, ERd
							setFlagsMOV(cdef, 32);
							*Rd.ptr = cdef;
							printInstruction("%04x - MOV.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
						}break;
						case 0x1:{ // ADD.l #xx:32, ERd
							setFlagsADD(*Rd.ptr, cdef, 32);
							*Rd.ptr += cdef;
							printInstruction("%04x - ADD.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
						}break;
						case 0x2:{
							// CMP.l #xx:32, ERd
							setFlagsSUB(*Rd.ptr, cdef, 32);
							printInstruction("%04x - CMP.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
						}break;
						case 0x3:{ // SUB.l #xx:32, ERd
							setFlagsSUB(*Rd.ptr, cdef, 32);
							*Rd.ptr -= cdef;
							printInstruction("%04x - SUB.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
						}break;
						case 0x4:{ // OR.l #xx:32, ERd
							uint32_t newValue = cdef | *Rd.ptr;
							setFlagsMOV(newValue, 32);
							*Rd.ptr = newValue;
							printInstruction("%04x - OR.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
						}break;
						case 0x5:{ // XOR.l #xx:32, ERd
							uint32_t newValue = cdef ^ *Rd.ptr;
							setFlagsMOV(newValue, 32);
							*Rd.ptr = newValue;
							printInstruction("%04x - XOR.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
						}break;
						case 0x6:{ // AND.l #xx:32, ERd
							uint32_t newValue = cdef & *Rd.ptr;
							setFlagsMOV(newValue, 32);
							*Rd.ptr = newValue;
							printInstruction("%04x - AND.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
						}break;
						default:{
							return 1;
						} break;
					}
					printRegistersState();
					pc+=4;
				}break;
				case 0xB:{
					printInstruction("%04x - EEPMOV\n", pc);
					return 1; // UNIMPLEMENTED
				}break;
				case 0xC:{
					uint8_t mostSignificantBit = dH >> 7;
					switch(c){
						case 0x77:{
							// BLD #xx:3, @ERd
							struct RegRef32 Rd = getRegRef32(bH);		
							int bitToLoad = dH;
							printInstruction("%04x - BLD #%d, @ER%d\n", pc, bitToLoad, Rd.idx);
							flags.C = getMemory8(*Rd.ptr) & (1 << bitToLoad);
							printRegistersState();
							pc+=2;

						}break;
						default:{
							return 1;
						} break;

					}



				} break;
				case 0xE:{
					// Here bH is the "register designation field" dont know what that is, so ignorign it for now
					// togetherwith bL it can also be "aa" which is the "absolute address field"
					if (cH == 0x6){
						switch(cL){
							case 0x3:{
								printInstruction("%04x - BTST\n", pc);
								return 1; // UNIMPLEMENTED
							}break;
							default:{
								return 1;
							} break;
						}
					}else if (cH == 0x7){
						uint8_t mostSignificantBit = dH >> 7;
						switch(cL){
							case 0x3:{
								printInstruction("%04x - BTST\n", pc);
							}break;
							case 0x4:{
								if (mostSignificantBit == 0x1){
									printInstruction("%04x - BIOR\n", pc);
								}else{
									printInstruction("%04x - BOR\n", pc);
								}
							}break;
							case 0x5:{
								if (mostSignificantBit == 0x1){
									printInstruction("%04x - BXOR\n", pc);
								}else{
									printInstruction("%04x - BIXOR\n", pc);
								}
							}break;
							case 0x6:{
								if (mostSignificantBit == 0x1){
									printInstruction("%04x - BIAND\n", pc);
								}else{
									printInstruction("%04x - BAND\n", pc);
								}
							}break;
							case 0x7:{
								if (mostSignificantBit == 0x1){
									printInstruction("%04x - BILD\n", pc);
								}else{ // BLD #xx:3, @ERd
									int bitToLoad = dH;
									uint32_t address = (0x0000FF00) | b;
									printInstruction("%04x - BLD #%d, @0x%x:8\n", pc, bitToLoad, address);
									flags.C =  getMemory8(address) & (1 << bitToLoad);
								}
							}break;
							default:{
								return 1;
							} break;
						}


					}
					pc+=2; 
				}break;
				case 0xD:{
					struct RegRef32 Rd = getRegRef32(bH);		
					switch(c){
						case 0x70:{ // BSET #xx:3, @ERd
							int bitToSet = dH;
							printInstruction("%04x - BSET #%d, @ER%d\n", pc, bitToSet, Rd.idx);
							setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) | (1 << bitToSet));
						}break;
						case 0x60:{ // BSET Rn, @ERd
							struct RegRef8 Rn = getRegRef8(dH);		
							int bitToSet = *Rn.ptr;
							printInstruction("%04x - BSET r%d%c, @ER%d\n", pc, Rn.idx, Rn.loOrHiReg, Rd.idx);
							setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) | (1 << bitToSet));
						}break;
						case 0x71:{ // BNOT #xx:3, @ERd
							int bitToInvert = dH;
							bool bitValue = getMemory8(*Rd.ptr) & bitToInvert;
							if (bitValue == 1){
								setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) & ~(1 << bitToInvert));
							} else{
								setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) | (1 << bitToInvert));
							}
							printInstruction("%04x - BNOT #%d, @ER%d\n", pc, bitToInvert, Rd.idx);
						}break;
						case 0x72:{ // BCLR #xx:3, @ERd
							int bitToClear = dH;
							printInstruction("%04x - BCLR #%d, @ER%d\n", pc, bitToClear, Rd.idx);
							setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) & ~(1 << bitToClear));
						}break;
						case 0x62:{ // BCLR Rn, @ERd
							struct RegRef8 Rn = getRegRef8(dH);		
							int bitToClear = *Rn.ptr;
							printInstruction("%04x - BCLR r%d%c, @ER%d\n", pc, Rn.idx, Rn.loOrHiReg, Rd.idx);
							setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) & ~(1 << bitToClear));
						}break;
						case 0x67:{ // BST ##xx:3, @ERd
							int bitToSet = dH;
							if (flags.C == 0){
								setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) & ~(1 << bitToSet));
							} else{
								setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) | (1 << bitToSet));
							}
							printInstruction("%04x - BST #%d, @ER%d\n", pc, bitToSet, Rd.idx);
						}break;
						default:{
							return 1;
						} break;
					}
					printMemory(*Rd.ptr, 1);
					printRegistersState();
					pc+=2;
				}break;
				case 0xF:{
					uint32_t address = (0x0000FF00) | b;
					switch(c){
						case 0x70:{ // BSET #xx:3, @aa:8
							int bitToSet = dH;
							printInstruction("%04x - BSET #%d, @0x%x:8\n", pc, bitToSet, address);
							setMemory8(address, getMemory8(address) | (1 << bitToSet));
						}break;
						case 0x60:{ // BSET Rn, @aa:8
							struct RegRef8 Rn = getRegRef8(dH);		
							int bitToSet = *Rn.ptr;
							printInstruction("%04x - BSET r%d%c, @0x%x:8\n", pc, Rn.idx, Rn.loOrHiReg, address);
							setMemory8(address, getMemory8(address) | (1 << bitToSet));
						}break;
						case 0x72:{ // BCLR #xx:3, @aa:8
							int bitToClear = dH;
							printInstruction("%04x - BCLR #%d, @0x%x:8\n", pc, bitToClear, address);
							setMemory8(address, getMemory8(address) & ~(1 << bitToClear));
						}break;
						case 0x62:{ // BCLR Rn, @aa:8
							struct RegRef8 Rn = getRegRef8(dH);		
							int bitToClear = *Rn.ptr;
							printInstruction("%04x - BCLR r%d%c, @0x%x:8\n", pc, Rn.idx, Rn.loOrHiReg, address);
							setMemory8(address, getMemory8(address) & ~(1 << bitToClear));
							
						}break;
						case 0x67:{ // BST - Unused in the ROM
							return 1;
						} break;
						default:{
							return 1;
						} break;
					}
					printMemory(address, 1);
					pc+=2;
				} break;
				default:{
					return 1;
				} break;
			}
		}break;
		case 0x8:{ // ADD.B #xx:8, Rd
			struct RegRef8 Rd = getRegRef8(aL);

			uint8_t value = (bH << 4) | bL;

			setFlagsADD(*Rd.ptr, value, 8);
			*Rd.ptr += value;

			printInstruction("%04x - ADD.b 0x%x,R%d%c\n", pc, value, Rd.idx, Rd.loOrHiReg); //Note: Dmitry's dissasembler sometimes outputs address in decimal (0xdd) not sure why
			printRegistersState();
		}break;
		case 0x9:{
			printInstruction("%04x - ADDX\n", pc);
			return 1; // UNIMPLEMENTED
		}break;

		case 0xA:{ // CMP.B #xx:8, Rd
			struct RegRef8 Rd = getRegRef8(aL);

			uint8_t value = (bH << 4) | bL;

			setFlagsSUB(*Rd.ptr, value, 8);

			printInstruction("%04x - CMP.b 0x%x,R%d%c\n", pc, value, Rd.idx, Rd.loOrHiReg); 
			printRegistersState();

		}break;

		case 0xB:{
			printInstruction("%04x - SUBX\n", pc);
			return 1; // UNIMPLEMENTED
		}break;

		case 0xC:{ // OR.b #xx:8, Rd
			struct RegRef8 Rd = getRegRef8(aL);

			uint8_t value = b;
			uint8_t newValue = value | *Rd.ptr;
			setFlagsMOV(newValue, 8);
			*Rd.ptr = newValue;

			printInstruction("%04x - OR.b 0x%x,R%d%c\n", pc, value, Rd.idx, Rd.loOrHiReg); 
			printRegistersState();
		}break;

		case 0xD:{ // XOR.b #xx:8, Rd
			struct RegRef8 Rd = getRegRef8(aL);

			uint8_t value = b;
			uint8_t newValue = value ^ *Rd.ptr;
			setFlagsMOV(newValue, 8);
			*Rd.ptr = newValue;

			printInstruction("%04x - XOR.b 0x%x,R%d%c\n", pc, value, Rd.idx, Rd.loOrHiReg); 
			printRegistersState();
		}break;

		case 0xE:{ // AND #xx:8, Rd
			struct RegRef8 Rd = getRegRef8(aL);

			uint8_t value = b;
			uint8_t newValue = value & *Rd.ptr;
			setFlagsMOV(newValue, 8);
			*Rd.ptr = newValue;

			printInstruction("%04x - AND.b 0x%x,R%d%c\n", pc, value, Rd.idx, Rd.loOrHiReg); 
			printRegistersState();
		}break;

		case 0xF:{ // MOV.B #xx:8, Rd
			struct RegRef8 Rd = getRegRef8(aL);

			uint8_t value = b;

			setFlagsMOV(value, 8);
			*Rd.ptr = value;

			printInstruction("%04x - MOV.b 0x%x,R%d%c\n", pc, value, Rd.idx, Rd.loOrHiReg); 
			printRegistersState();
		}break;

		default:{
			printInstruction("???\n");
			return 1; // UNIMPLEMENTED
		} break;
	}

	// SSU
	if (~*SSU.SSER & TE){ // TE == 0
		*SSU.SSSR |= TDRE; // Set TDRE
	}

	if ((*SSU.SSER & (TE | RE)) == (TE | RE)){ // Transmission and recieve enabled
		if(~*SSU.SSSR & TDRE){ 
			// Here we'll start the transmission that'll take 8 cycles. But for now it happens instantly.
			// Accelerometer
			if(~(getMemory8(PORT9)) & ACCEL_PIN){ // TODO: find more readable way to deal with pins
				switch(accel.buffer.state){
					case ACCEL_GETTING_ADDRESS:{
						accel.buffer.address = *SSU.SSTDR & 0x0F; // The "&" removes 0x80 (RW flag, not part of the address)
						accel.buffer.offset = 0;
						accel.buffer.state = ACCEL_GETTING_BYTES;
					}break;
					case ACCEL_GETTING_BYTES:{
						*SSU.SSRDR = accel.memory[(accel.buffer.address) + accel.buffer.offset]; 
						accel.buffer.offset += 1;
					}break;
				}
			}
			// EEPROM
			if(~(getMemory8(PORT1)) & EEPROM_PIN){ 
				switch(eeprom.buffer.state){
					case EEPROM_EMPTY:{
						switch(*SSU.SSTDR){
							case 0x5:{ // RDSR - read status register
								*SSU.SSRDR = eeprom.status; 
							} break;
							case 0x3:{ // READ - read from memory
								eeprom.buffer.state = EEPROM_GETTING_ADDRESS_HI;
							} break;
						}

					} break;
					case EEPROM_GETTING_ADDRESS_HI:{
						eeprom.buffer.hiAddress = *SSU.SSTDR;
						eeprom.buffer.state = EEPROM_GETTING_ADDRESS_LO;

					} break;

					case EEPROM_GETTING_ADDRESS_LO:{
						eeprom.buffer.loAddress = *SSU.SSTDR;
						eeprom.buffer.state = EEPROM_GETTING_BYTES;
					} break;

					case EEPROM_GETTING_BYTES:{
						*SSU.SSRDR = eeprom.memory[((eeprom.buffer.hiAddress << 8) | eeprom.buffer.loAddress) + eeprom.buffer.offset];
						eeprom.buffer.offset  = (eeprom.buffer.offset + 1);
					} break;
				}
			}
			*SSU.SSSR = *SSU.SSSR | RDRF; 
			*SSU.SSSR = *SSU.SSSR | TDRE; 
			*SSU.SSSR = *SSU.SSSR | TEND; 
		}
	}
	else if (*SSU.SSER & TE){ 
		if(~*SSU.SSSR & TDRE){ 
			// Accelerometer
			if(~(getMemory8(PORT9)) & ACCEL_PIN){ 
				switch(accel.buffer.state){
					case ACCEL_GETTING_ADDRESS:{
						accel.buffer.address = *SSU.SSTDR;
						accel.buffer.state = ACCEL_GETTING_BYTES;
					}break;
					case ACCEL_GETTING_BYTES:{
						accel.memory[accel.buffer.address] = *SSU.SSTDR;
					}break;
				}
			}
			// EEPROM
			if(~(getMemory8(PORT1)) & EEPROM_PIN){ 
				switch (eeprom.buffer.state) {
					case EEPROM_EMPTY:{
						switch(*SSU.SSTDR){
							case 0x6:{ // WREN - write enable
								eeprom.status |= 0x2; // WEL - write enable latch. Note: I dont see any WRDI or WRSR instructions in the ROM that disable this latch, could cause issues later on
								// TODO: maybe not even needed, see if removing it changes anything
							}break;
							case 0x2: { // WRITE
								eeprom.buffer.state = EEPROM_GETTING_ADDRESS_HI;
							}break;
						}

					} break;
					case EEPROM_GETTING_ADDRESS_HI:{
						eeprom.buffer.hiAddress = *SSU.SSTDR;
						eeprom.buffer.state = EEPROM_GETTING_ADDRESS_LO;
					} break;

					case EEPROM_GETTING_ADDRESS_LO:{
						eeprom.buffer.loAddress = *SSU.SSTDR;
						eeprom.buffer.state = EEPROM_GETTING_BYTES;
					} break;

					case EEPROM_GETTING_BYTES:{
						eeprom.memory[((eeprom.buffer.hiAddress << 8) | eeprom.buffer.loAddress) + eeprom.buffer.offset] = *SSU.SSTDR;
						eeprom.buffer.offset = (eeprom.buffer.offset + 1) % EEPROM_PAGE_SIZE;
					} break;
				}
			}
			// LCD

			if((getMemory8(PORT1)) & LCD_DATA_PIN){ 
				size_t lcdMemIndex = (lcd.currentPage * LCD_WIDTH * LCD_BYTES_PER_STRIPE) + lcd.currentColumn*LCD_BYTES_PER_STRIPE + lcd.currentByte;
				assert(lcdMemIndex < LCD_MEM_SIZE);
				lcd.memory[lcdMemIndex] = *SSU.SSTDR;	
				if (lcd.currentByte == 1){
					lcd.currentColumn = (lcd.currentColumn + 1);
				}
				lcd.currentByte = (lcd.currentByte + 1) % 2;
			}
			else if(~(getMemory8(PORT1)) & LCD_PIN){
				switch(lcd.state){
					case LCD_EMPTY:{
						switch(*SSU.SSTDR){
							case 0x00:
							case 0x01:
							case 0x02:
							case 0x03:
							case 0x04:
							case 0x05:
							case 0x06:
							case 0x07:
							case 0x08:
							case 0x09:
							case 0x0A:
							case 0x0B:
							case 0x0C:
							case 0x0D:
							case 0x0E:
							case 0x0F:{
								lcd.currentColumn = (*SSU.SSTDR & 0xF) | (lcd.currentColumn & 0xF0); // Set lower column address
								lcd.currentByte = 0;
							}break;
							case 0x10:
							case 0x11:
							case 0x12:
							case 0x13:
							case 0x14:
							case 0x15:
							case 0x16:
							case 0x17:{
								lcd.currentColumn = ((*SSU.SSTDR & 0b111) << 4) | (lcd.currentColumn & 0xF); // Set upper column address
								lcd.currentByte = 0;
							} break;
							case 0xB0:
							case 0xB1:
							case 0xB2:
							case 0xB3:
							case 0xB4:
							case 0xB5:
							case 0xB6:
							case 0xB7:
							case 0xB8:
							case 0xB9:
							case 0xBA:
							case 0xBB:
							case 0xBC:
							case 0xBD:
							case 0xBE:
							case 0xBF:{
								lcd.currentPage = *SSU.SSTDR & 0xF;
							}break;
							case 0xE1:{ // Exit power save mode
								lcd.powerSave = false; // TODO: see if this is even necesary
							}break;
							case 0xAE:{ // Display OFF 
								lcd.displayOn = false; 
							}break;
							case 0xAF:{ // Display ON 
								lcd.displayOn = true; 
							}break;
							case 0x81:{
								lcd.state = LCD_READING_CONTRAST;
							} break;
							default:{
								// Well ignore most commands
							} break;
						}
					} break;
					case LCD_READING_CONTRAST:{
						lcd.contrast = *SSU.SSTDR;
						lcd.state = LCD_EMPTY;
					}break;

				}
			}

			//if (*SSU.SSER & 0b100){
			// generate TX1. Maybe doesnt happen in the ROM
			//}
			// Here we'll start the transmission that'll take 8 cycles. But for now it happens instantly.
			*SSU.SSSR = *SSU.SSSR | TDRE; 
			*SSU.SSSR = *SSU.SSSR | TEND; 
		}
	}
	else if (*SSU.SSER & RE){ 
		return 1; // TODO: Check if this mode is used in the ROM
	}

	if((getMemory8(PORT9)) & ACCEL_PIN){ 
		accel.buffer.state = ACCEL_GETTING_ADDRESS;
		accel.buffer.offset = 0x0;
	}

	if((getMemory8(PORT1)) & EEPROM_PIN){ // TODO: can be optimized by checking when the pin gets sets instead of all the time
		eeprom.buffer.state = EEPROM_EMPTY;
		eeprom.buffer.offset = 0x0;
		eeprom.buffer.offset = 0x0;
	}

	if(~(getMemory8(PORT1)) & LCD_DATA_PIN){ 
		//lcd.currentColumn = 0;
		//lcd.currentPage = 0;
	}

	 	

	pc+=2;
	
	// Interrupt handling
	// Note: Remember to check priorities when adding interrupt types here
	// TODO: this doesnt follow this rule: 3.8.4 Conflict between Interrupt Generation and Disabling
	if (!flags.I){
		if ((*IRQ_IRR2 & IRRTB1) && (*IRQ_IENR2 & IENTB1)){
			interruptSavedAddress = pc;
			interruptSavedFlags = flags;
			flags.I = true;
			pc = VECTOR_TIMER_B1;
		}
	}

	// Clock handling
	uint32_t cyclesEllapsed = 2; // TODO: determine based on instruction type
	for(uint32_t i = 0; i < cyclesEllapsed; i++){
		*cycleCount += 1;
		if((*cycleCount % 32768) == 0){  // Draw once every 2048 cycles for now
			*redrawScreen = true;
		}


		if ((*cycleCount % (SYSTEM_CLOCK_CYCLES_PER_SECOND / SUB_CLOCK_CYCLES_PER_SECOND)) == 0){ 
			subClockCyclesEllapsed += 1;
			runSubClock();
		}
	}
	
	// TODO: transfer clock is clk / 4 (SSU)
	
	return 0;	
}

void initWalker(){
	int entry = 0x02C4;
	//int entry = 0x0;

	uint64_t subClockCyclesEllapsed = 0;
	// 0x0000 - 0xBFFF - ROM 
	// 0xF020 - 0xF0FF - MMIO
	// 0xF780 - 0xFF7F - RAM 
	// 0xFF80 - 0xFFFF - MMIO
	memory = malloc(MEM_SIZE);
	memset(memory, 0, MEM_SIZE);
	
	memset(&eeprom, 0, sizeof(eeprom));
	eeprom.memory = malloc(EEPROM_SIZE);
	memset(eeprom.memory, 0xFF, EEPROM_SIZE);

#ifndef INIT_EEPROM
	FILE *eepromFile = fopen("roms/eeprom.bin", "r");
	fread(eeprom.memory, 1, 64* 1024, eepromFile);
	fclose(eepromFile);
#endif

	memset(&accel, 0, sizeof(accel));
	accel.memory = malloc(29);
	memset(accel.memory, 0, 29);
	accel.memory[0] = 0x2; // Chip id

	memset(&lcd, 0, sizeof(lcd));
	lcd.powerSave = false; 
	lcd.contrast = 20;
	lcd.state = LCD_EMPTY;
	lcd.memory = malloc(LCD_MEM_SIZE);

	FILE* romFile = fopen("roms/rom.bin","r");
	if(!romFile){
		printf("Can't find rom");
	}

	fseek (romFile , 0 , SEEK_END);
	int romSize = ftell (romFile);
	rewind (romFile);

	fread(memory,1,romSize ,romFile);
	fclose(romFile);

	// Init SSU registers
	SSU.SSCRH = &memory[0xF0E0]; 
	SSU.SSCRL = &memory[0xF0E1]; 
	SSU.SSMR = &memory[0xF0E2]; 
	SSU.SSER = &memory[0xF0E3]; 
	SSU.SSSR = &memory[0xF0E4]; 
	SSU.SSRDR = &memory[0xF0E9]; 
	SSU.SSTDR = &memory[0xF0EB]; 
	SSU.SSTRSR = 0x0; 

	*SSU.SSRDR = 0x0; 
	*SSU.SSTDR = 0x0;
	*SSU.SSER = 0x0; 
	*SSU.SSSR = 0x4; // TDRE = 1 (Transmit data empty) 
	
	// Init general purpose registers
	for(int i=0; i < 8;i++){
		ER[i] = malloc(4);
		*ER[i] = 0;
		R[i] = (uint16_t*) ER[i];
		E[i] = (uint16_t*) ER[i] + 1;
		RL[i] = (uint8_t*) R[i];
		RH[i] = (uint8_t*) R[i] + 1;
	}
	SP = ER[7];
	flags = (struct Flags){0};
	printRegistersState();

	// Init Timers
	TimerB.on = false;
	TimerB.TMB1 = &memory[0xF0D0];
	setMemory8(0xf0d0, 0b00111000); 
	TimerB.TCB1 = &memory[0xF0D1];
	setMemory8(0xf0d1, 0); 
	TimerB.TLBvalue = 0;

	memset(&TimerW, 0, sizeof(TimerW));
	TimerW.TMRW = &memory[0xf0f0];
	setMemory8(0xf0f0, 0b01001000); 
	TimerW.TCRW = &memory[0xf0f1];
	setMemory8(0xf0f1, 0);
	TimerW.TIERW = &memory[0xf0f2];
	setMemory8(0xf0f2, 0b01110000);
	TimerW.TSRW = &memory[0xf0f3];
	setMemory8(0xf0f3, 0b01110000);
	TimerW.TIOR0 = &memory[0xf0f4];
	setMemory8(0xf0f4, 0b10001000);
	TimerW.TIOR1 = &memory[0xf0f5];
	setMemory8(0xf0f5, 0b10001000);
	TimerW.TCNT = (uint16_t*)&memory[0xf0f6];
	setMemory16(0xf0f6, 0);
	TimerW.GRA = (uint16_t*)&memory[0xf0f8];
	setMemory16(0xf0f8, 0xffff);
	TimerW.GRB = (uint16_t*)&memory[0xf0fa];
	setMemory16(0xf0fa, 0xffff);
	TimerW.GRC = (uint16_t*)&memory[0xf0fc];
	setMemory16(0xf0fc, 0xffff);
	/*
	TimerW.GRD = (uint16_t*)&memory[0xf0fe];
	setMemory8(0xf0fe, 0);
	*/ // Unused in the ROM

	// Init Clock halt registers
	CKSTPR1 = &memory[0xfffa];	
	setMemory8(0xFFFA, 0b00000011); 

	CKSTPR2 = &memory[0xfffb];	
	setMemory8(0xFFFA, 0b00000100);

	// Init Interrupt stuff
	IRQ_IENR2 = &memory[0xfff4];
	*IRQ_IENR2 = 0;
	IRQ_IRR2 = &memory[0xfff7];
	*IRQ_IRR2 = 0;
	interruptSavedAddress = 0;
	pc = entry;
	dumpArrayToFile(memory, MEM_SIZE, "mem_dump");
	dumpArrayToFile(eeprom.memory, EEPROM_SIZE, "eeprom_dump");
}
