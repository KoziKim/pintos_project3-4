#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS 부팅 이후 타이머 틱 횟수입니다. */
static int64_t ticks;

/* 타이머 틱당 루프 수입니다.
   timer_calibrate()에 의해 초기화됩니다. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* 8254 프로그래머블 인터벌 타이머(PIT)가 초당 PIT_FREQ 횟수를 인터럽트하도록 설정하고 해당 인터럽트를 등록합니다. */
void
timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값, 가장 가까운 값으로 반올림. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* 짧은 지연을 구현하는 데 사용되는 loops_per_tick을 보정합니다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* 타이머 틱 1보다 큰 2의 거듭제곱으로 틱당 대략적인 loops_per_tick을 계산합니다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* 다음 8비트의 loops_per_tick을 세분화합니다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* OS 부팅 이후 타이머 틱 횟수를 반환합니다. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* THEN 이후 경과한 타이머 틱 수를 반환하며, 이 값은 timer_ticks()가 반환한 값이어야 합니다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks();
	ASSERT (intr_get_level () == INTR_ON);
	thread_sleep(start + ticks);	
	// while(timer_elapsed(start)<ticks)
	// 	thread_yield()
}

/* 약 MS 밀리초 동안 실행을 일시 중단합니다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* 약 US 마이크로초 동안 실행을 일시 중단합니다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 약 NS 나노초 동안 실행을 일시 중단합니다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 인쇄합니다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* 타이머 인터럽트 핸들러. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
	if (get_next_tick_to_awake() <= ticks) { // 매 틱마다 깨우는 것이 아니라 깨울 틱을 확인하고 깨움.
		thread_awake(ticks);
	}
}

/* LOOPS 반복이 타이머 틱을 두 번 이상 기다리면 참을 반환하고, 그렇지 않으면 거짓을 반환합니다. */
static bool
too_many_loops (unsigned loops) {
	/* 타이머 틱을 기다립니다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* LOOPS 루프를 실행합니다. */
	start = ticks;
	busy_wait (loops);

	/* 틱 수가 변경되면 너무 오래 반복한 것입니다. */
	barrier ();
	return start != ticks;
}

/* 짧은 지연을 구현하기 위해 간단한 루프 LOOPS 횟수를 반복합니다.

   코드 정렬이 타이밍에 큰 영향을 미칠 수 있으므로 
   이 함수가 다른 위치에서 다르게 정렬되면 결과를 예측하기 어렵기 때문에 NO_INLINE을 표시했습니다. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* 약 NUM/DENOM 초 동안 절전합니다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* 반올림하여 NUM/DENOM 초를 타이머 틱으로 변환합니다.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM 틱.
	   1초 / TIMER_FREQ 틱
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 적어도 한 번의 전체 타이머 틱을 기다리고 있습니다.  
			timer_sleep()을 사용하면 다른 프로세스에 CPU를 양보하게 되므로 주의하세요. */
		timer_sleep (ticks);
	} else {
		/* 그렇지 않으면 보다 정확한 서브 틱 타이밍을 위해 바쁜 대기 루프를 사용합니다.
			오버플로우 가능성을 피하기 위해 분자와 분모를 1000으로 축소합니다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
