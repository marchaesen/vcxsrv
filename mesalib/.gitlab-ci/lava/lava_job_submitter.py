#!/usr/bin/env python3
#
# Copyright (C) 2020 - 2023 Collabora Limited
# Authors:
#     Gustavo Padovan <gustavo.padovan@collabora.com>
#     Guilherme Gallo <guilherme.gallo@collabora.com>
#
# SPDX-License-Identifier: MIT

"""Send a job to LAVA, track it and collect log back"""

import contextlib
import json
import pathlib
import sys
import time
from collections import defaultdict
from dataclasses import dataclass, field, fields
from datetime import datetime, timedelta, UTC
from os import environ, getenv
from typing import Any, Optional, Self

import fire
from lavacli.utils import flow_yaml as lava_yaml

from lava.exceptions import (
    MesaCIException,
    MesaCIFatalException,
    MesaCIRetriableException,
    MesaCIParseException,
    MesaCIRetryError,
    MesaCITimeoutError,
)
from lava.utils import (
    CONSOLE_LOG,
    GitlabSection,
    LAVAJob,
    LAVAJobDefinition,
    LogFollower,
    LogSectionType,
    call_proxy,
    fatal_err,
    hide_sensitive_data,
    print_log,
    setup_lava_proxy,
)
from lava.utils import DEFAULT_GITLAB_SECTION_TIMEOUTS as GL_SECTION_TIMEOUTS

# Initialize structural logging with a defaultdict, it can be changed for more
# sophisticated dict-like data abstractions.
STRUCTURAL_LOG = defaultdict(list)

try:
    from structured_logger import StructuredLogger
except ImportError as e:
    print_log(
        f"Could not import StructuredLogger library: {e}. "
        "Falling back to defaultdict based structured logger."
    )

# Timeout in seconds to decide if the device from the dispatched LAVA job has
# hung or not due to the lack of new log output.
DEVICE_HANGING_TIMEOUT_SEC = int(getenv("DEVICE_HANGING_TIMEOUT_SEC", 5 * 60))

# How many seconds the script should wait before try a new polling iteration to
# check if the dispatched LAVA job is running or waiting in the job queue.
WAIT_FOR_DEVICE_POLLING_TIME_SEC = int(
    getenv("LAVA_WAIT_FOR_DEVICE_POLLING_TIME_SEC", 1)
)

# How many seconds the script will wait to let LAVA finalize the job and give
# the final details.
WAIT_FOR_LAVA_POST_PROCESSING_SEC = int(getenv("LAVA_WAIT_LAVA_POST_PROCESSING_SEC", 5))
WAIT_FOR_LAVA_POST_PROCESSING_RETRIES = int(
    getenv("LAVA_WAIT_LAVA_POST_PROCESSING_RETRIES", 6)
)

# How many seconds to wait between log output LAVA RPC calls.
LOG_POLLING_TIME_SEC = int(getenv("LAVA_LOG_POLLING_TIME_SEC", 5))

# How many retries should be made when a timeout happen.
NUMBER_OF_RETRIES_TIMEOUT_DETECTION = int(
    getenv("LAVA_NUMBER_OF_RETRIES_TIMEOUT_DETECTION", 2)
)

CI_JOB_TIMEOUT_SEC = int(getenv("CI_JOB_TIMEOUT", 3600))
# How many seconds the script will wait to let LAVA run the job and give the final details.
EXPECTED_JOB_DURATION_SEC = int(getenv("EXPECTED_JOB_DURATION_SEC", 60 * 10))
# CI_JOB_STARTED is given by GitLab CI/CD in UTC timezone by default.
CI_JOB_STARTED_AT_RAW = getenv("CI_JOB_STARTED_AT", "")
CI_JOB_STARTED_AT: datetime = (
    datetime.fromisoformat(CI_JOB_STARTED_AT_RAW)
    if CI_JOB_STARTED_AT_RAW
    else datetime.now(tz=UTC)
)


