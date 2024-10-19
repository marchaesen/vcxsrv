#!/usr/bin/env python3
# Copyright © 2020 - 2022 Collabora Ltd.
# Authors:
#   Tomeu Vizoso <tomeu.vizoso@collabora.com>
#   David Heidelberg <david.heidelberg@collabora.com>
#
# For the dependencies, see the requirements.txt
# SPDX-License-Identifier: MIT

"""
Helper script to restrict running only required CI jobs
and show the job(s) logs.
"""

import argparse
import re
import sys
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from functools import partial
from itertools import chain
from subprocess import check_output, CalledProcessError
from typing import Dict, TYPE_CHECKING, Iterable, Literal, Optional, Tuple

import gitlab
import gitlab.v4.objects
from colorama import Fore, Style
from gitlab_common import (
    GITLAB_URL,
    TOKEN_DIR,
    get_gitlab_pipeline_from_url,
    get_gitlab_project,
    get_token_from_default_dir,
    pretty_duration,
    print_once,
    read_token,
    wait_for_pipeline,
)
from gitlab_gql import GitlabGQL, create_job_needs_dag, filter_dag, print_dag

if TYPE_CHECKING:
    from gitlab_gql import Dag

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
    "canceling": Fore.MAGENTA,
    "manual": "",
    "pending": "",
    "skipped": "",
}

COMPLETED_STATUSES = frozenset({"success", "failed"})
RUNNING_STATUSES = frozenset({"created", "pending", "running"})


def print_job_status(
    job: gitlab.v4.objects.ProjectPipelineJob,
    new_status: bool = False,
    job_name_field_pad: int = 0,
) -> None:
    """It prints a nice, colored job status with a link to the job."""
    if job.status in {"canceled", "canceling"}:
        return

    if new_status and job.status == "created":
        return

    job_name_field_pad = len(job.name) if job_name_field_pad < 1 else job_name_field_pad

    duration = job_duration(job)

    print_once(
        STATUS_COLORS[job.status]
        + "🞋 job "  # U+1F78B Round target
        + link2print(job.web_url, job.name, job_name_field_pad)
        + (f"has new status: {job.status}" if new_status else f"{job.status}")
        + (f" ({pretty_duration(duration)})" if job.started_at else "")
        + Style.RESET_ALL
    )


def job_duration(job: gitlab.v4.objects.ProjectPipelineJob) -> float:
    """
    Given a job, report the time lapsed in execution.
    :param job: Pipeline job
    :return: Current time in execution
    """
    if job.duration:
        return job.duration
    elif job.started_at:
        return time.perf_counter() - time.mktime(job.started_at.timetuple())
    return 0.0


def pretty_wait(sec: int) -> None:
    """shows progressbar in dots"""
    for val in range(sec, 0, -1):
        print(f"⏲  {val} seconds", end="\r")  # U+23F2 Timer clock
        time.sleep(1)


