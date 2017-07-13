#include <stdio.h>
#include "vm.h"
#include "disk.h"
#include "pagetable.h"

PT pageTable;
Invert invert_table[MAX_FRAME];
TLB tlb[MAX_PID][TLB_ENTRY/2];
int tlbset_ptr[MAX_PID][TLB_ENTRY/2] = {1};

int freePageNo=0;
//int free_frame_list = MAX_FRAME;

int lru_flag = 0;
int clock_flag[MAX_FRAME] = {0};
int current_frame;
int current_frame_tlb;
int frame_ind = 0;
int frame_ref;
struct Node *tlbhead = NULL;

// If the page is already in the physical memory (page hit), return frame number.
// otherwise (page miss), return -1.
int is_page_hit(int pid, int pageNo, char type)
{
		// look at the page table with pid, check if pageNo valid or not
	if(pageTable.entry[pid][pageNo].valid == true){
		// hit achieved
		// we should update both page table and tlb
		// update page table associated LRU and CLOCK
		current_frame = pageTable.entry[pid][pageNo].frameNo;
		// change dirty in page table
        if(type == 'W'){
			pageTable.entry[pid][pageNo].dirty = true;
		}

        // update current frame and LRU flag for page table
		if(replacementPolicy == LRU){
            frame_ref = pageTable.entry[pid][pageNo].frameNo;
			lru_flag = 1;
			page_replacement();
		}

        //update current frame and clock flag
		if(replacementPolicy == CLOCK){
            frame_ref = pageTable.entry[pid][pageNo].frameNo;
			clock_flag[frame_ref] = 1;
		}

        // if there is a page hit, meaning there must be a TLB miss, thus
        // we have to update TLB after find a page hit.
        // update TLB
        tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)] = 1 - tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)];
        tlb[pid][pageNo%(TLB_ENTRY/2)].frameNo[tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)]] = pageTable.entry[pid][pageNo].frameNo;
		tlb[pid][pageNo%(TLB_ENTRY/2)].valid[tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)]] = true;
        tlb[pid][pageNo%(TLB_ENTRY/2)].entry[tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)]] = pageNo;

        // return the frameNo
		return pageTable.entry[pid][pageNo].frameNo;
	}
	return -1;
}

int pagefault_handler(int pid, int pageNo, char type, bool *hit)
{
		//we should update both page table and tlb
		*hit = false;

		// two cases: filled or not
		if(frame_ind == MAX_FRAME) {
				// we don't have available frame anymore;
				// set the LRU flag as 2
                lru_flag = 2;

                // get victim frame pid and page number
                int victim_frame = page_replacement();
                int victim_pid = invert_table[victim_frame].pid;
                int victim_page = invert_table[victim_frame].page;
                
                // update TLB, tedious way, where tlbset_ptr is binary
                int j;
                for(j = 0; j < 2; j++){
                    if(tlb[victim_pid][victim_page%(TLB_ENTRY/2)].entry[j] == victim_page){
                            tlb[victim_pid][victim_page%(TLB_ENTRY/2)].valid[j] = false;
                            tlbset_ptr[victim_pid][victim_page%(TLB_ENTRY/2)] = 1 - j;
                        }
                }

                tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)] = 1 - tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)];
                tlb[pid][pageNo%(TLB_ENTRY/2)].entry[tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)]] = pageNo;
                tlb[pid][pageNo%(TLB_ENTRY/2)].frameNo[tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)]] = pageTable.entry[pid][pageNo].frameNo;
                tlb[pid][pageNo%(TLB_ENTRY/2)].valid[tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)]] = true;
                
                // update Page table
				pageTable.entry[victim_pid][victim_page].valid = false;           
				if(pageTable.entry[victim_pid][victim_page].dirty == true){
					disk_write(victim_frame, victim_pid, victim_page);
					pageTable.entry[victim_pid][victim_page].dirty = false;
				}

				pageTable.entry[pid][pageNo].valid = true;
				pageTable.entry[pid][pageNo].frameNo = victim_frame;
				disk_read(pageTable.entry[pid][pageNo].frameNo, pid, pageNo);
				invert_table[pageTable.entry[pid][pageNo].frameNo].pid = pid;
                invert_table[pageTable.entry[pid][pageNo].frameNo].page = pageNo;
                pageTable.entry[pid][pageNo].dirty = false;
                if(type == 'W'){
                        pageTable.entry[pid][pageNo].dirty = true;
                }
                return pageTable.entry[pid][pageNo].frameNo;
            }
            else{
            	// it is not full
            	lru_flag = 3;
            	pageTable.entry[pid][pageNo].valid = true;
            	pageTable.entry[pid][pageNo].frameNo = frame_ind;
            	current_frame = pageTable.entry[pid][pageNo].frameNo;
            	if(replacementPolicy == LRU){
                    frame_ref = pageTable.entry[pid][pageNo].frameNo;
            		page_replacement();
            	}
            	if(replacementPolicy == CLOCK){
                    frame_ref = pageTable.entry[pid][pageNo].frameNo;
            		clock_flag[frame_ref] = 1;
            	}
            	disk_read(pageTable.entry[pid][pageNo].frameNo, pid, pageNo);
            	invert_table[pageTable.entry[pid][pageNo].frameNo].pid = pid;
            	invert_table[pageTable.entry[pid][pageNo].frameNo].page = pageNo;
            	pageTable.entry[pid][pageNo].dirty = false;
            	if(type == 'W'){
            		pageTable.entry[pid][pageNo].dirty = true;
            	}
            	// update TLB
            	tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)] = 1 - tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)];
                tlb[pid][pageNo%(TLB_ENTRY/2)].valid[tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)]] = true;
                tlb[pid][pageNo%(TLB_ENTRY/2)].entry[tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)]] = pageNo;
                tlb[pid][pageNo%(TLB_ENTRY/2)].frameNo[tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)]] = pageTable.entry[pid][pageNo].frameNo;
                frame_ind++;
                return pageTable.entry[pid][pageNo].frameNo;
            }
}


