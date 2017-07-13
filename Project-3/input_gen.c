#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include "vm.h"

int main(int argc, char **argv)
{
		if(argc < 2) {
				printf("usage: ./input_gen number_of_request\n");
				return -1;
		}

		int req_num = atoi(argv[1]);
		int i, pid, page, offset, addr;
		char type;

		srand(time(NULL));

		for(i = 0; i < req_num; i++)
		{
				pid = rand() % MAX_PID;
				page = rand() % MAX_PAGE;
				offset = rand() % 0xFF;
				if(rand()%2 == 0) type = 'R';
				else type = 'W';
				addr = (page << 8) + offset;

				printf("%d %c 0x%x\n", pid,type, addr);
		}

		return 0;
}

