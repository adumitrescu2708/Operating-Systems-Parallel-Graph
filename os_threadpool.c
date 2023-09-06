// SPDX-License-Identifier: BSD-3-Clause
/**
 * Name:	Dumitrescu Alexandra
 * Group:	333CA
 * For:	Operating Systems - Parallel Graph
 */
#include "os_threadpool.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <semaphore.h>


/**
 * States of the threadpool, values of should_stop attribute.
 * When the threadpool has should_stop value on **START**, all
 * threads in threadpool continously are beign assigned new tasks to
 * execute, and when it's set to **STOP** all operations on
 * threadpool are being blocked and the main thread waits for
 * the other threads to join.
 */
#define START 1
#define STOP -1

/**
 * Semaphores used for reducing the use of busy waiting.
 * Semaphore_add_task - Used for blocking the thread that calls
 * |			add_task_to_queue method from adding more than the number
 * |			allowed for tasks in the queue.
 * Semaphore_received_tasks - Used for blocking the workers until
 * |			new tasks are being received, in order to reduce
 * |			the busy waiting.
 */
sem_t semaphore_add_tasks;
sem_t semaphore_received_tasks;

/**
 * Creates a **task** that thread must execute, including the function and
 * its arguments allocates memory and initialises fields with given parameters.
 */
os_task_t *task_create(void *arg, void (*f)(void *))
{
	os_task_t *new_task = (os_task_t *) malloc(sizeof(os_task_t));

	if (new_task == NULL) {
		fprintf(stderr, "Malloc for new task failed!");
		return NULL;
	}
	if (f == NULL) {
		fprintf(stderr, "Invalid function!");
		return NULL;
	}
	new_task->argument = arg;
	new_task->task = f;
	return new_task;
}

/**
 * Creates a new **queue task** containing the task that thread must execute,
 * allocates memory and initialises fields with given parameters.
 */
os_task_queue_t *queue_task_create(os_task_t *t)
{
	if (t == NULL)
		return NULL;

	os_task_queue_t *new_queue_task = (os_task_queue_t *)malloc(sizeof(os_task_queue_t));

	if (new_queue_task == NULL) {
		fprintf(stderr, "Malloc for new queue task failed!");
		return NULL;
	}

	new_queue_task->task = t;
	new_queue_task->next = NULL;

	return new_queue_task;
}

/**
 * In order to maintain the queue of tasks, we need to implement the
 * FIFO strategy. In this way, for adding a new task in the data structure,
 * we first obtain the last task in the list, and then we add the new one,
 * after the last one. For extracting the last element, we just traverse the
 * list.
 */
os_task_queue_t *get_last_queue_task(os_threadpool_t *tp)
{
	if (tp == NULL)
		return NULL;

	os_task_queue_t *start = tp->tasks;

	if (start == NULL)
		return NULL;

	while (start->next != NULL)
		start = start->next;

	return start;
}


/**
 * When adding a new task in the threadpool's queue, we first use
 * the semaphore Semaphore_add_task to maintain the number
 * of elements in the queue lower that the given size of the threadpool.
 * If the number is greater, the calling thread will be blocked until
 * a new empty space is available.
 *
 * The new task will be added at the end of the list. Since multiple
 * threads might try to add simultaneously multiple tasks at the end
 * of the list, this operation is protected by the threadpool's lock.
 *
 * Once a new task has been added to the queue, we announce the threads
 * that a new task is available, with semaphore_received_tasks
 */
void add_task_in_queue(os_threadpool_t *tp, os_task_t *t)
{
	if (tp == NULL || t == NULL || tp->should_stop == STOP) {
		free(t);
		return;
	}

	// Wait for an empty space in the queue
	sem_wait(&semaphore_add_tasks);

	// Create a new queue task
	os_task_queue_t *new_queue_task = queue_task_create(t);

	//  Use threadpool's lock to add the new task at the end of the list
	pthread_mutex_lock(&tp->taskLock);
	os_task_queue_t *last_queue_task = get_last_queue_task(tp);

	if (last_queue_task == NULL)
		tp->tasks = new_queue_task;
	else
		last_queue_task->next = new_queue_task;
	pthread_mutex_unlock(&tp->taskLock);

	// Announce that new tasks have been added
	sem_post(&semaphore_received_tasks);
}

