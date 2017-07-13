typedef struct {
		int pid;
		int page;
} Invert;

typedef struct {
		int frameNo;
		bool valid;
		bool dirty;
} PTE;

typedef struct {
		int hitCount;
		int missCount;
		int tlbHitCount;
		int tlbMissCount;
} STATS;

typedef struct {
		PTE entry[MAX_PID][MAX_PAGE];
		STATS stats;
} PT;

typedef struct {
		int entry[2]; // virtual pageno
		int frameNo[2];
		bool valid[2];
		int lru;
} TLB;

// Linked List for LRU
// discard, use Professor Lee's structure instead
struct LRULLST {
		int frameNo;
		struct LRULLST *next;
};

int pagefault_handler(int pid, int pageNo, char type, bool *hit);
extern int lru_flag;
extern int frame_ind;
extern int frame_ref;
extern int current_frame;
extern int clock_flag[MAX_FRAME];
extern int tlbset_ptr[MAX_PID][TLB_ENTRY/2];