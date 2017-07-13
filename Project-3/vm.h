#include <stdbool.h>

//#define NDEBUG

#define ZERO 0

#ifdef NDEBUG
#define debug(M, ...)
#else
#define debug(M, ...) fprintf(stderr, "DEBUG(%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#define RANDOM 0
#define FIFO 1
#define LRU 2
#define CLOCK 3

#define TLB_ENTRY 4
#define MAX_FRAME 8
#define MAX_PAGE 8
#define MAX_PID 2

extern int replacementPolicy;

int page_replacement();
int MMU(int pid, int addr, char type, bool *hit, bool *tlbhit);
void disk_print_stats();
void pt_print_stats();
void pt_init();
void tlb_init();