// If the page is in TLB (tlb hit), return frame number.
// otherwise (tlb miss), return -1.
int is_TLB_hit(int pid, int pageNo, int type)
{
		// we here use a loop to check all the tlb entries
		int k;
		for(k = 0; k < 2; k++){
			if(tlb[pid][pageNo%(TLB_ENTRY/2)].valid[k] == true && tlb[pid][pageNo%(TLB_ENTRY/2)].entry[k]==pageNo){
				tlbset_ptr[pid][pageNo%(TLB_ENTRY/2)] = k;
				current_frame = pageTable.entry[pid][pageNo].frameNo;
				if(type == 'W'){
					pageTable.entry[pid][pageNo].dirty = true;
				}
				if(replacementPolicy == LRU){
                    frame_ref = pageTable.entry[pid][pageNo].frameNo;
					lru_flag = 1;
					page_replacement();
				}
				if(replacementPolicy == CLOCK){
                    frame_ref = pageTable.entry[pid][pageNo].frameNo;
					clock_flag[current_frame] = 1;
				}
				return pageTable.entry[pid][pageNo].frameNo;
			}				
		}
		return -1;
}

int MMU(int pid, int addr, char type, bool *hit, bool *tlbhit)
{
		int frameNo;
		int pageNo = (addr >> 8);
		int offset = addr - (pageNo << 8);
		
		if(pageNo > MAX_PAGE) {
				printf("invalid page number (MAX_PAGE = 0x%x): pid %d, addr %x\n", MAX_PAGE, pid, addr);
				return -1;
		}
		if(pid > MAX_PID) {
				printf("invalid pid (MAX_PID = %d): pid %d, addr %x\n", MAX_PID, pid, addr);
				return -1;
		}
		
		// TLB hit
		frameNo = is_TLB_hit(pid, pageNo, type);
		if(frameNo > -1) {
				*tlbhit = true;
				*hit = true;
				pageTable.stats.hitCount++;
				pageTable.stats.tlbHitCount++;
				return (frameNo << 8) + offset;
		} else *tlbhit = false;

		// page hit
		frameNo = is_page_hit(pid, pageNo, type);
		if(frameNo > -1) {
				*hit = true;
				pageTable.stats.hitCount++;
				pageTable.stats.tlbMissCount++;

				return (frameNo << 8) + offset;
		}
		
		// pagefault
		frameNo = pagefault_handler(pid, pageNo, type, hit);

		pageTable.stats.missCount++;
		pageTable.stats.tlbMissCount++;
		return (frameNo << 8) + offset;
}

void pt_print_stats()
{
		int req = pageTable.stats.hitCount + pageTable.stats.missCount;
		int hit = pageTable.stats.hitCount;
		int miss = pageTable.stats.missCount;
		int tlbHit = pageTable.stats.tlbHitCount;
		int tlbMiss = pageTable.stats.tlbMissCount;

		printf("Request: %d\n", req);
		printf("Page Hit: %d (%.2f%%)\n", hit, (float) hit*100 / (float)req);
		printf("Page Miss: %d (%.2f%%)\n", miss, (float)miss*100 / (float)req);
		printf("TLB Hit: %d (%.2f%%)\n", tlbHit, (float) tlbHit*100 / (float)req);
		printf("TLB Miss: %d (%.2f%%)\n", tlbMiss, (float) tlbMiss*100 / (float)req);

}

void pt_init()
{
		int i,j;

		pageTable.stats.hitCount = 0;
		pageTable.stats.missCount = 0;
		pageTable.stats.tlbHitCount = 0;
		pageTable.stats.tlbMissCount = 0;

		for(i = 0; i < MAX_PID; i++) {
				for(j = 0; j < MAX_PAGE; j++) {
						pageTable.entry[i][j].valid = false;
				}
		}
}

void tlb_init()
{
		int i, j;
		for (i = 0; i < MAX_PID; i++)
		{
				for(j = 0; j < TLB_ENTRY / 2; j++)
				{
						tlb[i][j].valid[0] = tlb[i][j].valid[1] = false;
						tlb[i][j].lru = 0;
				}
		}
}

