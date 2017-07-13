#include <stdio.h>
#include "list.h"

int main()
{
		/*Driver code to test the implementation*/
		struct Node *head = NULL; // empty list. set head as NULL. 

		// Calling an Insert and printing list both in forward as well as reverse direction. 
		head = list_insert_head(head, 2); list_print(head);
		head = list_insert_head(head, 4); list_print(head);
		head = list_insert_tail(head, 6); list_print(head);
		head = list_insert_tail(head, 8); list_print(head);
		head = list_remove(head, 6); list_print(head);
		head = list_remove(head, 8); list_print(head);

}