def raise_exception_from_metadata(metadata: dict, job_id: int) -> None:
    """
    Investigate infrastructure errors from the job metadata.
    If it finds an error, raise it as MesaCIRetriableException.
    """
    if "result" not in metadata or metadata["result"] != "fail":
        return
    if "error_type" in metadata:
        error_type: str = metadata["error_type"]
        error_msg: str = metadata.get("error_msg", "")
        full_err_msg: str = error_type if not error_msg else f"{error_type}: {error_msg}"
        if error_type == "Job":
            # This happens when LAVA assumes that the job cannot terminate or
            # with mal-formed job definitions. As we are always validating the
            # jobs, only the former is probable to happen. E.g.: When some LAVA
            # action timed out more times than expected in job definition.
            raise MesaCIRetriableException(
                f"LAVA job {job_id} failed with {full_err_msg}. Retry."
                "(possible LAVA timeout misconfiguration/bug). Retry."
            )
        if error_type:
            raise MesaCIRetriableException(
                f"LAVA job {job_id} failed with error type: {full_err_msg}. Retry."
            )
    if "case" in metadata and metadata["case"] == "validate":
        raise MesaCIRetriableException(
            f"LAVA job {job_id} failed validation (possible download error). Retry."
        )


def raise_lava_error(job) -> None:
    # Look for infrastructure errors, raise them, and retry if we see them.
    results_yaml = call_proxy(job.proxy.results.get_testjob_results_yaml, job.job_id)
    results = lava_yaml.load(results_yaml)
    for res in results:
        metadata = res["metadata"]
        raise_exception_from_metadata(metadata, job.job_id)

    # If we reach this far, it means that the job ended without hwci script
    # result and no LAVA infrastructure problem was found
    job.status = "fail"



def fetch_logs(job, max_idle_time, log_follower) -> None:
    is_job_hanging(job, max_idle_time)

    time.sleep(LOG_POLLING_TIME_SEC)
    new_log_lines = fetch_new_log_lines(job)
    parsed_lines = parse_log_lines(job, log_follower, new_log_lines)

    for line in parsed_lines:
        print_log(line)


def is_job_hanging(job, max_idle_time):
    # Poll to check for new logs, assuming that a prolonged period of
    # silence means that the device has died and we should try it again
    if datetime.now(tz=UTC) - job.last_log_time > max_idle_time:
        max_idle_time_min = max_idle_time.total_seconds() / 60

        raise MesaCITimeoutError(
            f"{CONSOLE_LOG['FG_BOLD_YELLOW']}"
            f"LAVA job {job.job_id} unresponsive for {max_idle_time_min} "
            "minutes; retrying the job."
            f"{CONSOLE_LOG['RESET']}",
            timeout_duration=max_idle_time,
        )


def parse_log_lines(job, log_follower, new_log_lines):
    if log_follower.feed(new_log_lines):
        # If we had non-empty log data, we can assure that the device is alive.
        job.heartbeat()
    parsed_lines = log_follower.flush()

    # Only parse job results when the script reaches the end of the logs.
    # Depending on how much payload the RPC scheduler.jobs.logs get, it may
    # reach the LAVA_POST_PROCESSING phase.
    if log_follower.current_section.type in (
        LogSectionType.TEST_CASE,
        LogSectionType.LAVA_POST_PROCESSING,
    ):
        parsed_lines = job.parse_job_result_from_log(parsed_lines)
    return parsed_lines


def fetch_new_log_lines(job):
    # The XMLRPC binary packet may be corrupted, causing a YAML scanner error.
    # Retry the log fetching several times before exposing the error.
    for _ in range(5):
        with contextlib.suppress(MesaCIParseException):
            new_log_lines = job.get_logs()
            break
    else:
        raise MesaCIParseException
    return new_log_lines


def submit_job(job):
    try:
        job.submit()
    except Exception as mesa_ci_err:
        raise MesaCIRetriableException(
            f"Could not submit LAVA job. Reason: {mesa_ci_err}"
        ) from mesa_ci_err


def wait_for_job_get_started(job, attempt_no):
    print_log(f"Waiting for job {job.job_id} to start.")
    while not job.is_started():
        current_job_duration_sec: int = int(
            (datetime.now(tz=UTC) - CI_JOB_STARTED_AT).total_seconds()
        )
        remaining_time_sec: int = max(0, CI_JOB_TIMEOUT_SEC - current_job_duration_sec)
        if remaining_time_sec < EXPECTED_JOB_DURATION_SEC:
            job.cancel()
            raise MesaCIFatalException(
                f"{CONSOLE_LOG['FG_BOLD_YELLOW']}"
                f"Job {job.job_id} only has {remaining_time_sec} seconds "
                "remaining to run, but it is expected to take at least "
                f"{EXPECTED_JOB_DURATION_SEC} seconds."
                f"{CONSOLE_LOG['RESET']}",
            )
        time.sleep(WAIT_FOR_DEVICE_POLLING_TIME_SEC)
    job.refresh_log()
    print_log(f"Job {job.job_id} started.")


