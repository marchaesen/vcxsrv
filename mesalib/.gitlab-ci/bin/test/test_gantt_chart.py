from contextlib import suppress
from datetime import datetime, timedelta
from unittest import mock
from unittest.mock import MagicMock, patch

import ci_post_gantt
import pytest
from ci_gantt_chart import generate_gantt_chart
from ci_post_gantt import Gitlab, MockGanttExit


def create_mock_job(
    name, id, status, created_at, queued_duration, started_at, finished_at=None
):
    mock_job = MagicMock()
    mock_job.name = name
    mock_job.status = status
    mock_job.id = id
    mock_job.created_at = created_at
    mock_job.queued_duration = queued_duration
    mock_job.started_at = started_at
    mock_job.finished_at = finished_at
    return mock_job


@pytest.fixture
def fake_pipeline():
    current_time = datetime.fromisoformat("2024-12-17 23:54:13.940091+00:00")
    created_at = current_time - timedelta(minutes=10)

    job1 = create_mock_job(
        name="job1",
        id="1",
        status="success",
        created_at=created_at.isoformat(),
        queued_duration=1,  # seconds
        started_at=(created_at + timedelta(seconds=2)).isoformat(),
        finished_at=(created_at + timedelta(minutes=1)).isoformat(),
    )

    mock_pipeline = MagicMock()
    mock_pipeline.web_url = "https://gitlab.freedesktop.org/mesa/mesa/-/pipelines/9999"
    mock_pipeline.duration = 600  # Total pipeline duration in seconds
    mock_pipeline.created_at = created_at.isoformat()
    mock_pipeline.yaml_errors = False
    mock_pipeline.jobs.list.return_value = [job1]
    return mock_pipeline


def test_generate_gantt_chart(fake_pipeline):
    fig = generate_gantt_chart(fake_pipeline)

    fig_dict = fig.to_dict()
    assert "data" in fig_dict

    # Extract all job names from the "y" axis in the Gantt chart data
    all_job_names = set()
    for trace in fig_dict["data"]:
        if "y" in trace:
            all_job_names.update(trace["y"])

    assert any(
        "job1" in job for job in all_job_names
    ), "job1 should be present in the Gantt chart"


def test_ci_timeout(fake_pipeline):
    fig = generate_gantt_chart(fake_pipeline, ci_timeout=1)

    fig_dict = fig.to_dict()

    timeout_line = None
    for shape in fig_dict.get("layout", {}).get("shapes", []):
        if shape.get("line", {}).get("dash") == "dash":
            timeout_line = shape
            break

    assert timeout_line is not None, "Timeout line should exist in the Gantt chart"
    timeout_x = timeout_line["x0"]

    # Check that the timeout line is 1 minute after the pipeline creation time
    pipeline_created_at = datetime.fromisoformat(fake_pipeline.created_at)
    expected_timeout = pipeline_created_at + timedelta(minutes=1)
    assert (
        timeout_x == expected_timeout
    ), f"Timeout should be at {expected_timeout}, got {timeout_x}"


def test_marge_bot_user_id():
    with patch("ci_post_gantt.Gitlab") as MockGitlab:
        mock_gitlab_instance = MagicMock(spec=Gitlab)
        mock_gitlab_instance.users = MagicMock()
        MockGitlab.return_value = mock_gitlab_instance

        marge_bot_user_id = 12345
        ci_post_gantt.main("fake_token", None, marge_bot_user_id)
        mock_gitlab_instance.users.get.assert_called_once_with(marge_bot_user_id)


