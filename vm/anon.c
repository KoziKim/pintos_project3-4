/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/string.h"
#include "lib/kernel/bitmap.h"


/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};
/* 스왑 디스크에서 사용 가능한 영역과 사용된 영역을 관리하기 위한 자료구조로 bitmap 사용
	스왑 영역은 PGSIZE 단위로 관리 => 기본적으로 스왑 영역은 디스크이니 섹터로 관리 하는데
	이를 페이지 단위로 관리하려면 섹터 단위를 페이지 단위로 바꿔줄 필요가 있음
	이 단위가 SECTORs_PER_PAGE (8섹터당 1페이지 관리)*/
struct bitmap *swap_table;
int bitcnt;
const size_t SECTORS_PER_PAGE = PGSIZE/DISK_SECTOR_SIZE;
/* Initialize the data for anonymous pages */

void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1);
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	swap_table = bitmap_create(swap_size);
}

bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	// struct uninit_page* uninit_page = &page->uninit;
	// memset(uninit_page,0,sizeof(struct uninit_page));
	/* Set up the handler */
	page->operations = &anon_ops; // operations를 anon-ops로 지정

	struct anon_page *anon_page = &page->anon;
	// anon_page->swap_sector = -1;
	
	// return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	/*디스크에서 메모리로 데이터 내용을 읽어서 스왑 디스크에서 익명 페이지로 스왑합니다. 
	데이터의 위치는 페이지가 스왑 아웃될 때  페이지 구조에 스왑 디스크가 
	저장되어 있어야 한다는 것입니다. 스왑 테이블을 업데이트해야 합니다*/

	// 스왑 아웃을 할 때 저장해 두었던 섹터(슬롯)를 가져옴
	int empty_slot = anon_page->swap_sector;

	// 스왑테이블에 해당 슬롯(섹터)가 있는지 확인
	if (bitmap_test(swap_table, empty_slot) == false)
		return false;
	
	for (int i = 0; i < SECTORS_PER_PAGE; i++)
	{
		disk_read(swap_disk, empty_slot * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE *i);
	}
	bitmap_set(swap_table, empty_slot, false);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	int empty_slot = bitmap_scan (swap_table, 0, 1, false);

	if ((empty_slot) == BITMAP_ERROR) {
        return false;
    }
    /* 
    한 페이지를 디스크에 써주기 위해 SECTORS_PER_PAGE 개의 섹터에 저장해야 한다.
    이때 디스크에 각 섹터 크기의 DISK_SECTOR_SIZE만큼 써준다.
	SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE; 8 = 4096 / 512
	swap_size = disk_size(swap_disk)/SECTORS_PER_PAGE; 
    */
   	for (int i = 0; i<SECTORS_PER_PAGE; i++){
		disk_write(swap_disk, empty_slot*SECTORS_PER_PAGE+i, page->va+DISK_SECTOR_SIZE*i);
	}

    /*
    swap table의 해당 페이지에 대한 swap slot의 비트를 true로 바꿔주고
    해당 페이지의 PTE에서 present bit을 0으로 바꿔준다.
    이제 프로세스가 이 페이지에 접근하면 page fault가 뜬다.
    */
	bitmap_set(swap_table,empty_slot, true);
	pml4_clear_page(thread_current()->pml4, page->va);

	/* 페이지의 swap_index 값을 이 페이지가 저장된 swap slot의 번호로 써준다.*/
	anon_page->swap_sector = empty_slot;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
