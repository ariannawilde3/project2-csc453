from pathlib import Path
import re

DISPATCH_RE = re.compile(r"^(\d+) DISPATCH(?: CPU(\d+))? (\S+)$")
PREEMPT_RE = re.compile(r"^(\d+) PREEMPT(?: CPU(\d+))? (\S+)$")
COMPLETE_RE = re.compile(r"^(\d+) COMPLETE(?: CPU(\d+))? (\S+)$")
ARRIVE_RE = re.compile(r"^(\d+) ARRIVE (\S+)$")


def load_workload(workload_path):
    jobs = {}
    for raw in Path(workload_path).read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 4:
            raise ValueError(f"invalid workload line: {line}")
        job_id, arrival, priority, total = parts
        jobs[job_id] = {
            "arrival": int(arrival),
            "priority": int(priority),
            "total": int(total),
        }
    return jobs


def parse_stats(stats_path):
    lines = Path(stats_path).read_text().strip().splitlines()
    if not lines or lines[0] != "STATS":
        return None, "stats missing STATS header"

    per_job = {}
    avg = None
    for line in lines[1:]:
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "AVG":
            if len(parts) != 7:
                return None, f"malformed AVG line: {line}"
            avg = {
                "response": float(parts[2]),
                "turnaround": float(parts[4]),
                "waiting": float(parts[6]),
            }
        else:
            if len(parts) != 7:
                return None, f"malformed stats line: {line}"
            per_job[parts[0]] = {
                "response": int(parts[2]),
                "turnaround": int(parts[4]),
                "waiting": int(parts[6]),
            }
    if avg is None:
        return None, "missing AVG line"
    return {"jobs": per_job, "avg": avg}, "ok"


def check_stats_consistency(trace_metrics, stats_path):
    parsed, msg = parse_stats(stats_path)
    if parsed is None:
        return False, msg

    stats_jobs = parsed["jobs"]
    if set(stats_jobs) != set(trace_metrics):
        return False, "stats jobs do not match workload jobs"

    total_response = 0.0
    total_turnaround = 0.0
    total_waiting = 0.0
    for job_id, expected in trace_metrics.items():
        actual = stats_jobs[job_id]
        if actual != expected:
            return False, f"stats mismatch for {job_id}: expected {expected}, got {actual}"
        total_response += actual["response"]
        total_turnaround += actual["turnaround"]
        total_waiting += actual["waiting"]

    count = len(trace_metrics)
    expected_avg = {
        "response": round(total_response / count + 1e-9, 2),
        "turnaround": round(total_turnaround / count + 1e-9, 2),
        "waiting": round(total_waiting / count + 1e-9, 2),
    }
    actual_avg = parsed["avg"]
    for key in ("response", "turnaround", "waiting"):
        if abs(expected_avg[key] - actual_avg[key]) > 0.01:
            return False, f"AVG {key} mismatch: expected {expected_avg[key]:.2f}, got {actual_avg[key]:.2f}"
    return True, "ok"


