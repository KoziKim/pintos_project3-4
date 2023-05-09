#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* 구조체 스레드의 `마법' 멤버에 대한 임의의 값입니다.
스택 오버플로를 감지하는 데 사용됩니다.
자세한 내용은 thread.h 상단의 큰 주석을 참고하세요. */
#define THREAD_MAGIC 0xcd6abf4b

/* 기본 스레드의 임의 값 이 값을 수정하지 마십시오. */
#define THREAD_BASIC 0xd42df210

#define min(a, b) (((a) < (b)) ? (a) : (b))

/* THREAD_READY 상태의 프로세스 목록, 즉 실행할 준비가 되었지만 실제로 실행되지는 않은 프로세스의 목록입니다. */
static struct list ready_list;
// 자고 있는 스레드 리스트
static struct list sleep_list;
/* 유휴 스레드. */
static struct thread *idle_thread;

/* 초기 스레드, init.c:main()을 실행하는 스레드. */
static struct thread *initial_thread;

/* allocate_tid()에서 사용하는 잠금입니다. */
static struct lock tid_lock;

/* 스레드 삭제 요청 */
static struct list destruction_req;

/* 통계. */
static long long idle_ticks; /* 유휴 상태로 보낸 타이머 틱 수입니다. */
static long long kernel_ticks; /* 커널 스레드의 타이머 틱 수입니다. */
static long long user_ticks; /* 사용자 프로그램의 타이머 틱 수입니다. */

static long long next_tick_to_awake;

/* 스케줄링. */
#define TIME_SLICE 4 /* 각 스레드에 부여할 타이머 틱 수입니다. */
static unsigned thread_ticks; /* 마지막 양보 이후 타이머 틱 수입니다. */
// // sleep list에서 대기중인 스레드들의 wakeup_tick 최솟값
// static unsigned next_tick_to_awake;
/* false(기본값)이면 라운드 로빈 스케줄러를 사용합니다.
   true이면 multi-level feedback queue scheduler를 사용합니다.
   커널 명령줄 옵션 "-o mlfqs"로 제어합니다. */
bool thread_mlfqs;


static void kernel_thread (thread_func *, void *aux);
static void idle (void *aux UNUSED);

