#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "queue.h"

// TODO: might be better to replace the implementation with an array instead of a linked list, as it will allow the memory to be statically allocated.

void addElement(struct Queue *queue, int value){
	struct Element* newElement = malloc(sizeof(struct Element));
	newElement->value = value;
	newElement->next = NULL;
	if(queue->last != NULL){
		queue->last->next = newElement;
	}
	if(queue->first == NULL){
		queue->first = newElement;
	}
	queue->last = newElement;
}

int popElement(struct Queue* queue){
	int value = queue->first->value;
	struct Element* newFirst = queue->first->next;
	free(queue->first);
	queue->first = newFirst;
	return value;
}

bool isEmpty(struct Queue* queue){
	return (queue->first == NULL);
}

void printQueue(struct Queue* queue){
	struct Element* current = queue->first;
	while(current != NULL){
		printf("%d ", current->value);
		current = current->next;
	}
	printf("\n");

}
/* Tests
int main(){
	struct Queue q1 = {0};
	addElement(&q1, 5);
	printQueue(&q1);
	addElement(&q1, 6);
	printQueue(&q1);
	addElement(&q1, 7);
	printQueue(&q1);
	popElement(&q1);
	printQueue(&q1);
	addElement(&q1, 8);
	printQueue(&q1);
	popElement(&q1);
	printQueue(&q1);
}
*/
