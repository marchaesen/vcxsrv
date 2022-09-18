#!/usr/bin/env python3
# Copyright © 2020 - 2022 Collabora Ltd.
# Authors:
#   Tomeu Vizoso <tomeu.vizoso@collabora.com>
#   David Heidelberg <david.heidelberg@collabora.com>
#
# SPDX-License-Identifier: MIT

"""
Helper script to restrict running only required CI jobs
and show the job(s) logs.
"""

import argparse
import re
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from functools import partial
from itertools import chain
from typing import Optional

import gitlab
from colorama import Fore, Style
from gitlab_common import get_gitlab_project, read_token, wait_for_pipeline
from gitlab_gql import GitlabGQL, create_job_needs_dag, filter_dag, print_dag

REFRESH_WAIT_LOG = 10
REFRESH_WAIT_JOBS = 6

URL_START = "\033]8;;"
URL_END = "\033]8;;\a"

STATUS_COLORS = {
    "created": "",
    "running": Fore.BLUE,
    "success": Fore.GREEN,
    "failed": Fore.RED,
    "canceled": Fore.MAGENTA,
    "manual": "",
    "pending": "",
    "skipped": "",
}

COMPLETED_STATUSES = ["success", "failed"]


def print_job_status(job) -> None:
    """It prints a nice, colored job status with a link to the job."""
    if job.status == "canceled":
        return

    print(
        STATUS_COLORS[job.status]
        + "🞋 job "
        + URL_START
        + f"{job.web_url}\a{job.name}"
        + URL_END
        + f" :: {job.status}"
        + Style.RESET_ALL
    )


def print_job_status_change(job) -> None:
    """It reports job status changes."""
    if job.status == "canceled":
        return

    print(
        STATUS_COLORS[job.status]
        + "🗘 job "
        + URL_START
        + f"{job.web_url}\a{job.name}"
        + URL_END
        + f" has new status: {job.status}"
        + Style.RESET_ALL
    )


def pretty_wait(sec: int) -> None:
    """shows progressbar in dots"""
    for val in range(sec, 0, -1):
        print(f"⏲  {val} seconds", end="\r")
        time.sleep(1)


def monitor_pipeline(
    project,
    pipeline,
    target_job: Optional[str],
    dependencies,
    force_manual: bool,
    stress: bool,
) -> tuple[Optional[int], Optional[int]]:
    """Monitors pipeline and delegate canceling jobs"""
    statuses = {}
    target_statuses = {}
    stress_succ = 0
    stress_fail = 0

    if target_job:
        target_jobs_regex = re.compile(target_job.strip())

    while True:
        to_cancel = []
        for job in pipeline.jobs.list(all=True, sort="desc"):
            # target jobs
            if target_job and target_jobs_regex.match(job.name):
                if force_manual and job.status == "manual":
                    enable_job(project, job, True)

                if stress and job.status in ["success", "failed"]:
                    if job.status == "success":
                        stress_succ += 1
                    if job.status == "failed":
                        stress_fail += 1
                    retry_job(project, job)

                if (job.id not in target_statuses) or (
                    job.status not in target_statuses[job.id]
                ):
                    print_job_status_change(job)
                    target_statuses[job.id] = job.status
                else:
                    print_job_status(job)

                continue

            # all jobs
            if (job.id not in statuses) or (job.status not in statuses[job.id]):
                print_job_status_change(job)
                statuses[job.id] = job.status

            # dependencies and cancelling the rest
            if job.name in dependencies:
                if job.status == "manual":
                    enable_job(project, job, False)

            elif target_job and job.status not in [
                "canceled",
                "success",
                "failed",
                "skipped",
            ]:
                to_cancel.append(job)

        if target_job:
            cancel_jobs(project, to_cancel)

        if stress:
            print(
                "∑ succ: " + str(stress_succ) + "; fail: " + str(stress_fail),
                flush=False,
            )
            pretty_wait(REFRESH_WAIT_JOBS)
            continue

        print("---------------------------------", flush=False)

        if len(target_statuses) == 1 and {"running"}.intersection(
            target_statuses.values()
        ):
            return next(iter(target_statuses)), None

        if {"failed", "canceled"}.intersection(target_statuses.values()):
            return None, 1

        if {"success", "manual"}.issuperset(target_statuses.values()):
            return None, 0

        pretty_wait(REFRESH_WAIT_JOBS)


