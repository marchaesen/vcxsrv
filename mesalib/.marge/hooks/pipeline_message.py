#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

# Provide a markdown-formatted message summarizing the reasons why a pipeline failed.
# Marge bot can use this script to provide more helpful comments when CI fails.
# Example for running locally:
# ./bin/ci/pipeline_message.sh --project-id 176 --pipeline-id 1310098


import argparse
import asyncio
import logging
from typing import Any

import aiohttp

PER_PAGE: int = 6000


async def get_pipeline_status(
    session: aiohttp.ClientSession, project_id: str, pipeline_id: str
):
    url = f"https://gitlab.freedesktop.org/api/v4/projects/{project_id}/pipelines/{pipeline_id}"
    logging.info(f"Fetching pipeline status from {url}")
    async with session.get(url) as response:
        response.raise_for_status()
        pipeline_details = await response.json()
    return pipeline_details.get("status")


async def get_jobs_for_pipeline(
    session: aiohttp.ClientSession, project_id: str, pipeline_id: str
):
    url = f"https://gitlab.freedesktop.org/api/v4/projects/{project_id}/pipelines/{pipeline_id}/jobs"
    logging.info(url)
    jobs = []
    params = {"per_page": PER_PAGE}
    async with session.get(url, params=params) as response:
        response.raise_for_status()
        jobs = await response.json()
    return jobs


def get_problem_jobs(jobs: list[dict[str, Any]]):
    ignore_stage_list = [
        "postmerge",
        "performance",
    ]
    problem_jobs = []
    for job in jobs:
        if any(ignore.lower() in job["stage"] for ignore in ignore_stage_list):
            continue
        if job["status"] in {"failed", "canceled"}:
            problem_jobs.append(job)
    return problem_jobs


def unexpected_improvements(failed_test_array):
    if failed_test_array["unexpected_improvements"]:
        unexpected_improvements_count = len(
            failed_test_array["unexpected_improvements"]
        )
        return f" {unexpected_improvements_count} improved test{'s' if unexpected_improvements_count != 1 else ''}"
    return ""


def fails(failed_test_array):
    if failed_test_array["fails"]:
        fails_count = len(failed_test_array["fails"])
        return f" {fails_count} failed test{'s' if fails_count != 1 else ''}"
    return ""


def crashes(failed_test_array):
    if failed_test_array["crashes"]:
        crash_count = len(failed_test_array["crashes"])
        return f" {crash_count} crashed test{'s' if crash_count != 1 else ''}"
    return ""


def get_failed_test_details(failed_test_array):
    message = ""
    max_tests_to_display = 5

    if failed_test_array["unexpected_improvements"]:
        for i, test in enumerate(failed_test_array["unexpected_improvements"]):
            if i > max_tests_to_display:
                message += "  \nand more...<br>"
                break
            message += f"{test}<br>"

    if failed_test_array["fails"]:
        for i, test in enumerate(failed_test_array["fails"]):
            if i > max_tests_to_display:
                message += "  \nand more...<br>"
                break
            message += f"{test}<br>"

    if failed_test_array["crashes"]:
        for i, test in enumerate(failed_test_array["crashes"]):
            if i > max_tests_to_display:
                message += "  \nand more...<br>"
                break
            message += f"{test}<br>"

    return message


def get_failed_test_summary_message(failed_test_array):
    summary_msg = "<summary>"
    summary_msg += unexpected_improvements(failed_test_array)
    summary_msg += fails(failed_test_array)
    summary_msg += crashes(failed_test_array)
    summary_msg += "</summary>"
    return summary_msg


def sort_failed_tests_by_status(failures_csv):
    failed_test_array = {
        "unexpected_improvements": [],
        "fails": [],
        "crashes": [],
        "timeouts": [],
    }

    for test in failures_csv.splitlines():
        if "UnexpectedImprovement" in test:
            failed_test_array["unexpected_improvements"].append(test)
        elif "Fail" in test:
            failed_test_array["fails"].append(test)
        elif "Crash" in test:
            failed_test_array["crashes"].append(test)
        elif "Timeout" in test:
            failed_test_array["timeouts"].append(test)

    return failed_test_array


async def get_failures_csv(session, project_id, job):
    job_id = job["id"]
    url = f"https://gitlab.freedesktop.org/api/v4/projects/{project_id}/jobs/{job_id}/artifacts/results/failures.csv"
    async with session.get(url) as response:
        if response.status == 200:
            text = await response.text()
            return text
        else:
            logging.debug(f"No response from: {url}")
            return ""


async def get_test_failures(session, project_id, job):
    failures_csv = await get_failures_csv(session, project_id, job)
    if not failures_csv:
        return ""

    # If just one test failed, don't bother with more complicated sorting
    lines = failures_csv.splitlines()
    if len(lines) == 1:
        return ": " + lines[0] + "<br>"

    failed_test_array = sort_failed_tests_by_status(failures_csv)
    failures_msg = "<details>"
    failures_msg += get_failed_test_summary_message(failed_test_array)
    failures_msg += get_failed_test_details(failed_test_array)
    failures_msg += "</details>"

    return failures_msg


async def get_trace_failures(session, project_id, job):
    project_json = await get_project_json(session, project_id)
    path = project_json.get("path", "")
    if not path:
        return ""

    job_id = job["id"]
    url = f"https://mesa.pages.freedesktop.org/-/{path}/-/jobs/{job_id}/artifacts/results/summary/problems.html"
    async with session.get(url) as response:
        if response.status == 200:
            return url
        else:
            logging.debug(f"No response from: {url}")
            return ""


