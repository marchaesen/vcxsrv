import os
import sys
import subprocess
import yaml
import pytest

from textwrap import dedent
from unittest.mock import patch

from bin.ci.update_tag import (
    from_component_to_build_tag,
    filter_components,
    from_component_to_tag_var,
    from_script_name_to_component,
    from_script_name_to_tag_var,
    load_container_yaml,
    load_image_tags_yaml,
    update_image_tag_in_yaml,
    run_build_script,
    main,
)


@pytest.fixture
def temp_image_tags_file(tmp_path, monkeypatch):
    """
    Fixture that creates a temporary YAML file (to simulate CONDITIONAL_TAGS_FILE)
    and updates the global variable accordingly.
    """
    temp_file = tmp_path / "conditional-build-image-tags.yml"
    # Write initial dummy content.
    temp_file.write_text(yaml.dump({"variables": {}}))
    monkeypatch.setattr("bin.ci.update_tag.CONDITIONAL_TAGS_FILE", str(temp_file))
    return temp_file


@pytest.fixture
def temp_container_ci_file(tmp_path, monkeypatch):
    """
    Fixture that creates a temporary container CI file (to simulate CONTAINER_CI_FILE)
    and updates the global variable accordingly.
    """
    temp_file = tmp_path / "container-ci.yml"
    temp_file.touch()
    monkeypatch.setattr("bin.ci.update_tag.CONTAINER_CI_FILE", temp_file)
    return temp_file


@pytest.fixture
def mock_container_dir(tmp_path, monkeypatch):
    """
    Fixture that creates a dummy container directory and build script.
    """
    # Create a dummy container directory and build script.
    container_dir = tmp_path / "container"
    container_dir.mkdir()

    # Patch CONTAINER_DIR so that the build script is found.
    monkeypatch.setattr("bin.ci.update_tag.CONTAINER_DIR", container_dir)

    # Create a dummy setup-test-env.sh file and patch set_dummy_env_vars.
    dummy_setup_path = tmp_path / "setup-test-env.sh"
    dummy_setup_path.write_text("echo Setup")
    monkeypatch.setattr(
        "bin.ci.update_tag.prepare_setup_env_script", lambda: dummy_setup_path
    )

    return container_dir


@pytest.fixture
def mock_build_script(mock_container_dir):
    """
    Fixture that creates a dummy build script in the container directory.
    """
    build_script = mock_container_dir / "build-foo.sh"
    return build_script


###############################################################################
# Tests for argument parsing and helper functions
###############################################################################


@pytest.mark.parametrize(
    "component, expected_tag_var",
    [
        ("foo", "FOO_TAG"),
        ("my-component", "MY_COMPONENT_TAG"),
        ("foo-bar-baz", "FOO_BAR_BAZ_TAG"),
    ],
)
def test_from_component_to_tag_var(component, expected_tag_var):
    """
    Test that from_component_to_tag_var returns the correct tag variable name.
    """
    assert from_component_to_tag_var(component) == expected_tag_var


@pytest.mark.parametrize(
    "component, expected_build_tag",
    [
        ("foo", "CONDITIONAL_BUILD_FOO_TAG"),
        ("my-component", "CONDITIONAL_BUILD_MY_COMPONENT_TAG"),
        ("foo-bar-baz", "CONDITIONAL_BUILD_FOO_BAR_BAZ_TAG"),
    ],
)
def test_from_component_to_build_tag(component, expected_build_tag):
    """
    Test that from_component_to_build_tag returns the correct build tag name.
    """
    assert from_component_to_build_tag(component) == expected_build_tag


@pytest.mark.parametrize(
    "script_name, expected_component",
    [
        ("build-foo.sh", "foo"),
        ("build-my-component.sh", "my-component"),
        ("build-foo-bar-baz.sh", "foo-bar-baz"),
    ],
)
def test_from_script_name_to_component(script_name, expected_component):
    """
    Test that from_script_name_to_component returns the correct component name.
    """
    assert from_script_name_to_component(script_name) == expected_component