def enable_job(project, job, target: bool) -> None:
    """enable manual job"""
    pjob = project.jobs.get(job.id, lazy=True)
    pjob.play()
    if target:
        jtype = "🞋 "
    else:
        jtype = "(dependency)"
    print(Fore.MAGENTA + f"{jtype} job {job.name} manually enabled" + Style.RESET_ALL)


def retry_job(project, job) -> None:
    """retry job"""
    pjob = project.jobs.get(job.id, lazy=True)
    pjob.retry()
    jtype = "↻"
    print(Fore.MAGENTA + f"{jtype} job {job.name} manually enabled" + Style.RESET_ALL)


def cancel_job(project, job) -> None:
    """Cancel GitLab job"""
    pjob = project.jobs.get(job.id, lazy=True)
    pjob.cancel()
    print(f"♲ {job.name}")


def cancel_jobs(project, to_cancel) -> None:
    """Cancel unwanted GitLab jobs"""
    if not to_cancel:
        return

    with ThreadPoolExecutor(max_workers=6) as exe:
        part = partial(cancel_job, project)
        exe.map(part, to_cancel)


def print_log(project, job_id) -> None:
    """Print job log into output"""
    printed_lines = 0
    while True:
        job = project.jobs.get(job_id)

        # GitLab's REST API doesn't offer pagination for logs, so we have to refetch it all
        lines = job.trace().decode("unicode_escape").splitlines()
        for line in lines[printed_lines:]:
            print(line)
        printed_lines = len(lines)

        if job.status in COMPLETED_STATUSES:
            print(Fore.GREEN + f"Job finished: {job.web_url}" + Style.RESET_ALL)
            return
        pretty_wait(REFRESH_WAIT_LOG)


def parse_args() -> None:
    """Parse args"""
    parser = argparse.ArgumentParser(
        description="Tool to trigger a subset of container jobs "
        + "and monitor the progress of a test job",
        epilog="Example: mesa-monitor.py --rev $(git rev-parse HEAD) "
        + '--target ".*traces" ',
    )
    parser.add_argument("--target", metavar="target-job", help="Target job")
    parser.add_argument(
        "--rev", metavar="revision", help="repository git revision", required=True
    )
    parser.add_argument(
        "--token",
        metavar="token",
        help="force GitLab token, otherwise it's read from ~/.config/gitlab-token",
    )
    parser.add_argument(
        "--force-manual", action="store_true", help="Force jobs marked as manual"
    )
    parser.add_argument("--stress", action="store_true", help="Stresstest job(s)")
    return parser.parse_args()


def find_dependencies(target_job: str, project_path: str, sha: str) -> set[str]:
    gql_instance = GitlabGQL()
    dag, _ = create_job_needs_dag(
        gql_instance, {"projectPath": project_path.path_with_namespace, "sha": sha}
    )

    target_dep_dag = filter_dag(dag, target_job)
    print(Fore.YELLOW)
    print("Detected job dependencies:")
    print()
    print_dag(target_dep_dag)
    print(Fore.RESET)
    return set(chain.from_iterable(target_dep_dag.values()))


if __name__ == "__main__":
    try:
        t_start = time.perf_counter()

        args = parse_args()

        token = read_token(args.token)

        gl = gitlab.Gitlab(url="https://gitlab.freedesktop.org", private_token=token)

        cur_project = get_gitlab_project(gl, "mesa")

        print(f"Revision: {args.rev}")
        pipe = wait_for_pipeline(cur_project, args.rev)
        print(f"Pipeline: {pipe.web_url}")
        deps = set()
        if args.target:
            print("🞋 job: " + Fore.BLUE + args.target + Style.RESET_ALL)
            deps = find_dependencies(
                target_job=args.target, sha=args.rev, project_path=cur_project
            )
        target_job_id, ret = monitor_pipeline(
            cur_project, pipe, args.target, deps, args.force_manual, args.stress
        )

        if target_job_id:
            print_log(cur_project, target_job_id)

        t_end = time.perf_counter()
        spend_minutes = (t_end - t_start) / 60
        print(f"⏲ Duration of script execution: {spend_minutes:0.1f} minutes")

        sys.exit(ret)
    except KeyboardInterrupt:
        sys.exit(1)
