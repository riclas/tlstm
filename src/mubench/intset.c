/*
 * File:
 *   intset.c
 * Author(s):
 *   Pascal Felber <pascal.felber@unine.ch>
 * Description:
 *   Integer set stress test.
 *
 * Copyright (c) 2007-2008.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>


#ifdef MUBENCH_WLPDSTM
#include <atomic_ops.h>
#include "stm.h"
#elif defined MUBENCH_TANGER
#include "tanger-stm.h"
#endif /* MUBENCH_TANGER */

#ifdef DEBUG
#define IO_FLUSH                        fflush(NULL)
/* Note: stdio is thread-safe */
#endif

#ifdef MUBENCH_WLPDSTM
#define fetch_and_inc_full(addr) (AO_fetch_and_add1_full((volatile AO_t *)(addr)))

#define START                           BEGIN_TRANSACTION_DESC
#define START_ID(ID,start,commit,ptid,taskid)                    BEGIN_TRANSACTION_DESC_ID(ID,start,commit,ptid,taskid)
#define START_RO                        START
#define START_RO_ID(ID,start,commit,ptid,taskid)                 START_ID(ID,start,commit,ptid,taskid)
#define LOAD(addr)                      wlpdstm_read_word_desc(tx, (Word *)(addr))
#define STORE(addr, value)              wlpdstm_write_word_desc(tx, (Word *)addr, (Word)value)
#define COMMIT                          END_TRANSACTION
#define MALLOC(size)                    wlpdstm_tx_malloc_desc(tx, size)
#define FREE(addr, size)                wlpdstm_tx_free_desc(tx, addr, size)

#elif defined MUBENCH_TANGER

#define START                           tanger_begin()
#define START_ID(ID)                    tanger_begin()
#define START_RO                        tanger_begin()
#define START_RO_ID(ID)                 tanger_begin()
#define LOAD(addr)                      (*(addr))
#define STORE(addr, value)              (*(addr) = (value))
#define COMMIT                          tanger_commit()
#define MALLOC(size)                    malloc(size)
#define FREE(addr, size)                free(addr)

#elif defined MUBENCH_SEQUENTIAL

#define START                           /* nothing */
#define START_ID(ID)                    /* nothing */
#define START_RO                        /* nothing */
#define START_RO_ID(ID)                 /* nothing */
#define LOAD(addr)                      (*(addr))
#define STORE(addr, value)              (*(addr) = (value))
#define COMMIT                          /* nothing */
#define MALLOC(size)                    malloc(size)
#define FREE(addr, size)                free(addr)

#endif /* MUBENCH_SEQUENTIAL */

#define DEFAULT_DURATION                10000
#define DEFAULT_INITIAL                 256
#define DEFAULT_NB_THREADS              1
#define DEFAULT_NB_TASKS                1
#define DEFAULT_RANGE                   0xFFFF
#define DEFAULT_SEED                    0
#define DEFAULT_UPDATE                  20

#define XSTR(s)                         STR(s)
#define STR(s)                          #s

/* ################################################################### *
 * GLOBALS
 * ################################################################### */

#ifdef MUBENCH_WLPDSTM
static volatile AO_t stop;
#else
static volatile int stop;
#endif /* MUBENCH_WLPDSTM */

#ifdef USE_RBTREE

/* ################################################################### *
 * RBTREE
 * ################################################################### */

#ifdef  MUBENCH_WLPDSTM
#define TM_ARGDECL_ALONE                tx_desc* tx
#define TM_ARGDECL                      tx_desc* tx,
#define TM_ARG                          tx, 
#define TM_ARG_LAST                     , tx
#define TM_ARG_ALONE                    tx
#define TM_STARTUP()                    wlpdstm_global_init(nb_tasks)
#ifdef COLLECT_STATS
#define TM_SHUTDOWN()                   wlpdstm_print_stats()
#else
#define TM_SHUTDOWN()                   /* nothing */
#endif /* COLLECT_STATS */

#define TM_THREAD_ENTER()               wlpdstm_thread_init(); \
										tx_desc *tx = wlpdstm_get_tx_desc()

#define TM_THREAD_EXIT()                /* nothing */

