#include <stdio.h>
#include "vm.h"
#include "disk.h"
#include "pagetable.h"
#include "list.h"

struct Node *head = NULL;
int clock_index = 0;
int fifo_index = 0;

// random
int random()
{
		int next = rand() % MAX_PAGE;
		return next;
}


// fifo
int fifo()
{
		// test result: 4:6
		int tmp = fifo_index % MAX_FRAME;
		fifo_index++;
		return tmp;
}

// lru
int lru()
{
		if(lru_flag == 1){
			head = list_remove(head, frame_ref);
			head = list_insert_head(head, frame_ref);
		}
		if(lru_flag == 2){
			while(head -> next != NULL){
				head = head -> next;
			}
			frame_ref = head -> data;
			while(head -> prev != NULL){
				head = head -> prev;
			}
			head = list_remove_tail(head);
			head = list_insert_head(head, frame_ref);
			return frame_ref;
		}
		if(lru_flag == 3){
			head = list_remove(head, frame_ref);
			head = list_insert_head(head, frame_ref);
		}
		return 0;
}

// clock
int clock()
{
		int i;
		for(i = clock_index; i < MAX_FRAME; i++){
			if(clock_flag[i] == 1){
				clock_flag[i] = 0;
			} 
			else{
				clock_index = i;
				clock_flag[i] = 1;
				return clock_index;
			}
		}
		for(i = 0; i < clock_index; i++){
			if(clock_flag[i] == 1){
				clock_flag[i] = 0;
			}
			else{
				clock_index = i;
				clock_flag[i] = 1;
				return clock_index;
			}
		}
		return clock_index;
}

int page_replacement()
{
		int frame;
		if(replacementPolicy == RANDOM)  frame = random(); 
		else if(replacementPolicy == FIFO)  frame = fifo();
		else if(replacementPolicy == LRU) frame = lru();
		else if(replacementPolicy == CLOCK) frame = clock();

		return frame;
}

