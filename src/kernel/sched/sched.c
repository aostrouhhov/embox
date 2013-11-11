/**
 * @file
 * @brief TODO --Alina
 *
 * @date 22.04.10
 * @author Dmitry Avdyukhin
 *          - Initial implementation
 * @author Kirill Skorodumov
 *          - Some insignificant contribution
 * @author Alina Kramar
 *          - Initial work on priority inheritance support
 * @author Eldar Abusalimov
 *          - Rewriting from scratch:
 *              - Interrupts safety, adaptation to Critical API
 *              - @c startq for deferred wake/resume processing
 */

#include <assert.h>
#include <errno.h>
#include <time.h>

#include <kernel/critical.h>
#include <kernel/thread.h>
#include <kernel/thread/current.h>
#include <kernel/sched/sched_strategy.h>
#include <kernel/thread/state.h>
#include <kernel/task/signal.h>

#include <hal/context.h>
#include <hal/ipl.h>

#include <profiler/tracing/trace.h>

#include <embox/unit.h>

#include <kernel/sched.h>

#include <sched.h>





static void sched_switch(void);


CRITICAL_DISPATCHER_DEF(sched_critical, sched_switch, CRITICAL_SCHED_LOCK);

//TODO these variable for scheduler (may be create object scheduler?)
struct runq rq;
static int switch_posted;


static inline int in_harder_critical(void) {
	return critical_inside(__CRITICAL_HARDER(CRITICAL_SCHED_LOCK));
}

static inline int in_sched_locked(void) {
	return !critical_allows(CRITICAL_SCHED_LOCK);
}

/* Activate thread and add it to runq */
static void sched_run(struct thread *t) {
	struct thread *current = thread_self();

	assert(t);
	assert(current != t);
	assert(!thread_state_active(t->state));
	assert(!thread_state_exited(t->state));

	t->state = thread_state_do_activate(t->state);

	runq_queue_insert(&rq.queue, t);
}

int sched_init(struct thread *idle, struct thread *current) {
	assert(idle && current);

	runq_queue_init(&rq.queue);

	sched_run(idle);

	sched_ticker_init();

	return 0;
}

void sched_start(struct thread *t) {
	assert(t);
	assert(!in_harder_critical());
	assert(!thread_state_active(t->state));

	sched_lock();
	{
		sched_run(t);
		if (thread_priority_get(t) > thread_priority_get(thread_self())) {
			sched_post_switch();
		}
	}
	sched_unlock();
}

void sched_wake(struct thread *t) {
	struct thread *current = thread_self();

	assert(t != current);
	assert(in_sched_locked());
	assert(t);
	assert(thread_state_sleeping(t->state));

	t->state = thread_state_do_wake(t->state);
	runq_queue_insert(&rq.queue, t);

	if (thread_priority_get(t) > thread_priority_get(current)) {
		sched_post_switch();
	}
}

void sched_sleep(struct thread *t) {
	assert(in_sched_locked() && !in_harder_critical());
	assert(thread_state_running(t->state));

	t->state = thread_state_do_sleep(t->state);
	/* we don't remove current because it is not in runq, we just mark it as
	 * waiting and after sched switch all will be correct
	 */

	assert(thread_state_sleeping(t->state));

	sched_post_switch();
}

void sched_finish(struct thread *t) {
	assert(!in_harder_critical());
	assert(t);

	sched_lock();
	{
		assert(!thread_state_exited(t->state));

		if (thread_state_running(t->state)) {
			t->state = thread_state_do_exit(t->state);

			if (t != thread_self()) {
				runq_queue_remove(&rq.queue, t);
			} else {
				sched_post_switch();
			}
		} else {
			if (thread_state_sleeping(t->state)) {
				wait_queue_remove(t->wait_link);
			}

			t->state = thread_state_do_exit(t->state);
		}

		assert(thread_state_exited(t->state));
	}
	sched_unlock();
}

void sched_post_switch(void) {
	sched_lock();
	{
		switch_posted = 1;
		critical_request_dispatch(&sched_critical);
	}
	sched_unlock();
}

int sched_change_priority(struct thread *t, sched_priority_t prior) {
	struct thread *current = thread_self();

	assert(t);
	assert((prior >= SCHED_PRIORITY_MIN) && (prior <= SCHED_PRIORITY_MAX));

	sched_lock();
	{
		assert(!thread_state_exited(t->state));

		thread_priority_set(t, prior);

		/* if in runq */
		if (thread_state_running(t->state) && current != t) {
			runq_queue_remove(&rq.queue, t);
			runq_queue_insert(&rq.queue, t);

			if (prior > thread_priority_get(current)) {
				sched_post_switch();
			}
		}

		assert(thread_priority_get(t) == prior);
	}
	sched_unlock();

	return 0;
}

/**
 * Called by critical dispatching code with IRQs disabled.
 */
static void sched_switch(void) {
	struct thread *prev, *next;
	clock_t new_clock;

	assert(!in_sched_locked());

	sched_lock();
	{
		if (!switch_posted) {
			goto out;
		}
		switch_posted = 0;

		ipl_enable();

		prev = thread_get_current();

		if (thread_state_running(prev->state)) {
			runq_queue_insert(&rq.queue, prev);
		}

		next = runq_queue_extract(&rq.queue);

		assert(next != NULL);
		assert(thread_state_running(next->state));

		if (prev == next) {
			ipl_disable();
			goto out;
		} else {
			prev->state = thread_state_do_outcpu(prev->state);
			next->state = thread_state_do_oncpu(next->state);
		}

		if (prev->policy == SCHED_FIFO && next->policy != SCHED_FIFO) {
			sched_ticker_init();
		}

		if (prev->policy != SCHED_FIFO && next->policy == SCHED_FIFO) {
			sched_ticker_fini(&rq);
		}

		/* Running time recalculation */
		new_clock = clock();
		sched_timing_stop(prev, new_clock);
		sched_timing_start(next, new_clock);

		trace_point("context switch");

		ipl_disable();

		thread_set_current(next);
		context_switch(&prev->context, &next->context);
	}
out:
	sched_unlock_noswitch();

	task_signal_hnd();
}


