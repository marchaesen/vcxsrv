import re
from dataclasses import dataclass
from datetime import datetime, timedelta
from enum import Enum, auto
from os import getenv
from typing import Optional, Pattern, Union

from lava.utils.gitlab_section import GitlabSection


class LogSectionType(Enum):
    UNKNOWN = auto()
    LAVA_SUBMIT = auto()
    LAVA_QUEUE = auto()
    LAVA_BOOT = auto()
    TEST_DUT_SUITE = auto()
    TEST_SUITE = auto()
    TEST_CASE = auto()
    LAVA_POST_PROCESSING = auto()

# How long to wait whilst we try to submit a job; make it fairly short,
# since the job will be retried.
LAVA_SUBMIT_TIMEOUT = int(getenv("LAVA_SUBMIT_TIMEOUT", 5))

# How long should we wait for a device to become available?
# For post-merge jobs, this should be ~infinite, but we can fail more
# aggressively for pre-merge.
LAVA_QUEUE_TIMEOUT = int(getenv("LAVA_QUEUE_TIMEOUT", 60))

# Empirically, successful device boot in LAVA time takes less than 3
# minutes.
# LAVA itself is configured to attempt thrice to boot the device,
# summing up to 9 minutes.
# It is better to retry the boot than cancel the job and re-submit to avoid
# the enqueue delay.
LAVA_BOOT_TIMEOUT = int(getenv("LAVA_BOOT_TIMEOUT", 9))

# Estimated overhead in minutes for a job from GitLab to reach the test phase,
# including LAVA scheduling and boot duration
LAVA_TEST_OVERHEAD_MIN = 5

# Test DUT suite phase is where the initialization happens in DUT, not on docker.
# The device will be listening to SSH session until the end of the job.
LAVA_TEST_DUT_SUITE_TIMEOUT = int(getenv("CI_JOB_TIMEOUT")) // 60 - LAVA_TEST_OVERHEAD_MIN

# Test suite phase is where the initialization happens on docker.
LAVA_TEST_SUITE_TIMEOUT = int(getenv("LAVA_TEST_SUITE_TIMEOUT", 5))

# Test cases may take a long time, this script has no right to interrupt
# them. But if the test case takes almost 1h, it will never succeed due to
# Gitlab job timeout.
LAVA_TEST_CASE_TIMEOUT = int(getenv("CI_JOB_TIMEOUT")) // 60 - LAVA_TEST_OVERHEAD_MIN

# LAVA post processing may refer to a test suite teardown, or the
# adjustments to start the next test_case
LAVA_POST_PROCESSING_TIMEOUT = int(getenv("LAVA_POST_PROCESSING_TIMEOUT", 5))

FALLBACK_GITLAB_SECTION_TIMEOUT = timedelta(minutes=10)
DEFAULT_GITLAB_SECTION_TIMEOUTS = {
    LogSectionType.LAVA_SUBMIT: timedelta(minutes=LAVA_SUBMIT_TIMEOUT),
    LogSectionType.LAVA_QUEUE: timedelta(minutes=LAVA_QUEUE_TIMEOUT),
    LogSectionType.LAVA_BOOT: timedelta(minutes=LAVA_BOOT_TIMEOUT),
    LogSectionType.TEST_DUT_SUITE: timedelta(minutes=LAVA_TEST_DUT_SUITE_TIMEOUT),
    LogSectionType.TEST_SUITE: timedelta(minutes=LAVA_TEST_SUITE_TIMEOUT),
    LogSectionType.TEST_CASE: timedelta(minutes=LAVA_TEST_CASE_TIMEOUT),
    LogSectionType.LAVA_POST_PROCESSING: timedelta(
        minutes=LAVA_POST_PROCESSING_TIMEOUT
    ),
}


@dataclass(frozen=True)
class LogSection:
    regex: Union[Pattern, str]
    levels: tuple[str]
    section_id: str
    section_header: str
    section_type: LogSectionType
    collapsed: bool = False

    def from_log_line_to_section(
        self, lava_log_line: dict[str, str], main_test_case: Optional[str],
        timestamp_relative_to: Optional[datetime]
    ) -> Optional[GitlabSection]:
        if lava_log_line["lvl"] not in self.levels:
            return

        if match := re.search(self.regex, lava_log_line["msg"]):
            section_id = self.section_id.format(*match.groups())
            section_header = self.section_header.format(*match.groups())
            is_main_test_case = section_id == main_test_case
            timeout = DEFAULT_GITLAB_SECTION_TIMEOUTS[self.section_type]
            return GitlabSection(
                id=section_id,
                header=f"{section_header} - Timeout: {timeout}",
                type=self.section_type,
                start_collapsed=self.collapsed,
                suppress_start=is_main_test_case,
                suppress_end=is_main_test_case,
                timestamp_relative_to=timestamp_relative_to,
            )


LOG_SECTIONS = (
    LogSection(
        regex=re.compile(r"<?STARTTC>? ([^>]*)"),
        levels=("target", "debug"),
        section_id="{}",
        section_header="test_case {}",
        section_type=LogSectionType.TEST_CASE,
    ),
    LogSection(
        regex=re.compile(r"<?STARTRUN>? ([^>]*ssh.*server.*)"),
        levels=("debug"),
        section_id="{}",
        section_header="[dut] test_suite {}",
        section_type=LogSectionType.TEST_DUT_SUITE,
    ),
    LogSection(
        regex=re.compile(r"<?STARTRUN>? ([^>]*)"),
        levels=("debug"),
        section_id="{}",
        section_header="[docker] test_suite {}",
        section_type=LogSectionType.TEST_SUITE,
    ),
    LogSection(
        regex=re.compile(r"ENDTC>? ([^>]+)"),
        levels=("target", "debug"),
        section_id="post-{}",
        section_header="Post test_case {}",
        collapsed=True,
        section_type=LogSectionType.LAVA_POST_PROCESSING,
    ),
)