#elif defined MUBENCH_TANGER
#define TM_ARGDECL_ALONE                /* nothing */
#define TM_ARGDECL                      /* nothing */
#define TM_ARG                          /* nothing */
#define TM_ARG_ALONE                    /* nothing */
#define TM_ARG_LAST                     /* nothing */
#define TM_STARTUP()                    tanger_init()
#define TM_SHUTDOWN()                   tanger_shutdown()
#define TM_THREAD_ENTER()               tanger_thread_init()
#define TM_THREAD_EXIT()                tanger_thread_shutdown()

#elif defined MUBENCH_SEQUENTIAL

#define TM_ARGDECL_ALONE                /* nothing */
#define TM_ARGDECL                      /* nothing */
#define TM_ARG                          /* nothing */
#define TM_ARG_ALONE                    /* nothing */
#define TM_ARG_LAST                     /* nothing */
#define TM_STARTUP()                    /* nothing */
#define TM_SHUTDOWN()                   /* nothing */
#define TM_THREAD_ENTER()               /* nothing */
#define TM_THREAD_EXIT()                /* nothing */

#endif /* MUBENCH_SEQUENTIAL */

#define TM_SHARED_READ(var)             LOAD(&(var))
#define TM_SHARED_READ_P(var)           LOAD(&(var))

#define TM_SHARED_WRITE(var, val)       STORE(&(var), val)
#define TM_SHARED_WRITE_P(var, val)     STORE(&(var), val)

#define TM_MALLOC(size)                 MALLOC(size)
#define TM_FREE(ptr)                    FREE(ptr, sizeof(*ptr))

#include "rbtree.h"

#include "rbtree.c"

typedef rbtree_t intset_t;

intset_t *set_new()
{
  return rbtree_alloc();
}

void set_delete(intset_t *set)
{
  rbtree_free(set);
}

int set_size(intset_t *set)
{
  int size;
  node_t *n;

  if (!rbtree_verify(set, 0)) {
    printf("Validation failed!\n");
    exit(1);
  }

  size = 0;
  for (n = firstEntry(set); n != NULL; n = successor(n))
    size++;

  return size;
}

int set_add_seq(intset_t *set, intptr_t val) {
	return !rbtree_insert(set, val, val);
}

#ifdef MUBENCH_WLPDSTM
int set_add(intset_t *set, intptr_t val, unsigned start, unsigned commit, unsigned ptid, unsigned task_id, tx_desc *tx)
{
	int res = 0;

	START_ID(0, start, commit, ptid, task_id);
	res = !TMrbtree_insert(tx, set, val, val);
	COMMIT;

	return res;
}
#elif defined MUBENCH_TANGER || defined MUBENCH_SEQUENTIAL
int set_add(intset_t *set, intptr_t val)
{
	int res = 0;
	
	START_ID(0);
	res = !rbtree_insert(set, val, val);
	COMMIT;
	
	return res;
}
#endif /* MUBENCH_TANGER || MUBENCH_SEQUENTIAL */

#ifdef MUBENCH_WLPDSTM
int set_remove(intset_t *set, intptr_t val, unsigned start, unsigned commit, unsigned ptid, unsigned task_id, tx_desc *tx)
{
	int res = 0;

    START_ID(1, start, commit, ptid, task_id);
    res = TMrbtree_delete(tx, set, val);
    COMMIT;

	return res;
}
#elif defined MUBENCH_TANGER || defined MUBENCH_SEQUENTIAL
int set_remove(intset_t *set, intptr_t val)
{
	int res = 0;

	START_ID(1);
	res = rbtree_delete(set, val);
	COMMIT;
	
	return res;
}
#endif /* MUBENCH_TANGER || MUBENCH_SEQUENTIAL */

#ifdef MUBENCH_WLPDSTM
int set_contains(intset_t *set, intptr_t val, unsigned start, unsigned commit, unsigned ptid, unsigned task_id, tx_desc *tx)
{
	int res = 0;

    START_RO_ID(2, start, commit, ptid, task_id);
    res = TMrbtree_contains(tx, set, val);
    COMMIT;

	return res;
}
#elif defined MUBENCH_TANGER || defined MUBENCH_SEQUENTIAL
int set_contains(intset_t *set, intptr_t val)
{
	int res = 0;

	START_RO_ID(2);
	res = rbtree_contains(set, val);
	COMMIT;
	
	return res;
}
#endif /* MUBENCH_TANGER || MUBENCH_SEQUENTIAL */

