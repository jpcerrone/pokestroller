#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <memory.h>

#include "main.h"

enum Mode{
	STEP,
	RUN
};

static enum Mode mode;

// General purpose registers
static uint32_t* ER[8];
static uint16_t* R[8];
static uint16_t* E[8];
static uint8_t* RL[8];
static uint8_t* RH[8];

static uint32_t* SP;
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

static uint8_t* memory;

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
	for(int i=0; i < 8; i++){
		printf("ER%d: [0x%08X], ", i, *ER[i]); 
	}
	printf("\n");
	printf("I: %d, H: %d, N: %d, Z: %d, V: %d, C: %d ", flags.I, flags.H, flags.N, flags.Z, flags.V, flags.C);
	printf("\n\n");

}

void printMemory(uint32_t address, int byteCount){
	address = address & 0x0000ffff; // Keep lower 16 bits only
	for(int i = 0; i < byteCount; i++){ 
		printf("MEMORY - 0x%04x -> %02x\n", address + i, memory[address + i]);
	}
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

	flags.C = ((uint64_t)value1 + value2 > maxValue) ? 1 : 0; 
	flags.V = ~(value1 ^ value2) & ((value1 + value2) ^ value1) & negativeFlag; // If both operands have the same sign and the results is from a different sign, overflow has occured.
	flags.H = (((value1 & maxValueLo) + (value2 & maxValueLo) & halfCarryFlag) == halfCarryFlag) ? 1 : 0; 
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

int main(){
	int entry = 0x02C4;
	//int entry = 0x0;
	mode = STEP;
	int instructionsToStep = 0;

	// 0x0000 - 0xBFFF - ROM 
	// 0xF020 - 0xF0FF - MMIO
	// 0xF780 - 0xFF7F - RAM 
	// 0xFF80 - 0xFFFF - MMIO
	memory = malloc(64 * 1024);
	memset(memory, 0, 64 * 1024);

	FILE* romFile = fopen("roms/rom.bin","r");
	if(!romFile){
		printf("Can't find rom");
	}

	fseek (romFile , 0 , SEEK_END);
	int romSize = ftell (romFile);
	rewind (romFile);

	fread(memory,1,romSize ,romFile);

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

	int pc = entry;
	while(pc != romSize){
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
		switch(aH){
			case 0x0:{
				switch(aL){
					case 0x0:{
						printf("%04x - NOP\n", pc);
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

												printf("%04x - MOV.l @%x:16, ER%d\n", pc, address, Rd.idx); 
												printRegistersState();

											}break;

											case 0x8:{ // MOV.l Rs, @aa:16 
												uint32_t address = (cdef & 0x0000FFFF) | 0x00FF0000; 

												struct RegRef32 Rs = getRegRef32(dL);

												uint32_t value = *Rs.ptr;
												setFlagsMOV(value, 32);
												setMemory32(address, value);

												printf("%04x - MOV.l ER%d,@%x:16 \n", pc, Rs.idx, address); 
												printMemory(address, 4);
												printRegistersState();

											}break;
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

											printf("%04x - MOV.l @ER%d+, ER%d\n", pc, Rs.idx, Rd.idx); 

										} else{
											struct RegRef32 Rs = getRegRef32(dL);
											struct RegRef32 Rd = getRegRef32(dH);

											*Rd.ptr -= 4;

											uint32_t value = *Rs.ptr;
											setMemory32(*Rd.ptr, value);
											setFlagsMOV(value, 32);

											printf("%04x - MOV.l ER%d, @-ER%d, \n", pc, Rs.idx, Rd.idx); 
											printMemory(*Rd.ptr, 4);

										}
										printRegistersState();
										pc +=2;


									} break;
									case 0x6F:{ 
										uint16_t disp = ef;
										bool msbDisp = disp & 0x8000;
										uint32_t signExtendedDisp = msbDisp ? (0xFFFF0000 & disp) : disp;

										if (!(dH & 0b1000)){ // From memory @(d:16, ERs), ERd
											struct RegRef32 Rs = getRegRef32(dH);
											struct RegRef32 Rd = getRegRef32(dL);

											uint32_t value = getMemory32(*Rs.ptr + signExtendedDisp); 
											*Rd.ptr = value;
											setFlagsMOV(value, 32);

											printf("%04x - MOV.l @(%d:16, ER%d), ER%d\n", pc, disp, Rs.idx, Rd.idx); 

										} else{ // To memory  ERs, @(d:16,ERd) 
											struct RegRef32 Rs = getRegRef32(dL);
											struct RegRef32 Rd = getRegRef32(dH);

											uint32_t value = *Rs.ptr;
											setFlagsMOV(value, 32);


											setMemory32(*Rd.ptr + signExtendedDisp, value);
											printf("%04x - MOV.l ER%d,@(%d:16, ER%d)\n", pc, Rs.idx, disp, Rd.idx); 
											printMemory(*Rd.ptr + signExtendedDisp, 4);
										}
										printRegistersState();
										pc+=4;
									} break;
									case 0x69:{ // MOV.L @ERs, ERd
										struct RegRef32 Rs = getRegRef32(dH);
										struct RegRef32 Rd = getRegRef32(dL);

										uint32_t value = getMemory32(*Rs.ptr);

										setFlagsMOV(value, 32);
										*Rd.ptr = value;

										printf("%04x - MOV.l @ER%d, ER%d\n", pc, Rs.idx, Rd.idx ); 
										printRegistersState();

										pc += 2;
									}break;
									case 0x66:{ // AND.L Rs, ERd
										struct RegRef32 Rs = getRegRef32(dH);
										struct RegRef32 Rd = getRegRef32(dL);

										uint32_t value = *Rs.ptr;
										uint32_t newValue = value & *Rd.ptr;

										setFlagsMOV(newValue, 32);
										*Rd.ptr = newValue;

										printf("%04x - AND.l R%d, ER%d\n", pc, Rs.idx, Rd.idx ); 
										printRegistersState();

										pc += 2;
									}break;

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
												printf("%04x - STC\n", pc);
											}else{
												printf("%04x - LDC\n", pc);
											}
										}break;
									}
								}
							}break;
							case 0x8:{
								printf("%04x - SLEEP\n", pc);
							}break;
							case 0xC:{
								pc+=2;
								if (bL == 0x0 && cH == 0x5){
									switch(cL){
										case 0x0:
										case 0x2:{
											printf("%04x - MULXS\n", pc);
										}break;
									}
								};
							}break;
							case 0xD:{
								pc+=2;
								if (bL == 0x0 && cH == 0x5){
									switch(cL){ // TODO replace with if, and see if merging it with C & F makes it more readable
										case 0x1:
										case 0x3:{
											printf("%04x - DIVXS\n", pc);
										}break;
									}
								};
							}break;
							case 0xF:{
								pc+=2;
								if (bL == 0x0 && cH == 0x6){
									switch(cL){ // TODO replace with if, and see if merging it with C & F makes it more readable
										case 0x4:{
											printf("%04x - OR\n", pc);
										}break;
										case 0x5:{
											printf("%04x - XOR\n", pc);
										}break;
										case 0x6:{
											printf("%04x - AND\n", pc);
										}break;
									}
								};
							}break;

							default:{
								printf("???\n");
							}break;

						}
					}break;
					case 0x2:{
						printf("%04x - STC\n", pc);
					}break;
					case 0x3:{
						printf("%04x - LDC\n", pc);
					}break;
					case 0x4:{
						printf("%04x - ORC\n", pc);
					}break;
					case 0x5:{
						printf("%04x - XORC\n", pc);
					}break;
					case 0x6:{
						printf("%04x - ANDC\n", pc);
					}break;
					case 0x7:{
						printf("%04x - LDC\n", pc);
					}break;
					case 0x8:{ // ADD.B Rs, Rd
						struct RegRef8 Rs = getRegRef8(bH);
						struct RegRef8 Rd = getRegRef8(bL);

						setFlagsADD(*Rd.ptr, *Rs.ptr, 8);
						*Rd.ptr += *Rs.ptr;

						printf("%04x - ADD.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
						printRegistersState();
					}break;
					case 0x9:{ // ADD.W Rs, Rd
						struct RegRef16 Rs = getRegRef16(bH);
						struct RegRef16 Rd = getRegRef16(bL);

						setFlagsADD(*Rd.ptr, *Rs.ptr, 16);

						*Rd.ptr += *Rs.ptr;
						printf("%04x - ADD.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
						printRegistersState();

					}break;
					case 0xA:{
						switch(bH){
							case 0x0:{
								printf("%04x - INC\n", pc);
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
								printf("%04x - ADD.l ER%d, ER%d\n", pc, Rs.idx,  Rd.idx); 
								printRegistersState();
							}break;

						}
					}break;
					case 0xB:{
						switch(bH){
							case 0x0:
							case 0x8:
							case 0x9:{
								printf("%04x - ADDS\n", pc);
							}break;
							case 0x5:
							case 0x7:
							case 0xD:
							case 0xF:{
								printf("%04x - INC\n", pc);
							}break;
						}
					}break;
					case 0xC:{ // MOV.B Rs, Rd
						struct RegRef8 Rs = getRegRef8(bH);
						struct RegRef8 Rd = getRegRef8(bL);

						setFlagsMOV(*Rs.ptr, 8);
						*Rd.ptr = *Rs.ptr;

						printf("%04x - MOV.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
						printRegistersState();

					}break;
					case 0xD:{ // MOV.W Rs, Rd
						struct RegRef16 Rs = getRegRef16(bH);
						struct RegRef16 Rd = getRegRef16(bL);

						setFlagsMOV(*Rs.ptr, 16);

						*Rd.ptr = *Rs.ptr;
						printf("%04x - MOV.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
						printRegistersState();

					}break;
					case 0xE:{
						printf("%04x - ADDX\n", pc);
					}break;
					case 0xF:{
						switch(bH){
							case 0x0:{
								printf("%04x - DAA\n", pc);
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
								printf("%04x - MOV.l ER%d, ER%d\n", pc, Rs.idx,  Rd.idx); 
								printRegistersState();
							}break;
						}
					}break;

				}
			}break;
			case 0x1:{
				switch(aL){
					case 0x0:{
						switch(bH){
							case 0x0:
							case 0x1:
							case 0x3:{
								printf("%04x - SHLL\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xB:{
								printf("%04x - SHAL\n", pc);
							}break;
						}
					}break;
					case 0x1:{
						switch(bH){
							case 0x0:
							case 0x1:
							case 0x3:{
								printf("%04x - SHLR\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xB:{
								printf("%04x - SHAR\n", pc);
							}break;
						}
					}break;
					case 0x2:{
						switch(bH){
							case 0x0:
							case 0x1:
							case 0x3:{
								printf("%04x - ROTXL\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xB:{
								printf("%04x - ROTL\n", pc);
							}break;
						}
					}break;
					case 0x3:{
						switch(bH){
							case 0x0:
							case 0x1:
							case 0x3:{
								printf("%04x - ROTXR\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xB:{
								printf("%04x - ROTR\n", pc);
							}break;
						}
					}break;
					case 0x4:{
						printf("%04x - OR.B\n", pc);
					}break;
					case 0x5:{
						printf("%04x - XOR.B\n", pc);
					}break;
					case 0x6:{ // AND.B Rs, Rd
						struct RegRef8 Rs = getRegRef8(bH);
						struct RegRef8 Rd = getRegRef8(bL);

						uint8_t newValue = *Rs.ptr & *Rd.ptr;

						setFlagsMOV(newValue, 8);
						*Rd.ptr = newValue;

						printf("%04x - AND.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
						printRegistersState();
					}break;
					case 0x7:{
						switch(bH){
							case 0x0:
							case 0x1:
							case 0x3:{
								printf("%04x - NOT\n", pc);
							}break;
							case 0x5:
							case 0x7:{
								printf("%04x - EXTU\n", pc);
							}break;
							case 0x8:
							case 0x9:
							case 0xB:{
								printf("%04x - NEG\n", pc);
							}break;
							case 0xD:
							case 0xF:{
								printf("%04x - EXTS\n", pc);
							}break;

						}
					}break;
					case 0x8:{ // SUB.b Rs, Rd
						struct RegRef8 Rs = getRegRef8(bH);
						struct RegRef8 Rd = getRegRef8(bL);

						setFlagsADD(*Rd.ptr, -((int8_t)*Rs.ptr), 8);
						*Rd.ptr -= *Rs.ptr;

						printf("%04x - SUB.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
						printRegistersState();

					}break;
					case 0x9:{ // SUB.W Rs, Rd

						struct RegRef16 Rs = getRegRef16(bH);
						struct RegRef16 Rd = getRegRef16(bL);

						setFlagsADD(*Rd.ptr, -((int16_t)*Rs.ptr), 16);

						*Rd.ptr -= *Rs.ptr;
						printf("%04x - SUB.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
						printRegistersState();

					}break;
					case 0xA:{
						switch(bH){
							case 0x0:{
								printf("%04x - DEC\n", pc);
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
								
								setFlagsADD(*Rd.ptr, -((int32_t)*Rs.ptr), 32);

								*Rd.ptr -= *Rs.ptr;
								printf("%04x - SUB.l ER%d, ER%d\n", pc, Rs.idx,  Rd.idx); 
								printRegistersState();
							}break;

						}
					}break;
					case 0xB:{
						switch(bH){
							case 0x0:{ // SUBS #1, ERd
								struct RegRef32 Rd = getRegRef32(bL);

								*Rd.ptr -= 1;
								printf("%04x - SUBS #1, ER%d\n", pc, Rd.idx); 
								printRegistersState();
							}break;
							case 0x8:{ // SUBS #2, ERd
								struct RegRef32 Rd = getRegRef32(bL);

								*Rd.ptr -= 2;
								printf("%04x - SUBS #2, ER%d\n", pc, Rd.idx); 
								printRegistersState();
							}break;
							case 0x9:{ // SUBS #4, ERd
								struct RegRef32 Rd = getRegRef32(bL);

								*Rd.ptr -= 4;
								printf("%04x - SUBS #4, ER%d\n", pc, Rd.idx); 
								printRegistersState();
							}break;
							case 0x5:
							case 0x7:
							case 0xD:
							case 0xF:{
								printf("%04x - DEC\n", pc);
							}break;
						}
					}break;
					case 0xC:{ // CMP.b Rs, Rd
						struct RegRef8 Rs = getRegRef8(bH);
						struct RegRef8 Rd = getRegRef8(bL);

						setFlagsADD(*Rd.ptr, -((int8_t)*Rs.ptr), 8);

						printf("%04x - SUB.b R%d%c,R%d%c\n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx, Rd.loOrHiReg); 
						printRegistersState();

					}break;
					case 0xD:{ // CMP.W Rs, Rd
						struct RegRef16 Rs = getRegRef16(bH);
						struct RegRef16 Rd = getRegRef16(bL);
						setFlagsADD(*Rd.ptr, -((int16_t)*Rs.ptr), 16);

						printf("%04x - CMP.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
						printRegistersState();
					}break;
					case 0xE:{
						printf("%04x - SUBX\n", pc);
					}break;
					case 0xF:{
						switch(bH){
							case 0x0:{
								printf("%04x - DAS\n", pc);
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

								setFlagsADD(*Rd.ptr, -((int32_t)*Rs.ptr), 32);

								printf("%04x - CMP.l ER%d, ER%d\n", pc, Rs.idx,  Rd.idx); 
								printRegistersState();
							}break;
						}
					}break;
				}
			}break;

			case 0x2:{ // MOV.B @aa:8, Rd
				uint32_t address = (b & 0x000000FF) | 0x00FFFF00; // Upper 16 bits assumed to be 1
				uint8_t value = getMemory8(address);

				struct RegRef8 Rd = getRegRef8(aL);
				setFlagsMOV(value, 8);
				*Rd.ptr = value;

				printf("%04x - MOV.b @%x:8, R%d%c\n", pc, address, Rd.idx, Rd.loOrHiReg); 
				printRegistersState();


			}break;
			case 0x3:{ // MOV.B Rs, @aa:8 
				uint32_t address = (b & 0x000000FF) | 0x00FFFF00; // Upper 16 bits assumed to be 1

				struct RegRef8 Rs = getRegRef8(aL);
				uint8_t value = *Rs.ptr;
				setFlagsMOV(value, 8);
				setMemory8(address, value);

				printf("%04x - MOV.b R%d%c,@%x:8 \n", pc, Rs.idx, Rs.loOrHiReg, address); 
				printMemory(address, 1);
				printRegistersState();



			}break;
			case 0x4:{
				int8_t disp = b;

				switch(aL){
					case 0x0:{ // BRA d:8
						printf("%04x - BRA %d:8\n", pc, disp);
						pc += disp; 
					}break;
					case 0x1:{ // Unused in the ROM
						printf("%04x - BRN %d:8\n", pc, disp);
					}break;
					case 0x2:{
						printf("%04x - BHI %d:8\n", pc, disp);
						if(!(flags.C | flags.Z)){
							pc += disp;
						}
					}break;
					case 0x3:{
						printf("%04x - BLS %d:8\n", pc, disp);
						if((flags.C | flags.Z)){
							pc += disp;
						}
					}break;
					case 0x4:{
						printf("%04x - BCC %d:8\n", pc, disp);
						if(!(flags.C)){
							pc += disp;
						}
					}break;
					case 0x5:{
						printf("%04x - BCS %d:8\n", pc, disp);
						if(flags.C){
							pc += disp;
						}
					}break;
					case 0x6:{
						printf("%04x - BNE %d:8\n", pc, disp);
						if(!(flags.Z)){
							pc += disp;
						}
					}break;
					case 0x7:{
						printf("%04x - BEQ %d:8\n", pc, disp);
						if(flags.Z){
							pc += disp;
						}
					}break;
					case 0x8:{
						printf("%04x - BVC %d:8\n", pc, disp);
						if(!(flags.V)){
							pc += disp;
						}
					}break;
					case 0x9:{
						printf("%04x - BVS %d:8\n", pc, disp);
						if(flags.V){
							pc += disp;
						}
					}break;
					case 0xA:{
						printf("%04x - BPL %d:8\n", pc, disp);
						if(!(flags.N)){
							pc += disp;
						}
					}break;
					case 0xB:{
						printf("%04x - BMI %d:8\n", pc, disp);
						if(flags.N){
							pc += disp;
						}
					}break;
					case 0xC:{
						printf("%04x - BGE %d:8\n", pc, disp);
						if(!(flags.N ^ flags.V)){
							pc += disp;
						}
					}break;
					case 0xD:{
						printf("%04x - BLT %d:8\n", pc, disp);
						if((flags.N ^ flags.V)){
							pc += disp;
						}

					}break;
					case 0xE:{
						printf("%04x - BGT %d:8\n", pc, disp);
						if(!(flags.Z | (flags.N ^ flags.V))){
							pc += disp;
						}

					}break;
					case 0xF:{
						printf("%04x - BLE %d:8\n", pc, disp);
						if(flags.Z | (flags.N ^ flags.V)){
							pc += disp;
						}
					}break;
				}
			}break;
			case 0x5:{
				switch(aL){
					case 0x0:
					case 0x2:{
						printf("%04x - MULXU\n", pc);
					}break;
					case 0x1:
					case 0x3:{
						printf("%04x - DIVXU\n", pc);
					}break;
					case 0x4:{ // RTS
						printf("%04x - RTS\n", pc);
						pc = getMemory16(*SP) - 2;
						*SP += 1;
						printRegistersState();
					}break;
					case 0x5:{ // BSR d:8
						int8_t disp = b;
						printf("%04x - BSR @%d:8\n", pc, disp);
						*SP -= 2;
						setMemory16(*SP, pc + 2);

						pc = pc + disp; // No need to increment pc by 2 since we're always doing that at the end of the loop

						printMemory(*SP, 2);
						printRegistersState();
					}break;
					case 0xC:{ // BSR d:16
						int16_t disp = cd; 
						printf("%04x - BSR @%d:16\n", pc, disp);
						*SP -= 2;
						setMemory16(*SP, pc + 4);

						pc = pc + 2 + disp; // Increment pc by 2 since we need the next instruction (pc + 4) and we're already adding 2 at the end of the loop.

						printMemory(*SP, 2);
						printRegistersState();
					}break;
					case 0x6:{
						printf("%04x - RTE\n", pc);
					}break;
					case 0x7:{
						printf("%04x - TRAPA\n", pc);
					}break;
					case 0x8:{
						int16_t disp = cd;
						switch(bH){
							case 0x0:{
								printf("%04x - BRA %d:16\n", pc, disp);
								pc += 2 + disp;
							}break;
							case 0x1:{ // Unused in the ROM
								printf("%04x - BRN %d:16\n", pc, disp);
							}break;
							case 0x2:{
								printf("%04x - BHI %d:16\n", pc, disp);
								if(!(flags.C | flags.Z)){
									pc += 2 + disp;
								}else{
									pc += 2;
								}
							}break;
							case 0x3:{
								printf("%04x - BLS %d:16\n", pc, disp);
								if(flags.C | flags.Z){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0x4:{
								printf("%04x - BCC %d:16\n", pc, disp);
								if(!flags.C){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0x5:{
								printf("%04x - BCS %d:16\n", pc, disp);
								if(flags.C){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0x6:{
								printf("%04x - BNE %d:16\n", pc, disp);
								if(!flags.Z){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0x7:{
								printf("%04x - BEQ %d:16\n", pc, disp);
								if(flags.Z){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0x8:{
								printf("%04x - BVC %d:16\n", pc, disp);
								if(!flags.V){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0x9:{
								printf("%04x - BVS %d:16\n", pc, disp);
								if(flags.V){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0xA:{
								printf("%04x - BPL %d:16\n", pc, disp);
								if(!flags.N){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0xB:{
								printf("%04x - BMI %d:16\n", pc, disp);
								if(flags.N){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0xC:{
								printf("%04x - BGE %d:16\n", pc, disp);
								if(!(flags.N ^ flags.V)){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0xD:{
								printf("%04x - BLT %d:16\n", pc, disp);
								if(flags.N ^ flags.V){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0xE:{
								printf("%04x - BGT %d:16\n", pc, disp);
								if(!(flags.Z | (flags.N ^ flags.V))){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
							case 0xF:{
								printf("%04x - BLE %d:16\n", pc, disp);
								if((flags.Z | (flags.N ^ flags.V))){
									pc += 2 + disp;
								}else{
									pc += 2;
								}

							}break;
						}				
					}break;
					case 0x9:{ // JMP @ERn
						struct RegRef32 Er = getRegRef32(bH);
						printf("%04x - JMP @ER%d\n", pc, Er.idx);
						pc = *Er.ptr - 2; // Sub 2 cause we're incrementing 2 at the end of the loop
					}break;
					case 0xA:{ // JMP @aa:24
						uint32_t address = (b << 16) | cd;
						printf("%04x - JMP @0x%04x:24\n", pc, address);
						pc = address - 2; // Sub 2 cause we're incrementing 2 at the end of the loop
					}break;
					case 0xB:{ // JMP @@aa:8 - UNUSED IN THE ROM, left unimplemented.
						printf("%04x - ????\n", pc);
					}break;
					case 0xD:{ // JSR @ERn
						struct RegRef32 Er = getRegRef32(bH);
						
						*SP -= 2;
						setMemory16(*SP, pc + 2);

						printf("%04x - JSR @ER%d\n", pc, Er.idx);
						pc = *Er.ptr - 2; // Sub 2 cause we're incrementing 2 at the end of the loop

						printMemory(*SP, 2);
						printRegistersState();
					}break;
					case 0xE:{ // JSR @aa:24
						uint32_t address = (b << 16) | cd;

						*SP -= 2;
						setMemory16(*SP, pc + 4);

						printf("%04x - JSR @0x%04x:24\n", pc, address);
						pc = address - 2; // Sub 2 cause we're incrementing 2 at the end of the loop

						printMemory(*SP, 2);
						printRegistersState();

					}break;
					case 0xF:{ // JSR @@aa:24 - UNUSED IN THE ROM, left unimplemented.
						printf("%04x - ????\n", pc);
					}break;

				}
			}break;
			case 0x6:{
				switch(aL){
					case 0x0:{ // BSET Rn, Rd

						struct RegRef8 Rd = getRegRef8(bL);		
						struct RegRef8 Rn = getRegRef8(bH);		
						int bitToSet = *Rn.ptr;

						*Rd.ptr = *Rd.ptr | (1 << bitToSet);

						printf("%04x - BSET r%d%c, r%d%c\n", pc, Rn.idx, Rn.loOrHiReg, Rd.idx, Rd.loOrHiReg);
						printRegistersState();
					}break;
					case 0x1:{
						printf("%04x - BNOT\n", pc);
					}break;
					case 0x2:{ // BCLR Rn, Rd
						struct RegRef8 Rd = getRegRef8(bL);		
						struct RegRef8 Rn = getRegRef8(bH);		
						int bitToClear = *Rn.ptr;

						*Rd.ptr = *Rd.ptr & ~(1 << bitToClear);

						printf("%04x - BCLR r%d%c, r%d%c\n", pc, Rn.idx, Rn.loOrHiReg, Rd.idx, Rd.loOrHiReg);
						printRegistersState();
					}break;
					case 0x3:{
						printf("%04x - BTST\n", pc);
					}break;
					case 0x4:{
						printf("%04x - OR\n", pc);
					}break;
					case 0x5:{
						printf("%04x - XOR\n", pc);
					}break;
					case 0x6:{// AND.w Rs, Rd
						struct RegRef16 Rd = getRegRef16(bL);
						struct RegRef16 Rs = getRegRef16(bH);
						uint16_t newValue = *Rs.ptr & *Rd.ptr;
						setFlagsMOV(newValue, 16);
						*Rd.ptr = newValue;

						printf("%04x - AND.w %c%d,%c%d\n", pc, Rs.loOrHiReg, Rs.idx, Rd.loOrHiReg,  Rd.idx); 
						printRegistersState();
					}break;
					case 0x7:{
						uint8_t mostSignificantBit = bH >> 7;
						if (mostSignificantBit == 0x1){
							printf("%04x - BIST\n", pc);
						}else{
							printf("%04x - BST\n", pc);
						}					
					}break;
					case 0x8:{ // MOV.B @ERs, Rd
						struct RegRef32 Rs = getRegRef32(bH);
						struct RegRef8 Rd = getRegRef8(bL);

						uint8_t value = getMemory8(*Rs.ptr);

						setFlagsMOV(value, 8);
						*Rd.ptr = value;

						printf("%04x - MOV.b @ER%d, R%d%c\n", pc, Rs.idx, Rd.idx, Rd.loOrHiReg); 
						printRegistersState();


					}break;
					case 0x9:{ // MOV.W @ERs, Rd
						struct RegRef32 Rs = getRegRef32(bH);
						struct RegRef16 Rd = getRegRef16(bL);
						uint16_t value = getMemory16(*Rs.ptr);

						setFlagsMOV(value, 16);
						*Rd.ptr = value;

						printf("%04x - MOV.w @ER%d, %c%d\n", pc, Rs.idx, Rd.loOrHiReg, Rd.idx ); 
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

								printf("%04x - MOV.b @%x:16, R%d%c\n", pc, address, Rd.idx, Rd.loOrHiReg); 
								printRegistersState();

							}break;

							case 0x8:{ // MOV.B Rs, @aa:16 
								uint32_t address = (cd & 0x0000FFFF) | 0x00FF0000; // Upper 16 bits assumed to be 1

								struct RegRef8 Rs = getRegRef8(bL);

								uint8_t value = *Rs.ptr;
								setFlagsMOV(value, 8);
								setMemory8(address, value);

								printf("%04x - MOV.b R%d%c,@%x:16 \n", pc, Rs.idx, Rs.loOrHiReg, address); 
								printMemory(address, 1);
								printRegistersState();

							}break;
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

								printf("%04x - MOV.w @%x:16, %c%d\n", pc, address, Rd.loOrHiReg, Rd.idx); 
								printRegistersState();

							}break;

							case 0x8:{ // MOV.B Rs, @aa:16 
								uint32_t address = (cd & 0x0000FFFF) | 0x00FF0000; // Upper 16 bits assumed to be 1

								struct RegRef16 Rs = getRegRef16(bL);

								uint16_t value = *Rs.ptr;
								setFlagsMOV(value, 16);
								setMemory16(address, value);

								printf("%04x - MOV.w %c%d,@%x:16 \n", pc, Rs.loOrHiReg, Rs.idx, address); 
								printMemory(address, 2);
								printRegistersState();

							}break;
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

							printf("%04x - MOV.b @ER%d+, R%d%c\n", pc, Rs.idx, Rd.idx, Rd.loOrHiReg); 

						} else{
							struct RegRef32 Rd = getRegRef32(bH);
							struct RegRef8 Rs = getRegRef8(bL);

							*Rd.ptr -= 1;


							uint8_t value = *Rs.ptr;
							setMemory8(*Rs.ptr, value);
							setFlagsMOV(value, 8);

							printf("%04x - MOV.b R%d%c, @-ER%d, \n", pc, Rs.idx, Rs.loOrHiReg, Rd.idx); 
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

							printf("%04x - MOV.w @ER%d+, %c%d\n", pc, Rs.idx, Rd.loOrHiReg, Rd.idx); 

						} else{
							struct RegRef32 Rd = getRegRef32(bH);
							struct RegRef16 Rs = getRegRef16(bL);

							*Rd.ptr -= 2;

							uint16_t value = *Rs.ptr;
							setMemory16(*Rd.ptr, value);
							setFlagsMOV(value, 16);

							printf("%04x - MOV.w %c%d, @-ER%d, \n", pc, Rs.loOrHiReg, Rs.idx, Rd.idx); 
							printMemory(*Rd.ptr, 2);

						}
						printRegistersState();



					} break;
					case 0xE:{ // MOV.B @(d:16, ERs), Rd
						struct RegRef8 Rd = getRegRef8(bL);
						struct RegRef32 Rs = getRegRef32(bH);

						uint16_t disp = cd;
						bool msbDisp = disp & 0x8000;
						uint32_t signExtendedDisp = msbDisp ? (0xFFFF0000 & disp) : disp;

						if (!(bH & 0b1000)){ // From memory

						uint8_t value = getMemory8(*Rs.ptr + signExtendedDisp);
							*Rd.ptr = value;
							setFlagsMOV(value, 8);

							printf("%04x - MOV.b @(%d:16, ER%d), R%d%c\n", pc, disp, Rs.idx, Rd.idx, Rd.loOrHiReg); 

						} else{ // To memory
							// NOTE: asumming the contents of the 8 bit register are copied into the first byte pointed by ERd, though that might not be the case 
							// since memory is accesed in 16 bits ? Will need to test
							uint8_t value = *Rd.ptr;
							setFlagsMOV(value, 8);
							setMemory8(*Rs.ptr + signExtendedDisp, value);
							printf("%04x - MOV.b R%d%c, @(%d:16, ER%d), \n", pc, Rd.idx, Rd.loOrHiReg, disp, Rs.idx); 
							printMemory(*Rs.ptr + signExtendedDisp, 1);
						}
						printRegistersState();
						pc+=2;


					}break;
					case 0xF:{ // MOV.W @(d:16, ERs), Rd
						struct RegRef16 Rd = getRegRef16(bL);
						struct RegRef32 Rs = getRegRef32(bH);

						uint16_t disp = cd;
						bool msbDisp = disp & 0x8000;
						uint32_t signExtendedDisp = msbDisp ? (0xFFFF0000 & disp) : disp;

						if (!(bH & 0b1000)){ // From memory
							uint16_t value = getMemory16(*Rs.ptr + signExtendedDisp);
							*Rd.ptr = value;
							setFlagsMOV(value, 16);

							printf("%04x - MOV.w @(%d:16, ER%d), %c%d\n", pc, disp, Rs.idx, Rd.loOrHiReg, Rd.idx); 

						} else{ // To memory
							uint16_t value = *Rd.ptr;
							setFlagsMOV(value, 16);
							setMemory16(*Rs.ptr + signExtendedDisp, value);
							printf("%04x - MOV.w %c%d, @(%d:16, ER%d), \n", pc, Rd.loOrHiReg, Rd.idx, disp, Rs.idx); 
							printMemory(*Rs.ptr + signExtendedDisp, 2);
						}
						printRegistersState();
						pc+=2;


					}break;
				}
			}break;
			case 0x7:{
				uint8_t mostSignificantBit = bH >> 7;
				switch(aL){
					case 0x0:{ // BSET #xx:3, Rd

						struct RegRef8 Rd = getRegRef8(bL);		

						int bitToSet = bH;

						*Rd.ptr = *Rd.ptr | (1 << bitToSet);

						printf("%04x - BSET #%d, r%d%c\n", pc, bitToSet, Rd.idx, Rd.loOrHiReg);
						printRegistersState();
					}break;
					case 0x1:{
						printf("%04x - BNOT\n", pc);
					}break;
					case 0x2:{ // BCLR #xx:3, Rd

						struct RegRef8 Rd = getRegRef8(bL);		

						int bitToClear = bH;

						*Rd.ptr = *Rd.ptr & ~(1 << bitToClear);

						printf("%04x - BCLR #%d, r%d%c\n", pc, bitToClear, Rd.idx, Rd.loOrHiReg);
						printRegistersState();
					}break;
					case 0x3:{
						printf("%04x - BTST\n", pc);
					}break;
					case 0x4:{
						if (mostSignificantBit == 0x1){
							printf("%04x - BIOR\n", pc);
						}else{
							printf("%04x - BOR\n", pc);
						}
					}break;
					case 0x5:{
						if (mostSignificantBit == 0x1){
							printf("%04x - BIXOR\n", pc);
						}else{
							printf("%04x - BXOR\n", pc);
						}
					}break;
					case 0x6:{
						if (mostSignificantBit == 0x1){
							printf("%04x - BIAND\n", pc);
						}else{
							printf("%04x - BAND\n", pc);
						}
					}break;
					case 0x7:{
						if (mostSignificantBit == 0x1){
							printf("%04x - BILD\n", pc);
						}else{ // BLD #xx:3, Rd
							struct RegRef8 Rd = getRegRef8(bL);		

							int bitToLoad = bH;

							flags.C = *Rd.ptr & (1 << bitToLoad);

							printf("%04x - BLD #%d, r%d%c\n", pc, bitToLoad, Rd.idx, Rd.loOrHiReg);
							printRegistersState();
						}					
					}break;
					case 0x8:{
						printf("%04x - MOV\n", pc);
					}break;
					case 0x9:{ 
						switch(bH){ // TODO: see if the next isntructions like CMP will use the same logic and abstaact it
							case 0x0:{ // MOV.w #xx:16, Rd
								struct RegRef16 Rd = getRegRef16(bL);

								setFlagsMOV(cd, 16);
								*Rd.ptr = cd;

								printf("%04x - MOV.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
								printRegistersState();
								pc+=2;
							}break;
							case 0x1:{ // ADD.w #xx:16, Rd
								struct RegRef16 Rd = getRegRef16(bL);

								setFlagsADD(*Rd.ptr, cd, 16);
								*Rd.ptr += cd;

								printf("%04x - ADD.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
								printRegistersState();
								pc+=2;
							}break;
							case 0x2:{ // CMP.w #xx:16, Rd
								struct RegRef16 Rd = getRegRef16(bL);

								setFlagsADD(*Rd.ptr, -((int16_t)cd), 16);

								printf("%04x - CMP.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
								printRegistersState();
								pc+=2;
							}break;
							case 0x3:{ // SUB.w #xx:16, Rd
								struct RegRef16 Rd = getRegRef16(bL);

								setFlagsADD(*Rd.ptr, -((int16_t)cd), 16);
								*Rd.ptr -= cd;

								printf("%04x - SUB.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
								printRegistersState();
								pc+=2;
							}break;
							case 0x4:{
								printf("%04x - OR\n", pc);
							}break;
							case 0x5:{
								printf("%04x - XOR\n", pc);
							}break;
							case 0x6:{ // AND.w #xx:16, Rd
								struct RegRef16 Rd = getRegRef16(bL);
								uint16_t value = cd;
								uint16_t newValue = cd & *Rd.ptr;
								setFlagsMOV(newValue, 16);
								*Rd.ptr = newValue;

								printf("%04x - AND.w 0x%x,%c%d\n", pc, cd, Rd.loOrHiReg,  Rd.idx); 
								printRegistersState();
								pc+=2;
							}break;
						}
					}break;

					case 0xA:{ 
						switch(bH){
							case 0x0:{ // MOV.l #xx:32, ERd
								struct RegRef32 Rd = getRegRef32(bL);

								setFlagsMOV(cdef, 32);

								*Rd.ptr = cdef;
								printf("%04x - MOV.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
								printRegistersState();
								pc+=4;
							}break;
							case 0x1:{ // ADD.l #xx:32, ERd
								struct RegRef32 Rd = getRegRef32(bL);

								setFlagsADD(*Rd.ptr, cdef, 32);

								*Rd.ptr += cdef;
								printf("%04x - ADD.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
								printRegistersState();
								pc+=4;

							}break;
							case 0x2:{
								// CMP.l #xx:32, ERd
								struct RegRef32 Rd = getRegRef32(bL);

								setFlagsADD(*Rd.ptr, -((int32_t)cdef), 32);

								printf("%04x - CMP.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
								printRegistersState();
								pc+=4;

							}break;
							case 0x3:{ // SUB.l #xx:32, ERd
								struct RegRef32 Rd = getRegRef32(bL);
								setFlagsADD(*Rd.ptr, -((int32_t)cdef), 32);

								*Rd.ptr -= cdef;
								printf("%04x - SUB.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
								printRegistersState();
								pc+=4;
							}break;
							case 0x4:{
								printf("%04x - OR\n", pc);
							}break;
							case 0x5:{
								printf("%04x - XOR\n", pc);
							}break;
							case 0x6:{ // AND.l #xx:32, ERd
								struct RegRef32 Rd = getRegRef32(bL);

								uint32_t newValue = cdef & *Rd.ptr;
								setFlagsMOV(newValue, 32);

								*Rd.ptr = newValue;
								printf("%04x - AND.l 0x%04x, ER%d\n", pc, cdef,  Rd.idx); 
								printRegistersState();
								pc+=4;
							}break;
						}
					}break;
					case 0xB:{
						printf("%04x - EEPMOV\n", pc);
					}break;
					case 0xC:{
						uint8_t mostSignificantBit = dH >> 7;
						switch(c){
							case 0x77:{
								// BLD #xx:3, @ERd
								struct RegRef32 Rd = getRegRef32(bH);		
								int bitToLoad = dH;
								printf("%04x - BLD #%d, @ER%d\n", pc, bitToLoad, Rd.idx);
								flags.C = getMemory8(*Rd.ptr) & (1 << bitToLoad);
								printRegistersState();
								pc+=2;

							}break;

						}



					} break;
					case 0xE:{
						pc+=2; // TODO: Probably wrong
						// Here bH is the "register designation field" dont know what that is, so ignorign it for now
						// togetherwith bL it can also be "aa" which is the "absolute address field"
						if (cH == 0x6){
							switch(cL){
								case 0x3:{
									printf("%04x - BTST\n", pc);
								}break;
							}
						}else if (cH == 0x7){
							uint8_t mostSignificantBit = dH >> 7;
							switch(cL){
								case 0x3:{
									printf("%04x - BTST\n", pc);
								}break;
								case 0x4:{
									if (mostSignificantBit == 0x1){
										printf("%04x - BIOR\n", pc);
									}else{
										printf("%04x - BOR\n", pc);
									}
								}break;
								case 0x5:{
									if (mostSignificantBit == 0x1){
										printf("%04x - BXOR\n", pc);
									}else{
										printf("%04x - BIXOR\n", pc);
									}
								}break;
								case 0x6:{
									if (mostSignificantBit == 0x1){
										printf("%04x - BIAND\n", pc);
									}else{
										printf("%04x - BAND\n", pc);
									}
								}break;
								case 0x7:{
									if (mostSignificantBit == 0x1){
										printf("%04x - BILD\n", pc);
									}else{ // BLD #xx:3, @ERd
										int bitToLoad = dH;
										uint32_t address = (0x00FFFF00) | b;
										printf("%04x - BLD #%d, @0x%x:8\n", pc, bitToLoad, address);
										flags.C =  getMemory8(address) & (1 << bitToLoad);
									}
								}break;
							}


						}
					}break;
					case 0xD:{
						struct RegRef32 Rd = getRegRef32(bH);		
						switch(c){
							case 0x70:{ // BSET #xx:3, @ERd
								int bitToSet = dH;
								printf("%04x - BSET #%d, @ER%d\n", pc, bitToSet, Rd.idx);
								setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) | (1 << bitToSet));
							}break;
							case 0x60:{ // BSET Rn, @ERd
								struct RegRef8 Rn = getRegRef8(dH);		
								int bitToSet = *Rn.ptr;
								printf("%04x - BSET r%d%c, @ER%d\n", pc, Rn.idx, Rn.loOrHiReg, Rd.idx);
								setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) | (1 << bitToSet));
							}break;
							case 0x72:{ // BCLR #xx:3, @ERd
								int bitToClear = dH;
								printf("%04x - BCLR #%d, @ER%d\n", pc, bitToClear, Rd.idx);
								setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) & ~(1 << bitToClear));
							}break;
							case 0x62:{ // BCLR Rn, @ERd
								struct RegRef8 Rn = getRegRef8(dH);		
								int bitToClear = *Rn.ptr;
								printf("%04x - BCLR r%d%c, @ER%d\n", pc, Rn.idx, Rn.loOrHiReg, Rd.idx);
								setMemory8(*Rd.ptr, getMemory8(*Rd.ptr) & ~(1 << bitToClear));
							}break;
						}
						printMemory(*Rd.ptr, 1);
						printRegistersState();
						pc+=2;
					}break;
					case 0xF:{
						uint32_t address = (0x00FFFF00) | b;
						switch(c){
							case 0x70:{ // BSET #xx:3, @aa:8
								int bitToSet = dH;
								printf("%04x - BSET #%d, @0x%x:8\n", pc, bitToSet, address);
								setMemory8(address, getMemory8(address) | (1 << bitToSet));
							}break;
							case 0x60:{ // BSET Rn, @aa:8
								struct RegRef8 Rn = getRegRef8(dH);		
								int bitToSet = *Rn.ptr;
								printf("%04x - BSET r%d%c, @0x%x:8\n", pc, Rn.idx, Rn.loOrHiReg, address);
								setMemory8(address, getMemory8(address) | (1 << bitToSet));
							}break;
							case 0x72:{ // BCLR #xx:3, @aa:8
								int bitToClear = dH;
								printf("%04x - BCLR #%d, @0x%x:8\n", pc, bitToClear, address);
								setMemory8(address, getMemory8(address) & ~(1 << bitToClear));
							}break;
							case 0x62:{ // BCLR Rn, @aa:8
								struct RegRef8 Rn = getRegRef8(dH);		
								int bitToClear = *Rn.ptr;
								printf("%04x - BCLR r%d%c, @0x%x:8\n", pc, Rn.idx, Rn.loOrHiReg, address);
								setMemory8(address, getMemory8(address) & ~(1 << bitToClear));
							}break;
						}
						printMemory(address, 1);
						pc+=2;
					} break;
						/*
						// Here bH is the "register designation field" dont know what that is, so ignorign it for now
						// togetherwith bL it can also be "aa" which is the "absolute address field"
						if (cH == 0x6){
							uint8_t mostSignificantBit = dH >> 7;

							if (cL == 0x7){
								if (mostSignificantBit == 0x1){
									printf("%04x - BIST\n", pc);
								}else{
									printf("%04x - BST\n", pc);
							}						}
						}
						if (cH == 0x6 || cH == 0x7){
							switch(cL){
								case 0:{
									printf("%04x - BSET\n", pc);
								}break;
								case 1:{
									printf("%04x - BNOT\n", pc);
								}break;
								case 2:{
									printf("%04x - BCLR\n", pc);
								}break;

							}
						}
						pc+=2;
					*/
				}
			}break;
			case 0x8:{ // ADD.B #xx:8, Rd
				struct RegRef8 Rd = getRegRef8(aL);

				uint8_t value = (bH << 4) | bL;

				setFlagsADD(*Rd.ptr, value, 8);
				*Rd.ptr += value;

				printf("%04x - ADD.b 0x%x,R%d%c\n", pc, value, Rd.idx, Rd.loOrHiReg); //Note: Dmitry's dissasembler sometimes outputs address in decimal (0xdd) not sure why
				printRegistersState();
			}break;
			case 0x9:{
				printf("%04x - ADDX\n", pc);
			}break;

			case 0xA:{ // CMP.B #xx:8, Rd
				struct RegRef8 Rd = getRegRef8(aL);

				uint8_t value = (bH << 4) | bL;

				setFlagsADD(*Rd.ptr, -((int8_t)value), 8);

				printf("%04x - CMP.b 0x%x,R%d%c\n", pc, value, Rd.idx, Rd.loOrHiReg); 
				printRegistersState();

			}break;

			case 0xB:{
				printf("%04x - SUBX\n", pc);
			}break;

			case 0xC:{
				printf("%04x - OR\n", pc);
			}break;

			case 0xD:{
				printf("%04x - XOR\n", pc);
			}break;

			case 0xE:{ // AND #xx:8, Rd
				struct RegRef8 Rd = getRegRef8(aL);

				uint8_t value = b;
				uint8_t newValue = value & *Rd.ptr;
				setFlagsMOV(newValue, 8);
				*Rd.ptr = newValue;

				printf("%04x - AND.b 0x%x,R%d%c\n", pc, value, Rd.idx, Rd.loOrHiReg); 
				printRegistersState();
			}break;

			case 0xF:{ // MOV.B #xx:8, Rd
				struct RegRef8 Rd = getRegRef8(aL);

				uint8_t value = b;

				setFlagsMOV(value, 8);
				*Rd.ptr = value;

				printf("%04x - MOV.b 0x%x,R%d%c\n", pc, value, Rd.idx, Rd.loOrHiReg); 
				printRegistersState();
			}break;

			default:{
				printf("???\n");
			} break;
		}
		pc+=2;
		if (mode == RUN){
			continue;
		} else if(mode == STEP){
			if (instructionsToStep == 0){
				scanf(" %d", &instructionsToStep);
			}
			instructionsToStep--;
		}
	}
	fclose(romFile);
}
