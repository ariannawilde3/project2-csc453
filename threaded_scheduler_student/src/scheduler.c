#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scheduler.h"

#define MAX_JOBS 256

typedef struct {
    job_t *items[MAX_JOBS];
    int size;
} ready_queue_t;

typedef struct {
    const sim_config_t *cfg;

    job_t jobs[MAX_JOBS];
    int njobs;

    FILE *trace;
    FILE *stats;

    ready_queue_t rq;
    job_t **cpu_jobs;

    int tick;
    int next_arrival;
    int completed;

    int tick_generation;
    int workers_done;
    int shutdown;

    pthread_mutex_t mutex;
    pthread_cond_t workers_cv;
    pthread_cond_t scheduler_cv;
} multi_state_t;

typedef struct {
    multi_state_t *state;
    int cpu_id;
} cpu_arg_t;

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
        if (trace == NULL) {
            fprintf(stderr, "could not open %s\n", cfg -> trace_path);
            return -1;
        }
    } else {
        trace = stdout;
    }

    
    if (cfg -> stats_path != NULL) {
        stats = fopen(cfg -> stats_path, "w");
        if (stats == NULL) {
            fprintf(stderr, "could not open %s\n", cfg -> stats_path);
            if (cfg -> trace_path != NULL) {
                fclose(trace);
            }
            return -1;
        }
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

static void *cpu_worker_thread(void *arg) {
    cpu_arg_t *cpu_arg = arg;
    multi_state_t *state = cpu_arg -> state;
    int cpu_id = cpu_arg -> cpu_id;
    int last_generation = 0;

    pthread_mutex_lock(&state -> mutex);

    while (!state -> shutdown) {
        while (last_generation == state -> tick_generation && !state -> shutdown) {
            pthread_cond_wait(&state -> workers_cv, &state -> mutex);
        }
        if (state -> shutdown) {
            break;
        }

        last_generation = state -> tick_generation;

        job_t *job = state -> cpu_jobs[cpu_id];
        if (job != NULL) {
            job -> remaining_time--;
            job -> rr_ticks_used++;
        }

        state -> workers_done++;
        if (state -> workers_done == state -> cfg -> cpus) {
            pthread_cond_signal(&state -> scheduler_cv);
        }
    }

    pthread_mutex_unlock(&state -> mutex);
    return NULL;
}

static int pick_next_job(const sim_config_t *cfg, ready_queue_t *rq) {
    if (rq -> size == 0) { return -1; }
    if (cfg -> policy == POLICY_SJF || cfg -> policy == POLICY_SRTF) {
        return pickBestSJF(rq);
    }
 
    return 0;
}

static void admit_arrivals(multi_state_t *state) {
    while (state -> next_arrival < state -> njobs &&
           state -> jobs[state -> next_arrival].arrival_time == state -> tick) {
        job_t *j = &state -> jobs[state -> next_arrival];

        j -> ready_enqueue_time = state -> tick;
        queuePush(&state -> rq, j);

        fprintf(state -> trace, "%d ARRIVE %s\n", state -> tick, j -> id);
        
        state -> next_arrival++;
    }
}

static void dispatch_idle_cpus(multi_state_t *state) {
    const sim_config_t *cfg = state -> cfg;

    for (int cpu = 0; cpu < cfg -> cpus; cpu++) {
        if (state -> cpu_jobs[cpu] == NULL && state -> rq.size > 0) {
            int idx = pick_next_job(cfg, &state -> rq);
            job_t *j = state -> rq.items[idx];

            queueRemove(&state -> rq, idx);

            j -> total_wait_time += state -> tick - j -> ready_enqueue_time;
            j -> rr_ticks_used = 0;

            if (!j -> started) {
                j -> started = 1;
                j -> first_run_time = state -> tick;
            }

            state -> cpu_jobs[cpu] = j;

            fprintf(state -> trace, "%d DISPATCH CPU%d %s\n",
                    state -> tick, cpu, j -> id);
        }
    }
}

static void preempt_srtf_jobs(multi_state_t *state) {
    if (state -> cfg -> policy != POLICY_SRTF) {
        return;
    }

    for (int cpu = 0; cpu < state -> cfg -> cpus; cpu++) {
        job_t *running = state -> cpu_jobs[cpu];

        if (running == NULL || state -> rq.size == 0) {
            continue;
        }

        int best = pickBestSJF(&state -> rq);
        if (isBetter(state -> rq.items[best], running)) {
            fprintf(state -> trace, "%d PREEMPT CPU%d %s\n",
                    state -> tick, cpu, running -> id);

            running -> ready_enqueue_time = state -> tick;
            queuePush(&state -> rq, running);
            state -> cpu_jobs[cpu] = NULL;
        }
    }
}

static void run_worker_tick(multi_state_t *state) {
    state -> workers_done = 0;
    state -> tick_generation++;

    pthread_cond_broadcast(&state -> workers_cv);
    while (state -> workers_done < state -> cfg -> cpus) {
        pthread_cond_wait(&state -> scheduler_cv, &state -> mutex);
    }
}

static void process_cpu_results(multi_state_t *state) {
    const sim_config_t *cfg = state -> cfg;

    for (int cpu = 0; cpu < cfg -> cpus; cpu++) {
        job_t *j = state -> cpu_jobs[cpu];

        if (j == NULL) { continue; }

        if (j -> remaining_time == 0) {
            fprintf(state -> trace, "%d COMPLETE CPU%d %s\n",
                    state -> tick, cpu, j -> id);

            j -> completion_time = state -> tick;
            state -> cpu_jobs[cpu] = NULL;
            state -> completed++;
        } else if (cfg -> policy == POLICY_RR &&
                   j -> rr_ticks_used >= cfg -> quantum &&
                   state -> rq.size > 0) {
            fprintf(state -> trace, "%d PREEMPT CPU%d %s\n",
                    state -> tick, cpu, j -> id);

            j -> ready_enqueue_time = state -> tick + 1;
            queuePush(&state -> rq, j);
            state -> cpu_jobs[cpu] = NULL;
        }
    }
}


static void *scheduler_thread(void *arg) {
    multi_state_t *state = arg;

    pthread_mutex_lock(&state -> mutex);

    while (state -> completed < state -> njobs) {
        admit_arrivals(state);
        preempt_srtf_jobs(state);
        dispatch_idle_cpus(state);
        run_worker_tick(state);
        process_cpu_results(state);

        state -> tick++;
    }

    fprintf(state -> trace, "END\n");
    writeStats(state -> stats, state -> jobs, state -> njobs);

    state -> shutdown = 1;
    pthread_cond_broadcast(&state -> workers_cv);

    pthread_mutex_unlock(&state -> mutex);
    return NULL;
}


int run_scheduler_multi_cpu(const sim_config_t *cfg) {
    multi_state_t state;
    pthread_t scheduler;
    pthread_t *workers = NULL;
    cpu_arg_t *args = NULL;
    int scheduler_created = 0;
    int workers_created = 0;
    int rc = 0;

    memset(&state, 0, sizeof(state));
    state.cfg = cfg;
    state.njobs = read_jobs(cfg -> input_path, state.jobs);
    if (state.njobs <= 0) {
        return -1;
    }

    state.trace = cfg -> trace_path != NULL ? fopen(cfg -> trace_path, "w") : stdout;
    state.stats = cfg -> stats_path != NULL ? fopen(cfg -> stats_path, "w") : stdout;

    if (state.trace == NULL) {
        fprintf(stderr, "could not open %s\n", cfg -> trace_path);
        return -1;
    }

    if (state.stats == NULL) {
        fprintf(stderr, "could not open %s\n", cfg -> stats_path);
        if (cfg -> trace_path != NULL) {
            fclose(state.trace);
        }
        return -1;
    }

    state.cpu_jobs = calloc(cfg -> cpus, sizeof(job_t *));
    if (state.cpu_jobs == NULL) {
        fprintf(stderr, "could not allocate CPU job slots\n");
        if (cfg -> trace_path != NULL) {
            fclose(state.trace);
        }
        if (cfg -> stats_path != NULL) {
            fclose(state.stats);
        }
        return -1;
    }

    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.workers_cv, NULL);
    pthread_cond_init(&state.scheduler_cv, NULL);

    workers = calloc(cfg -> cpus, sizeof(pthread_t));
    if (workers == NULL) {
        fprintf(stderr, "could not allocate worker thread handles\n");
        rc = -1;
        goto cleanup;
    }

    args = calloc(cfg -> cpus, sizeof(cpu_arg_t));
    if (args == NULL) {
        fprintf(stderr, "could not allocate worker arguments\n");
        rc = -1;
        goto cleanup;
    }

    for (int i = 0; i < cfg -> cpus; i++) {
        args[i].state = &state;
        args[i].cpu_id = i;
        if (pthread_create(&workers[i], NULL, cpu_worker_thread, &args[i]) != 0) {
            fprintf(stderr, "could not create CPU worker thread %d\n", i);
            pthread_mutex_lock(&state.mutex);
            state.shutdown = 1;
            pthread_cond_broadcast(&state.workers_cv);
            pthread_mutex_unlock(&state.mutex);
            rc = -1;
            break;
        }
        workers_created++;
    }

    if (rc == 0 && pthread_create(&scheduler, NULL, scheduler_thread, &state) != 0) {
        fprintf(stderr, "could not create scheduler thread\n");
        pthread_mutex_lock(&state.mutex);
        state.shutdown = 1;
        pthread_cond_broadcast(&state.workers_cv);
        pthread_mutex_unlock(&state.mutex);
        rc = -1;
    } else if (rc == 0) {
        scheduler_created = 1;
    }

    if (scheduler_created) {
        pthread_join(scheduler, NULL);
    }

    for (int i = 0; i < workers_created; i++) {
        pthread_join(workers[i], NULL);
    }

cleanup:
    if (cfg -> trace_path != NULL) {
        fclose(state.trace);
    }

    if (cfg -> stats_path != NULL) {
        fclose(state.stats);
    }

    pthread_cond_destroy(&state.scheduler_cv);
    pthread_cond_destroy(&state.workers_cv);
    pthread_mutex_destroy(&state.mutex);

    free(args);
    free(workers);
    free(state.cpu_jobs);

    return rc;
}
