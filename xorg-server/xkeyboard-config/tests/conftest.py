# SPDX-License-Identifier: MIT

import os
import sys
from pathlib import Path

import pytest

tests_dir = Path(__file__).parent.resolve()
sys.path.insert(0, str(tests_dir))

try:
    import xdist  # noqa: F401

    # Otherwise we get unknown hook 'pytest_xdist_auto_num_workers'
    def pytest_xdist_auto_num_workers(config):
        return os.getenv("FDO_CI_CONCURRENT", None)

except ImportError:
    pass


def pytest_addoption(parser: pytest.Parser):
    parser.addoption(
        "--layout-compat-config",
        action="append",
        default=[],
        type=Path,
        help="List of layout compatibility files",
    )