#else /* USE_RBTREE */

/* ################################################################### *
 * INT SET
 * ################################################################### */

typedef struct node {
  int val;
  struct node *next;
} node_t;

typedef struct intset {
  node_t *head;
} intset_t;

#ifdef MUBENCH_WLPDSTM
node_t *new_node(int val, node_t *next, tx_desc *tx)
{
	node_t *node;

    node = (node_t *)MALLOC(sizeof(node_t));

	if (node == NULL) {
		perror("malloc");
		exit(1);
	}

	node->val = val;
	node->next = next;

	return node;
}
#elif defined MUBENCH_TANGER || defined MUBENCH_SEQUENTIAL
node_t *new_node(int val, node_t *next)
{
	node_t *node;
	
	node = (node_t *)malloc(sizeof(node_t));

	if (node == NULL) {
		perror("malloc");
		exit(1);
	}
	
	node->val = val;
	node->next = next;
	
	return node;
}
#endif /* MUBENCH_TANGER || MUBENCH_SEQUENTIAL */

intset_t *set_new()
{
  intset_t *set;
  node_t *min, *max;

  if ((set = (intset_t *)malloc(sizeof(intset_t))) == NULL) {
    perror("malloc");
    exit(1);
  }
  max = new_node(INT_MAX, NULL, NULL);
  min = new_node(INT_MIN, max, NULL);
  set->head = min;

  return set;
}

void set_delete(intset_t *set)
{
  node_t *node, *next;

  node = set->head;
  while (node != NULL) {
    next = node->next;
    free(node);
    node = next;
  }
  free(set);
}

int set_size(intset_t *set)
{
  int size = 0;
  node_t *node;

  /* We have at least 2 elements */
  node = set->head->next;
  while (node->next != NULL) {
    size++;
    node = node->next;
  }

  return size;
}

int set_add_seq(intset_t *set, int val)
{
	int result;
	node_t *prev, *next;
	int v;
	
#ifdef DEBUG
	printf("++> set_add(%d)\n", val);
	IO_FLUSH;
#endif

    prev = (node_t *)LOAD(&set->head);
    next = (node_t *)LOAD(&prev->next);
    while (1) {
		v = (int)LOAD(&next->val);
		if (v >= val)
			break;
		prev = next;
		next = (node_t *)LOAD(&prev->next);
    }
    result = (v != val);
    if (result) {
		STORE(&prev->next, new_node(val, next, tx));
    }
	
	return result;
}

#ifdef MUBENCH_WLPDSTM
int set_add(intset_t *set, int val, tx_desc *tx)
{
	int result;
	node_t *prev, *next;
	int v;

#ifdef DEBUG
	printf("++> set_add(%d)\n", val);
	IO_FLUSH;
#endif

    START_ID(3);
    prev = (node_t *)LOAD(&set->head);
    next = (node_t *)LOAD(&prev->next);
    while (1) {
      v = (int)LOAD(&next->val);
      if (v >= val)
        break;
      prev = next;
      next = (node_t *)LOAD(&prev->next);
    }
    result = (v != val);
    if (result) {
      STORE(&prev->next, new_node(val, next, tx));
    }
    COMMIT;

	return result;
}
#elif defined MUBENCH_TANGER || defined MUBENCH_SEQUENTIAL
int set_add(intset_t *set, int val)
{
	int result;
	node_t *prev, *next;
	int v;
	
#ifdef DEBUG
	printf("++> set_add(%d)\n", val);
	IO_FLUSH;
#endif
	
	START_ID(3);
	prev = set->head;
	next = prev->next;
	while (next->val < val) {
		prev = next;
		next = prev->next;
	}
	result = (next->val != val);
	if (result) {
		prev->next = new_node(val, next, tx);
	}
	COMMIT;
	
	return result;
}
#endif /* MUBENCH_TANGER || MUBENCH_SEQUENTIAL */

