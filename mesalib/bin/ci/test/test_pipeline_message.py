import logging
from unittest.mock import AsyncMock, patch

import pytest

from pipeline_message import (
    get_failed_test_summary_message,
    get_problem_jobs,
    get_trace_failures,
    main,
    process_problem_jobs,
    search_job_log_for_errors,
    sort_failed_tests_by_status,
    unexpected_improvements,
)


def test_get_problem_jobs():
    jobs = [
        {"stage": "build", "status": "failed"},
        {"stage": "test", "status": "canceled"},
        {"stage": "postmerge", "status": "failed"},
        {"stage": "performance", "status": "failed"},
        {"stage": "deploy", "status": "failed"},
    ]

    problem_jobs = get_problem_jobs(jobs)

    assert len(problem_jobs) == 3
    assert problem_jobs[0]["stage"] == "build"
    assert problem_jobs[1]["stage"] == "test"
    assert problem_jobs[2]["stage"] == "deploy"


def test_sort_failed_tests_by_status():
    failures_csv = """\
Test1,UnexpectedImprovement
Test2,Fail
Test3,Crash
Test4,Timeout
Test5,Fail
Test6,UnexpectedImprovement
"""
    sorted_tests = sort_failed_tests_by_status(failures_csv)

    assert len(sorted_tests["unexpected_improvements"]) == 2
    assert len(sorted_tests["fails"]) == 2
    assert len(sorted_tests["crashes"]) == 1
    assert len(sorted_tests["timeouts"]) == 1

    assert sorted_tests["unexpected_improvements"] == [
        "Test1,UnexpectedImprovement",
        "Test6,UnexpectedImprovement",
    ]
    assert sorted_tests["fails"] == ["Test2,Fail", "Test5,Fail"]
    assert sorted_tests["crashes"] == ["Test3,Crash"]
    assert sorted_tests["timeouts"] == ["Test4,Timeout"]


def test_get_failed_test_summary_message():
    failed_test_array = {
        "unexpected_improvements": [
            "test1 UnexpectedImprovement",
            "test2 UnexpectedImprovement",
        ],
        "fails": ["test3 Fail", "test4 Fail", "test5 Fail"],
        "crashes": ["test6 Crash"],
        "timeouts": [],
    }

    summary_message = get_failed_test_summary_message(failed_test_array)

    assert "<summary>" in summary_message
    assert "2 improved tests" in summary_message
    assert "3 failed tests" in summary_message
    assert "1 crashed test" in summary_message
    assert "</summary>" in summary_message


def test_unexpected_improvements():
    message = "<summary>"
    failed_test_array = {
        "unexpected_improvements": ["test_improvement_1", "test_improvement_2"],
        "fails": [],
        "crashes": [],
        "timeouts": [],
    }
    result = unexpected_improvements(failed_test_array)
    assert result == " 2 improved tests", f"Unexpected result: {result}"


@pytest.mark.asyncio
@patch("pipeline_message.get_pipeline_status", new_callable=AsyncMock)
async def test_gitlab_api_failure(mock_get_pipeline_status):
    mock_get_pipeline_status.side_effect = Exception("GitLab API not responding")
    message = await main("1234567")
    assert message == ""


@pytest.mark.asyncio
async def test_no_message_when_pipeline_not_failed():
    project_id = "176"
    pipeline_id = "12345"

    with patch(
        "pipeline_message.get_pipeline_status", new_callable=AsyncMock
    ) as mock_get_pipeline_status:
        mock_get_pipeline_status.return_value = "success"

        message = await main(pipeline_id, project_id)
        assert (
            message == ""
        ), f"Expected no message for successful pipeline, but got: {message}"


@pytest.mark.asyncio
async def test_single_problem_job_not_summarized():
    session = AsyncMock()
    project_id = "176"
    problem_jobs = [
        {
            "id": 1234,
            "name": "test-job",
            "web_url": "http://example.com/job/1234",
            "status": "canceled",
        }
    ]

    mock_response = AsyncMock()
    mock_response.status = 200
    mock_response.text.return_value = ""  # Empty CSV response for test
    session.get.return_value = mock_response

    message = await process_problem_jobs(session, project_id, problem_jobs)

    assert "summary" not in message
    assert "[test-job](http://example.com/job/1234)" in message


