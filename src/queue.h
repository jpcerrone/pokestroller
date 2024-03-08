#pragma once
#include <stdlib.h>
#include <stdio.h>

struct Element{
	int value;
	struct Element* next;
};
struct Queue{
	struct Element* first;
	struct Element* last;
};

void addElement(struct Queue *queue, int value);
int popElement(struct Queue* queue);
bool isEmpty(struct Queue* queue);
void printQueue(struct Queue* queue);