def monitor_pipeline(
    project: gitlab.v4.objects.Project,
    pipeline: gitlab.v4.objects.ProjectPipeline,
    target_jobs_regex: re.Pattern,
    include_stage_regex: re.Pattern,
    exclude_stage_regex: re.Pattern,
    dependencies: set[str],
    stress: int,
) -> tuple[Optional[int], Optional[int], Dict[str, Dict[int, Tuple[float, str, str]]]]:
    """Monitors pipeline and delegate canceling jobs"""
    statuses: dict[str, str] = defaultdict(str)
    target_statuses: dict[str, str] = defaultdict(str)
    stress_status_counter: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    execution_times = defaultdict(lambda: defaultdict(tuple))
    target_id: int = -1
    name_field_pad: int = len(max(dependencies, key=len))+2
    # In a running pipeline, we can skip following job traces that are in these statuses.
    skip_follow_statuses: frozenset[str] = (COMPLETED_STATUSES)

    # Pre-populate the stress status counter for already completed target jobs.
    if stress:
        # When stress test, it is necessary to collect this information before start.
        for job in pipeline.jobs.list(all=True, include_retried=True):
            if target_jobs_regex.fullmatch(job.name) and \
               include_stage_regex.fullmatch(job.stage) and \
               not exclude_stage_regex.fullmatch(job.stage) and \
               job.status in COMPLETED_STATUSES:
                stress_status_counter[job.name][job.status] += 1
                execution_times[job.name][job.id] = (job_duration(job), job.status, job.web_url)

    # jobs_waiting is a list of job names that are waiting for status update.
    # It occurs when a job that we want to run depends on another job that is not yet finished.
    jobs_waiting = []
    # FIXME: This function has too many parameters, consider refactoring.
    enable_job_fn = partial(
        enable_job,
        project=project,
        pipeline=pipeline,
        job_name_field_pad=name_field_pad,
        jobs_waiting=jobs_waiting,
    )
    while True:
        deps_failed = []
        to_cancel = []
        jobs_waiting.clear()
        for job in sorted(pipeline.jobs.list(all=True), key=lambda j: j.name):
            if target_jobs_regex.fullmatch(job.name) and \
               include_stage_regex.fullmatch(job.stage) and \
               not exclude_stage_regex.fullmatch(job.stage):
                target_id = job.id
                target_status = job.status

                if stress and target_status in COMPLETED_STATUSES:
                    if (
                        stress < 0
                        or sum(stress_status_counter[job.name].values()) < stress
                    ):
                        stress_status_counter[job.name][target_status] += 1
                        execution_times[job.name][job.id] = (job_duration(job), target_status, job.web_url)
                        job = enable_job_fn(job=job, action_type="retry")
                else:
                    execution_times[job.name][job.id] = (job_duration(job), target_status, job.web_url)
                    job = enable_job_fn(job=job, action_type="target")

                print_job_status(job, target_status not in target_statuses[job.name], name_field_pad)
                target_statuses[job.name] = target_status
                continue

            # all other non-target jobs
            if job.status != statuses[job.name]:
                print_job_status(job, True, name_field_pad)
                statuses[job.name] = job.status

            # run dependencies and cancel the rest
            if job.name in dependencies:
                job = enable_job_fn(job=job, action_type="dep")
                if job.status == "failed":
                    deps_failed.append(job.name)
            else:
                to_cancel.append(job)

        cancel_jobs(project, to_cancel)

        if stress:
            enough = True
            for job_name, status in sorted(stress_status_counter.items()):
                print(
                    f"* {job_name:{name_field_pad}}succ: {status['success']}; "
                    f"fail: {status['failed']}; "
                    f"total: {sum(status.values())} of {stress}",
                    flush=False,
                )
                if stress < 0 or sum(status.values()) < stress:
                    enough = False

            if not enough:
                pretty_wait(REFRESH_WAIT_JOBS)
                continue

        if jobs_waiting:
            print_once(
                f"{Fore.YELLOW}Waiting for jobs to update status:",
                ", ".join(jobs_waiting),
                Fore.RESET,
            )
            pretty_wait(REFRESH_WAIT_JOBS)
            continue

        if len(target_statuses) == 1 and RUNNING_STATUSES.intersection(
            target_statuses.values()
        ):
            return target_id, None, execution_times

        if (
            {"failed"}.intersection(target_statuses.values())
            and not RUNNING_STATUSES.intersection(target_statuses.values())
        ):
            return None, 1, execution_times

        if (
            {"skipped"}.intersection(target_statuses.values())
            and not RUNNING_STATUSES.intersection(target_statuses.values())
        ):
            print(
                Fore.RED,
                "Target in skipped state, aborting. Failed dependencies:",
                deps_failed,
                Fore.RESET,
            )
            return None, 1, execution_times

        if skip_follow_statuses.issuperset(target_statuses.values()):
            return None, 0, execution_times

        pretty_wait(REFRESH_WAIT_JOBS)


def get_pipeline_job(
    pipeline: gitlab.v4.objects.ProjectPipeline,
    job_id: int,
) -> gitlab.v4.objects.ProjectPipelineJob:
    pipeline_jobs = pipeline.jobs.list(all=True)
    return [j for j in pipeline_jobs if j.id == job_id][0]


