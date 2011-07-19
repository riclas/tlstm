/**
 * threadpool.c
 *
 * This file will contain your implementation of a threadpool.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#include "threadpool.h"

typedef struct operations_st
{
	dispatch_fn func_to_dispatch;
	task_data_t * func_arg;
} operation;

typedef struct _threadpool_st {
	pthread_t      *array;      // The threads themselves.

	pthread_mutex_t mutex;      // protects all vars declared below.
	pthread_cond_t  job_posted; // dispatcher: "Hey guys, there's a job!"
	pthread_cond_t  job_taken;  // a worker: "Got it!"

	int             arrsz;      // Number of entries in array.
	operation      *ops;       //array of operations to run
	int             num_ops_waiting, num_ops_working;
	int             next_op;
} _threadpool;


/*----------------------------------------------------------------------
  Define the life of a working thread.
 */
void * do_work(void * owning_pool) {
	// Convert pointer to owning pool to proper type.
	_threadpool *pool = (_threadpool *) owning_pool;

	// When we get a posted job, we copy it into these local vars.
	dispatch_fn  myjob;
	task_data_t *myarg;

	TM_THREAD_ENTER();

	// Main loop: wait for job posting, do job(s) ... forever
	for( ; ; ) {
		// Grab mutex so we can begin waiting for a job
		if (0 != pthread_mutex_lock(&pool->mutex))
		{
			perror("\nMutex lock failed!:");
			exit(EXIT_FAILURE);
		}

		while(pool->num_ops_waiting == 0)
		{
			pthread_cond_wait(&pool->job_posted, &pool->mutex);
		}

		// while we find work to do
		myjob = pool->ops[pool->next_op].func_to_dispatch;
		myarg = pool->ops[pool->next_op].func_arg;

//		printf("next op %d task_id %d\n", pool->next_op, myarg->task_id);
		pool->next_op = (pool->next_op + 1) % pool->arrsz;

		myarg->tx = tx;

		pool->num_ops_waiting--;

		pthread_cond_signal(&pool->job_posted);

		// Yield mutex so other jobs can be taken
		if (0 != pthread_mutex_unlock(&pool->mutex))
		{
			perror("\n\nMutex unlock failed!:");
			exit(EXIT_FAILURE);
		}

		// Run the job we've taken
		myjob(myarg);

		// Grab mutex so we can grab posted job, or (if no job is posted)
		//   begin waiting for next posting.
		if (0 != pthread_mutex_lock(&pool->mutex))
		{
			perror("\n\nMutex lock failed!:");
			exit(EXIT_FAILURE);
		}

		pool->num_ops_working--;

		pthread_cond_signal(&pool->job_taken);

		// Yield mutex so other jobs can be posted
		if (0 != pthread_mutex_unlock(&pool->mutex))
		{
			perror("\n\nMutex unlock failed!:");
			exit(EXIT_FAILURE);
		}
	}

	return NULL;
}  

/*----------------------------------------------------------------------
  Create a thread pool.
 */
threadpool create_threadpool(int num_threads_in_pool) {
	_threadpool *pool;  // pool we create and hand back
	int i;              // work var

	// sanity check the argument
	if (num_threads_in_pool <= 0)
		return NULL;

	// create the _threadpool struct
	pool = (_threadpool *) malloc(sizeof(_threadpool));
	if (pool == NULL) {
		fprintf(stderr, "\n\nOut of memory creating a new threadpool!\n");
		return NULL;
	}

	// initialize everything but the thread array
	pthread_mutex_init(&(pool->mutex), NULL);
	pthread_cond_init(&(pool->job_posted), NULL);
	pthread_cond_init(&(pool->job_taken), NULL);
	pool->arrsz = num_threads_in_pool;
	pool->ops = (operation *) malloc(num_threads_in_pool * sizeof(operation));
	pool->num_ops_waiting = pool->num_ops_working = 0;
	pool->next_op = 0;
	if (pool == NULL) {
		fprintf(stderr, "\n\nOut of memory creating a new operations array!\n");
		return NULL;
	}

	// create the array of threads within the pool
	pool->array =
			(pthread_t *) malloc (pool->arrsz * sizeof(pthread_t));
	if (NULL == pool->array) {
		fprintf(stderr, "\n\nOut of memory allocating thread array!\n");
		free(pool);
		pool = NULL;
		return NULL;
	}

	// bring each thread to life
	for (i = 0; i < pool->arrsz; ++i) {
		if (0 != pthread_create(pool->array + i, NULL, do_work, (void *) pool)) {
			perror("\n\nThread creation failed:");
			exit(EXIT_FAILURE);
		}
		pthread_detach(pool->array[i]);  // automatic cleanup when thread exits.
	}

	return (threadpool) pool;
}


/*----------------------------------------------------------------------
  Dispatch a thread
 */
void dispatch(threadpool from_me, dispatch_fn dispatch_to_here,
		task_data_t *arg, int task_id) {
	_threadpool *pool = (_threadpool *) from_me;

	// Grab the mutex
	if (0 != pthread_mutex_lock(&pool->mutex)) {
		perror("Mutex lock failed (!!):");
		exit(-1);
	}

	while(pool->num_ops_working == pool->arrsz)
	{
		pthread_cond_signal(&pool->job_posted);
		pthread_cond_wait(&pool->job_taken,&pool->mutex);
	}

	// Finally, there's room to post a job. Do so and signal workers.
	arg->val = rand_r(&arg->seed) % 100;
	arg->task_id = task_id;

	pool->ops[arg->task_id % pool->arrsz].func_arg = arg;
	pool->ops[arg->task_id % pool->arrsz].func_to_dispatch = dispatch_to_here;

	pool->num_ops_waiting++;
	pool->num_ops_working++;

	pthread_cond_signal(&pool->job_posted);

	// Yield mutex so a worker can pick up the job
	if (pthread_mutex_unlock(&pool->mutex) != 0)
	{
		perror("\n\nMutex unlock failed!:");
		exit(EXIT_FAILURE);
	}
}
