#include <stdio.h>
void dumpArrayToFile(void* array, size_t size, char* fileName){
	FILE* fileToWrite;
	fileToWrite = fopen(fileName,"wb");
	fwrite(array, 1, size, fileToWrite);
	fclose(fileToWrite);
}
