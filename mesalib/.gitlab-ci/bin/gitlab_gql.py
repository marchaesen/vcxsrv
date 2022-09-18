#!/usr/bin/env python3

import re
from argparse import ArgumentDefaultsHelpFormatter, ArgumentParser, Namespace
from dataclasses import dataclass, field
from os import getenv
from pathlib import Path
from typing import Any, Iterable, Optional, Pattern, Union

import yaml
from filecache import DAY, filecache
from gql import Client, gql
from gql.transport.aiohttp import AIOHTTPTransport
from graphql import DocumentNode

Dag = dict[str, list[str]]
TOKEN_DIR = Path(getenv("XDG_CONFIG_HOME") or Path.home() / ".config")


def get_token_from_default_dir() -> str:
    try:
        token_file = TOKEN_DIR / "gitlab-token"
        return token_file.resolve()
    except FileNotFoundError as ex:
        print(
            f"Could not find {token_file}, please provide a token file as an argument"
        )
        raise ex


def get_project_root_dir():
    root_path = Path(__file__).parent.parent.parent.resolve()
    gitlab_file = root_path / ".gitlab-ci.yml"
    assert gitlab_file.exists()

    return root_path


@dataclass
class GitlabGQL:
    _transport: Any = field(init=False)
    client: Client = field(init=False)
    url: str = "https://gitlab.freedesktop.org/api/graphql"
    token: Optional[str] = None

    def __post_init__(self):
        self._setup_gitlab_gql_client()

    def _setup_gitlab_gql_client(self) -> Client:
        # Select your transport with a defined url endpoint
        headers = {}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        self._transport = AIOHTTPTransport(url=self.url, headers=headers)

        # Create a GraphQL client using the defined transport
        self.client = Client(
            transport=self._transport, fetch_schema_from_transport=True
        )

    @filecache(DAY)
    def query(
        self, gql_file: Union[Path, str], params: dict[str, Any]
    ) -> dict[str, Any]:
        # Provide a GraphQL query
        source_path = Path(__file__).parent
        pipeline_query_file = source_path / gql_file

        query: DocumentNode
        with open(pipeline_query_file, "r") as f:
            pipeline_query = f.read()
            query = gql(pipeline_query)

        # Execute the query on the transport
        return self.client.execute(query, variable_values=params)

    def invalidate_query_cache(self):
        self.query._db.clear()


def create_job_needs_dag(
    gl_gql: GitlabGQL, params
) -> tuple[Dag, dict[str, dict[str, Any]]]:

    result = gl_gql.query("pipeline_details.gql", params)
    dag = {}
    jobs = {}
    pipeline = result["project"]["pipeline"]
    if not pipeline:
        raise RuntimeError(f"Could not find any pipelines for {params}")

    for stage in pipeline["stages"]["nodes"]:
        for stage_job in stage["groups"]["nodes"]:
            for job in stage_job["jobs"]["nodes"]:
                needs = job.pop("needs")["nodes"]
                jobs[job["name"]] = job
                dag[job["name"]] = {node["name"] for node in needs}

    for job, needs in dag.items():
        needs: set
        partial = True

        while partial:
            next_depth = {n for dn in needs for n in dag[dn]}
            partial = not needs.issuperset(next_depth)
            needs = needs.union(next_depth)

        dag[job] = needs

    return dag, jobs


def filter_dag(dag: Dag, regex: Pattern) -> Dag:
    return {job: needs for job, needs in dag.items() if re.match(regex, job)}


def print_dag(dag: Dag) -> None:
    for job, needs in dag.items():
        print(f"{job}:")
        print(f"\t{' '.join(needs)}")
        print()


def fetch_merged_yaml(gl_gql: GitlabGQL, params) -> dict[Any]:
    gitlab_yml_file = get_project_root_dir() / ".gitlab-ci.yml"
    content = Path(gitlab_yml_file).read_text()
    params["content"] = content
    raw_response = gl_gql.query("job_details.gql", params)
    if merged_yaml := raw_response["ciConfig"]["mergedYaml"]:
        return yaml.safe_load(merged_yaml)

    gl_gql.invalidate_query_cache()
    raise ValueError(
        """
    Could not fetch any content for merged YAML,
    please verify if the git SHA exists in remote.
    Maybe you forgot to `git push`?  """
    )


def recursive_fill(job, relationship_field, target_data, acc_data: dict, merged_yaml):
    if relatives := job.get(relationship_field):
        if isinstance(relatives, str):
            relatives = [relatives]

        for relative in relatives:
            parent_job = merged_yaml[relative]
            acc_data = recursive_fill(parent_job, acc_data, merged_yaml)

    acc_data |= job.get(target_data, {})

    return acc_data