def enable_job(
    project: gitlab.v4.objects.Project,
    pipeline: gitlab.v4.objects.ProjectPipeline,
    job: gitlab.v4.objects.ProjectPipelineJob,
    action_type: Literal["target", "dep", "retry"],
    job_name_field_pad: int = 0,
    jobs_waiting: list[str] = [],
) -> gitlab.v4.objects.ProjectPipelineJob:
    # We want to run this job, but it is not ready to run yet, so let's try again in the next
    # iteration.
    if job.status == "created":
        jobs_waiting.append(job.name)
        return job

    if (
        (job.status in COMPLETED_STATUSES and action_type != "retry")
        or job.status in {"skipped"} | RUNNING_STATUSES
    ):
        return job

    pjob = project.jobs.get(job.id, lazy=True)

    if job.status in {"success", "failed", "canceled", "canceling"}:
        new_job = pjob.retry()
        job = get_pipeline_job(pipeline, new_job["id"])
    else:
        pjob.play()
        job = get_pipeline_job(pipeline, pjob.id)

    if action_type == "target":
        jtype = "🞋 target"  # U+1F78B Round target
    elif action_type == "retry":
        jtype = "↻ retrying"  # U+21BB Clockwise open circle arrow
    else:
        jtype = "↪ dependency"  # U+21AA Left Arrow Curving Right

    job_name_field_pad = len(job.name) if job_name_field_pad < 1 else job_name_field_pad
    print(Fore.MAGENTA + f"{jtype} job {job.name:{job_name_field_pad}}manually enabled" + Style.RESET_ALL)

    return job


def cancel_job(
    project: gitlab.v4.objects.Project,
    job: gitlab.v4.objects.ProjectPipelineJob
) -> None:
    """Cancel GitLab job"""
    if job.status not in RUNNING_STATUSES:
        return
    pjob = project.jobs.get(job.id, lazy=True)
    pjob.cancel()
    print(f"🗙 {job.name}", end=" ")  # U+1F5D9 Cancellation X


def cancel_jobs(
    project: gitlab.v4.objects.Project,
    to_cancel: list
) -> None:
    """Cancel unwanted GitLab jobs"""
    if not to_cancel:
        return

    with ThreadPoolExecutor(max_workers=6) as exe:
        part = partial(cancel_job, project)
        exe.map(part, to_cancel)

    # The cancelled jobs are printed without a newline
    print_once()


def print_log(
    project: gitlab.v4.objects.Project,
    job_id: int
) -> None:
    """Print job log into output"""
    printed_lines = 0
    while True:
        job = project.jobs.get(job_id)

        # GitLab's REST API doesn't offer pagination for logs, so we have to refetch it all
        lines = job.trace().decode().splitlines()
        for line in lines[printed_lines:]:
            print(line)
        printed_lines = len(lines)

        if job.status in COMPLETED_STATUSES:
            print(Fore.GREEN + f"Job finished: {job.web_url}" + Style.RESET_ALL)
            return
        pretty_wait(REFRESH_WAIT_LOG)


def parse_args() -> argparse.Namespace:
    """Parse args"""
    parser = argparse.ArgumentParser(
        description="Tool to trigger a subset of container jobs "
        + "and monitor the progress of a test job",
        epilog="Example: mesa-monitor.py --rev $(git rev-parse HEAD) "
        + '--target ".*traces" ',
    )
    parser.add_argument(
        "--target",
        metavar="target-job",
        help="Target job regex. For multiple targets, pass multiple values, "
             "eg. `--target foo bar`. Only jobs in the target stage(s) "
             "supplied, and their dependencies, will be considered.",
        required=True,
        nargs=argparse.ONE_OR_MORE,
    )
    parser.add_argument(
        "--include-stage",
        metavar="include-stage",
        help="Job stages to include when searching for target jobs. "
             "For multiple targets, pass multiple values, eg. "
             "`--include-stage foo bar`.",
        default=[".*"],
        nargs=argparse.ONE_OR_MORE,
    )
    parser.add_argument(
        "--exclude-stage",
        metavar="exclude-stage",
        help="Job stages to exclude when searching for target jobs. "
             "For multiple targets, pass multiple values, eg. "
             "`--exclude-stage foo bar`. By default, performance and "
             "post-merge jobs are excluded; pass --exclude-stage '' to "
             "include them for consideration.",
        default=["performance", ".*-postmerge"],
        nargs=argparse.ONE_OR_MORE,
    )
    parser.add_argument(
        "--token",
        metavar="token",
        type=str,
        default=get_token_from_default_dir(),
        help="Use the provided GitLab token or token file, "
             f"otherwise it's read from {TOKEN_DIR / 'gitlab-token'}",
    )
    parser.add_argument(
        "--force-manual", action="store_true",
        help="Deprecated argument; manual jobs are always force-enabled"
    )
    parser.add_argument(
        "--stress",
        default=0,
        type=int,
        help="Stresstest job(s). Specify the number of times to rerun the selected jobs, "
             "or use -1 for indefinite. Defaults to 0. If jobs have already been executed, "
             "this will ensure the total run count respects the specified number.",
    )
    parser.add_argument(
        "--project",
        default="mesa",
        help="GitLab project in the format <user>/<project> or just <project>",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Exit after printing target jobs and dependencies",
    )

    mutex_group1 = parser.add_mutually_exclusive_group()
    mutex_group1.add_argument(
        "--rev", default="HEAD", metavar="revision", help="repository git revision (default: HEAD)"
    )
    mutex_group1.add_argument(
        "--pipeline-url",
        help="URL of the pipeline to use, instead of auto-detecting it.",
    )
    mutex_group1.add_argument(
        "--mr",
        type=int,
        help="ID of a merge request; the latest pipeline in that MR will be used.",
    )

    args = parser.parse_args()

    # argparse doesn't support groups inside add_mutually_exclusive_group(),
    # which means we can't just put `--project` and `--rev` in a group together,
    # we have to do this by heand instead.
    if args.pipeline_url and args.project != parser.get_default("project"):
        # weird phrasing but it's the error add_mutually_exclusive_group() gives
        parser.error("argument --project: not allowed with argument --pipeline-url")

    return args


