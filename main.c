#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
int main(){

	FILE* input = fopen("roms/rom.bin","r");
	if(!input){
		printf("Can't find rom");
	}

	fseek (input , 0 , SEEK_END);
	int size = ftell (input);
	rewind (input);

	uint16_t* instrByteArray = malloc(size);
	fread(instrByteArray,2,size/2 ,input);
	int byteIdx = 0;
	while(byteIdx != size){
		uint16_t instruction = *instrByteArray; // 0xbHbL aHaL ie 0x086A 
		uint8_t aH = (instruction >> 4) & 0xF; //0b BBBB bbbb AAAA aaaa
		uint8_t aL = instruction & 0xF;
		uint8_t bH = (instruction >> 12) & 0x000F;
		uint8_t bL = (instruction >> 8) & 0x000F;

		switch(aH){
			case 0x0:{
				switch(aL){
					case 0x0:{
				       		printf("%x - NOP\n", byteIdx);
					}break;
					case 0x1:{
						switch(bH){
							case 0x0:{
								printf("%x - MOV\n", byteIdx);
							}break;
							case 0x4:{
								printf("%x - LDC/STC\n", byteIdx);
							}break;
							case 0x8:{
								printf("%x - SLEEP\n", byteIdx);
							}break;
							case 0xC:{
								instrByteArray+=2;
								uint16_t instructionExtension = *instrByteArray;
								uint8_t cH = (instructionExtension  >> 4) & 0xF; //0b BBBB bbbb AAAA aaaa
								uint8_t cL = instructionExtension & 0xF;
								byteIdx+=2;
								if (bL == 0x0 && cH == 0x5){
									switch(cL){
										case 0x0:
										case 0x2:{
											printf("%x - MULXS\n", byteIdx);
										}break;
									}
								};
							}break;
							case 0xD:{
								instrByteArray+=2;
								uint16_t instructionExtension = *instrByteArray;
								uint8_t cH = (instructionExtension  >> 4) & 0xF; //0b BBBB bbbb AAAA aaaa
								uint8_t cL = instructionExtension & 0xF;
								byteIdx+=2;
								if (bL == 0x0 && cH == 0x5){
									switch(cL){ // TODO replace with if, and see if merging it with C & F makes it more readable
										case 0x1:
										case 0x3:{
											printf("%x - DIVXS\n", byteIdx);
										}break;
									}
								};
							}break;
							case 0xF:{
								instrByteArray+=2;
								uint16_t instructionExtension = *instrByteArray;
								uint8_t cH = (instructionExtension  >> 4) & 0xF; //0b BBBB bbbb AAAA aaaa
								uint8_t cL = instructionExtension & 0xF;
								byteIdx+=2;
								if (bL == 0x0 && cH == 0x6){
									switch(cL){ // TODO replace with if, and see if merging it with C & F makes it more readable
										case 0x4:{
											printf("%x - OR\n", byteIdx);
											  }break;
										case 0x5:{
											printf("%x - XOR\n", byteIdx);
										}break;
										case 0x6:{
											printf("%x - AND\n", byteIdx);
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
					printf("%x - STC\n", byteIdx);
				}break;
				case 0x3:{
					printf("%x - LDC\n", byteIdx);
				}break;
				case 0x4:{
					printf("%x - ORC\n", byteIdx);
				}break;
				case 0x5:{
					printf("%x - XORC\n", byteIdx);
				}break;
				case 0x6:{
					printf("%x - ANDC\n", byteIdx);
				}break;
				case 0x7:{
					printf("%x - LDC\n", byteIdx);
				}break;
				case 0x8:
				case 0x9:{
					printf("%x - ADD\n", byteIdx);
				}break;
				case 0xA:{
					switch(bH){
						case 0x0:{
							printf("%x - INC\n", byteIdx);
						}break;
						case 0x8:
						case 0x9:
						case 0xA:
						case 0xB:
						case 0xC:
						case 0xD:
						case 0xE:
						case 0xF:{
							printf("%x - ADD\n", byteIdx);
						}break;

					}
				}break;







				}
		}break;
		case 0x2:
		case 0x3:{
			printf("%x - MOV.B\n", byteIdx);
		}break;
		case 0x8:{
			printf("%x - ADD\n", byteIdx);
		}break;
		case 0x9:{
			printf("%x - ADDX\n", byteIdx);
		}break;

		case 0xA:{
			printf("%x - CMP\n", byteIdx);
		}break;

		case 0xB:{
			printf("%x - SUBX\n", byteIdx);
		}break;

		case 0xC:{
			printf("%x - OR\n", byteIdx);
		}break;

		case 0xD:{
			printf("%x - XOR\n", byteIdx);
		}break;

		case 0xE:{
			printf("%x - AND\n", byteIdx);
		}break;

		case 0xF:{
			printf("%x - MOV\n", byteIdx);
		}break;

		default:{
			printf("???\n");
		} break;
		}
		instrByteArray+=1;
		byteIdx+=2;
	}
	fclose(input);
}
