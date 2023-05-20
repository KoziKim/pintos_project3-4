/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
void do_munmap (void *addr);


/* DO NOT MODIFY this struct */
/* file-backed pages를 위한 함수 포인터의 테이블 */
/* .destroy 필드가 file_backed_destroy 값을 가지고 있는 것을 알 수 있는데, 
	file_backed_destroy는 page를 destroy하고 같은 파일에 정의된 함수다. */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	// struct uninit_page *uninit = &page->uninit;
	// void *aux = uninit->aux;

	// /* Set up the handler */
	// page->operations = &file_ops;

	// memset(uninit, 0, sizeof(struct uninit_page));

	// struct lazy_load_info *info = (struct lazy_load_info *)aux;
	// struct file_page *file_page = &page->file;
	// file_page->file = info->file;
	// file_page->length = info->page_read_bytes;
	// file_page->offset = info->offset;
	// return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	// close(file_page->file);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	// (0) size_t length = file 중 offset 에서 length 만큼 읽어서 addr 에 매핑
	
	// (1) file 이 다른 곳에서 닫힐 수 있음, 따라서 같은 file 을 reopen
	struct file * get_file = file_reopen(file); 
	// (2) 아래 코드에서 addr 이 변경되므로 매핑을 시작할 주소인 addr 은 따로 저장
	void *start_addr  = addr;

	// (3) 파일의 길이와 length 를 비교 후 read_bytes 설정
	size_t read_bytes;
	size_t file_len = file_length(file);
	if (file_len < length) 
		read_bytes = file_len;
	else
    	read_bytes = length;
	// (4) 이렇게 해야 PGSIZE 에 맞춰서 zero bytes 설정
	// 
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	// (5) 메모리 매핑을 page 단위로 이루어지므로 PGSIZE 의 배수인지 확인
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	// (6) addr 페이지가 올바른 시작 위치를 갖고 있는지 확인
	ASSERT (pg_ofs (addr) == 0);
	// (7) 메모리 매핑은 PGSIZE 단위라서 올바른 offset 는 PGSIZE 의 배수
	ASSERT (offset % PGSIZE == 0);

	// (8) 매핑할 데이터가 남아있거나(read_bytes), 0 으로 채워야할 바이트가 남아있으면(zero_bytes) 계속 페이지를 할당하고 할당한 페이지를 초기화
	while (read_bytes > 0 || zero_bytes > 0) {

		// (9) read_bytes 가 PGSIZE 보다 작으면 전부 다 매핑
		size_t page_read_bytes;
		if (read_bytes < PGSIZE) 
	    	page_read_bytes = read_bytes;
		else
	    	page_read_bytes = PGSIZE;
		// (10) page 에 남는 공간을 0으로 채우기
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		// (11) 페이지를 할당하기 위한 정보를 담는 구조체 : container
		struct segment *container = (struct segment *)malloc(sizeof(struct segment));
		container->file = get_file;
		container->offset = offset;
		container->read_bytes = page_read_bytes;

		// (12) 페이지 할당 후 초기화
		if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, lazy_load_segment, container)){
			return NULL;
		}

		// (13) 다음 페이지를 위한 작업
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return start_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	while(true){
		struct thread *curr = thread_current();
		// (1) 주어진 addr 에 해당하는 page 찾아서 저장
		struct page *find_page = spt_find_page(&curr->spt, addr);
		// (2) 페이지가 없다면 do_munmap 함수 종료, 이게 while 문 종료 조건
		if (find_page == NULL) return NULL;

		// (3) uninit.aux 에는 페이지 할당 시 사용된 struct segment 가 있음
		// -> 해제할 페이지의 정보를 얻기 위한 구조체
		struct segment* container = (struct segment*)find_page->uninit.aux;


		// (4) 메모리에서 해제할 페이지의 dirty bit 이 1인지 확인
		if (pml4_is_dirty(curr->pml4, find_page->va)){
			// (5) 변경 사항을 디스크 파일에 업데이트
			file_write_at(container->file, addr, container->read_bytes, container->offset);
			// (6) dirty bit 을 다시 0으로 바꿔줌
			pml4_set_dirty(curr->pml4,find_page->va,0);
		}

		// (7) 페이지를 pml4 에서 삭제
		pml4_clear_page(curr->pml4, find_page->va); 
		// (8) 다음 페이지로 이동
		addr += PGSIZE;
	}
}