static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* T가 유효한 스레드를 가리키는 것처럼 보이면 true를 반환합니다. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* 실행 중인 스레드를 반환합니다.
 * CPU의 스택 포인터 `rsp'를 읽은 다음, 이를 반올림하여
 * 페이지의 시작 부분으로 반올림합니다.  
 '구조체 스레드'는 항상 페이지의 시작 부분에 있고 스택 포인터는 중간 어딘가에 있으므로, 이것은 현재 스레드를 찾습니다. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// thread_start에 대한 글로벌 descriptor 테이블입니다.
// gdt는 thread_init 이후에 설정되기 때문에, 
// 임시 gdt를 먼저 설정해야 합니다.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* 현재 실행 중인 코드를 스레드로 변환하여 스레딩 시스템을 초기화합니다.
일반적으로는 작동하지 않으며 이 경우에만 로더 때문에 가능합니다.
S는 스택의 하단을 페이지 경계에 두도록 주의를 기울였습니다.

또한 실행 대기열과 tid 잠금을 초기화합니다.

이 함수를 호출한 후 thread_create()로 스레드를 생성하기 전에 반드시 페이지 할당자를 초기화해야 합니다.

이 함수가 완료될 때까지 thread_current()를 호출하는 것은 안전하지 않습니다. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* 커널에 대한 임시 gdt를 다시 로드합니다.
	 * 이 gdt에는 사용자 컨텍스트가 포함되지 않습니다.
	 * 커널은 gdt_init ()에서 사용자 컨텍스트가 포함된 gdt를 다시 빌드합니다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* 글로벌 스레드 컨텍스트 초기화 */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);
	list_init (&sleep_list);
	next_tick_to_awake = INT64_MAX;

	/* 실행 중인 스레드에 대한 스레드 구조를 설정합니다. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* 인터럽트를 활성화하여 preemptive 스레드 스케줄링을 시작합니다.
   또한 유휴 스레드를 생성합니다. */
void
thread_start (void) {
	/* 유휴 스레드를 생성합니다. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* preemptive 스레드 스케줄링을 시작합니다. */
	intr_enable ();

	/* 유휴 스레드가 idle_thread를 초기화할 때까지 기다립니다. */
	sema_down (&idle_started);
}

/* 각 타이머 틱마다 타이머 인터럽트 핸들러가 호출합니다.
   따라서 이 함수는 외부 인터럽트 컨텍스트에서 실행됩니다. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* 통계 업데이트. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* 스레드 통계를 출력합니다. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

	/* 주어진 초기 우선순위를 가진 NAME이라는 이름의 새 커널 스레드를 생성하고, 이 스레드는 AUX를 인수로 전달하는 FUNCTION을 실행한 후 준비 큐에 추가합니다.
	새 스레드의 스레드 식별자를 반환하거나 생성에 실패하면 TID_ERROR를 반환합니다.

	thread_start()가 호출된 경우 thread_create()가 반환되기 전에 새 스레드가 스케줄링될 수 있습니다.
	심지어 thread_create()가 반환되기 전에 종료될 수도 있습니다.
	반대로, 새 스레드가 스케줄되기 전에 원래 스레드가 얼마든지 실행될 수 있습니다.
	순서를 보장해야 하는 경우 세마포어 또는 다른 형태의 동기화를 사용하세요.

	제공된 코드는 새 스레드의 `priority' 멤버를 PRIORITY로 설정하지만 실제 우선순위 스케줄링은 구현되지 않습니다.
	우선순위 스케줄링은 문제 1-3의 목표입니다. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* 스레드를 할당합니다. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* 스레드를 초기화합니다. */
	init_thread (t, name, priority);

	// 승훈이를 따라서 thread_create() 안에서 fdt를 정의해주어 보았다. PAL_ZERO와 FDT_PAGES에 주목.
	t->fdt = palloc_get_multiple(PAL_ZERO, FDT_PAGES);
	if (t->fdt == NULL){
		return TID_ERROR;
	}
	t->next_fd = 2;

	tid = t->tid = allocate_tid ();

	/* 예약된 경우 kernel_thread를 호출합니다.
	 * 참고) rdi는 첫번째 인자, rsi는 두번째 인자입니다. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* load 성공 여부 
		성공 1, 실패 0
	*/
	t->success_load = 0;
	t->exit_status = 0;
	t->running_file = NULL;
	
	// 초기화 중 놓친 부분!!! child_list에 바로 생성된 child를 push해주는 것을 까먹음...
	list_push_back(&thread_current()->child_list, &t->child_elem);

	/* 실행 대기열에 추가합니다. */
	thread_unblock (t);
	// project 1 핵심 오류 중 하나였던 부분... 
	// thread_block() 안에 thread_yield()를 넣었기 때문에 오류가 난 것이라 생각했는데,
	// 진짜 문제는 unblock 안에 yield가 들어가되  != idle_thread 인 경우를 걸러줬었어야 했던 것이었다.
	// if (t->priority > thread_current()->priority) {
	// 	thread_yield();
	// }

	return tid;
}

static bool
thread_less (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->wakeup_tick < b->wakeup_tick;
}

static bool
priority_more (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->priority > b->priority;
}

