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
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	
	struct thread *curr = thread_current();
	struct file_page *file_page UNUSED = &page->file;

	if (page == NULL)
		return NULL;
	
	struct segment *seg = (struct segment*)page->uninit.aux;

	struct file *file = seg->file;

	off_t offset = seg->offset;
	size_t page_read_bytes = seg->read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;

	file_seek(file,offset);
	
	if(file_read(file, kva, page_read_bytes) != (int)page_read_bytes){
		return false;
	}

	memset(kva + page_read_bytes, 0, page_zero_bytes);

	return true;

	
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct thread *curr = thread_current();
	
	struct file_page *file_page UNUSED = &page->file;
	
	if (page == NULL)
		return NULL;
	struct segment *seg = (struct segment*)page->uninit.aux;

	if (pml4_is_dirty(curr->pml4, page->va)){
		
		file_write_at(seg->file, page->va, seg->read_bytes, seg->offset);

		pml4_set_dirty(curr->pml4, page->va, 0);
	}

	pml4_clear_page(curr->pml4, page->va);
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	//size_t length = 사용자가 요청한 길이
	//size_t length = file 중 offset 중에서 length 만큼 읽어서 addr에 매핑
	//file이 다른 곳에서 예기치 못하게 닫힐 수 있음, 따라서 같은 file을 reopen
	struct file *target = file_reopen(file);
	//아래 반복문에서 addr이 변경되므로 매핑을 시작할 주소인 addr은 따로 저장
	void *start_addr = addr;

	/* 주어진 파일 길이와 length를 비교해서 length보다 file 크기가 작으면 파일 통으로 싣고 파일 길이가 더 크면 주어진 length만큼만 load*/
	size_t read_bytes = file_length(target) < length ? file_length(target) : length;
	// 이렇게 해야 PGSIZE에 맞춰서 zero bytes 설정
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;
	// (5) 메모리 매핑을 page 단위로 이루어지므로 PGSIZE 의 배수인지 확인
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	// (6) addr 페이지가 올바른 시작 위치를 갖고 있는지 확인
	ASSERT (pg_ofs (addr) == 0);
	// (7) 메모리 매핑은 PGSIZE 단위라서 올바른 offset 는 PGSIZE 의 배수
	ASSERT (offset % PGSIZE == 0);

	//매핑할 데이터가 남아있거나(read_bytes), 0으로 채워야 할 바이트가 남아있으면(zero_bytes) 계속 페이지를 할당하고 할당한 페이지를 초기화
	while (read_bytes > 0 || zero_bytes > 0)
	{
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		// 페이지를 할당하기 위한 정보를 담는 구조체
		struct segment *seg = (struct segment *)malloc(sizeof(struct segment));

		seg->file = target;
		seg->offset = offset;
		seg->read_bytes = page_read_bytes;

		// 페이지 할당 후 초기화 
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, seg))
			return NULL;
		
		// 다음 페이지를 위한 작업
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
		// 주어진 addr에 해당하는 page를 찾아서 저장
		struct page *page = spt_find_page(&curr->spt, addr);
		// 페이지가 없다면 do_munmap 함수 종료, 이게 while 문 종료 조건
		if (page == NULL)
			return NULL;
		//unitit.aux에는 페이지 할당시 사용된 struct segment 가 있음. -> 해제할 페이지의 정보를 얻기위한 구조체
		struct segment *seg = (struct segment*)page->uninit.aux;

		//메모리에서 해제할 페이지의 dirt bit이 1인지 확인
		if (pml4_is_dirty(curr->pml4, page->va)){
			// 변경사항을 디스크 파일에 업데이트
			file_write_at(seg->file, addr, seg->read_bytes, seg->offset);
			// dirty bit 를 다시 0으로 바꿔 줌
			pml4_set_dirty(curr->pml4, page->va, 0);
		}
		// 페이지를 pml4에서 삭제
		pml4_clear_page(curr->pml4, page->va);
		// 다음 페이지로 이동
		addr += PGSIZE;
	}
}