def print_detected_jobs(
    target_dep_dag: "Dag",
    dependency_jobs: Iterable[str],
    target_jobs: Iterable[str],
) -> None:
    def print_job_set(color: str, kind: str, job_set: Iterable[str]):
        print(
            color + f"Running {len(job_set)} {kind} jobs: ",
            "\n\t",
            ", ".join(sorted(job_set)),
            Fore.RESET,
            "\n",
        )

    print(Fore.YELLOW + "Detected target job and its dependencies:", "\n")
    print_dag(target_dep_dag)
    print_job_set(Fore.MAGENTA, "dependency", dependency_jobs)
    print_job_set(Fore.BLUE, "target", target_jobs)


def find_dependencies(
    token: str | None,
    target_jobs_regex: re.Pattern,
    include_stage_regex: re.Pattern,
    exclude_stage_regex: re.Pattern,
    project_path: str,
    iid: int
) -> set[str]:
    """
    Find the dependencies of the target jobs in a GitLab pipeline.

    This function uses the GitLab GraphQL API to fetch the job dependency graph
    of a pipeline, filters the graph to only include the target jobs and their
    dependencies, and returns the names of these jobs.

    Args:
        token (str | None): The GitLab API token. If None, the API is accessed without
                            authentication.
        target_jobs_regex (re.Pattern): A regex pattern to match the names of the target jobs.
        project_path (str): The path of the GitLab project.
        iid (int): The internal ID of the pipeline.

    Returns:
        set[str]: A set of the names of the target jobs and their dependencies.

    Raises:
        SystemExit: If no target jobs are found in the pipeline.
    """
    gql_instance = GitlabGQL(token=token)
    dag = create_job_needs_dag(
        gql_instance, {"projectPath": project_path.path_with_namespace, "iid": iid}
    )

    target_dep_dag = filter_dag(dag, target_jobs_regex, include_stage_regex, exclude_stage_regex)
    if not target_dep_dag:
        print(Fore.RED + "The job(s) were not found in the pipeline." + Fore.RESET)
        sys.exit(1)

    dependency_jobs = set(chain.from_iterable(d["needs"] for d in target_dep_dag.values()))
    target_jobs = set(target_dep_dag.keys())
    print_detected_jobs(target_dep_dag, dependency_jobs, target_jobs)
    return target_jobs.union(dependency_jobs)


def print_monitor_summary(
    execution_collection: Dict[str, Dict[int, Tuple[float, str, str]]],
    t_start: float,
) -> None:
    """Summary of the test execution"""
    t_end = time.perf_counter()
    spend_minutes = (t_end - t_start) / 60
    print(f"⏲ Duration of script execution: {spend_minutes:0.1f} minutes")  # U+23F2 Timer clock
    if len(execution_collection) == 0:
        return
    print(f"⏲ Jobs execution times:")  # U+23F2 Timer clock
    job_names = list(execution_collection.keys())
    job_names.sort()
    name_field_pad = len(max(job_names, key=len)) + 2
    for name in job_names:
        job_executions = execution_collection[name]
        job_times = ', '.join([__job_duration_record(job_execution)
                               for job_execution in sorted(job_executions.items())])
        print(f"* {name:{name_field_pad}}: ({len(job_executions)}) {job_times}")


