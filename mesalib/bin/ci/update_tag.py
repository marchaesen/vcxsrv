#!/usr/bin/env python3
import logging
import os
import re
import sys
import argparse
import subprocess
import yaml
from typing import Optional, Set, Any

from datetime import datetime, timezone
from pathlib import Path

CI_PROJECT_DIR: str | None = os.environ.get("CI_PROJECT_DIR", None)
GIT_REPO_ROOT: str = CI_PROJECT_DIR or str(Path(__file__).resolve().parent.parent.parent)
SETUP_TEST_ENV_PATH: Path = Path(GIT_REPO_ROOT) / ".gitlab-ci" / "setup-test-env.sh"
CONDITIONAL_TAGS_FILE: Path = (
    Path(GIT_REPO_ROOT) / ".gitlab-ci" / "conditional-build-image-tags.yml"
)
CONTAINER_DIR: Path = Path(GIT_REPO_ROOT) / ".gitlab-ci" / "container"
CONTAINER_CI_FILE: Path = CONTAINER_DIR / "gitlab-ci.yml"

# Very simple type alias for GitLab YAML data structure
# It is composed by a dictionary of job names, each with a dictionary of fields
# (e.g., script, stage, rules, etc.)
YamlData = dict[str, dict[str, Any]]

# Dummy environment vars to make build scripts happy
# To avoid set -u errors in build scripts
DUMMY_ENV_VARS: dict[str, str] = {
    # setup-test-env.sh
    "CI_JOB_STARTED_AT": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S%z"),
    # build-deqp.sh
    "DEQP_API": "dummy",
    "DEQP_TARGET": "dummy",
}


def from_component_to_build_tag(component: str) -> str:
    # e.g., "angle" -> "CONDITIONAL_BUILD_ANGLE_TAG"
    return "CONDITIONAL_BUILD_" + re.sub(r"-", "_", component.upper()) + "_TAG"


def from_component_to_tag_var(component: str) -> str:
    # e.g., "angle" -> "ANGLE_TAG"
    return re.sub(r"-", "_", component.upper()) + "_TAG"


def from_script_name_to_component(script_name: str) -> str:
    # e.g., "build-angle.sh" -> "angle"
    return re.sub(r"^build-([a-z0-9_-]+)\.sh$", r"\1", script_name)


def from_script_name_to_tag_var(script_name: str) -> str:
    # e.g., "build-angle.sh" -> "ANGLE_TAG"
    return (
        re.sub(r"^build-([a-z0-9_-]+)\.sh$", r"\1_TAG", script_name)
        .replace("-", "_")
        .upper()
    )


def prepare_setup_env_script() -> Path:
    """
    Sets up dummy environment variables to mimic the script in the CI repo.
    Returns the path to the setup-test-env.sh script.
    """
    if not SETUP_TEST_ENV_PATH.exists():
        sys.exit(".gitlab-ci/setup-test-env.sh not found. Exiting.")

    # Dummy environment vars to mimic the script
    for key, value in DUMMY_ENV_VARS.items():
        os.environ[key] = value

    os.environ["CI_PROJECT_DIR"] = GIT_REPO_ROOT

    return SETUP_TEST_ENV_PATH


def validate_build_script(script_filename: str) -> bool:
    """
    Returns True if the build script for the given component uses the structured tag variable.
    """
    build_script = CONTAINER_DIR / script_filename
    with open(build_script, "r", encoding="utf-8") as f:
        script_content = f.read()

    tag_var = from_script_name_to_tag_var(script_filename)
    if not re.search(tag_var, script_content, re.IGNORECASE):
        logging.debug(
            f"Skipping {build_script} because it doesn't use {tag_var}",
        )
        return False
    return True


def load_container_yaml() -> YamlData:
    if not os.path.isfile(CONTAINER_CI_FILE):
        sys.exit(f"File not found: {CONTAINER_CI_FILE}")

    # Ignore !reference and other custom GitLab tags, we just want to know the
    # job names and fields
    yaml.SafeLoader.add_multi_constructor('', lambda loader, suffix, node: None)
    with open(CONTAINER_CI_FILE, "r", encoding="utf-8") as f:

        data = yaml.load(f, Loader=yaml.SafeLoader)
        if not isinstance(data, dict):
            return {"variables": {}}
        return data