/* Puts the current thread to sleep.  
It will not be scheduled again until awoken by thread_unblock().

This function must be called with interrupts turned off.  
It is usually a better idea to use one of the synchronization primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

	/* 차단된 스레드 T를 실행 준비 상태로 전환합니다.
	T가 차단되지 않으면 에러입니다.
	(실행 중인 스레드를 준비 상태로 만들려면 thread_yield()를 사용합니다.)

	이 함수는 실행 중인 스레드를 선점하지 않습니다.
	호출자가 인터럽트 자체를 비활성화했다면 스레드를 원자적으로 차단 해제하고 다른 데이터를 업데이트할 수 있을 것으로 기대할 수 있습니다. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered(&ready_list, &t->elem, priority_more, NULL);
	t->status = THREAD_READY;

	if (thread_current() != idle_thread && t->priority > thread_current()->priority) {
		thread_yield();
	}
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* 실행 중인 스레드를 반환합니다.
   이것은 running_thread()에 몇 가지 건전성 검사를 더한 것입니다.
   자세한 내용은 thread.h 상단의 큰 주석을 참조하세요. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* T가 실제로 스레드인지 확인하세요.
	이러한 어설션 중 하나가 실행되면 스레드의 스택이 오버플로되었을 수 있습니다.
	각 스레드의 스택은 4KB 미만이므로 몇 개의 큰 자동 배열이나 중간 정도의 재귀로 인해 스택 오버플로가 발생할 수 있습니다. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* 실행 중인 스레드의 tid를 반환합니다. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* 현재 스레드의 스케줄을 취소하고 파괴합니다.  
호출자에게 반환하지 않습니다. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* 상태를 죽어가는 것으로 설정하고 다른 프로세스를 예약하세요.
	   schedule_tail()을 호출하는 동안 소멸됩니다. */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* CPU를 반환합니다.  
현재 스레드는 절전 상태가 되지 않으며 스케줄러의 변덕에 따라 즉시 다시 스케줄링될 수 있습니다. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, priority_more, NULL);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	thread_current ()->origin_priority = new_priority;
	
	refresh_priority();
	
	if (list_empty(&ready_list)) {
		return;
	}
	if (thread_current()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority) {
		thread_yield();
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->origin_priority = priority;
	t->wait_on_lock = NULL;
	t->magic = THREAD_MAGIC;
	list_init (&t->donor_list);
	list_init (&t->child_list);
	t->running_file = NULL;

	sema_init (&t->fork_sema, 0);
	sema_init (&t->wait_sema, 0);
	sema_init (&t->exit_sema, 0);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip ( instruction pointer - 명령어 포인터 ).
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs ( code segment )
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags ( flag register )
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != idle_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

void thread_sleep(int64_t ticks) {
	struct thread *th_curr = thread_current();
	
	enum intr_level old_level;
	old_level = intr_disable();

	th_curr->wakeup_tick = ticks;
	if (th_curr != idle_thread) {
		list_insert_ordered(&sleep_list, &th_curr->elem, thread_less, NULL);
	}
	update_next_tick_to_awake(ticks);
	do_schedule(THREAD_BLOCKED);
	intr_set_level(old_level);
}

int64_t get_next_tick_to_awake(void) {
	return next_tick_to_awake;
}

void update_next_tick_to_awake(int64_t ticks) {
	next_tick_to_awake = min(next_tick_to_awake, ticks);
}

void thread_awake(int64_t ticks) {
	struct list_elem *sleep_curr_elem = list_begin(&sleep_list);
	struct thread *sleep_curr_t;
	next_tick_to_awake = INT64_MAX;
	
	enum intr_level old_level;
	old_level = intr_disable();

	while (sleep_curr_elem != list_end(&sleep_list)){
		sleep_curr_t = list_entry(sleep_curr_elem, struct thread, elem);

		if (sleep_curr_t->wakeup_tick > ticks )
		{
			update_next_tick_to_awake(sleep_curr_t->wakeup_tick);
			break;
		}
		sleep_curr_elem = list_remove(&sleep_curr_t->elem);
		thread_unblock(sleep_curr_t); // sleep_awake() 안의 thread_yield()를 뺐더니 깨졌던 알람 테게들이 통과했다~!

		// 오류났던 코드: 
		// thread_unblock(sleep_curr_thread);
		// list_pop_front(&sleep_list);
		// sleep_curr_elem = list_begin(&sleep_list);
		// sleep_curr_thread가 여전히 sleep_list의 맨 앞 요소인 상태에서 ready_list에 list_push_back이 되어, 마치 sleep_list의 맨 앞과 ready_list의 맨 뒤가 연결되버리는 오류 상황이 발생했다.
		// 그래서 다음 스레드에 대한 thread_awake() 수행의 thread_unblock() 중 ASSERT (t->status == THREAD_BLOCKED);에서 계속 터졌었다. 이미 ready인 상태의 스레드들이 thread_unblock()에 들어온 것.
	}
	intr_set_level(old_level);
}