@pytest.mark.parametrize(
    "script_name, expected_tag_var",
    [
        ("build-foo.sh", "FOO_TAG"),
        ("build-my-component.sh", "MY_COMPONENT_TAG"),
        ("build-foo-bar-baz.sh", "FOO_BAR_BAZ_TAG"),
    ],
)
def test_from_script_name_to_tag_var(script_name, expected_tag_var):
    """
    Test that from_script_name_to_tag_var returns the correct tag variable name.
    """
    assert from_script_name_to_tag_var(script_name) == expected_tag_var


def test_filter_components():
    """
    Test that filter_components returns only components that match an include regex
    and do not match any exclude regex. If includes is empty, an empty list is returned.
    """
    components = ["alpha", "beta", "gamma", "delta"]

    # When includes is empty, should return an empty list.
    assert filter_components(components, [], []) == []

    # Test includes only.
    # Components that start with 'a' or 'b'
    expected = ["alpha", "beta"]
    result = filter_components(components, ["^[a-b].*$"], [])
    assert result == expected

    # Test with an exclude that filters out "alpha" (which matches "lph").
    result = filter_components(components, ["^[a-b].*$"], ["^.*lph.*$"])
    # "alpha" is removed.
    assert result == ["beta"]


@pytest.mark.parametrize(
    "component, tag",
    [
        # Basic case
        ("test", "new_tag"),
        # Component with hyphen
        ("my-component", "v1.2.3"),
        # Uppercase component name
        ("UpperCase", "123"),
        # Numbers in component name
        ("123service", "build-456"),
    ],
)
def test_update_image_tag_in_yaml(component, tag, temp_image_tags_file):
    """
    Test multiple updates with different component names and tags, verifying:
    1. Correct variable name generation
    2. Proper tag value storage
    3. Maintained sorting of variables
    """
    # Initial update
    update_image_tag_in_yaml(component, tag)

    data = load_image_tags_yaml()
    expected_var = from_component_to_build_tag(component)
    assert data["variables"][expected_var] == tag

    # Add second component and verify both exist
    update_image_tag_in_yaml("another_component", "secondary_tag")
    updated_data = load_image_tags_yaml()

    assert from_component_to_build_tag("another_component") in updated_data["variables"]
    assert updated_data["variables"][expected_var] == tag  # Original value remains

    # Verify sorting
    variables = list(data["variables"].keys())
    assert len(updated_data["variables"]) == len(variables) + 1
    assert variables == sorted(variables), "Variables are not alphabetically sorted"


def test_if_run_extracts_the_tag_from_stdout(monkeypatch, mock_build_script):
    """
    Test that run_build_script returns the tag (last stdout line) when the build
    script executes successfully.
    """
    mock_build_script.write_text("#!/bin/bash\necho Build script\necho new_tag")
    # Create a fake subprocess.CompletedProcess to simulate a successful build.
    fake_result = subprocess.CompletedProcess(
        args=[], returncode=0, stdout="line1\nnew_tag", stderr=""
    )
    monkeypatch.setattr(subprocess, "run", lambda *args, **kwargs: fake_result)

    tag = run_build_script("foo", check_only=False)
    assert tag == "new_tag"


def test_running_real_process_works(monkeypatch, mock_build_script):
    """
    Test that run_build_script returns the tag (last stdout line) when the build
    script executes successfully.
    """
    mock_build_script.write_text("echo Build script\necho new_tag")
    tag = run_build_script("foo", check_only=False)
    assert tag == "new_tag"


###############################################################################
# Tests for main() argument features
###############################################################################