def find_candidate_components() -> list[str]:
    """
    1) Reads .gitlab-ci/container/gitlab-ci.yml to find component links:
        lines matching '.*.container-builds-<component>'
    2) Looks for matching build-<component>.sh in .gitlab-ci/container/
    3) Returns a sorted list of components in the intersection of these sets.
    """
    container_yaml = load_container_yaml()
    # Extract patterns like `container-builds-foo` from job names
    candidates: Set[str] = set()
    for job_name in container_yaml:
        if match := re.search(r"\.container-builds-([a-z0-9_-]+)$", str(job_name)):
            candidates.add(match.group(1))

    if not candidates:
        logging.error(
            f"No viable build components found in {CONTAINER_CI_FILE}. "
            "Please check the file for valid component names. "
            "They should be named like '.container-builds-<component>'."
        )
        return []

    # Find build scripts named build-<component>.sh
    build_scripts: list[str] = []
    for path in CONTAINER_DIR.glob("build-*.sh"):
        if validate_build_script(path.name):
            logging.info(f"Found build script: {path.name}")
            component = from_script_name_to_component(path.name)
            build_scripts.append(component)

    # Return sorted intersection of components found in build scripts and candidates
    return sorted(candidates.intersection(build_scripts))


def filter_components(
    components: list[str], includes: list[str], excludes: list[str]
) -> list[str]:
    """
    Returns components that match at least one `includes` regex and none of the `excludes` regex.
    If includes is empty, returns an empty list (unless user explicitly does --all or --include).
    """
    if not includes:
        return []

    filtered = []
    for comp in components:
        # Must match at least one "include"
        if not any(re.fullmatch(inc, comp) for inc in includes):
            logging.debug(f"Excluding {comp}, no matches in includes.")
            continue
        # Must not match any "exclude"
        if any(re.fullmatch(exc, comp) for exc in excludes):
            logging.debug(f"Excluding {comp}, matched exclude pattern.")
            continue
        filtered.append(comp)
    return filtered


def run_build_script(component: str, check_only: bool = False) -> Optional[str]:
    """
    Runs .gitlab-ci/container/build-<component>.sh to produce a new tag (last line of stdout).
    If check_only=True, we skip updates to the YAML (but do the build to see if it passes).
    Returns the extracted tag (string) on success, or None on failure.
    """
    # 1) Set up environment
    setup_env_script = prepare_setup_env_script()

    build_script = os.path.join(CONTAINER_DIR, f"build-{component}.sh")
    if not os.path.isfile(build_script):
        logging.error(f"Build script not found for {component}: {build_script}")
        return None

    # Tag var should appear in the script, e.g., ANGLE_TAG for 'angle'
    tag_var = from_component_to_tag_var(component)

    # We set up environment for the child process
    child_env: dict[str, str] = {}
    child_env["NEW_TAG_DRY_RUN"] = "1"
    if check_only:
        # For checking only
        child_env.pop("NEW_TAG_DRY_RUN", None)
        child_env["CI_NOT_BUILDING_ANYTHING"] = "1"
        if tag_value := get_current_tag_value(component):
            child_env[tag_var] = tag_value
        else:
            logging.error(f"No current tag value for {component}")
            return None

    logging.debug(
        f"Running build for {component} with "
        f"{tag_var}={child_env.get(tag_var)} "
        f"(check_only={check_only})"
    )

    # Run the build script
    result = subprocess.run(
        ["bash", "-c", f"source {setup_env_script} && bash -x {build_script}"],
        env=os.environ | child_env,
        capture_output=True,
        text=True,
    )
    logging.debug(f"{' '.join(result.args)}")

    # Tag check succeeded
    if result.returncode == 0:
        # Tag is assumed to be the last line of stdout
        lines = result.stdout.strip().splitlines()
        return lines[-1] if lines else ""

    # Tag check failed, let's dissect the error

    if result.returncode == 2:
        logging.error(
            f"Tag mismatch for {component}."
        )
        logging.error(result.stdout)
        return None

    # Check if there's an unbound variable error
    err_output = result.stderr
    unbound_match = re.search(r"([A-Z_]+)(?=: unbound variable)", err_output)
    if unbound_match:
        var_name = unbound_match.group(1)
        logging.fatal(f"Please set the variable {var_name} in {build_script}.")
        sys.exit(3)

    # Unexpected error in the build script, propagate the exit code
    logging.fatal(
        f"Build script for {component} failed with return code {result.returncode}"
    )
    logging.error(result.stdout)
    sys.exit(result.returncode)


