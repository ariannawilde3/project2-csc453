#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scheduler.h"

const char *policy_name(sched_policy_t policy) {
    switch (policy) {
        case POLICY_FCFS: return "FCFS";
        case POLICY_RR: return "RR";
        case POLICY_SJF: return "SJF";
        case POLICY_SRTF: return "SRTF";
        default: return "UNKNOWN";
    }
}

int parse_policy(const char *s, sched_policy_t *policy) {
    if (strcmp(s, "FCFS") == 0) *policy = POLICY_FCFS;
    else if (strcmp(s, "RR") == 0) *policy = POLICY_RR;
    else if (strcmp(s, "SJF") == 0) *policy = POLICY_SJF;
    else if (strcmp(s, "SRTF") == 0) *policy = POLICY_SRTF;
    else return -1;
    return 0;
}

void print_usage(FILE *fp, const char *progname) {
    fprintf(fp,
            "Usage:\n"
            "  %s --policy {FCFS|RR|SJF|SRTF} --input FILE [options]\n\n"
            "Options:\n"
            "  --quantum N        Required for RR, ignored otherwise\n"
            "  --cpus N           Number of CPUs to simulate, default 1\n"
            "  --trace-out FILE   Write event trace to FILE\n"
            "  --stats-out FILE   Write statistics to FILE\n",
            progname);
}

int parse_args(int argc, char **argv, sim_config_t *cfg) {
    int i;

    cfg->policy = POLICY_FCFS;
    cfg->quantum = 0;
    cfg->cpus = 1;
    cfg->input_path = NULL;
    cfg->trace_path = NULL;
    cfg->stats_path = NULL;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
            if (parse_policy(argv[++i], &cfg->policy) != 0) return -1;
        } else if (strcmp(argv[i], "--quantum") == 0 && i + 1 < argc) {
            cfg->quantum = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cpus") == 0 && i + 1 < argc) {
            cfg->cpus = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            cfg->input_path = argv[++i];
        } else if (strcmp(argv[i], "--trace-out") == 0 && i + 1 < argc) {
            cfg->trace_path = argv[++i];
        } else if (strcmp(argv[i], "--stats-out") == 0 && i + 1 < argc) {
            cfg->stats_path = argv[++i];
        } else {
            return -1;
        }
    }

    if (cfg->input_path == NULL) return -1;
    if (cfg->policy == POLICY_RR && cfg->quantum <= 0) return -1;
    if (cfg->policy != POLICY_RR && cfg->quantum < 0) return -1;
    if (cfg->cpus <= 0) return -1;

    return 0;
}

int main(int argc, char **argv) {
    sim_config_t cfg;
    int rc;

    if (parse_args(argc, argv, &cfg) != 0) {
        print_usage(stderr, argv[0]);
        return 1;
    }

    if (cfg.cpus == 1) rc = run_scheduler_single_cpu(&cfg);
    else rc = run_scheduler_multi_cpu(&cfg);

    return rc == 0 ? 0 : 1;
}
