#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "src/walker.h"

int main(int argc, char **argv){
	FILE* input1;
	FILE* input2;
	input1 = fopen(argv[1], "rb");
	input2 = fopen(argv[2], "rb");
	uint8_t* lcdMemory1 = malloc(LCD_MEM_SIZE);
	uint8_t* lcdMemory2 = malloc(LCD_MEM_SIZE);
	fread(lcdMemory1, 1, LCD_MEM_SIZE, input1);
	fread(lcdMemory2, 1, LCD_MEM_SIZE, input2);
	for(int y = 0; y < LCD_HEIGHT; y++){
		for(int i = 0; i < 2; i++){
			uint8_t* memory = (i==0) ? lcdMemory1 : lcdMemory2;
			for(int x = 0; x < LCD_WIDTH; x++){
				int yOffsetStripe = y%8;
				uint8_t firstByteForX = (memory[2*x + (y/8)*LCD_WIDTH*LCD_BYTES_PER_STRIPE] & (1<<yOffsetStripe)) >> yOffsetStripe;
				uint8_t secondByteForX = (memory[2*x + (y/8)*LCD_WIDTH*LCD_BYTES_PER_STRIPE + 1] & (1<<yOffsetStripe)) >> yOffsetStripe;
				int paletteIdx = firstByteForX + secondByteForX;
				switch (paletteIdx) {
					case 0:{
						printf(" ");
					} break;

					case 1:{

						printf("x");
					} break;
					case 2:{

						printf("*");
					} break;
					case 3:{

						printf("-");
					} break;
				}
			}
			printf("        ");
		}
		printf("\n");
	}
	fclose(input1);
	fclose(input2);
	return 0;
}