@pytest.mark.asyncio
@patch("pipeline_message.get_project_json", new_callable=AsyncMock)
@patch("pipeline_message.aiohttp.ClientSession", autospec=True)
async def test_get_trace_failures_no_response(
    mock_client_session_cls, mock_get_project_json, caplog
):
    caplog.set_level(logging.DEBUG)
    namespace = "mesa"
    mock_get_project_json.return_value = {"path": namespace}

    mock_get = AsyncMock()
    mock_get.status = 404

    mock_session_instance = mock_client_session_cls.return_value
    mock_session_instance.get.return_value = mock_get

    job_id = 12345678
    job = {"id": job_id}
    url = await get_trace_failures(mock_session_instance, "176", job)

    assert url == ""

    expected_log_message = f"No response from: https://mesa.pages.freedesktop.org/-/{namespace}/-/jobs/{job_id}/artifacts/results/summary/problems.html"
    assert any(expected_log_message in record.message for record in caplog.records)


@pytest.mark.asyncio
@patch("pipeline_message.get_job_log", new_callable=AsyncMock)
async def test_search_job_log_for_errors(mock_get_job_log):
    session = AsyncMock()
    project_id = "176"
    job = {"id": 12345}

    job_log = r"""
error_msg: something useful
[0m15:41:36.102:                GL_KHR_no_error GL_KHR_texture_compression_astc_sliced_3d
1 error generated
3 errors generated.
-- Looking for strerror_r - found
-- Looking for strerror_s - not found
[49/176] Building CXX object lib/Support/CMakeFiles/LLVMSupport.dir/ErrorHandling.cpp.o
[127/2034] Building C object lib/Support/CMakeFiles/LLVMSupport.dir/regerror.c.o
-- Performing Test HAS_WERROR_GLOBAL_CTORS
-- Performing Test C_SUPPORTS_WERROR_UNGUARDED_AVAILABILITY_NEW - Success
-- Performing Test LLVM_LIBSTDCXX_SOFT_ERROR
error aborting
error_msg      : None
error_type     : Job
[0Ksection_end:1734694783:job_data
[0K
[0m11:39:43.438: [1mFinished executing LAVA job in the attempt #3 [0m
[0Ksection_end:1734694783:lava_submit
[0K
[0;31m[01:54] ERROR: lava_submit: ret code: 1 [0m

[0;31m[01:54] ERROR: unknown-section: ret code: 1 [0m
section_end:1734694783:step_script
[0Ksection_start:1734694783:after_script
[0K[0K[36;1mRunning after_script[0;m[0;m
[32;1mRunning after script...[0;m
[32;1m$ curl -L --retry 4 -f --retry-all-errors --retry-delay 60 -s "https://" | tar --warning=no-timestamp --zstd -x[0;m
zstd: /*stdin*\: unexpected end of file # noqa: W605
tar: Child returned status 1
tar: Error is not recoverable: exiting now
section_end:1734695025:after_script
[0K[0;33mWARNING: after_script failed, but job will continue unaffected: exit code 1[0;m
section_start:1734695025:upload_artifacts_on_failure
[0K[0K[36;1mUploading artifacts for failed job[0;m[0;m
[32;1mUploading artifacts...[0;m
results/: found 11 matching artifact files and directories[0;m
Uploading artifacts as "archive" to coordinator... 201 Created[0;m  id[0;m=68509685 responseStatus[0;m=201 Created token[0;m=glcbt-64
[32;1mUploading artifacts...[0;m
[0;33mWARNING: results/junit.xml: no matching files. Ensure that the artifact path is relative to the working directory (/builds/mesa/mesa)[0;m
[31;1mERROR: No files to upload                         [0;m
section_end:1734695027:upload_artifacts_on_failure
[0Ksection_start:1734695027:cleanup_file_variables
[0K[0K[36;1mCleaning up project directory and file based variables[0;m[0;m
section_end:1734695027:cleanup_file_variables
[0K[31;1mERROR: Job failed: exit code 1
[0;m
[0;m
    """

    mock_get_job_log.return_value = job_log

    error_message = await search_job_log_for_errors(session, project_id, job)
    assert "something useful" in error_message