def setup_build_script_and_container_ci_file(container_dir, container_ci_file, comp):
    tag_var = from_component_to_build_tag(comp)
    build_check_var = from_component_to_tag_var(comp)
    build_script_path = container_dir / f"build-{comp}.sh"
    build_script_path.write_text(
        dedent(
            f"""
        #!/bin/bash
        ci_tag_build_time_check {tag_var}
        echo "new_tag"
        """
        )
    )
    container_ci_file.write_text(
        container_ci_file.read_text()
        + dedent(
            f"""
        .container-builds-{comp}:
            variables:
                {tag_var}: "${{{build_check_var}}}"
        """
        )
    )


def test_main_list(monkeypatch, capsys, mock_container_dir, temp_container_ci_file):
    """
    Test that when the --list argument is provided, main() prints all detected
    components
    """
    # Monkeypatch find_components to return a known list.
    for comp in ["comp-a", "comp-b"]:
        setup_build_script_and_container_ci_file(
            mock_container_dir, temp_container_ci_file, comp
        )
    # Set sys.argv to simulate passing --list.
    monkeypatch.setattr(sys, "argv", ["bin/ci/update_tag.py", "--list"])

    main()

    captured = capsys.readouterr().out
    assert "Detected components:" in captured
    assert "comp-a" in captured
    assert "comp-b" in captured


def test_main_check(monkeypatch, temp_image_tags_file, temp_container_ci_file):
    """
    Test that when --check is provided, main() calls run_build_script in check mode.
    """

    with patch(
        "bin.ci.update_tag.run_build_script", return_value="dummy_tag"
    ) as mock_run_build_script:
        # Simulate command line: --check compX
        monkeypatch.setattr(sys, "argv", ["bin/ci/update_tag.py", "--check", "comp-x"])

        main()
    # Verify run_build_script was called with check_only=True.
    assert mock_run_build_script.call_count == 1
    assert "comp-x" in mock_run_build_script.call_args.args[0]
    assert mock_run_build_script.call_args.kwargs["check_only"] is True


EXIT_CODE_SCENARIOS = {
    "unbound_variable": (
        "line 2: UNDEFINED_VARIABLE: unbound variable",
        127,
        3,
        "Please set the variable UNDEFINED_VARIABLE",
    ),
    "build script error": (
        "",
        50,
        50,
        "",
    ),
    "tag_mismatch": (
        "Tag mismatch for foo.",
        2,
        2,
        "Tag mismatch for foo.",
    ),
}


@pytest.mark.parametrize(
    "stderr_content, script_returncode, expected_exit_code, expected_error_message",
    EXIT_CODE_SCENARIOS.values(),
    ids=list(EXIT_CODE_SCENARIOS.keys()),
)
def test_build_script_error_exit_codes(
    monkeypatch,
    mock_build_script,
    capsys,
    stderr_content,
    script_returncode,
    expected_exit_code,
    expected_error_message,
):
    """
    Test that build script errors generate appropriate exit codes:
    1 - Unhandled error in this script
    2 - Tag mismatch when using --check
    3 - Unbound variable error in build script
    x - Build script failed with return code x
    """
    # Create a build script that will fail
    mock_build_script.write_text("#!/bin/bash\necho 'This will fail'")

    # Create a fake subprocess.CompletedProcess to simulate a failed build
    fake_result = subprocess.CompletedProcess(
        args=[], returncode=script_returncode, stdout="", stderr=stderr_content
    )
    monkeypatch.setattr(subprocess, "run", lambda *args, **kwargs: fake_result)
    monkeypatch.setattr(sys, "argv", ["bin/ci/update_tag.py", "--check", "foo"])
    monkeypatch.setattr(
        "bin.ci.update_tag.get_current_tag_value", lambda *args: "current_tag"
    )

    # Mock sys.exit to capture the exit code instead of exiting the test
    with pytest.raises(SystemExit) as e:
        main()

        # Check for expected error message if one is specified
        if expected_error_message:
            captured = capsys.readouterr()
            assert expected_error_message in captured.err

    # Verify correct exit code
    assert e.value.code == expected_exit_code
