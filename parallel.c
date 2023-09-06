// SPDX-License-Identifier: BSD-3-Clause
/**
 * Name:	Dumitrescu Alexandra
 * Group:	333CA
 * For:	Operating Systems - Parallel Graph
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

#include "os_graph.h"
#include "os_threadpool.h"
#include "os_list.h"

#define MAX_TASK 1000
#define MAX_THREAD 4

#define VISITED 1
#define PROCESSED 2
#define UNVISITED 0

int sum;
os_graph_t *graph;
pthread_mutex_t lock_graph;
os_threadpool_t *threadpool;
int *processed;
int *connected_components;

/**
 * This method is being parsed to the threapool worker and its meant to
 * add the given node's value to the sum of nodes and iterate trough the
 * unvisited nodes and add new tasks to the queue for those nodes.
 *
 * In order to keep track of the processed nodes, we differentiate the state
 * of **visited** node (this implies that the node's task has been added
 * to the queue) and **processed** node (this implies that the value of the node
 * has been added to the sum).
 *
 * This way, we can easily decide that the threadpool should stop once all
 * nodes has been visited and processed.
 *
 * Multiple nodes might want to add the value to the sum simultaneously,
 * creating a race condition. Therefor, we protect this operation with the
 * graph's lock.
 *
 * What is more, multiple threads might try to mark one node as being visited
 * but we only permit one visit at a time, and therefor we protect this with
 * the lock.
 */
void DFS(void *node_adr)
{
	// obtain the node in the graph
	int node = *((int *)node_adr);
	os_node_t *node_t = graph->nodes[node];

	// Protect adding to sum operation with graph's lock
	pthread_mutex_lock(&lock_graph);
	int value = processed[node];

	pthread_mutex_unlock(&lock_graph);

	if (value == UNVISITED) {
		pthread_mutex_lock(&lock_graph);
		sum += node_t->nodeInfo;
		pthread_mutex_unlock(&lock_graph);

		// Mark node as visited and processed, see definitions above
		graph->visited[node] = VISITED;
		processed[node] = PROCESSED;

		for (int i = 0; i < node_t->cNeighbours; i++) {
			// Only one visit at a time is permited
			if (graph->visited[node_t->neighbours[i]] == UNVISITED) {
				pthread_mutex_lock(&lock_graph);
				// If the neighbour remained unvisited, meaning the thread
				// is the first one to visit it, then visit it.
				if (graph->visited[node_t->neighbours[i]] == UNVISITED) {
					graph->visited[node_t->neighbours[i]] = VISITED;
					pthread_mutex_unlock(&lock_graph);
					// Create new task and add it to the queue
					os_task_t *task = task_create(&(node_t->neighbours[i]), DFS);

					add_task_in_queue(threadpool, task);
				} else {
					pthread_mutex_unlock(&lock_graph);
				}
			}
		}
	}
}

/**
 * We can detect that the processing of the graph is done once all
 * nodes has been processed and the queue of tasks in the graph is
 * empty.
 */
int processingIsDone(os_threadpool_t *threadpool)
{
	if (threadpool == NULL)
		return 0;

	for (int i = 0; i < graph->nCount; i++) {
		if (processed[i] == UNVISITED || graph->visited[i] == UNVISITED)
			return 0;
	}

	if (threadpool->tasks != NULL)
		return 0;

	return 1;
}

/**
 * We allocate memory for the processed nodes array and initialise it
 * with false. We, then, iterate through the nodes and start adding tasks for
 * the unvisited nodes to the threadpool. We follow the same idea as
 * described above.
 */
void traverse_graph(void)
{
	int counter_connected_components = 0;

	processed = malloc(graph->nCount * sizeof(int));
	connected_components = malloc(graph->nCount * sizeof(int));

	for (int i = 0; i < graph->nCount; i++)
		processed[i] = UNVISITED;

	for (int node = 0; node < graph->nCount; node++) {
		if (graph->visited[node] == 0) {
			pthread_mutex_lock(&lock_graph);
			if (graph->visited[node] == 0) {
				graph->visited[node] = VISITED;
				pthread_mutex_unlock(&lock_graph);
				connected_components[counter_connected_components] = node;
				os_task_t *task = task_create(&connected_components[counter_connected_components], DFS);

				counter_connected_components++;
				add_task_in_queue(threadpool, task);
			} else {
				pthread_mutex_unlock(&lock_graph);
			}
		}
	}
}


int main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("Usage: ./%s input_file\n", "main");
		exit(1);
	}

	FILE *input_file = fopen(argv[1], "r");

	if (input_file == NULL) {
		printf("[Error] Can't open file\n");
		return -1;
	}

	graph = create_graph_from_file(input_file);
	if (graph == NULL) {
		printf("[Error] Can't read the graph from file\n");
		return -1;
	}

	pthread_mutex_init(&lock_graph, NULL);
	threadpool = threadpool_create(MAX_TASK, MAX_THREAD);
	traverse_graph();
	threadpool_stop(threadpool, processingIsDone);
	pthread_mutex_destroy(&lock_graph);
	free(threadpool);
	free(processed);
	free(connected_components);

	printf("%d", sum);
	return 0;
}