def get_variables(job, merged_yaml, project_path, sha) -> dict[str, str]:
    p = get_project_root_dir() / ".gitlab-ci" / "image-tags.yml"
    image_tags = yaml.safe_load(p.read_text())

    variables = image_tags["variables"]
    variables |= merged_yaml["variables"]
    variables |= job["variables"]
    variables["CI_PROJECT_PATH"] = project_path
    variables["CI_PROJECT_NAME"] = project_path.split("/")[1]
    variables["CI_REGISTRY_IMAGE"] = "registry.freedesktop.org/${CI_PROJECT_PATH}"
    variables["CI_COMMIT_SHA"] = sha

    while recurse_among_variables_space(variables):
        pass

    return variables


# Based on: https://stackoverflow.com/a/2158532/1079223
def flatten(xs):
    for x in xs:
        if isinstance(x, Iterable) and not isinstance(x, (str, bytes)):
            yield from flatten(x)
        else:
            yield x


def get_full_script(job) -> list[str]:
    script = []
    for script_part in ("before_script", "script", "after_script"):
        script.append(f"# {script_part}")
        lines = flatten(job.get(script_part, []))
        script.extend(lines)
        script.append("")

    return script


def recurse_among_variables_space(var_graph) -> bool:
    updated = False
    for var, value in var_graph.items():
        value = str(value)
        dep_vars = []
        if match := re.findall(r"(\$[{]?[\w\d_]*[}]?)", value):
            all_dep_vars = [v.lstrip("${").rstrip("}") for v in match]
            # print(value, match, all_dep_vars)
            dep_vars = [v for v in all_dep_vars if v in var_graph]

        for dep_var in dep_vars:
            dep_value = str(var_graph[dep_var])
            new_value = var_graph[var]
            new_value = new_value.replace(f"${{{dep_var}}}", dep_value)
            new_value = new_value.replace(f"${dep_var}", dep_value)
            var_graph[var] = new_value
            updated |= dep_value != new_value

    return updated


def get_job_final_definiton(job_name, merged_yaml, project_path, sha):
    job = merged_yaml[job_name]
    variables = get_variables(job, merged_yaml, project_path, sha)

    print("# --------- variables ---------------")
    for var, value in sorted(variables.items()):
        print(f"export {var}={value!r}")

    # TODO: Recurse into needs to get full script
    # TODO: maybe create a extra yaml file to avoid too much rework
    script = get_full_script(job)
    print()
    print()
    print("# --------- full script ---------------")
    print("\n".join(script))

    if image := variables.get("MESA_IMAGE"):
        print()
        print()
        print("# --------- container image ---------------")
        print(image)


def parse_args() -> Namespace:
    parser = ArgumentParser(
        formatter_class=ArgumentDefaultsHelpFormatter,
        description="CLI and library with utility functions to debug jobs via Gitlab GraphQL",
        epilog=f"""Example:
        {Path(__file__).name} --rev $(git rev-parse HEAD) --print-job-dag""",
    )
    parser.add_argument("-pp", "--project-path", type=str, default="mesa/mesa")
    parser.add_argument("--sha", "--rev", type=str, required=True)
    parser.add_argument(
        "--regex",
        type=str,
        required=False,
        help="Regex pattern for the job name to be considered",
    )
    parser.add_argument("--print-dag", action="store_true", help="Print job needs DAG")
    parser.add_argument(
        "--print-merged-yaml",
        action="store_true",
        help="Print the resulting YAML for the specific SHA",
    )
    parser.add_argument(
        "--print-job-manifest", type=str, help="Print the resulting job data"
    )
    parser.add_argument(
        "--gitlab-token-file",
        type=str,
        default=get_token_from_default_dir(),
        help="force GitLab token, otherwise it's read from $XDG_CONFIG_HOME/gitlab-token",
    )

    args = parser.parse_args()
    args.gitlab_token = Path(args.gitlab_token_file).read_text()
    return args


def main():
    args = parse_args()
    gl_gql = GitlabGQL(token=args.gitlab_token)

    if args.print_dag:
        dag, jobs = create_job_needs_dag(
            gl_gql, {"projectPath": args.project_path, "sha": args.sha}
        )

        if args.regex:
            dag = filter_dag(dag, re.compile(args.regex))
        print_dag(dag)

    if args.print_merged_yaml:
        print(
            fetch_merged_yaml(
                gl_gql, {"projectPath": args.project_path, "sha": args.sha}
            )
        )

    if args.print_job_manifest:
        merged_yaml = fetch_merged_yaml(
            gl_gql, {"projectPath": args.project_path, "sha": args.sha}
        )
        get_job_final_definiton(
            args.print_job_manifest, merged_yaml, args.project_path, args.sha
        )


if __name__ == "__main__":
    main()