def bootstrap_log_follower(main_test_case, timestamp_relative_to) -> LogFollower:
    start_section = GitlabSection(
        id="dut_boot",
        header="Booting hardware device",
        type=LogSectionType.LAVA_BOOT,
        start_collapsed=True,
        suppress_end=True, # init-stage2 prints the end for us
        timestamp_relative_to=timestamp_relative_to,
    )
    print(start_section.start())
    return LogFollower(
        starting_section=start_section,
        main_test_case=main_test_case,
        timestamp_relative_to=timestamp_relative_to
    )


def follow_job_execution(job, log_follower):
    with log_follower:
        max_idle_time = timedelta(seconds=DEVICE_HANGING_TIMEOUT_SEC)
        # Start to check job's health
        job.heartbeat()
        while not job.is_finished:
            fetch_logs(job, max_idle_time, log_follower)
            structural_log_phases(job, log_follower)

    # Mesa Developers expect to have a simple pass/fail job result.
    # If this does not happen, it probably means a LAVA infrastructure error
    # happened.
    if job.status not in ["pass", "fail"]:
        raise_lava_error(job)

    # LogFollower does some cleanup after the early exit (trigger by
    # `hwci: pass|fail` regex), let's update the phases after the cleanup.
    structural_log_phases(job, log_follower)


def structural_log_phases(job, log_follower):
    phases: dict[str, Any] = {
        s.header.split(" - ")[0]: {
            k: str(getattr(s, k)) for k in ("start_time", "end_time")
        }
        for s in log_follower.section_history
    }
    job.log["dut_job_phases"] = phases


def print_job_final_status(job, timestamp_relative_to):
    job.refresh_log()
    if job.status == "running":
        job.status = "hung"

    colour = LAVAJob.COLOR_STATUS_MAP.get(job.status, CONSOLE_LOG["FG_RED"])
    with GitlabSection(
        "job_data",
        f"Hardware job info for {job.status} job",
        type=LogSectionType.LAVA_POST_PROCESSING,
        start_collapsed=True,
        colour=colour,
        timestamp_relative_to=timestamp_relative_to,
    ):
        wait_post_processing_retries: int = WAIT_FOR_LAVA_POST_PROCESSING_RETRIES
        while not job.is_post_processed() and wait_post_processing_retries > 0:
            # Wait a little until LAVA finishes processing metadata
            time.sleep(WAIT_FOR_LAVA_POST_PROCESSING_SEC)
            wait_post_processing_retries -= 1

        if not job.is_post_processed():
            waited_for_sec: int = (
                WAIT_FOR_LAVA_POST_PROCESSING_RETRIES
                * WAIT_FOR_LAVA_POST_PROCESSING_SEC
            )
            print_log(
                "Timed out waiting for LAVA post-processing after "
                f"{waited_for_sec} seconds. Printing incomplete information "
                "anyway."
            )

        details: dict[str, str] = job.show()
        for field, value in details.items():
            print(f"{field:<15}: {value}")
        job.refresh_log()


def execute_job_with_retries(
    proxy, job_definition, retry_count, jobs_log, main_test_case,
    timestamp_relative_to
) -> Optional[LAVAJob]:
    last_failed_job = None
    for attempt_no in range(1, retry_count + 2):
        # Need to get the logger value from its object to enable autosave
        # features, if AutoSaveDict is enabled from StructuredLogging module
        jobs_log.append({})
        job_log = jobs_log[-1]
        job = LAVAJob(proxy, job_definition, job_log)
        STRUCTURAL_LOG["dut_attempt_counter"] = attempt_no
        try:
            job_log["submitter_start_time"] = datetime.now(tz=UTC).isoformat()
            submit_job(job)
            queue_section = GitlabSection(
                id="dut_queue",
                header="Waiting for hardware device to become available",
                type=LogSectionType.LAVA_QUEUE,
                start_collapsed=False,
                timestamp_relative_to=timestamp_relative_to
            )
            with queue_section as section:
                wait_for_job_get_started(job, attempt_no)
            log_follower: LogFollower = bootstrap_log_follower(
                main_test_case, timestamp_relative_to
            )
            follow_job_execution(job, log_follower)
            return job

        except (MesaCIException, KeyboardInterrupt) as exception:
            job.handle_exception(exception)

        finally:
            print_job_final_status(job, timestamp_relative_to)
            # If LAVA takes too long to post process the job, the submitter
            # gives up and proceeds.
            job_log["submitter_end_time"] = datetime.now(tz=UTC).isoformat()
            last_failed_job = job
            print_log(
                f"{CONSOLE_LOG['BOLD']}"
                f"Finished executing LAVA job in the attempt #{attempt_no}"
                f"{CONSOLE_LOG['RESET']}"
            )
            if job.exception and not isinstance(job.exception, MesaCIRetriableException):
                break

    return last_failed_job


