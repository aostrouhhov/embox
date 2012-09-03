/**
 * @file
 * @brief Tests mutex lock/unlock methods.
 *
 * @details The test creates two threads: both of them is created with
 *      (suspend) flag THREAD_FLAG_SUSPENDED. Than first thread is waked up
 *      and it is executing while it will not finish. The test checks a correct
 *      sequence of stages.
 *
 *
 * @date Apr 18, 2012
 * @author Bulychev Anton
 */

#include <embox/test.h>
#include <kernel/thread/sync/mutex.h>
#include <unistd.h>

static struct thread *low, *high;
static struct mutex m;

EMBOX_TEST_SUITE("Mutex test");

TEST_SETUP(setup);

TEST_CASE("General") {
	test_assert_zero(thread_resume(low));
	test_assert_zero(thread_join(low, NULL));
	test_assert_zero(thread_join(high, NULL));
	test_assert_emitted("abcdefg");
}

static void *low_run(void *arg) {
	test_emit('a');
	mutex_lock(&m);
	test_emit('b');
	test_assert_zero(thread_resume(high));
	test_emit('d');
	mutex_unlock(&m);
	test_emit('g');
	return NULL;
}

static void *high_run(void *arg) {
	test_emit('c');
	mutex_lock(&m);
	test_emit('e');
	mutex_unlock(&m);
	test_emit('f');
	return NULL;
}

static int setup(void) {
	thread_priority_t l = 10, h = 20;

	mutex_init(&m);
	test_assert_zero(thread_create(&low, THREAD_FLAG_SUSPENDED, low_run, NULL));
	test_assert_zero(thread_create(&high, THREAD_FLAG_SUSPENDED, high_run, NULL));
	test_assert_zero(thread_set_priority(low, l));
	test_assert_zero(thread_set_priority(high, h));
	return 0;
}

