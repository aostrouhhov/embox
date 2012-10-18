/**
 * @file
 *
 * @brief Testing interface for thread with different priority
 *
 * @date Oct 17, 2012
 * @author: Anton Bondarev
 */

#include <embox/test.h>
#include <unistd.h>

#include <kernel/thread/api.h>

EMBOX_TEST_SUITE("test for different priority threads");

#define THREADS_QUANTITY  0x100

static void *thread_run(void *arg) {
	sleep(100);
	return 0;
}

TEST_CASE("Create 256 threads with different priority") {
	struct thread *threads[THREADS_QUANTITY];
	int i;

	for(i = 0; i < THREADS_QUANTITY; i++) {
		test_assert_zero(thread_create(&threads[i], 0, thread_run, NULL));
		test_assert_zero(thread_set_priority(threads[i], i));
	}
}