def retriable_follow_job(
    proxy, job_definition, main_test_case, timestamp_relative_to
) -> LAVAJob:
    number_of_retries = NUMBER_OF_RETRIES_TIMEOUT_DETECTION

    last_attempted_job = execute_job_with_retries(
        proxy, job_definition, number_of_retries, STRUCTURAL_LOG["dut_jobs"],
        main_test_case, timestamp_relative_to
    )

    if last_attempted_job.exception is not None:
        # Infra failed in all attempts
        raise MesaCIRetryError(
            f"{CONSOLE_LOG['BOLD']}"
            f"{CONSOLE_LOG['FG_RED']}"
            "Job failed after it exceeded the number of "
            f"{number_of_retries} retries."
            f"{CONSOLE_LOG['RESET']}",
            retry_count=number_of_retries,
            last_job=last_attempted_job,
        )

    return last_attempted_job


@dataclass
class PathResolver:
    def __post_init__(self):
        for field in fields(self):
            value = getattr(self, field.name)
            if not value:
                continue
            if field.type == pathlib.Path:
                value = pathlib.Path(value)
                setattr(self, field.name, value.resolve())


@dataclass
class LAVAJobSubmitter(PathResolver):
    boot_method: str
    device_type: str
    farm: str
    job_timeout_min: int  # The job timeout in minutes
    dtb_filename: str = None
    dump_yaml: bool = False  # Whether to dump the YAML payload to stdout
    first_stage_init: str = None
    jwt_file: pathlib.Path = None
    kernel_image_name: str = None
    kernel_image_type: str = ""
    kernel_url_prefix: str = None
    kernel_external: str = None
    lava_tags: str | tuple[str, ...] = ()  # Comma-separated LAVA tags for the job
    mesa_job_name: str = "mesa_ci_job"
    pipeline_info: str = ""
    rootfs_url: str = None
    validate_only: bool = False  # Whether to only validate the job, not execute it
    visibility_group: str = None  # Only affects LAVA farm maintainers
    structured_log_file: pathlib.Path = None  # Log file path with structured LAVA log
    ssh_client_image: str = None  # x86_64 SSH client image to follow the job's output
    project_name: str = None  # Project name to be used in the job name
    starting_section: str = None # GitLab section used to start
    job_submitted_at: [str | datetime] = None
    __structured_log_context = contextlib.nullcontext()  # Structured Logger context
    _overlays: dict = field(default_factory=dict, init=False)

    def __post_init__(self) -> Self:
        super().__post_init__()
        # Remove mesa job names with spaces, which breaks the lava-test-case command
        self.mesa_job_name = self.mesa_job_name.split(" ")[0]

        if self.structured_log_file:
            self.__structured_log_context = StructuredLoggerWrapper(self).logger_context()

        if self.job_submitted_at:
            self.job_submitted_at = datetime.fromisoformat(self.job_submitted_at)
        self.proxy = setup_lava_proxy()

        return self

    def append_overlay(
        self, compression: str, name: str, path: str, url: str, format: str = "tar"
    ) -> Self:
        """
        Append an overlay to the LAVA job definition.

        Args:
            compression (str): The compression type of the overlay (e.g., "gz", "xz").
            name (str): The name of the overlay.
            path (str): The path where the overlay should be applied.
            url (str): The URL from where the overlay can be downloaded.
            format (str, optional): The format of the overlay (default is "tar").

        Returns:
            Self: The instance of LAVAJobSubmitter with the overlay appended.
        """
        self._overlays[name] = {
            "compression": compression,
            "format": format,
            "path": path,
            "url": url,
        }
        return self

    def print(self) -> Self:
        """
        Prints the dictionary representation of the instance and returns the instance itself.

        Returns:
            Self: The instance of the class.
        """
        print(self.__dict__)
        return self

    def __prepare_submission(self) -> str:
        # Overwrite the timeout for the testcases with the value offered by the
        # user. The testcase running time should be at least 4 times greater than
        # the other sections (boot and setup), so we can safely ignore them.
        # If LAVA fails to stop the job at this stage, it will fall back to the
        # script section timeout with a reasonable delay.
        GL_SECTION_TIMEOUTS[LogSectionType.TEST_CASE] = timedelta(
            minutes=self.job_timeout_min
        )

        job_definition = LAVAJobDefinition(self).generate_lava_job_definition()

        if self.dump_yaml:
            self.dump_job_definition(job_definition)

        validation_job = LAVAJob(self.proxy, job_definition)
        if errors := validation_job.validate():
            fatal_err(f"Error in LAVA job definition: {errors}")

        return job_definition

    @classmethod
    def is_under_ci(cls):
        ci_envvar: str = getenv("CI", "false")
        return ci_envvar.lower() == "true"

    def dump_job_definition(self, job_definition) -> None:
        with GitlabSection(
            "yaml_dump",
            "LAVA job definition (YAML)",
            type=LogSectionType.LAVA_BOOT,
            start_collapsed=True,
        ):
            print(hide_sensitive_data(job_definition))

    def submit(self) -> None:
        """
        Prepares and submits the LAVA job.
        If `validate_only` is True, it validates the job without submitting it.
        If the job finishes with a non-pass status or encounters an exception,
        the program exits with a non-zero return code.
        """
        job_definition: str = self.__prepare_submission()

        if self.validate_only:
            return

        if self.starting_section:
            gl = GitlabSection(
                id=self.starting_section,
                header="Preparing to submit job for scheduling",
                type=LogSectionType.LAVA_SUBMIT,
                start_collapsed=True,
                timestamp_relative_to=self.job_submitted_at,
            )
            gl.start()
            print(gl.end())

        with self.__structured_log_context:
            last_attempt_job = None
            try:
                last_attempt_job = retriable_follow_job(
                    self.proxy, job_definition,
                    f'{self.project_name}_{self.mesa_job_name}',
                    self.job_submitted_at)

            except MesaCIRetryError as retry_exception:
                last_attempt_job = retry_exception.last_job

            except Exception as exception:
                STRUCTURAL_LOG["job_combined_fail_reason"] = str(exception)
                raise exception

            finally:
                self.finish_script(last_attempt_job)

    def finish_script(self, last_attempt_job):
        if not last_attempt_job:
            # No job was run, something bad happened
            STRUCTURAL_LOG["job_combined_status"] = "script_crash"
            current_exception = str(sys.exc_info()[1])
            STRUCTURAL_LOG["job_combined_fail_reason"] = current_exception
            print(f"Interrupting the script. Reason: {current_exception}")
            raise SystemExit(1)

        STRUCTURAL_LOG["job_combined_status"] = last_attempt_job.status
        STRUCTURAL_LOG["job_exit_code"] = last_attempt_job.exit_code

        if last_attempt_job.status != "pass":
            raise SystemExit(last_attempt_job.exit_code)