def load_image_tags_yaml() -> YamlData:
    try:
        with open(CONDITIONAL_TAGS_FILE, "r", encoding="utf-8") as f:
            data = yaml.safe_load(f)
            if not isinstance(data, dict):
                return {"variables": {}}
            if "variables" not in data:
                data["variables"] = {}
            return data
    except FileNotFoundError:
        return {"variables": {}}


def get_current_tag_value(component: str) -> Optional[str]:
    full_tag_var = from_component_to_build_tag(component)
    data = load_image_tags_yaml()
    variables = data.get("variables", {})
    if not isinstance(variables, dict):
        return None
    return variables.get(full_tag_var)


def update_image_tag_in_yaml(component: str, tag_value: str) -> None:
    """
    Uses PyYAML to edit the YAML file at IMAGE_TAGS_FILE, setting the environment variable
    for the given component. Maintains sorted keys.
    """
    full_tag_var = from_component_to_build_tag(component)
    data = load_image_tags_yaml()

    # Ensure we have a variables dictionary
    if "variables" not in data:
        data["variables"] = {}
    elif not isinstance(data["variables"], dict):
        data["variables"] = {}

    # Update the tag
    data["variables"][full_tag_var] = tag_value

    # Sort the variables
    data["variables"] = dict(sorted(data["variables"].items()))

    # Write back to file
    with open(CONDITIONAL_TAGS_FILE, "w", encoding="utf-8") as f:
        yaml.dump(data, f, sort_keys=False)  # Don't sort top-level keys


def parse_args():
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Manage container image tags for CI builds with regex-based includes/excludes.",
        epilog="""
Exit codes:
  0 - Success
  1 - Unhandled error in this script
  2 - Tag mismatch when using --check
  3 - Unbound variable error in build script
  x - Build script failed with return code x
        """,
    )

    parser.add_argument(
        "--include",
        "-i",
        action="append",
        default=[],
        help="Full match regex pattern for components to include.",
    )
    parser.add_argument(
        "--exclude",
        "-x",
        action="append",
        default=[],
        help="Full match regex pattern for components to exclude.",
    )
    parser.add_argument(
        "--all", action="store_true", help="Equivalent to --include '.*'"
    )
    parser.add_argument(
        "--check",
        "-c",
        action="append",
        default=[],
        help="Check matching components instead of updating YAML. "
        "If any component fails, exit with a non-zero exit code.",
    )
    parser.add_argument(
        "--list",
        "-l",
        action="store_true",
        help="List all available components and exit.",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="count",
        default=0,
        help="Increase verbosity level (-v for info, -vv for debug)",
    )

    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(0)

    return parser.parse_args()


def main():
    args = parse_args()

    # Configure logging based on verbosity level
    if args.verbose == 1:
        log_level = logging.INFO
    elif args.verbose == 2:
        log_level = logging.DEBUG
    else:
        log_level = logging.WARNING

    logging.basicConfig(level=log_level, format="%(levelname)s: %(message)s")

    # 0) Check if the YAML file exists
    if not os.path.isfile(CONDITIONAL_TAGS_FILE):
        logging.fatal(
            f"Conditional build image tags file not found: {CONDITIONAL_TAGS_FILE}"
        )
        return

    # 1) If checking, just run build scripts in check mode and propagate errors
    if args.check:
        tag_mismatch = False
        for comp in args.check:
            try:
                if run_build_script(comp, check_only=True) is None:
                    # The tag is invalid
                    tag_mismatch = True
            except SystemExit as e:
                # Let custom exit codes propagate
                raise e
            except Exception as e:
                logging.error(f"Internal error: {e}")
                sys.exit(3)
        # If any component failed, exit with code 1
        if tag_mismatch:
            sys.exit(2)
        return

    # Convert --all into a wildcard include
    if args.all:
        args.include.append(".*")

    # 2) If --list, just show all discovered components
    all_components = find_candidate_components()
    if args.list:
        print("Detected components:", ", ".join(all_components))
        return

    # 3) Filter components
    final_components = filter_components(all_components, args.include, args.exclude)

    if args.verbose:
        logging.debug(f"Found components: {all_components}")
        logging.debug(f"Filtered components: {final_components}")

    for comp in final_components:
        logging.info(f"Updating {comp}...")
        new_tag = run_build_script(comp, check_only=False)
        if new_tag is not None:
            update_image_tag_in_yaml(comp, new_tag)
            if args.verbose:
                logging.debug(f"Updated {comp} with tag: {new_tag}")


if __name__ == "__main__":
    main()