def test_project_ids():
    current_time = datetime.now()
    project_id_1 = 176
    event_1 = MagicMock()
    event_1.project_id = project_id_1
    event_1.created_at = (current_time - timedelta(days=1)).isoformat()
    event_1.note = {"body": f"Event for project {project_id_1}"}

    project_id_2 = 166
    event_2 = MagicMock()
    event_2.project_id = project_id_2
    event_2.created_at = (current_time - timedelta(days=2)).isoformat()
    event_2.note = {"body": f"Event for project {project_id_2}"}

    with patch("ci_post_gantt.Gitlab") as MockGitlab:
        mock_user = MagicMock()
        mock_user.events = MagicMock()
        mock_user.events.list.return_value = [event_1, event_2]

        mock_gitlab_instance = MagicMock(spec=Gitlab)
        mock_gitlab_instance.users = MagicMock()
        mock_gitlab_instance.users.get.return_value = mock_user
        MockGitlab.return_value = mock_gitlab_instance

        last_event_date = (current_time - timedelta(days=3)).isoformat()

        # Test a single project id
        ci_post_gantt.main("fake_token", last_event_date)
        marge_bot_single_project_scope = [
            event.note["body"]
            for event in mock_user.events.list.return_value
            if event.project_id == project_id_1
        ]
        assert f"Event for project {project_id_1}" in marge_bot_single_project_scope
        assert f"Event for project {project_id_2}" not in marge_bot_single_project_scope

        # Test multiple project ids
        ci_post_gantt.main(
            "fake_token", last_event_date, 9716, [project_id_1, project_id_2]
        )

        marge_bot_multiple_project_scope = [
            event.note["body"] for event in mock_user.events.list.return_value
        ]
        assert f"Event for project {project_id_1}" in marge_bot_multiple_project_scope
        assert f"Event for project {project_id_2}" in marge_bot_multiple_project_scope


def test_add_gantt_after_pipeline_message():
    current_time = datetime.now()

    plain_url = "https://gitlab.freedesktop.org/mesa/mesa/-/pipelines/12345"
    plain_message = (
        f"I couldn't merge this branch: CI failed! See pipeline {plain_url}."
    )
    event_plain = MagicMock()
    event_plain.project_id = 176
    event_plain.created_at = (current_time - timedelta(days=1)).isoformat()
    event_plain.note = {"body": plain_message}

    summary_url = "https://gitlab.freedesktop.org/mesa/mesa/-/pipelines/99999"
    summary_message = (
        "I couldn't merge this branch: "
        f"CI failed! See pipeline {summary_url}.<br>There were problems with job:"
        "[lavapipe](https://gitlab.freedesktop.org/mesa/mesa/-/jobs/68141218)<details><summary> "
        "3 crashed tests</summary>dEQP-VK.ray_query.builtin.instancecustomindex.frag.aabbs,Crash<br>dEQP"
        "-VK.ray_query.builtin.objecttoworld.frag.aabbs,Crash<br>dEQP-VK.sparse_resources.shader_intrinsics."
        "2d_array_sparse_fetch.g16_b16r16_2plane_444_unorm.11_37_3_nontemporal,Crash<br></details>"
    )
    event_with_summary = MagicMock()
    event_with_summary.project_id = 176
    event_with_summary.created_at = (current_time - timedelta(days=1)).isoformat()
    event_with_summary.note = {"body": summary_message}

    with patch("ci_post_gantt.Gitlab") as MockGitlab, patch(
        "ci_post_gantt.get_gitlab_pipeline_from_url", return_value=None
    ) as mock_get_gitlab_pipeline_from_url:

        def safe_mock(*args, **kwargs):
            with suppress(TypeError):
                raise MockGanttExit("Exiting for test purposes")

        mock_get_gitlab_pipeline_from_url.side_effect = safe_mock

        mock_user = MagicMock()
        mock_user.events = MagicMock()
        mock_user.events.list.return_value = [event_plain, event_with_summary]

        mock_gitlab_instance = MagicMock(spec=Gitlab)
        mock_gitlab_instance.users = MagicMock()
        mock_gitlab_instance.users.get.return_value = mock_user
        MockGitlab.return_value = mock_gitlab_instance

        last_event_date = (current_time - timedelta(days=3)).isoformat()
        ci_post_gantt.main("fake_token", last_event_date, 12345)
        mock_get_gitlab_pipeline_from_url.assert_has_calls(
            [
                mock.call(mock_gitlab_instance, plain_url),
                mock.call(mock_gitlab_instance, summary_url),
            ],
            any_order=True,
        )
