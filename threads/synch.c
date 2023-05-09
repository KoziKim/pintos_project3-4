/* 이 파일은 Nachos 교육용 운영체제의 소스 코드에서 파생되었습니다.  Nachos 저작권 고지는 아래에 전문을 옮겨 놓았습니다. */

/* 저작권 (c) 1992-1996 캘리포니아 대학교 리전트.
모든 권리 보유.

위의 저작권 고지 및 다음 두 단락이 이 소프트웨어의 모든 사본에 표시되는 경우, 이 소프트웨어 및 해당 설명서를 어떠한 목적으로든 수수료 없이, 서면 계약 없이 사용, 복사, 수정 및 배포할 수 있는 권한이 부여됩니다.
어떠한 경우에도 캘리포니아 대학교는 본 소프트웨어 및 해당 설명서의 사용으로 인해 발생하는 직접, 간접, 특별, 부수적 또는 결과적 손해에 대해 어떠한 당사자에게도 책임을 지지 않으며, 이는 캘리포니아 대학교가 그러한 손해의 가능성을 사전에 통지받은 경우에도 마찬가지입니다.
캘리포니아 대학교는 상품성 및 특정 목적에의 적합성에 대한 묵시적 보증을 포함하되 이에 국한되지 않는 모든 보증을 구체적으로 부인합니다.  본 계약에 따라 제공되는 소프트웨어는 "있는 그대로" 제공되며, 캘리포니아 대학교는 유지보수, 지원, 업데이트, 개선 또는 수정을 제공할 의무가 없습니다.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* 세마포어 SEMA를 VALUE로 초기화합니다.  세마포어는
   음수가 아닌 정수와 두 개의 원자 연산자로 구성됩니다.
   조작할 수 있습니다:

   - down 또는 "P": 값이 양수가 될 때까지 기다린 다음
   감소시킵니다.

   - up 또는 "V": 값을 증가시킵니다(대기 중인 스레드가 하나 있다면
   스레드를 깨웁니다). */
static bool
cond_priority_more (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED);
			
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어에서 다운 또는 "P" 연산.  세마값이 양수가 될 때까지 기다렸다가
	원자적으로 감소시킵니다.

이 함수는 잠자기 상태가 될 수 있으므로,   인터럽트 핸들러 내에서 호출해서는 안 됩니다.  이 함수는 인터럽트를 비활성화한 상태에서 호출할 수 있습니다.
인터럽트를 비활성화한 상태에서 호출할 수 있지만, 잠자기 상태가 되면 다음 스케줄된 스레드가 인터럽트를 다시 켜게 됩니다. 이 함수는 sema_down 함수입니다. 
*/
static bool
sema_priority_more (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->priority > b->priority;
}