/**
 * In order to maintain the queue's FIFO statement, for each time a task is
 * being retrieved from the queue, we return the first element in the list.
 *
 * Since multiple threads might try to simultaneously retrieve tasks from
 * the queue, we protect this operation using threadpool's lock.
 *
 * When a task is being removed from the list, we must announce that
 * a new thread that tried to add a new task, but was blocked can be unlocked,
 * using semaphore_add_tasks.
 */
os_task_t *get_task(os_threadpool_t *tp)
{
	if (tp == NULL || tp->should_stop == STOP)
		return NULL;

	// Remove first task from the list and retrieve its task
	pthread_mutex_lock(&tp->taskLock);
	// Get the first task
	os_task_queue_t *queue_task = tp->tasks;

	if (queue_task == NULL) {
		pthread_mutex_unlock(&tp->taskLock);
		return NULL;
	}
	// Get next task, the following of the first one
	os_task_queue_t *next_queue_task = queue_task->next;

	// Update the head of the queue
	tp->tasks = next_queue_task;
	pthread_mutex_unlock(&tp->taskLock);

	// Decrement the number of tasks in the queue
	sem_post(&semaphore_add_tasks);

	// Obtain the task from the selected node and return its value
	os_task_t *task = queue_task->task;

	free(queue_task);

	return task;
}

/**
 * This method initialises the threadpool and allocates memory for it.
 */
os_threadpool_t *threadpool_create(unsigned int nTasks, unsigned int nThreads)
{
	os_threadpool_t *threadpool = (os_threadpool_t *)malloc(sizeof(os_threadpool_t));

	if (threadpool == NULL) {
		fprintf(stderr, "Malloc for threadpool failed!");
		return NULL;
	}

	threadpool->num_threads = nThreads;
	threadpool->threads = (pthread_t *) malloc(threadpool->num_threads * sizeof(pthread_t));
	if (threadpool->threads == NULL) {
		fprintf(stderr, "Malloc for thread list failed!");
		return NULL;
	}

	pthread_mutex_init(&threadpool->taskLock, NULL);
	sem_init(&semaphore_add_tasks, 0, nTasks);
	sem_init(&semaphore_received_tasks, 0, 0);

	threadpool->tasks = NULL;
	threadpool->should_stop = START;
	for (int i = 0; i < threadpool->num_threads; i++)
		pthread_create(&(threadpool->threads[i]), NULL, thread_loop_function, threadpool);

	return threadpool;
}

/**
 * Each **worker** must execute the following loop: wait being blocked until
 * a new task is available in threadpool's queue, get the first available
 * queue task from the queue, execute its task and wait for the threadpool
 * to finish receiving task, checking the should_stop attribute.
 */
void *thread_loop_function(void *args)
{
	os_threadpool_t *threadpool = (os_threadpool_t *)args;

	while (1) {
		if (threadpool->should_stop == STOP)
			break;
		os_task_t *task_t;

		sem_wait(&semaphore_received_tasks);
		if (threadpool->should_stop == STOP)
			break;
		task_t = get_task(threadpool);

		if (task_t != NULL) {
			task_t->task(task_t->argument);
			free(task_t);
		}
	}
	pthread_exit(NULL);
}

/**
 * This method waits for the value of processing_is_done to be 1, in order to
 * set the threadpool's should_stop value and unblock the possible waiting
 * workers.
 * After unblocking the threads, the calling thread must wait for the workers
 * to join and then remove alloced memory.
 */
void threadpool_stop(os_threadpool_t *tp, int (*processingIsDone)(os_threadpool_t *))
{
	while (1) {
		if (processingIsDone(tp) == 1)
			break;
	}

	tp->should_stop = STOP;
	for (int i = 0; i < tp->num_threads; i++)
		sem_post(&semaphore_received_tasks);

	for (int i = 0; i < tp->num_threads; i++)
		pthread_join(tp->threads[i], NULL);

	pthread_mutex_destroy(&tp->taskLock);

	sem_destroy(&semaphore_add_tasks);
	sem_destroy(&semaphore_received_tasks);

	free(tp->threads);
}