@pytest.mark.asyncio
@patch("pipeline_message.get_job_log", new_callable=AsyncMock)
async def test_search_job_log_for_fatal_errors(mock_get_job_log):
    session = AsyncMock()
    project_id = "176"
    job = {"id": 12345}

    job_log = r"""
[0m15:41:36.105: [15:41:31.951] fatal: something fatal
Uploading artifacts as "archive" to coordinator... 201 Created[0;m  id[0;m=68509685 responseStatus[0;m=201 Created token[0;m=glcbt-64
[32;1mUploading artifacts...[0;m
[0;33mWARNING: results/junit.xml: no matching files. Ensure that the artifact path is relative to the working directory (/builds/mesa/mesa)[0;m
[31;1mERROR: No files to upload                         [0;m
section_end:1734695027:upload_artifacts_on_failure
[0Ksection_start:1734695027:cleanup_file_variables
[0K[0K[36;1mCleaning up project directory and file based variables[0;m[0;m
section_end:1734695027:cleanup_file_variables
[0K[31;1mERROR: Job failed: exit code 1
[0;m
[0;m
    """

    mock_get_job_log.return_value = job_log

    error_message = await search_job_log_for_errors(session, project_id, job)
    assert "something fatal" in error_message


@pytest.mark.asyncio
@patch("pipeline_message.get_job_log", new_callable=AsyncMock)
async def test_search_job_log_for_errors_but_find_none(mock_get_job_log):
    session = AsyncMock()
    project_id = "176"
    job = {"id": 12345}

    job_log = r"""
[0KRunning with gitlab-runner 17.4.0 (b92ee590)[0;m
[0K  on fdo-equinix-m3l-30-placeholder_63 XmDXAt7xd, system ID: s_785ae19292ea[0;m
section_start:1734736110:prepare_executor
[0K[0K[36;1mPreparing the "docker" executor[0;m[0;m
[0KUsing Docker executor with image registry.freedesktop.org/mesa/mesa/debian
[0KAuthenticating with credentials from job payload (GitLab Registry)[0;m
[0KPulling docker image registry.freedesktop.org/mesa/mesa/debian/x86_64_pyuti
[0KUsing docker image sha256:ebc7b3fe89be4d390775303adddb33539c235a2663165d78d
[0Ksection_start:1734736124:prepare_script
[0K[0K[36;1mPreparing environment[0;m[0;m
Running on runner-xmdxat7xd-project-23076-concurrent-1 via fdo-equinix-m3l-30...
section_end:1734736125:prepare_script
[0Ksection_start:1734736125:get_sources
[0K[0K[36;1mGetting source from Git repository[0;m[0;m
[32;1m$ /host/bin/curl -s -L --cacert /host/ca-certificates.crt --retry 4 -f --retry-delay 60 https://gitlab.
Checking if the user of the pipeline is allowed...
Checking if the job's project is part of a well-known group...
Checking if the job is part of an official MR pipeline...
Thank you for contributing to freedesktop.org
Running pre-clone script: 'set -o xtrace
wget -q -O download-git-cache.sh https://gitlab.freedesktop.org/mesa/mesa/-/raw/0d43b4cba639b809ad0e08a065ce01846e262249/.gitlab-ci/download-git-cache.sh
bash download-git-cache.sh
rm download-git-cache.sh
[31;1m errors
[0K[31;1mERROR:
[31;1m error
[31;1m Here is a blank error:
/builds/mesa/mesa/bin/ci/test/test_pipeline_message.py:162: AssertionError
Uploading artifacts as "archive" to coordinator... 201 Created[0;m  id[0;m=68509685 responseStatus[0;m=201 Created token[0;m=glcbt-64
[32;1mUploading artifacts...[0;m
[0;33mWARNING: results/junit.xml: no matching files. Ensure that the artifact path is relative to the working directory (/builds/mesa/mesa)[0;m
[31;1mERROR: No files to upload                         [0;m
section_end:1734695027:upload_artifacts_on_failure
[0Ksection_start:1734695027:cleanup_file_variables
[0K[0K[36;1mCleaning up project directory and file based variables[0;m[0;m
section_end:1734695027:cleanup_file_variables
[0K[31;1mERROR: Job failed: exit code 1
[0;m
[0;m
    """

    mock_get_job_log.return_value = job_log

    error_message = await search_job_log_for_errors(session, project_id, job)
    assert error_message == "", f"Unexpected error message: {error_message}"
