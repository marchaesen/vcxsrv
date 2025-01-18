#!/usr/bin/env python3
# Copyright © 2023 Collabora Ltd.
# Authors:
#   Helen Koike <helen.koike@collabora.com>
#
# For the dependencies, see the requirements.txt
# SPDX-License-Identifier: MIT


import argparse
from datetime import datetime, timedelta, timezone
from typing import Dict, List

import plotly.express as px
import plotly.graph_objs as go
from gitlab import Gitlab, base
from gitlab.v4.objects import ProjectPipeline
from gitlab_common import (GITLAB_URL, get_gitlab_pipeline_from_url,
                           get_token_from_default_dir, pretty_duration,
                           read_token)


def calculate_queued_at(job) -> datetime:
    started_at = job.started_at.replace("Z", "+00:00")
    return datetime.fromisoformat(started_at) - timedelta(seconds=job.queued_duration)


def calculate_time_difference(time1, time2) -> str:
    if type(time1) is str:
        time1 = datetime.fromisoformat(time1.replace("Z", "+00:00"))
    if type(time2) is str:
        time2 = datetime.fromisoformat(time2.replace("Z", "+00:00"))

    diff = time2 - time1
    return pretty_duration(diff.seconds)


def create_task_name(job) -> str:
    status_color = {"success": "green", "failed": "red"}.get(job.status, "grey")
    return f"{job.name}\t(<span style='color: {status_color}'>{job.status}</span>,<a href='{job.web_url}'>{job.id}</a>)"


def add_gantt_bar(
    job: base.RESTObject, tasks: List[Dict[str, str | datetime | timedelta]]
) -> None:
    queued_at = calculate_queued_at(job)
    task_name = create_task_name(job)

    tasks.append(
        {
            "Job": task_name,
            "Start": job.created_at,
            "Finish": queued_at,
            "Duration": calculate_time_difference(job.created_at, queued_at),
            "Phase": "Waiting dependencies",
        }
    )
    tasks.append(
        {
            "Job": task_name,
            "Start": queued_at,
            "Finish": job.started_at,
            "Duration": calculate_time_difference(queued_at, job.started_at),
            "Phase": "Queued",
        }
    )

    if job.finished_at:
        tasks.append(
            {
                "Job": task_name,
                "Start": job.started_at,
                "Finish": job.finished_at,
                "Duration": calculate_time_difference(job.started_at, job.finished_at),
                "Phase": "Time spent running",
            }
        )
    else:
        current_time = datetime.now(timezone.utc).isoformat()
        tasks.append(
            {
                "Job": task_name,
                "Start": job.started_at,
                "Finish": current_time,
                "Duration": calculate_time_difference(job.started_at, current_time),
                "Phase": "In-Progress",
            }
        )


def generate_gantt_chart(
    pipeline: ProjectPipeline, ci_timeout: float = 60
) -> go.Figure:
    if pipeline.yaml_errors:
        raise ValueError("Pipeline YAML errors detected")

    # Convert the data into a list of dictionaries for plotly
    tasks: List[Dict[str, str | datetime | timedelta]] = []

    for job in pipeline.jobs.list(all=True, include_retried=True):
        # we can have queued_duration without started_at when a job is canceled
        if not job.queued_duration or not job.started_at:
            continue
        add_gantt_bar(job, tasks)

    # Make it easier to see retried jobs
    tasks.sort(key=lambda x: x["Job"])

    title = f"Gantt chart of jobs in pipeline <a href='{pipeline.web_url}'>{pipeline.web_url}</a>."
    title += (
        f" Total duration {str(timedelta(seconds=pipeline.duration))}"
        if pipeline.duration
        else ""
    )

    # Create a Gantt chart
    default_colors = px.colors.qualitative.Plotly
    fig: go.Figure = px.timeline(
        tasks,
        x_start="Start",
        x_end="Finish",
        y="Job",
        color="Phase",
        title=title,
        hover_data=["Duration"],
        color_discrete_map={
            "In-Progress": default_colors[3],  # purple
            "Waiting dependencies": default_colors[0],  # blue
            "Queued": default_colors[1],  # red
            "Time spent running": default_colors[2],  # green
        },
    )

    # Calculate the height dynamically
    fig.update_layout(height=len(tasks) * 10, yaxis_tickfont_size=14)

    # Add a deadline line to the chart
    created_at = datetime.fromisoformat(pipeline.created_at.replace("Z", "+00:00"))
    timeout_at = created_at + timedelta(minutes=ci_timeout)
    fig.add_vrect(
        x0=timeout_at,
        x1=timeout_at,
        annotation_text=f"{int(ci_timeout)} min Timeout",
        fillcolor="gray",
        line_width=2,
        line_color="gray",
        line_dash="dash",
        annotation_position="top left",
        annotation_textangle=90,
    )

    return fig


def main(
    token: str | None,
    pipeline_url: str,
    output: str | None,
    ci_timeout: float = 60,
):
    if token is None:
        token = get_token_from_default_dir()

    token = read_token(token)
    gl = Gitlab(url=GITLAB_URL, private_token=token, retry_transient_errors=True)

    pipeline, _ = get_gitlab_pipeline_from_url(gl, pipeline_url)
    fig: go.Figure = generate_gantt_chart(pipeline, ci_timeout)
    if output and "htm" in output:
        fig.write_html(output)
    elif output:
        fig.update_layout(width=1000)
        fig.write_image(output)
    else:
        fig.show()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate the Gantt chart from a given pipeline."
    )
    parser.add_argument("pipeline_url", type=str, help="URLs to the pipeline.")
    parser.add_argument(
        "-o",
        "--output",
        type=str,
        help="Output file name. Use html or image suffixes to choose the format.",
    )
    parser.add_argument(
        "--token",
        metavar="token",
        help="force GitLab token, otherwise it's read from ~/.config/gitlab-token",
    )
    parser.add_argument(
        "--ci-timeout",
        metavar="ci_timeout",
        type=float,
        default=60,
        help="Time that marge-bot will wait for ci to finish. Defaults to one hour.",
    )
    args = parser.parse_args()
    main(args.token, args.pipeline_url, args.output, args.ci_timeout)
