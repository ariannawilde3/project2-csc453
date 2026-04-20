#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdio.h>
#include <pthread.h>

#define JOB_ID_MAX 16

typedef enum {
    JOB_NEW = 0,
    JOB_READY,
    JOB_RUNNING,
    JOB_DONE
} job_state_t;

typedef enum {
    POLICY_FCFS = 0,
    POLICY_RR,
    POLICY_SJF,
    POLICY_SRTF
} sched_policy_t;

typedef struct job {
    char id[JOB_ID_MAX];
    int arrival_time;
    int priority;
    int total_time;
    int remaining_time;
    job_state_t state;

    int first_run_time;
    int completion_time;
    int total_wait_time;
    int ready_enqueue_time;

    int rr_ticks_used;
    int assigned_cpu;
    int started;
    int original_index;
} job_t;

typedef struct {
    sched_policy_t policy;
    int quantum;
    int cpus;
    const char *input_path;
    const char *trace_path;
    const char *stats_path;
} sim_config_t;

typedef struct {
    job_t *jobs;
    int njobs;
} workload_t;

int run_scheduler_single_cpu(const sim_config_t *cfg);
int run_scheduler_multi_cpu(const sim_config_t *cfg);
const char *policy_name(sched_policy_t policy);
int parse_policy(const char *s, sched_policy_t *policy);
int parse_args(int argc, char **argv, sim_config_t *cfg);
void print_usage(FILE *fp, const char *progname);

#endif