def check_invariants(trace_path, workload_path=None, stats_path=None, cpus=None, policy=None, quantum=None):
    lines = Path(trace_path).read_text().strip().splitlines()
    if not lines or lines[-1] != "END":
        return False, "missing END"

    jobs = load_workload(workload_path) if workload_path else {}
    running_jobs = {}
    cpu_owner = {}
    arrived = set()
    completed = set()
    ready_since = {}
    first_dispatch = {}
    waiting_time = {job_id: 0 for job_id in jobs}
    remaining = {job_id: jobs[job_id]["total"] for job_id in jobs}
    last_time = -1
    strict_duration_checks = cpus in (None, 1)

    for line in lines[:-1]:
        match = ARRIVE_RE.match(line)
        if match:
            time = int(match.group(1))
            job = match.group(2)
            if time < last_time:
                return False, "trace timestamps are not monotonic"
            last_time = time
            if job in jobs:
                if time != jobs[job]["arrival"]:
                    return False, f"{job} arrived at {time}, expected {jobs[job]['arrival']}"
            if job in arrived:
                return False, f"{job} arrived more than once"
            if job in completed:
                return False, f"{job} arrived after completion"
            arrived.add(job)
            ready_since[job] = time
            continue

        match = DISPATCH_RE.match(line)
        if match:
            time = int(match.group(1))
            cpu = int(match.group(2)) if match.group(2) is not None else 0
            job = match.group(3)
            if time < last_time:
                return False, "trace timestamps are not monotonic"
            last_time = time
            if cpus is not None and (cpu < 0 or cpu >= cpus):
                return False, f"invalid CPU id {cpu}"
            if job in jobs and job not in arrived:
                return False, f"{job} dispatched before arrival"
            if job in completed:
                return False, f"{job} dispatched after completion"
            if job in running_jobs:
                return False, f"{job} dispatched while already running"
            if cpu in cpu_owner:
                return False, f"CPU{cpu} dispatched while already busy"
            if cpus is not None and len(running_jobs) >= cpus:
                return False, "more running jobs than CPUs"
            if job not in ready_since:
                return False, f"{job} dispatched without being ready"
            waiting_time[job] = waiting_time.get(job, 0) + (time - ready_since[job])
            del ready_since[job]
            running_jobs[job] = {"cpu": cpu, "dispatch_time": time}
            cpu_owner[cpu] = job
            first_dispatch.setdefault(job, time)
            continue

        match = PREEMPT_RE.match(line)
        if match:
            time = int(match.group(1))
            cpu = int(match.group(2)) if match.group(2) is not None else 0
            job = match.group(3)
            if time < last_time:
                return False, "trace timestamps are not monotonic"
            last_time = time
            if job not in running_jobs:
                return False, f"{job} preempted while not running"
            if running_jobs[job]["cpu"] != cpu:
                return False, f"{job} preempted on wrong CPU"

            if strict_duration_checks:
                segment = time - running_jobs[job]["dispatch_time"]
                if policy == "RR":
                    segment += 1
                    if quantum is not None and segment != quantum:
                        return False, f"{job} RR preempted after {segment} ticks instead of quantum {quantum}"
                elif policy == "SRTF" and segment <= 0:
                    return False, f"{job} SRTF preempted without executing any prior tick"

                remaining[job] -= segment
                if remaining[job] <= 0:
                    return False, f"{job} preempted with nonpositive remaining time"

            del cpu_owner[cpu]
            del running_jobs[job]
            ready_since[job] = time + 1 if policy == "RR" else time
            continue

        match = COMPLETE_RE.match(line)
        if match:
            time = int(match.group(1))
            cpu = int(match.group(2)) if match.group(2) is not None else 0
            job = match.group(3)
            if time < last_time:
                return False, "trace timestamps are not monotonic"
            last_time = time
            if job not in running_jobs:
                return False, f"{job} completed while not running"
            if running_jobs[job]["cpu"] != cpu:
                return False, f"{job} completed on wrong CPU"

            if strict_duration_checks:
                segment = time - running_jobs[job]["dispatch_time"] + 1
                remaining[job] -= segment
                if remaining[job] != 0:
                    return False, f"{job} completed with remaining time {remaining[job]}"

            del cpu_owner[cpu]
            del running_jobs[job]
            if job in completed:
                return False, f"{job} completed twice"
            completed.add(job)
            continue

        return False, f"unrecognized trace line: {line}"

    if running_jobs:
        return False, f"jobs left running at END: {sorted(running_jobs)}"
    if ready_since:
        return False, f"jobs left ready at END: {sorted(ready_since)}"

    if jobs:
        missing_arrivals = set(jobs) - arrived
        if missing_arrivals:
            return False, f"jobs never arrived: {sorted(missing_arrivals)}"
        missing_completions = set(jobs) - completed
        if missing_completions:
            return False, f"jobs never completed: {sorted(missing_completions)}"

    if stats_path and jobs:
        trace_metrics = {}
        for job_id, job in jobs.items():
            if job_id not in first_dispatch:
                return False, f"{job_id} never dispatched"
            completion_line_time = None
            for line in lines[:-1]:
                match = COMPLETE_RE.match(line)
                if match and match.group(3) == job_id:
                    completion_line_time = int(match.group(1))
                    break
            trace_metrics[job_id] = {
                "response": first_dispatch[job_id] - job["arrival"],
                "turnaround": (completion_line_time + 1) - job["arrival"],
                "waiting": waiting_time[job_id],
            }

        ok, msg = check_stats_consistency(trace_metrics, stats_path)
        if not ok:
            return False, msg

    return True, "ok"