#ifdef MUBENCH_WLPDSTM
int set_remove(intset_t *set, int val, tx_desc *tx)
{
	int result;
	node_t *prev, *next;
	int v;
	node_t *n;

#ifdef DEBUG
	printf("++> set_remove(%d)\n", val);
	IO_FLUSH;
#endif

    START_ID(4);
    prev = (node_t *)LOAD(&set->head);
    next = (node_t *)LOAD(&prev->next);
    while (1) {
      v = (int)LOAD(&next->val);
      if (v >= val)
        break;
      prev = next;
      next = (node_t *)LOAD(&prev->next);
    }
    result = (v == val);
    if (result) {
      n = (node_t *)LOAD(&next->next);
      STORE(&prev->next, n);
      /* Free memory (delayed until commit) */
      FREE(next, sizeof(node_t));
    }
    COMMIT;

	return result;
}
#elif defined MUBENCH_TANGER || defined MUBENCH_SEQUENTIAL
int set_remove(intset_t *set, int val)
{
	int result;
	node_t *prev, *next;
	int v;
	node_t *n;
	
#ifdef DEBUG
	printf("++> set_remove(%d)\n", val);
	IO_FLUSH;
#endif
	
	START_ID(4)
	prev = set->head;
	next = prev->next;
	while (next->val < val) {
		prev = next;
		next = prev->next;
	}
	result = (next->val == val);
	if (result) {
		prev->next = next->next;
		free(next);
	}
	COMMIT;
	
	return result;
}
#endif /* MUBENCH_TANGER || MUBENCH_SEQUENTIAL */

#ifdef MUBENCH_WLPDSTM
int set_contains(intset_t *set, int val, tx_desc *tx)
{
	int result;
	node_t *prev, *next;
	int v;

#ifdef DEBUG
	printf("++> set_contains(%d)\n", val);
	IO_FLUSH;
#endif

    START_RO_ID(5);
    prev = (node_t *)LOAD(&set->head);
    next = (node_t *)LOAD(&prev->next);
    while (1) {
      v = (int)LOAD(&next->val);
      if (v >= val)
        break;
      prev = next;
      next = (node_t *)LOAD(&prev->next);
    }
    result = (v == val);
    COMMIT;

	return result;
}
#elif defined MUBENCH_TANGER || defined MUBENCH_SEQUENTIAL
int set_contains(intset_t *set, int val)
{
	int result;
	node_t *prev, *next;
	int v;
	
#ifdef DEBUG
	printf("++> set_contains(%d)\n", val);
	IO_FLUSH;
#endif
	
	START_RO_ID(5);
	prev = set->head;
	next = prev->next;
	while (next->val < val) {
		prev = next;
		next = prev->next;
	}
	result = (next->val == val);
	COMMIT;
	
	return result;
}
#endif /* MUBENCH_TANGER || MUBENCH_SEQUENTIAL */

#endif /* USE_RBTREE */

/* ################################################################### *
 * BARRIER
 * ################################################################### */

typedef struct barrier {
  pthread_cond_t complete;
  pthread_mutex_t mutex;
  int count;
  int crossing;
} barrier_t;

void barrier_init(barrier_t *b, int n)
{
  pthread_cond_init(&b->complete, NULL);
  pthread_mutex_init(&b->mutex, NULL);
  b->count = n;
  b->crossing = 0;
}

void barrier_cross(barrier_t *b)
{
  pthread_mutex_lock(&b->mutex);
  /* One more thread through */
  b->crossing++;
  /* If not all here, wait */
  if (b->crossing < b->count) {
    pthread_cond_wait(&b->complete, &b->mutex);
  } else {
    pthread_cond_broadcast(&b->complete);
    /* Reset for next time */
    b->crossing = 0;
  }
  pthread_mutex_unlock(&b->mutex);
}

/* ################################################################### *
 * STRESS TEST
 * ################################################################### */

