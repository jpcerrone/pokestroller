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
	fread(instrByteArray,2,size/2 ,input); //ITS LOADED FINE
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
								instrByteArray+=1;
								uint16_t instructionExtension = *instrByteArray;
								uint8_t cH = (instructionExtension  >> 4) & 0xF; //0b BBBB bbbb AAAA aaaa
								uint8_t cL = instructionExtension & 0xF;
								byteIdx+=2;
								if (bL == 0x0 && cH == 0x6){
									switch(cL){
										case 0x9:
										case 0xB:
										case 0xD:
										case 0xF:{
											uint8_t dH = (instructionExtension >> 12) & 0x000F;
											uint8_t mostSignificantBit = dH >> 7;
											if (mostSignificantBit == 0x1){
												printf("%x - STC\n", byteIdx);
											}else{
												printf("%x - LDC\n", byteIdx);
											}
										}break;
									}
								}
							}break;
							case 0x8:{
								printf("%x - SLEEP\n", byteIdx);
							}break;
							case 0xC:{
								instrByteArray+=1;
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
								instrByteArray+=1;
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
								instrByteArray+=1;
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
				case 0xB:{
					switch(bH){
						case 0x0:
						case 0x8:
						case 0x9:{
							printf("%x - ADDS\n", byteIdx);
						}break;
						case 0x5:
						case 0x7:
						case 0xD:
						case 0xF:{
							printf("%x - INC\n", byteIdx);
						 }break;
					}
				}break;
				case 0xC:
				case 0xD:{
					printf("%x - MOV\n", byteIdx);
				}break;
				case 0xE:{
					printf("%x - ADDX\n", byteIdx);
				}break;
				case 0xF:{
					switch(bH){
						case 0x0:{
							printf("%x - DAA\n", byteIdx);
						}break;
						case 0x8:
						case 0x9:
						case 0xA:
						case 0xB:
						case 0xC:
						case 0xD:
						case 0xE:
						case 0xF:{
							printf("%x - MOV\n", byteIdx);
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
							printf("%x - SHLL\n", byteIdx);
						}break;
						case 0x8:
						case 0x9:
						case 0xB:{
							printf("%x - SHAL\n", byteIdx);
						 }break;
					}
				}break;
				case 0x1:{
					switch(bH){
						case 0x0:
						case 0x1:
						case 0x3:{
							printf("%x - SHLR\n", byteIdx);
						}break;
						case 0x8:
						case 0x9:
						case 0xB:{
							printf("%x - SHAR\n", byteIdx);
						 }break;
					}
				}break;
				case 0x2:{
					switch(bH){
						case 0x0:
						case 0x1:
						case 0x3:{
							printf("%x - ROTXL\n", byteIdx);
						}break;
						case 0x8:
						case 0x9:
						case 0xB:{
							printf("%x - ROTL\n", byteIdx);
						 }break;
					}
				}break;
				case 0x3:{
					switch(bH){
						case 0x0:
						case 0x1:
						case 0x3:{
							printf("%x - ROTXR\n", byteIdx);
						}break;
						case 0x8:
						case 0x9:
						case 0xB:{
							printf("%x - ROTR\n", byteIdx);
						 }break;
					}
				}break;
				case 0x4:{
						 printf("%x - OR.B\n", byteIdx);
					 }break;
				case 0x5:{
						 printf("%x - XOR.B\n", byteIdx);
					 }break;
				case 0x6:{
						 printf("%x - AND.B\n", byteIdx);
					 }break;
				case 0x7:{
					switch(bH){
						case 0x0:
						case 0x1:
						case 0x3:{
							printf("%x - NOT\n", byteIdx);
						}break;
						case 0x5:
						case 0x7:{
							printf("%x - EXTU\n", byteIdx);
						 }break;
						case 0x8:
						case 0x9:
						case 0xB:{
							printf("%x - NEG\n", byteIdx);
						}break;
						case 0xD:
						case 0xF:{
							printf("%x - EXTS\n", byteIdx);
						 }break;

					}
				}break;
				case 0x8:
				case 0x9:{
					printf("%x - SUB\n", byteIdx);
				}break;
				case 0xA:{
					switch(bH){
						case 0x0:{
							printf("%x - DEC\n", byteIdx);
						}break;
						case 0x8:
						case 0x9:
						case 0xA:
						case 0xB:
						case 0xC:
						case 0xD:
						case 0xE:
						case 0xF:{
							printf("%x - SUB\n", byteIdx);
						}break;

					}
				}break;
				case 0xB:{
					switch(bH){
						case 0x0:
						case 0x8:
						case 0x9:{
							printf("%x - SUBS\n", byteIdx);
						}break;
						case 0x5:
						case 0x7:
						case 0xD:
						case 0xF:{
							printf("%x - DEC\n", byteIdx);
						 }break;
					}
				}break;
				case 0xC:
				case 0xD:{
					printf("%x - CMP\n", byteIdx);
				}break;
				case 0xE:{
					printf("%x - SUBX\n", byteIdx);
				}break;
				case 0xF:{
					switch(bH){
						case 0x0:{
							printf("%x - DAS\n", byteIdx);
						}break;
						case 0x8:
						case 0x9:
						case 0xA:
						case 0xB:
						case 0xC:
						case 0xD:
						case 0xE:
						case 0xF:{
							printf("%x - CMP\n", byteIdx);
						 }break;
					}
				}break;



			}
		}break;

		case 0x2:
		case 0x3:{
			printf("%x - MOV.B\n", byteIdx);
		}break;
		case 0x4:{
			switch(aL){
				case 0x0:{
					printf("%x - BRA\n", byteIdx);
				}break;
				case 0x1:{
					printf("%x - BRN\n", byteIdx);
				}break;
				case 0x2:{
					printf("%x - BHI\n", byteIdx);
				}break;
				case 0x3:{
					printf("%x - BLS\n", byteIdx);
				}break;
				case 0x4:{
					printf("%x - BCC\n", byteIdx);
				}break;
				case 0x5:{
					printf("%x - BCS\n", byteIdx);
				}break;
				case 0x6:{
					printf("%x - BNE\n", byteIdx);
				}break;
				case 0x7:{
					printf("%x - BEQ\n", byteIdx);
				}break;
				case 0x8:{
					printf("%x - BVC\n", byteIdx);
				}break;
				case 0x9:{
					printf("%x - BVS\n", byteIdx);
				}break;
				case 0xA:{
					printf("%x - BPL\n", byteIdx);
				}break;
				case 0xB:{
					printf("%x - BMI\n", byteIdx);
				}break;
				case 0xC:{
					printf("%x - BGE\n", byteIdx);
				}break;
				case 0xD:{
					printf("%x - BLT\n", byteIdx);
				}break;
				case 0xE:{
					printf("%x - BGT\n", byteIdx);
				}break;
				case 0xF:{
					printf("%x - BLE\n", byteIdx);
				}break;


			}
		}break;
		case 0x5:{
			switch(aL){
				case 0x0:
				case 0x2:{
					printf("%x - MULXU\n", byteIdx);
				}break;
				case 0x1:
				case 0x3:{
					printf("%x - DIVXU\n", byteIdx);
				}break;
				case 0x4:{
					printf("%x - RTS\n", byteIdx);
				}break;
				case 0x5:
				case 0xC:{
					printf("%x - BSR\n", byteIdx);
				}break;
				case 0x6:{
					printf("%x - RTE\n", byteIdx);
				}break;
				case 0x7:{
					printf("%x - TRAPA\n", byteIdx);
				}break;
				case 0x8:{
					switch(bH){
						case 0x0:{
								 printf("%x - BRA\n", byteIdx);
							 }break;
						case 0x1:{
								 printf("%x - BRN\n", byteIdx);
							 }break;
						case 0x2:{
								 printf("%x - BHI\n", byteIdx);
							 }break;
						case 0x3:{
								 printf("%x - BLS\n", byteIdx);
							 }break;
						case 0x4:{
								 printf("%x - BCC\n", byteIdx);
							 }break;
						case 0x5:{
								 printf("%x - BCS\n", byteIdx);
							 }break;
						case 0x6:{
								 printf("%x - BNE\n", byteIdx);
							 }break;
						case 0x7:{
								 printf("%x - BEQ\n", byteIdx);
							 }break;
						case 0x8:{
								 printf("%x - BVC\n", byteIdx);
							 }break;
						case 0x9:{
								 printf("%x - BVS\n", byteIdx);
							 }break;
						case 0xA:{
								 printf("%x - BPL\n", byteIdx);
							 }break;
						case 0xB:{
								 printf("%x - BMI\n", byteIdx);
							 }break;
						case 0xC:{
								 printf("%x - BGE\n", byteIdx);
							 }break;
						case 0xD:{
								 printf("%x - BLT\n", byteIdx);
							 }break;
						case 0xE:{
								 printf("%x - BGT\n", byteIdx);
							 }break;
						case 0xF:{
								 printf("%x - BLE\n", byteIdx);
							 }break;


					}				
				}break;
				case 0x9:
				case 0xA:
				case 0xB:{
					printf("%x - JMP\n", byteIdx);
				}break;
				case 0xD:
				case 0xE:
				case 0xF:{
					printf("%x - JSR\n", byteIdx);
				}break;

			}
		}break;
		case 0x6:{
			switch(aL){
				case 0x0:{
					printf("%x - BSET\n", byteIdx);
				}break;
				case 0x1:{
					printf("%x - BNOT\n", byteIdx);
				}break;
				case 0x2:{
					printf("%x - BCLR\n", byteIdx);
				}break;
				case 0x3:{
					printf("%x - BTST\n", byteIdx);
				}break;
				case 0x4:{
					printf("%x - OR\n", byteIdx);
				}break;
				case 0x5:{
					printf("%x - XOR\n", byteIdx);
				}break;
				case 0x6:{
					printf("%x - AND\n", byteIdx);
				}break;
				case 0x7:{
					uint8_t mostSignificantBit = bH >> 7;
					if (mostSignificantBit == 0x1){
						printf("%x - BIST\n", byteIdx);
					}else{
						printf("%x - BST\n", byteIdx);
					}					
					 }break;
				case 0x8:
				case 0x9:
				case 0xA:
				case 0xB:
				case 0xC:
				case 0xD:
				case 0xE:
				case 0xF:{
					printf("%x - MOV\n", byteIdx);
				}break;
			}
		}break;
		case 0x7:{
			switch(aL){
				uint8_t mostSignificantBit = bH >> 7;
					
				case 0x0:{
					printf("%x - BSET\n", byteIdx);
				}break;
				case 0x1:{
					printf("%x - BNOT\n", byteIdx);
				}break;
				case 0x2:{
					printf("%x - BCLR\n", byteIdx);
				}break;
				case 0x3:{
					printf("%x - BTST\n", byteIdx);
				}break;
				case 0x4:{
					if (mostSignificantBit == 0x1){
						printf("%x - BIOR\n", byteIdx);
					}else{
						printf("%x - BOR\n", byteIdx);
					}
					}break;
				case 0x5:{
					if (mostSignificantBit == 0x1){
						printf("%x - BIXOR\n", byteIdx);
					}else{
						printf("%x - BXOR\n", byteIdx);
					}
				}break;
				case 0x6:{
					if (mostSignificantBit == 0x1){
						printf("%x - BIAND\n", byteIdx);
					}else{
						printf("%x - BAND\n", byteIdx);
					}
				}break;
				case 0x7:{
					if (mostSignificantBit == 0x1){
						printf("%x - BILD\n", byteIdx);
					}else{
						printf("%x - BLD\n", byteIdx);
					}					
				}break;
				case 0x8:{
						printf("%x - MOV\n", byteIdx);
				}break;
				case 0x9:
				case 0xA:{
					switch(bH){
						case 0x0:{
								 printf("%x - MOV\n", byteIdx);
							 }break;
						case 0x1:{
								 printf("%x - ADD\n", byteIdx);
							 }break;
						case 0x2:{
								 printf("%x - CMP\n", byteIdx);
							 }break;
						case 0x3:{
								 printf("%x - SUB \n", byteIdx);
							 }break;
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
					 }break;
				case 0xB:{
					printf("%x - EEPMOV\n", byteIdx);
					 }break;
				case 0xC:
				case 0xE:{
					instrByteArray+=1;
					uint16_t instructionExtension = *instrByteArray;
					uint8_t cH = (instructionExtension  >> 4) & 0xF; //0b BBBB bbbb AAAA aaaa
					uint8_t cL = instructionExtension & 0xF;
					byteIdx+=2;
					// Here bH is the "register designation field" dont know what that is, so ignorign it for now
					// togetherwith bL it can also be "aa" which is the "absolute adress field"
					if (cH == 0x6){
						switch(cL){
							case 0x3:{
								 printf("%x - BTST\n", byteIdx);
							 }break;
						}
					}else if (cH == 0x7){
							uint8_t dH = (instructionExtension >> 12) & 0x000F;
							uint8_t mostSignificantBit = dH >> 7;
							switch(cL){
								case 0x3:{
										 printf("%x - BTST\n", byteIdx);
									 }break;
								case 0x4:{
										 if (mostSignificantBit == 0x1){
											 printf("%x - BIOR\n", byteIdx);
										 }else{
											 printf("%x - BOR\n", byteIdx);
										 }
									 }break;
								case 0x5:{
										 if (mostSignificantBit == 0x1){
											 printf("%x - BXOR\n", byteIdx);
										 }else{
											 printf("%x - BIXOR\n", byteIdx);
										 }
									 }break;
								case 0x6:{
										 if (mostSignificantBit == 0x1){
											 printf("%x - BIAND\n", byteIdx);
										 }else{
											 printf("%x - BAND\n", byteIdx);
										 }
									 }break;
								case 0x7:{
										 if (mostSignificantBit == 0x1){
											 printf("%x - BILD\n", byteIdx);
										 }else{
											 printf("%x - BLD\n", byteIdx);
										 }
									 }break;
								}


						}
					}break;
			case 0xD:
			case 0xF:{ 
					instrByteArray+=1;
					uint16_t instructionExtension = *instrByteArray;
					uint8_t cH = (instructionExtension  >> 4) & 0xF; //0b BBBB bbbb AAAA aaaa
					uint8_t cL = instructionExtension & 0xF;
					byteIdx+=2;
					// Here bH is the "register designation field" dont know what that is, so ignorign it for now
					// togetherwith bL it can also be "aa" which is the "absolute adress field"
					if (cH == 0x6){
						uint8_t dH = (instructionExtension >> 12) & 0x000F;
						uint8_t mostSignificantBit = dH >> 7;

						if (cL == 0x7){
							if (mostSignificantBit == 0x1){
								printf("%x - BIST\n", byteIdx);
							}else{
								printf("%x - BST\n", byteIdx);
							}						}
					}
					if (cH == 0x6 || cH == 0x7){
						switch(cL){
							case 0:{
								       printf("%x - BSET\n", byteIdx);
							       }break;
							case 1:{
								       printf("%x - BNOT\n", byteIdx);
							       }break;
							case 2:{
								       printf("%x - BCLR\n", byteIdx);
							       }break;

						}
					}
				}break;
			}
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
