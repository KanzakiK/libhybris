#!/usr/bin/env python3
"""Run one Windows demo through the existing ARLinux instance-level host."""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, "/usr/lib/arlinux")

import windows  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    env = {
        name: os.environ[name]
        for name in (
            "HYBRIS_VULKAN_TRACE",
            "HYBRIS_BC_TRANSCODE",
            "HYBRIS_BC_FAST",
        )
        if name in os.environ
    }
    root = windows.ensure_runtime()
    prefix = windows.instance_prefix()
    result = windows.run(
        root,
        prefix,
        args.executable,
        args.arguments,
        env=env,
        timeout=args.timeout,
    )
    print(f"BC_DEMO_PROCESS exit={result.returncode}")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
