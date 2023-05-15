/* vm.c: Generic interface for virtual memory objects. */
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/init.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

uint64_t page_hash (const struct hash_elem *e, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux);

struct list frame_table;
struct list_elem* clock_ref; // vm_get_victim()
struct lock frame_table_lock;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
	clock_ref = list_begin(&frame_table);
	lock_init(&frame_table_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initializer according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		// 미할당 페이지 생성
		struct page *page = (struct page*)malloc(sizeof(struct page));
		if (page == NULL) {
			goto err;
		}

		// page_initiailizer 정의
		typedef bool (*page_initializer) (struct page *, enum vm_type, void *kva);
		page_initializer new_initializer = NULL;
		
		// vm_type에 따라 다른 initializer 호출
		switch (VM_TYPE(type)) {
			case VM_ANON:
				new_initializer = anon_initializer;
				break;
			case VM_FILE:
				new_initializer = file_backed_initializer;
				break;
			default:
				break;
		}
		// uninit new 호출
		uninit_new(page, upage, init, type, aux, new_initializer);

		page->writable = writable;

		/* TODO: Insert the page into the spt. */
		return spt_insert_page (spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page *page = (struct page*)malloc(sizeof(struct page)); // dummy page 생성
	/* TODO: Fill this function. */
	page->va = pg_round_down(va);
	struct hash_elem *e = hash_find(&spt->pages, &page->hash_elem);
	free(page);

	if (e != NULL){
		return hash_entry(e, struct page, hash_elem);
	}
	else
		return NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	int succ = false;
	/* TODO: Fill this function. */
	if (hash_insert(spt, &page->hash_elem) == NULL){
		succ = true;
		return succ;
	}
	else
		return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}


/* 이따 수정 */
/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	if (!list_empty(&frame_table)) {
		/* 프레임 테이블에서 FIFO 방식으로 첫 번째 프레임을 가져옴. 
		   (일단 이렇게 구현하고 나중에 Clock 알고리즘 등 사용해서 바꾸면 될듯) */
		victim = list_entry(list_pop_front(&frame_table), struct frame, frame_elem);
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = malloc(sizeof(struct frame));
	/* TODO: Fill this function. */
	frame->kva = palloc_get_page(PAL_USER);
	if(frame->kva == NULL){ // 가용 page 없으면
		frame = vm_evict_frame(); // 쫓아낸 프레임 받아옴
		/* => list_push_back 필요 x(이미 frame table 있음) */
		frame->page = NULL;
		return frame;
	}
	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table, &frame->frame_elem);
	lock_release(&frame_table_lock);
	frame->page = NULL; // 새 frame 가져옴, page 멤버 초기화

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	if(is_kernel_vaddr(addr) || addr == NULL){
		return false;
	}
	
	if(not_present){
		// thread 구조체 내의 rsp_stack을 설정 
		struct thread *curr = thread_current();
		void *rsp_stack = !user ? curr->rsp_stack : f->rsp;

		if (!vm_claim_page(addr)){
			if (rsp_stack-8 <= addr  && USER_STACK - 0x100000 <= addr && addr <= USER_STACK){
				vm_stack_growth(curr->stack_bottom-PGSIZE);
				return true;
			}  
			
			return false;
		}
		else
			return true;
	}
	return false;

}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	struct thread *cur = thread_current();

    page = spt_find_page(&cur->spt, va); // spt에서 해당 주소에 해당하는 page를 찾아 page 포인터 설정
    if (page == NULL) {
		return false; // page가 없으면 실패
	}
	return vm_do_claim_page (page); // vm_do_claim_page 호출하여 페이지를 클레임
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* 페이지가 이미 물리주소에 매핑 돼있는지 확인 */
    if (page->frame != NULL) {
        return false;
    }

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	/* writable이 true면 user process가 page 수정 가능, otherwise read-only. 
	KPAGE는 유저 풀에서 가져온 페이지여야함. 
	(UPAGE 이미 mapping, or 메모리 할당 실패) => false 리턴. 
	성공하면 true를 반환한다. 성공시에 swap_in()함수가 실행된다. */
	if(install_page(page->va,frame->kva,page->writable)){
		return swap_in (page, frame->kva);
	}
	return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	/* page_hash: 페이지의 가상 주소를 해시 값으로 변환하는 함수 */
	/* page_less: 페이지의 가상 주소를 비교하는 함수 */
	hash_init(&spt->pages, page_hash, page_less, NULL);
}

/* 페이지 p의 hash value 리턴 */
uint64_t
page_hash (const struct hash_elem *e, void *aux) {
  const struct page *p = hash_entry(e, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof(p->va));
}

/* a->va가 b->va보다 작을때 return true. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux) {
  const struct page *a = hash_entry(a_, struct page, hash_elem);
  const struct page *b = hash_entry(b_, struct page, hash_elem);

  return a->va < b->va;
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {

	/* 1. Copy parent page information */
	struct hash_iterator i;
	hash_first (&i, &src->pages);

	while (hash_next (&i)) {
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		enum vm_type type = page_get_type(src_page);		// 부모 페이지의 type
        void *upage = src_page->va;						// 부모 페이지의 가상 주소
        bool writable = src_page->writable;				// 부모 페이지의 쓰기 가능 여부
        vm_initializer *init = src_page->uninit.init;	// 부모의 초기화되지 않은 페이지들 할당 위해 
        void *aux = src_page->uninit.aux;

        // 페이지 타입 검사
        if (type == VM_UNINIT) {
            if(!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
                return false;
        }
		else {
			if(!vm_alloc_page(type, upage, writable))
                return false;
            if(!vm_claim_page(upage))
                return false;
			struct page *dst_page = spt_find_page(dst, upage);
            memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
		}
	}
	return true;
}

/* Callback function for hash_destroy() to free all pages in the supplemental page table */
void page_destroy_func(struct hash_elem *e, void *aux UNUSED) {
    struct page *page = hash_entry (e, struct page, hash_elem);

	ASSERT(is_user_vaddr(page->va));
	ASSERT(is_kernel_vaddr(page));
	free(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator i;
	struct frame* frame;
    hash_first (&i, &spt->pages);
	while (hash_next(&i)){
		struct page *target = hash_entry (hash_cur (&i), struct page, hash_elem);
		frame = target->frame;
		// file-backed file인 경우
		// if(page_get_type(target) == VM_FILE){
		// if(target->operations->type == VM_FILE){
		// 	do_munmap(target->va);
		// }
	}
	
	hash_destroy(&spt->pages, page_destroy_func);
	free(frame);
}