class StructuredLoggerWrapper:
    def __init__(self, submitter: LAVAJobSubmitter) -> None:
        self.__submitter: LAVAJobSubmitter = submitter

    def _init_logger(self):
        STRUCTURAL_LOG["fixed_tags"] = self.__submitter.lava_tags
        STRUCTURAL_LOG["dut_job_type"] = self.__submitter.device_type
        STRUCTURAL_LOG["farm"] = self.__submitter.farm
        STRUCTURAL_LOG["job_combined_fail_reason"] = None
        STRUCTURAL_LOG["job_combined_status"] = "not_submitted"
        STRUCTURAL_LOG["job_exit_code"] = None
        STRUCTURAL_LOG["dut_attempt_counter"] = 0

        # Initialize dut_jobs list to enable appends
        STRUCTURAL_LOG["dut_jobs"] = []

    @contextlib.contextmanager
    def _simple_logger_context(self):
        log_file = pathlib.Path(self.__submitter.structured_log_file)
        log_file.parent.mkdir(parents=True, exist_ok=True)
        try:
            # Truncate the file
            log_file.write_text("")
            yield
        finally:
            log_file.write_text(json.dumps(STRUCTURAL_LOG, indent=2))

    def logger_context(self):
        context = contextlib.nullcontext()
        try:
            global STRUCTURAL_LOG
            STRUCTURAL_LOG = StructuredLogger(
                self.__submitter.structured_log_file, truncate=True
            ).data
        except NameError:
            context = self._simple_logger_context()

        self._init_logger()
        return context


if __name__ == "__main__":
    # given that we proxy from DUT -> LAVA dispatcher -> LAVA primary -> us ->
    # GitLab runner -> GitLab primary -> user, safe to say we don't need any
    # more buffering
    sys.stdout.reconfigure(line_buffering=True)
    sys.stderr.reconfigure(line_buffering=True)

    fire.Fire(LAVAJobSubmitter)
