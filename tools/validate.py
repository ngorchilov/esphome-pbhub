#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


REQUIRED_ESPHOME_VERSION = "Version: 2026.7.0"


def run(command, cwd):
    return subprocess.run(
        command,
        cwd=cwd,
        check=False,
        capture_output=True,
        text=True,
    )


def report_failure(label, result):
    print(f"[FAIL] {label}", file=sys.stderr)
    if result.stdout:
        print(result.stdout.rstrip(), file=sys.stderr)
    if result.stderr:
        print(result.stderr.rstrip(), file=sys.stderr)


def check_esphome_version(esphome, root):
    result = run([esphome, "version"], root)
    if result.returncode != 0:
        report_failure("ESPHome version check", result)
        return False

    version = result.stdout.strip()
    if version != REQUIRED_ESPHOME_VERSION:
        print(
            f"[FAIL] expected {REQUIRED_ESPHOME_VERSION}, got {version!r}",
            file=sys.stderr,
        )
        return False

    print(f"[PASS] {REQUIRED_ESPHOME_VERSION}")
    return True


def check_positive(esphome, root, fixture):
    result = run([esphome, "-q", "config", str(fixture)], root)
    label = f"config {fixture.relative_to(root)}"
    if result.returncode != 0:
        report_failure(label, result)
        return False

    print(f"[PASS] {label}")
    return True


def check_negative(esphome, root, fixture):
    result = run([esphome, "-q", "config", str(fixture)], root)
    label = f"expected failure {fixture.relative_to(root)}"
    if result.returncode == 0:
        print(f"[FAIL] {label} unexpectedly passed", file=sys.stderr)
        return False

    expected_file = fixture.with_suffix(".expected")
    expected = [
        line.strip()
        for line in expected_file.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    output = result.stdout + result.stderr
    missing = [text for text in expected if text not in output]
    if missing:
        print(
            f"[FAIL] {label} missed expected text: {missing}",
            file=sys.stderr,
        )
        report_failure(label, result)
        return False

    print(f"[PASS] {label}")
    return True


def compile_fixture(esphome, root, fixture):
    result = run([esphome, "-q", "compile", str(fixture)], root)
    label = f"compile {fixture.relative_to(root)}"
    if result.returncode != 0:
        report_failure(label, result)
        return False

    print(f"[PASS] {label}")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Validate PBHUB fixtures with ESPHome 2026.7.0"
    )
    parser.add_argument(
        "--compile",
        action="store_true",
        help="also compile all Phase 1 positive fixtures",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    configured_esphome = os.environ.get("ESPHOME", "esphome")
    esphome = shutil.which(configured_esphome)
    if esphome is None:
        print(
            f"[FAIL] ESPHome executable not found: {configured_esphome}",
            file=sys.stderr,
        )
        return 1

    if not check_esphome_version(esphome, root):
        return 1

    passed = True

    positive_dir = root / "tests" / "positive"
    positive_fixtures = sorted(positive_dir.glob("*.yaml"))
    if not positive_fixtures:
        print("[FAIL] no positive YAML fixtures found", file=sys.stderr)
        return 1
    for fixture in positive_fixtures:
        passed = check_positive(esphome, root, fixture) and passed

    negative_dir = root / "tests" / "negative"
    negative_fixtures = sorted(negative_dir.glob("*.yaml"))
    if not negative_fixtures:
        print("[FAIL] no negative YAML fixtures found", file=sys.stderr)
        return 1
    expected_files = {path.stem for path in negative_dir.glob("*.expected")}
    fixture_names = {path.stem for path in negative_fixtures}
    if expected_files != fixture_names:
        print(
            "[FAIL] negative YAML fixtures and .expected files do not match",
            file=sys.stderr,
        )
        passed = False
    else:
        for fixture in negative_fixtures:
            expected_file = fixture.with_suffix(".expected")
            if not expected_file.read_text(encoding="utf-8").strip():
                print(
                    f"[FAIL] empty expected diagnostics: "
                    f"{expected_file.relative_to(root)}",
                    file=sys.stderr,
                )
                passed = False
                continue
            passed = check_negative(esphome, root, fixture) and passed

    if args.compile:
        compile_fixtures = sorted(
            positive_fixtures,
            key=lambda fixture: (fixture.name != "hub-only.yaml", fixture.name),
        )
        for fixture in compile_fixtures:
            passed = compile_fixture(esphome, root, fixture) and passed

    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