typedef struct task_data {
  unsigned ptid;
  int range;
  int update;
  unsigned long nb_add;
  unsigned long nb_remove;
  unsigned long nb_contains;
  unsigned long nb_found;
  int diff;
  unsigned int seed;
  intset_t *set;
  //tx_desc *tx;
  int **matrix;
  barrier_t *barrier;
  int *next_serial;
} task_data_t;

//#include "threadpool.c"

#define TEST_MATRIX_SIZE 4
/*
void task_threadpool(void *data){
	task_data_t *d = (task_data_t *)data;

	if (d->val < d->update) {
      if (d->last < 0) {
        // Add random value
        d->val = (rand_r(&d->seed) % d->range) + 1;
        if (set_add(d->set, d->val, d->task_id, d->tx)) {
          d->diff++;
          d->last = d->val;
        }
        d->nb_add++;
      } else {
        // Remove last value
        if (set_remove(d->set, d->last, d->task_id, d->tx))
          d->diff--;
        d->nb_remove++;
        d->last = -1;
      }
    } else {
      // Look for random value
      d->val = (rand_r(&d->seed) % d->range) + 1;
      if (set_contains(d->set, d->val, d->task_id, d->tx))
        d->nb_found++;
      d->nb_contains++;
    }
}
*/
void* task_threads(void *data){
  int serial, val, last = -1;
  task_data_t *d = (task_data_t *)data;

  /* init thread */
  TM_THREAD_ENTER();

  //unsigned long aborts = 0;
  unsigned start = 1;
  unsigned commit = 1;

  /* Wait on barrier */
  barrier_cross(d->barrier);

#ifdef MUBENCH_WLPDSTM
  while (AO_load_full(&stop) == 0) {
#else
  while (stop == 0) {
#endif /* MUBENCH_WLPDSTM */

  	serial = fetch_and_inc_full(d->next_serial);

	val = rand_r(&d->seed) % 100;
	if (val < d->update) {
	  if (last < 0) {
		/* Add random value */
		val = (rand_r(&d->seed) % d->range) + 1;
		if (set_add(d->set, val, start, commit, d->ptid, serial TM_ARG_LAST)) {
		  d->diff++;
		  last = val;
		}
		d->nb_add++;
	  } else {
		/* Remove last value */
		if (set_remove(d->set, last, start, commit, d->ptid, serial TM_ARG_LAST))
		  d->diff--;
		d->nb_remove++;
		last = -1;
	  }
	} else {
	  /* Look for random value */
	  val = (rand_r(&d->seed) % d->range) + 1;
	  if (set_contains(d->set, val, start, commit, d->ptid, serial TM_ARG_LAST))
		d->nb_found++;
	  d->nb_contains++;
	}
  }

  //d->nb_aborts = aborts;

  TM_THREAD_EXIT();

  return NULL;
}
/*
void task2(void *data){
	task_data_t *d = (task_data_t *)data;
    int i, sum = 0;
    tx_desc *tx = d->tx;

	START_ID(0);
	for(i = 0; i < TEST_MATRIX_SIZE; i++){
		sum = TM_SHARED_READ(d->matrix[d->task_id % TEST_MATRIX_SIZE][i]);
	}
	for(i = 0; i < TEST_MATRIX_SIZE; i++){
		TM_SHARED_WRITE(d->matrix[d->task_id % TEST_MATRIX_SIZE][i], sum + 1);
		//printf("%d ", d->matrix[d->task_id % 16][i]);
	}

	COMMIT;

}
*/

/*
 * This test makes each task add all values of a line of a matrix
 * with the corresponding value from the next line
 * If there are different values in one of the lines
 * it means there is a bug somewhere in the TM
 */

void* task_matrix(void *data){
	task_data_t *d = (task_data_t *)data;
    int i, serial, v1, v2, end = 0;

    TM_THREAD_ENTER();

    barrier_cross(d->barrier);

    while (AO_load_full(&stop) == 0 && !end) {

    	serial = fetch_and_inc_full(d->next_serial);

		START_ID(0,1,1,0,serial);

		for(i = 0; i < TEST_MATRIX_SIZE; i++){
			v1 = TM_SHARED_READ(d->matrix[serial % TEST_MATRIX_SIZE][i]);
		}

		if(serial % TEST_MATRIX_SIZE < TEST_MATRIX_SIZE - 1){
			for(i = 0; i < TEST_MATRIX_SIZE; i++){
				v2 = TM_SHARED_READ(d->matrix[serial % TEST_MATRIX_SIZE +1][i]);
				TM_SHARED_WRITE(d->matrix[serial % TEST_MATRIX_SIZE + 1][i], v1+v2);
			}
		}

		COMMIT;

		//check for bug
		if(d->matrix[1][0] != d->matrix[1][1]){
			end = 1;
			/*printf("counter: %d \n", counter);
			for(i = 0; i < TEST_MATRIX_SIZE; i++){
				for(j = 0; j < TEST_MATRIX_SIZE; j++){
					printf("%d ", d->matrix[i][j]);
				}
				printf("\n");
			}
			printf("\n");*/
		}
    }

    TM_THREAD_EXIT();

	return NULL;
}
/*
void *program_thread(void *data)
{
  thread_data_t *d = (thread_data_t *)data;

  pthread_t *tasks;
  pthread_attr_t attr;
  unsigned i;

  if ((tasks = (pthread_t *)malloc(d->nb_tasks * sizeof(pthread_t))) == NULL) {
    perror("malloc");
    exit(1);
  }

  int **matriz = malloc(TEST_MATRIX_SIZE * sizeof(int *));

  for(i = 0; i < TEST_MATRIX_SIZE; i++){
	matriz[i] = malloc(TEST_MATRIX_SIZE * sizeof(int));
    for(j = 0; j < TEST_MATRIX_SIZE; j++){
      matriz[i][j] = i+1;
    }
  }

  //for (i = 0; i < d->nb_tasks; i++){
    //d->tasks[i].last = -1;
    //d->tasks[i].stop = 0;
    //d->tasks[i].tx = NULL;
    //d->tasks[i].matrix = matriz;
  //}

  //threadpool tp;

  //tp = create_threadpool(d->nb_tasks);

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  for (i = 0; i < d->nb_tasks; i++){
	  if (pthread_create(&tasks[i], &attr, task_threads, (void *)(&d->tasks[i])) != 0) {
		fprintf(stderr, "Error creating thread\n");
		exit(1);
	  }
  }

  pthread_attr_destroy(&attr);

  //int task_id = 0;

  nanosleep(d->timeout, NULL);

#ifdef MUBENCH_WLPDSTM
  while (AO_load_full(&stop) == 0) {
#else
  while (stop == 0) {
#endif // MUBENCH_WLPDSTM

	  //for (i = 0; i < d->nb_tasks; i++) {
		  //dispatch(tp, task, &d->tasks[i], task_id++);
	  //}

  }

  for (i = 0; i < d->nb_tasks; i++) {
	  if (pthread_join(tasks[i], NULL) != 0) {
		fprintf(stderr, "Error waiting for thread completion\n");
		exit(1);
	  }
  }

  //for(i = 0; i < TEST_MATRIX_SIZE; i++){
	  //for(j = 0; j < TEST_MATRIX_SIZE; j++){
	    //printf("%d ", matriz[i][j]);
	  //}
	  //printf("\n");
  //}

  for(i = 0; i < TEST_MATRIX_SIZE; i++)
  free(matriz[i]);
  free(matriz);


  //free(tasks);

  return NULL;
}
*/
int main(int argc, char **argv)
{
  struct option long_options[] = {
    // These options don't set a flag
    {"help",                      no_argument,       NULL, 'h'},
    {"duration",                  required_argument, NULL, 'd'},
    {"initial-size",              required_argument, NULL, 'i'},
    {"num-threads",               required_argument, NULL, 'n'},
    {"num-tasks",                 required_argument, NULL, 't'},
    {"range",                     required_argument, NULL, 'r'},
    {"seed",                      required_argument, NULL, 's'},
    {"update-rate",               required_argument, NULL, 'u'},
    {NULL, 0, NULL, 0}
  };

  intset_t *set;
  int i, j, c, val, size;
  unsigned long reads, updates;
  task_data_t *data;
  pthread_t *threads;
  pthread_attr_t attr;
  barrier_t barrier;
  struct timeval start, end;
  struct timespec timeout;
  int duration = DEFAULT_DURATION;
  int initial = DEFAULT_INITIAL;
  int nb_threads = DEFAULT_NB_THREADS;
  int nb_tasks = DEFAULT_NB_TASKS;
  int range = DEFAULT_RANGE;
  int seed = DEFAULT_SEED;
  int update = DEFAULT_UPDATE;
  int **matriz = malloc(TEST_MATRIX_SIZE * sizeof(int *));

  for(i = 0; i < TEST_MATRIX_SIZE; i++){
    matriz[i] = malloc(TEST_MATRIX_SIZE * sizeof(int));
    for(j = 0; j < TEST_MATRIX_SIZE; j++){
	  matriz[i][j] = i+1;
    }
  }

  while(1) {
    i = 0;
    c = getopt_long(argc, argv, "hd:i:n:t:r:s:u:", long_options, &i);

    if(c == -1)
      break;

    if(c == 0 && long_options[i].flag == 0)
      c = long_options[i].val;

    switch(c) {
     case 0:
       /* Flag is automatically set */
       break;
     case 'h':
       printf("intset -- STM stress test "
#ifdef USE_RBTREE
              "(red-black tree)\n"
#else
              "(linked list)\n"
#endif
              "\n"
              "Usage:\n"
              "  intset [options...]\n"
              "\n"
              "Options:\n"
              "  -h, --help\n"
              "        Print this message\n"
              "  -d, --duration <int>\n"
              "        Test duration in milliseconds (0=infinite, default=" XSTR(DEFAULT_DURATION) ")\n"
              "  -i, --initial-size <int>\n"
              "        Number of elements to insert before test (default=" XSTR(DEFAULT_INITIAL) ")\n"
              "  -n, --num-threads <int>\n"
              "        Number of program threads (default=" XSTR(DEFAULT_NB_THREADS) ")\n"
              "  -t, --num-tasks <int>\n"
              "        Number of tasks to execute in a program thread (default=" XSTR(DEFAULT_NB_THREADS) ")\n"
              "  -r, --range <int>\n"
              "        Range of integer values inserted in set (default=" XSTR(DEFAULT_RANGE) ")\n"
              "  -s, --seed <int>\n"
              "        RNG seed (0=time-based, default=" XSTR(DEFAULT_SEED) ")\n"
              "  -u, --update-rate <int>\n"
              "        Percentage of update transactions (default=" XSTR(DEFAULT_UPDATE) ")\n"
         );
       exit(0);
     case 'd':
       duration = atoi(optarg);
       break;
     case 'i':
       initial = atoi(optarg);
       break;
     case 'n':
       nb_threads = atoi(optarg);
       break;
     case 't':
       nb_tasks = atoi(optarg);
       break;
     case 'r':
       range = atoi(optarg);
       break;
     case 's':
       seed = atoi(optarg);
       break;
     case 'u':
       update = atoi(optarg);
       break;
     case '?':
       printf("Use -h or --help for help\n");
       exit(0);
     default:
       exit(1);
    }
  }

  assert(duration >= 0);
  assert(initial >= 0);
  assert(nb_threads > 0);
  assert(nb_tasks > 0);
  assert(range > 0);
  assert(update >= 0 && update <= 100);

#ifdef USE_RBTREE
  printf("Set type     : red-black tree\n");
#else
  printf("Set type     : linked list\n");
#endif
  printf("Duration     : %d\n", duration);
  printf("Initial size : %d\n", initial);
  printf("Nb threads   : %d\n", nb_threads);
  printf("Nb tasks     : %d\n", nb_tasks);
  printf("Value range  : %d\n", range);
  printf("Seed         : %d\n", seed);
  printf("Update rate  : %d\n", update);

  timeout.tv_sec = duration / 1000;
  timeout.tv_nsec = (duration % 1000) * 1000000;

  if ((data = (task_data_t *)malloc(nb_threads * nb_tasks * sizeof(task_data_t))) == NULL) {
    perror("malloc");
    exit(1);
  }
  if ((threads = (pthread_t *)malloc(nb_threads * nb_tasks * sizeof(pthread_t))) == NULL) {
    perror("malloc");
    exit(1);
  }

  if (seed == 0)
    srand((int)time(0));
  else
    srand(seed);

  set = set_new();

  stop = 0;

  /* Init STM */
  printf("Initializing STM\n");
  TM_STARTUP();

  /* Populate set */
  printf("Adding %d entries to set\n", initial);
  for (i = 0; i < initial; i++) {
    val = (rand() % range) + 1;
    set_add_seq(set, val);
  }
  size = set_size(set);
  printf("Set size     : %d\n", size);

  /* Access set from all threads */
  barrier_init(&barrier, nb_threads * nb_tasks + 1);
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  for (i = 0; i < nb_threads; i++) {
	  for(j = 0; j < nb_tasks; j++){
		int index = i*nb_tasks + j;
		int next_serial = 0;

		printf("Creating task %d of program thread %d \n", j, i);
		data[index].range = range;
		data[index].update = update;
		data[index].nb_add = 0;
		data[index].nb_remove = 0;
		data[index].nb_contains = 0;
		data[index].nb_found = 0;
		data[index].diff = 0;
		data[index].seed = rand();
		data[index].set = set;
		data[index].barrier = &barrier;
		data[index].ptid = i;
		data[index].matrix = matriz;
		data[index].next_serial = &next_serial;

		if (pthread_create(&threads[index], &attr, task_threads, (void *)(&data[index])) != 0) {
		  fprintf(stderr, "Error creating thread\n");
		  exit(1);
		}
	  }
  }
  pthread_attr_destroy(&attr);

  /* Start threads */
  barrier_cross(&barrier);

  printf("STARTING...\n");
  gettimeofday(&start, NULL);
  if (duration > 0) {
    nanosleep(&timeout, NULL);
  }
#ifdef MUBENCH_WLPDSTM
  AO_store_full(&stop, 1);
#else
  stop = 1;
#endif /* MUBENCH_WLPDSTM */
  gettimeofday(&end, NULL);
  printf("STOPPING...\n");

  /* Wait for thread completion */
  for (i = 0; i < nb_threads*nb_tasks; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      fprintf(stderr, "Error waiting for thread completion\n");
      exit(1);
    }
  }

  //print the matrix after the test is over
  for(i = 0; i < TEST_MATRIX_SIZE; i++){
  	  for(j = 0; j < TEST_MATRIX_SIZE; j++){
  	    printf("%d ", matriz[i][j]);
  	  }
  	  printf("\n");
  }

  duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);
  reads = 0;
  updates = 0;

  for (i = 0; i < nb_threads; i++) {
	unsigned long nb_add = 0;
	unsigned long nb_remove = 0;
	unsigned long nb_contains = 0;
	unsigned long nb_found = 0;
    for (j = 0; j < nb_tasks; j++) {
	  nb_add += data[i*nb_tasks + j].nb_add;
	  nb_remove += data[i*nb_tasks + j].nb_remove;
	  nb_contains += data[i*nb_tasks + j].nb_contains;
	  nb_found += data[i*nb_tasks + j].nb_found;
	  size += data[i*nb_tasks + j].diff;
	  size += data[i*nb_tasks + j].diff;
 	}
    printf("Thread %d\n", i);
    printf("  #add        : %lu\n", nb_add);
    printf("  #remove     : %lu\n", nb_remove);
    printf("  #contains   : %lu\n", nb_contains);
    printf("  #found      : %lu\n", nb_found);
    reads += nb_contains;
    updates += (nb_add + nb_remove);
  }
  printf("Set size      : %d (expected: %d)\n", set_size(set), size);
  printf("Duration      : %d (ms)\n", duration);
  printf("#txs          : %lu (%f / s)\n", reads + updates, (reads + updates) * 1000.0 / duration);
  printf("#read txs     : %lu (%f / s)\n", reads, reads * 1000.0 / duration);
  printf("#update txs   : %lu (%f / s)\n", updates, updates * 1000.0 / duration);

  TM_SHUTDOWN();

  free(threads);
  free(data);

  return 0;
}