def __job_duration_record(dict_item: tuple) -> str:
    """
    Format each pair of job and its duration.
    :param job_execution: item of execution_collection[name][idn]: Dict[int, Tuple[float, str, str]]
    """
    job_id = f"{dict_item[0]}"  # dictionary key
    job_duration, job_status, job_url = dict_item[1]  # dictionary value, the tuple
    return (f"{STATUS_COLORS[job_status]}"
            f"{link2print(job_url, job_id)}: {pretty_duration(job_duration):>8}"
            f"{Style.RESET_ALL}")


def link2print(url: str, text: str, text_pad: int = 0) -> str:
    text_pad = len(text) if text_pad < 1 else text_pad
    return f"{URL_START}{url}\a{text:{text_pad}}{URL_END}"


def main() -> None:
    try:
        t_start = time.perf_counter()

        args = parse_args()

        token = read_token(args.token)

        gl = gitlab.Gitlab(url=GITLAB_URL,
                           private_token=token,
                           retry_transient_errors=True)

        REV: str = args.rev

        if args.pipeline_url:
            pipe, cur_project = get_gitlab_pipeline_from_url(gl, args.pipeline_url)
            REV = pipe.sha
        else:
            mesa_project = gl.projects.get("mesa/mesa")
            projects = [mesa_project]
            if args.mr:
                REV = mesa_project.mergerequests.get(args.mr).sha
            else:
                REV = check_output(['git', 'rev-parse', REV]).decode('ascii').strip()

                if args.rev == 'HEAD':
                    try:
                        branch_name = check_output([
                            'git', 'symbolic-ref', '-q', 'HEAD',
                        ]).decode('ascii').strip()
                    except CalledProcessError:
                        branch_name = ""

                    # Ignore detached heads
                    if branch_name:
                        tracked_remote = check_output([
                            'git', 'for-each-ref', '--format=%(upstream)',
                            branch_name,
                        ]).decode('ascii').strip()

                        # Ignore local branches that do not track any remote
                        if tracked_remote:
                            remote_rev = check_output([
                                'git', 'rev-parse', tracked_remote,
                            ]).decode('ascii').strip()

                            if REV != remote_rev:
                                print(
                                    f"Local HEAD commit {REV[:10]} is different than "
                                    f"tracked remote HEAD commit {remote_rev[:10]}"
                                )
                                print("Did you forget to `git push` ?")

                projects.append(get_gitlab_project(gl, args.project))
            (pipe, cur_project) = wait_for_pipeline(projects, REV)

        print(f"Revision: {REV}")
        print(f"Pipeline: {pipe.web_url}")

        target = '|'.join(args.target)
        target = target.strip()

        print("🞋 target job: " + Fore.BLUE + target + Style.RESET_ALL)  # U+1F78B Round target

        # Implicitly include `parallel:` jobs
        target = f'({target})' + r'( \d+/\d+)?'

        target_jobs_regex = re.compile(target)

        include_stage = '|'.join(args.include_stage)
        include_stage = include_stage.strip()

        print("🞋 target from stages: " + Fore.BLUE + include_stage + Style.RESET_ALL)  # U+1F78B Round target

        include_stage_regex = re.compile(include_stage)

        exclude_stage = '|'.join(args.exclude_stage)
        exclude_stage = exclude_stage.strip()

        print("🞋 target excluding stages: " + Fore.BLUE + exclude_stage + Style.RESET_ALL)  # U+1F78B Round target

        exclude_stage_regex = re.compile(exclude_stage)

        deps = find_dependencies(
            token=token,
            target_jobs_regex=target_jobs_regex,
            include_stage_regex=include_stage_regex,
            exclude_stage_regex=exclude_stage_regex,
            iid=pipe.iid,
            project_path=cur_project
        )

        if args.dry_run:
            sys.exit(0)

        target_job_id, ret, exec_t = monitor_pipeline(
            cur_project,
            pipe,
            target_jobs_regex,
            include_stage_regex,
            exclude_stage_regex,
            deps,
            args.stress
        )

        if target_job_id:
            print_log(cur_project, target_job_id)

        print_monitor_summary(exec_t, t_start)

        sys.exit(ret)
    except KeyboardInterrupt:
        sys.exit(1)


if __name__ == "__main__":
    main()