void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_insert_ordered(&sema->waiters, &thread_current ()->elem, sema_priority_more, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어에 대한 다운 또는 "P" 연산, 하지만  
   세마포어가 아직 0이 아닌 경우에만. 세마포어가 감소하면 참을 반환하고
   그렇지 않으면 거짓을 반환합니다.

   이 함수는 인터럽트 핸들러에서 호출할 수 있습니다. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* 세마포어에서 위 또는 "V" 연산.  SEMA의 값을 증가시키고
   SEMA를 기다리는 스레드 중 하나라도 있다면 깨웁니다.

   이 함수는 인터럽트 핸들러에서 호출할 수 있습니다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	sema->value++;
	list_sort(&sema->waiters, sema_priority_more, NULL);
	if (!list_empty (&sema->waiters)) {
		struct thread *th = list_entry (list_front (&sema->waiters), struct thread, elem);
		thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
	}
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* 한 쌍의 스레드 사이를 "핑퐁" 
   제어하는 세마포어를 자체 테스트합니다. 
   printf() 호출을 삽입하여 
   무슨 일이 일어나고 있는지 확인합니다. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수입니다. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* LOCK을 초기화합니다.  잠금은 주어진 시간에 최대 하나의
   스레드만 보유할 수 있습니다.  우리의 잠금은 "재귀적"이 아닙니다.
   즉, 현재 잠금을 보유하고 있는 스레드가 해당 잠금을 획득하려고 하면
   해당 잠금을 획득하려고 시도하는 것은 오류입니다.

   잠금은 초기값이 1인 세마포어의 특수화입니다. 잠금과 이러한 세마포어의
   차이점은 두 가지입니다.  첫째, 세마포어는 1보다 큰 값을 가질 수 있지만, 잠금은 한 번에 하나의 스레드만 소유할 수 있습니다.  
   둘째, 세마포어는 소유자가 없습니다,
   즉, 한 스레드가 세마포어를 "다운"시킨 다음 다른 스레드가 세마포어를 "업"할 수 있지만, 잠금이 있는 경우 동일한 스레드가 잠금을 획득하고 해제해야 합니다.  이러한 제한이 부담스럽다면, 이는 락 대신 세마포어를 사용해야 한다는 좋은 신호입니다,
   세마포어를 사용해야 한다는 좋은 신호입니다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

void
priority_donate (struct lock *lock) {
	struct thread *curr_th = thread_current();
	int curr_priority = curr_th->priority;

	// while (lock->holder->wait_on_lock->holder != NULL) { // 원래 코드...
	for (int dep=0; dep<8; dep++) {
		if (!curr_th->wait_on_lock) {
			break;
		}
		curr_th = curr_th->wait_on_lock->holder;
		curr_th->priority = curr_priority;

		// list_push_front(&lock->holder->donor_list, &thread_current()->donor_elem);
		// list_sort(&lock->holder->donor_list, sema_priority_more, NULL);

		// lock->holder->priority = thread_get_priority();
		// curr_th = curr_th->wait_on_lock->holder;
	}
}

// void
// remove_with_lock (struct lock *lock) {
// 	struct thread *curr_th = list_entry(list_begin(&thread_current()->donor_list), struct thread, donor_elem);
	
// 	while (curr_th->wait_on_lock->holder!=NULL) {
// 		struct list_elem *curr_donor_elem = list_begin(&curr_th->donor_list);

// 		if (curr_th->wait_on_lock == lock) {
// 			curr_donor_elem = list_remove(curr_donor_elem);
// 		}
// 		curr_th = curr_th->wait_on_lock->holder;
// 	}
// }

void
remove_with_lock (struct lock *lock)
{
  struct list_elem *e;
  struct thread *cur = thread_current ();

  for (e = list_begin (&cur->donor_list); e != list_end (&cur->donor_list); e = list_next (e)){
    struct thread *t = list_entry (e, struct thread, donor_elem);
    if (t->wait_on_lock == lock)
      list_remove (&t->donor_elem);
  }
}

void 
refresh_priority(void) {
	struct thread *curr_th = thread_current();

	curr_th->priority = curr_th->origin_priority;

	if (!list_empty(&curr_th->donor_list)) {
		list_sort(&curr_th->donor_list, sema_priority_more, NULL);

		struct thread *highest_donor = list_entry(list_front(&curr_th->donor_list), struct thread, donor_elem);

		if (highest_donor->priority > curr_th->priority) {
			curr_th->priority = highest_donor->priority;
		}
	}
}

/* 필요한 경우 잠금을 획득하고 잠금을 사용할 수 있을 때까지 대기합니다. 현재 스레드가 이미 잠금을 보유하고 있지 않아야 합니다.

   이 함수는 잠자기 상태일 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안 됩니다.
   이 함수는 인터럽트를 비활성화한 상태에서 호출할 수 있지만, 절전해야 할 경우 인터럽트가 다시 켜집니다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	if (lock->holder != NULL) {
		thread_current()->wait_on_lock = lock;
		list_push_front(&lock->holder->donor_list, &thread_current()->donor_elem);
		priority_donate(lock);
	}
	sema_down (&lock->semaphore);
	thread_current()->wait_on_lock = NULL;
	lock->holder = thread_current();
}

/* LOCK을 획득하려고 시도하고 성공하면 true를, 실패하면 false를 반환합니다.
   현재 스레드가 이미 잠금을 보유하고 있지 않아야 합니다.

   이 함수는 잠자기 상태가 아니므로 인터럽트 핸들러 내에서 호출할 수 있습니다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 현재 스레드가 소유하고 있어야 하는 LOCK을 해제합니다.
   이것이 lock_release 함수입니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로 인터럽트 핸들러 내에서 잠금을 해제하려고 시도하는 것은 의미가 없습니다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	remove_with_lock(lock);
	refresh_priority();

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 보유하면 참을 반환하고, 그렇지 않으면 거짓을 반환합니다.
   (다른 스레드가 잠금을 보유하고 있는지 테스트하는 것은 느릴 수 있습니다.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 목록에 하나의 세마포어가 있습니다. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* 조건 변수 COND를 초기화합니다. 
   조건 변수를 사용하면 한 코드가 조건에 대한 신호를 보내고 협력 코드가 신호를 수신하고 그에 따라 동작할 수 있습니다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

	/* 원자적으로 LOCK을 해제하고 다른 코드에 의해 COND가 신호될 때까지 기다립니다.
	COND가 신호를 받은 후 LOCK을 다시 획득한 후 반환합니다.
	이 함수를 호출하기 전에 LOCK을 유지해야 합니다.

   	이 함수가 구현하는 모니터는 "Hoare" 스타일이 아닌 "Mesa" 스타일로, 신호 송수신이 원자 연산이 아닙니다.
	따라서 일반적으로 호출자는 대기가 완료된 후 조건을 재확인하고 필요한 경우 다시 대기해야 합니다.

	주어진 조건 변수는 하나의 잠금에만 연결되지만 하나의 잠금은 여러 개의 조건 변수와 연결될 수 있습니다.
	즉, 잠금에서 조건 변수로의 일대다 매핑이 존재합니다.

	이 함수는 잠자기 상태가 될 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안 됩니다.
	이 함수는 인터럽트를 비활성화한 상태에서 호출할 수 있지만, 잠자기 상태가 되어야 할 경우 인터럽트가 다시 켜집니다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back(&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* COND에서 대기 중인 스레드가 있는 경우(LOCK으로 보호됨), 이 함수는 스레드 중 하나에 대기 상태를 깨우라는 신호를 보냅니다.
   이 함수를 호출하기 전에 LOCK 을 유지해야 합니다.

   인터럽트 핸들러는 잠금을 획득할 수 없으므로 인터럽트 핸들러 내에서 조건 변수에 신호를 보내려고 하는 것은 의미가 없습니다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));
	if (!list_empty (&cond->waiters)) {
		list_sort(&cond->waiters, cond_priority_more, NULL);

		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);

	}
}

	/* COND에서 대기 중인 모든 스레드(있는 경우)를 깨웁니다(LOCK으로 보호됨).
	이 함수를 호출하기 전에 LOCK을 유지해야 합니다.

	인터럽트 핸들러는 잠금을 획득할 수 없으므로 인터럽트 핸들러 내에서 조건 변수에 신호를 보내려고 하는 것은 의미가 없습니다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

static bool
cond_priority_more (const struct list_elem *a_, const struct list_elem *b_,void *aux UNUSED) 
{
	const struct semaphore_elem *a = list_entry (a_, struct semaphore_elem, elem);
	const struct semaphore_elem *b = list_entry (b_, struct semaphore_elem, elem);

	const struct thread *a_th = list_entry(list_front(&a->semaphore.waiters), struct thread, elem);
	const struct thread *b_th = list_entry(list_front(&b->semaphore.waiters), struct thread, elem);
  return a_th->priority > b_th->priority;
}