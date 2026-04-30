#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scheduler.h"

#define MAX_JOBS 256

typedef struct {
    job_t *items[MAX_JOBS];
    int size;
} ready_queue_t;

static int read_jobs(const char *path, job_t jobs[]) {
    FILE *filePointer = fopen(path, "r");
    if (filePointer == NULL) {
        fprintf(stderr, "could not open %s\n", path);
        return -1;
    }

    int count = 0;
    char line[256];

    while (fgets(line, sizeof(line), filePointer) != NULL) {

        if (line[0] == '\n' || line[0] == '#') {
            continue;
        }

        char id[32];
        int arrival;
        int priority;
        int cpuTime;
        int got = sscanf(line, "%s %d %d %d", id, &arrival, &priority, &cpuTime);

        if (got != 4) {
            continue;
        }

        strcpy(jobs[count].id, id);
        jobs[count].arrival_time = arrival;
        jobs[count].priority = priority;
        jobs[count].total_time = cpuTime;
        jobs[count].remaining_time = cpuTime;
        jobs[count].first_run_time = -1;
        jobs[count].completion_time = -1;
        jobs[count].total_wait_time = 0;
        jobs[count].ready_enqueue_time = -1;
        jobs[count].rr_ticks_used = 0;
        jobs[count].started = 0;
        jobs[count].original_index = count;

        count++;
    }

    fclose(filePointer);
    return count;
}


static void queuePush(ready_queue_t *q, job_t *j) {

    q -> items [q -> size] = j;
    q -> size++; 
}

static void queueRemove(ready_queue_t *q, int i) {

    for (int k = i; k < q -> size -1; k++) {
        q -> items[k] = q -> items[k + 1];
    }

    q -> size--;
}

static int isBetter (job_t *a, job_t *b) {

    if (a -> remaining_time != b-> remaining_time) {
        return a -> remaining_time < b -> remaining_time;
    }

    if (a -> arrival_time != b -> arrival_time) {
        return a -> arrival_time < b -> arrival_time;
    }

    if (a -> priority != b -> priority) {
        return a -> priority > b -> priority;
    }

    return strcmp (a -> id, b -> id) < 0;
}

static int pickBestSJF (ready_queue_t *q) {

    if (q -> size == 0) {
        return -1;
    }

    int best = 0;
    for (int i = 1; i < q -> size; i++) {
        if (isBetter(q -> items[i], q -> items[best])) {
            best = i;
        }
    }

    return best;
}

static void writeStats(FILE *filePointer, job_t jobs[], int n) {

    fprintf(filePointer, "STATS\n");

    double totalResponse = 0;
    double totalTurnaround = 0;
    double totalWaiting = 0;

    for (int i = 0; i < n; i++) {
        int response = jobs[i].first_run_time - jobs[i].arrival_time;
        int turnaround = (jobs[i].completion_time + 1) - jobs[i].arrival_time;
        int waiting = jobs[i].total_wait_time;

        fprintf(filePointer, "%s response %d turnaround %d waiting %d\n", jobs[i].id, response, turnaround, waiting);

        totalResponse += response;
        totalTurnaround += turnaround;
        totalWaiting += waiting;
    }

    fprintf(filePointer, "AVG response %.2f turnaround %.2f waiting %.2f\n", totalResponse / n, totalTurnaround / n, totalWaiting / n);
}


int run_scheduler_single_cpu(const sim_config_t *cfg) {
    
    job_t jobs[MAX_JOBS];
    int njobs = read_jobs(cfg -> input_path, jobs);
    if (njobs <= 0) {
        return -1;
    }

    FILE *trace;
    FILE *stats;
    if (cfg -> trace_path != NULL) {
        trace = fopen(cfg -> trace_path, "w");
    } else {
        trace = stdout;
    }

    
    if (cfg -> stats_path != NULL) {
        stats = fopen(cfg -> stats_path, "w");
    } else {
        stats = stdout;
    }

    ready_queue_t rq;
    rq.size = 0;

    job_t *running = NULL;
    int nextArrival = 0;
    int completed = 0;
    int tick = 0;

    while (completed < njobs) {
        
        while (nextArrival < njobs && jobs[nextArrival].arrival_time == tick) {

            jobs[nextArrival].ready_enqueue_time = tick;
            queuePush(&rq, &jobs[nextArrival]);
            fprintf(trace, "%d ARRIVE %s\n", tick, jobs[nextArrival].id);
            nextArrival++;
        }

        if (cfg -> policy == POLICY_SRTF && running != NULL && rq.size > 0) {

            int best = pickBestSJF(&rq);
            if (isBetter(rq.items[best], running)) {
                fprintf(trace, "%d PREEMPT %s\n", tick, running->id);
                running -> ready_enqueue_time = tick;
                queuePush(&rq, running);
                running = NULL;
            }
        }

        if (running == NULL && rq.size > 0) {

            int idx;
            if (cfg -> policy == POLICY_SJF || cfg -> policy == POLICY_SRTF) {
                idx = pickBestSJF(&rq);
            } else {
                idx = 0;
            }

            job_t *j = rq.items[idx];
            queueRemove(&rq, idx);

            j -> total_wait_time += tick - j -> ready_enqueue_time;

            j -> rr_ticks_used = 0;

            if (j -> started == 0) {
                j -> started = 1;
                j -> first_run_time = tick;
            }

            running = j;
            fprintf(trace, "%d DISPATCH %s\n", tick, j->id);
        }

        if (running != NULL) {

            running -> remaining_time--;
            running -> rr_ticks_used++;

            if (running -> remaining_time == 0) {

                fprintf(trace, "%d COMPLETE %s\n", tick, running->id);
                running->completion_time = tick;
                completed++;
                running = NULL;
            } else if (cfg -> policy == POLICY_RR && running -> rr_ticks_used >= cfg -> quantum && rq.size > 0) {

                fprintf(trace, "%d PREEMPT %s\n", tick, running -> id);
                running -> ready_enqueue_time = tick + 1;
                queuePush(&rq, running);
                running = NULL;
            }
        }

        tick++;
    }

    fprintf(trace, "END\n");
    writeStats(stats, jobs, njobs);
    
    if (cfg -> trace_path != NULL) {
        fclose(trace);
    }

    if (cfg -> stats_path != NULL) {
        fclose(stats);
    }

    return 0;
}

int run_scheduler_multi_cpu(const sim_config_t *cfg) {
    (void)cfg;
    fprintf(stderr,
            "TODO: implement the multi-CPU threaded scheduler in src/scheduler.c\n"
            "Required behavior:\n"
            "- preserve the single-CPU scheduling semantics\n"
            "- create one scheduler thread and N CPU worker threads\n"
            "- protect shared state with mutexes\n"
            "- sleep on condition variables instead of busy waiting\n");
    return -1;
}
