#include <stdio.h>
#include "vm.h"

int numDiskRead = 0;
int numDiskWrite = 0;


void disk_read(int frame, int pid, int page)
{
		numDiskRead++;
}

void disk_write(int frame, int pid, int page)
{
		numDiskWrite++;
}

void disk_print_stats()
{
		printf("Disk read: %d\n", numDiskRead);
		printf("Disk write: %d\n", numDiskWrite);
}