async def get_project_json(session, project_id):
    url_project_id = f"https://gitlab.freedesktop.org/api/v4/projects/{project_id}"
    async with session.get(url_project_id) as response:
        if response.status == 200:
            return await response.json()
        else:
            logging.debug(f"No response from: {url_project_id}")
            return ""


async def get_job_log(session: aiohttp.ClientSession, project_id: str, job_id: int):
    project_json = await get_project_json(session, project_id)
    path_with_namespace = project_json.get("path_with_namespace", "")
    if not path_with_namespace:
        return ""

    url_job_log = (
        f"https://gitlab.freedesktop.org/{path_with_namespace}/-/jobs/{job_id}/raw"
    )
    async with session.get(url_job_log) as response:
        if response.status == 200:
            return await response.text()
        else:
            logging.debug(f"No response from job log: {url_job_log}")
            return ""


async def search_job_log_for_errors(session, project_id, job):
    log_error_message = ""

    # Bypass these generic error messages in hopes of finding a more specific error.
    # The entries are case insensitive. Keep them in alphabetical order and don't
    # forget to add a comma after each entry
    ignore_list = [
        "403: b",
        "aborting",
        "building c",
        "continuing",
        "error_msg      : None",
        "error_type",
        "error generated",
        "errors generated",
        "exit code",
        "exit status",
        "exiting now",
        "job failed",
        "no_error",
        "no files to upload",
        "performing test",
        "ret code",
        "retry",
        "retry-all-errors",
        "strerror_",
        "success",
        "unknown-section",
    ]
    job_log = await get_job_log(session, project_id, job["id"])

    for line in reversed(job_log.splitlines()):
        if "fatal" in line.lower():
            # remove date and formatting before fatal message
            log_error_message = line[line.lower().find("fatal") :]
            break

        if "error" in line.lower():
            if any(ignore.lower() in line.lower() for ignore in ignore_list):
                continue

            # remove date and formatting before error message
            log_error_message = line[line.lower().find("error") :].strip()

            # if there is no further info after the word error then it's not helpful
            # so reset the message and try again.
            if log_error_message.lower() in {"error", "errors", "error:", "errors:"}:
                log_error_message = ""
                continue
            break

        # timeout msg from .gitlab-ci/lava/lava_job_submitter.py
        if "expected to take at least" in line.lower():
            log_error_message = line
            break

    return log_error_message


async def process_single_job(session, project_id, job):
    job_url = job.get("web_url", "")
    if not job_url:
        logging.info(f"Job {job['name']} is missing a web_url")

    job_name = job.get("name", "Unnamed Job")
    message = f"[{job_name}]({job_url})"

    # if a job times out it's cancelled, so worth mentioning here
    if job["status"] == "canceled":
        return f"{message}: canceled<br>"

    # if it's not a script failure then all we can do is give the gitlab assigned reason
    if job["failure_reason"] != "script_failure":
        return f"{message}: {job['failure_reason']}<br>"

    test_failures = await get_test_failures(session, project_id, job)
    if test_failures:
        return f"{message}{test_failures}"

    trace_failures = await get_trace_failures(session, project_id, job)
    if trace_failures:
        return f"{message}: has a [trace failure]({trace_failures})<br>"

    log_error_message = await search_job_log_for_errors(session, project_id, job)
    if log_error_message:
        return f"{message}: {log_error_message}<br>"

    return f"{message}<br>"


async def process_job_with_limit(session, project_id, job):
    # Use at most 10 concurrent tasks
    semaphore = asyncio.Semaphore(10)
    async with semaphore:
        return await process_single_job(session, project_id, job)


async def process_problem_jobs(session, project_id, problem_jobs):

    problem_jobs_count = len(problem_jobs)

    if problem_jobs_count == 1:
        message = f"<br>There were problems with job: "
        message += await process_single_job(session, project_id, problem_jobs[0])
        return message

    message = f"<details>"
    message += f"<summary>"
    message += f"There were problems with {problem_jobs_count} jobs: "
    message += "</summary>"

    tasks = [process_job_with_limit(session, project_id, job) for job in problem_jobs]

    results = await asyncio.gather(*tasks)

    for result in results:
        message += result

    message += f"</details>"

    return message


async def main(pipeline_id: str, project_id: str = "176") -> str:

    message = ""

    try:
        timeout = aiohttp.ClientTimeout(total=120)
        logging.basicConfig(level=logging.INFO)

        async with aiohttp.ClientSession(timeout=timeout) as session:
            pipeline_status = await get_pipeline_status(
                session, project_id, pipeline_id
            )
            logging.debug(f"Pipeline status: {pipeline_status}")
            if pipeline_status != "failed":
                return message

            jobs = await get_jobs_for_pipeline(session, project_id, pipeline_id)
            problem_jobs = get_problem_jobs(jobs)

            if len(problem_jobs) == 0:
                return message

            message = await process_problem_jobs(session, project_id, problem_jobs)
    except Exception as e:
        logging.error(f"An error occurred: {e}")
        return ""

    return message


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Fetch GitLab pipeline details")
    parser.add_argument(
        "--project-id", default="176", help="Project ID (default: 176 i.e. mesa/mesa)"
    )
    parser.add_argument("--pipeline-id", required=True, help="Pipeline ID")

    args = parser.parse_args()

    message = asyncio.run(main(args.pipeline_id, args.project_id))

    print(message)
