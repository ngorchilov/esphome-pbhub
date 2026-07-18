#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


REQUIRED_ESPHOME_VERSION = "Version: 2026.7.0"

TARGET_PACKAGES = {
    "esp32-idf.yaml": "esp-idf",
    "esp32-idf-topology.yaml": "esp-idf",
    "esp32-arduino.yaml": "arduino",
    "esp32-arduino-topology.yaml": "arduino",
}

FRAMEWORK_FIXTURES = {
    "arduino-hub-only.yaml",
    "arduino-topologies.yaml",
}

POSITIVE_FIXTURES = {
    "adc-slots.yaml",
    "all-endpoints.yaml",
    "digital-entities.yaml",
    "hub-only.yaml",
    "multi-hub.yaml",
    "ownership.yaml",
    "pwm-contract.yaml",
    "rgb-bounds.yaml",
    "servo-contract.yaml",
}

TARGET_INCLUDE_PATTERN = re.compile(
    r"!include\s+\.\./common/"
    r"(esp32-(?:idf|arduino)(?:-topology)?\.yaml)"
)

SCENARIO_INCLUDE_PATTERN = re.compile(
    r"!include\s+\.\./common/"
    r"(pbhub-(?:core-only|full-topology)\.yaml)"
)

PAIRED_SCENARIOS = {
    "tests/positive/hub-only.yaml": "pbhub-core-only.yaml",
    "tests/framework/arduino-hub-only.yaml": "pbhub-core-only.yaml",
    "tests/positive/multi-hub.yaml": "pbhub-full-topology.yaml",
    "tests/framework/arduino-topologies.yaml": "pbhub-full-topology.yaml",
}

TARGET_PACKAGE_PAIRS = (
    ("esp32-idf.yaml", "esp32-arduino.yaml"),
    ("esp32-idf-topology.yaml", "esp32-arduino-topology.yaml"),
)

FRAMEWORK_BUILD_MARKERS = {
    "esp-idf": (
        " -DUSE_ESP_IDF ",
        " -DUSE_ESP32_FRAMEWORK_ESP_IDF ",
    ),
    "arduino": (
        " -DUSE_ARDUINO ",
        " -DUSE_ESP32_FRAMEWORK_ARDUINO ",
    ),
}

FRAMEWORK_FORBIDDEN_MARKERS = {
    "esp-idf": (
        " -DUSE_ARDUINO ",
        " -DUSE_ESP32_FRAMEWORK_ARDUINO ",
    ),
    "arduino": (
        " -DUSE_ESP_IDF ",
        " -DUSE_ESP32_FRAMEWORK_ESP_IDF ",
    ),
}

OPTIONAL_FEATURE_DEFINES = (
    "#define USE_BINARY_SENSOR",
    "#define USE_LIGHT",
    "#define USE_OUTPUT",
    "#define USE_OUTPUT_FLOAT_POWER_SCALING",
    "#define USE_SENSOR",
    "#define USE_SWITCH",
)

CORE_ONLY_FIXTURES = {
    "hub-only.yaml",
    "arduino-hub-only.yaml",
}

FULL_SURFACE_FIXTURES = {
    "multi-hub.yaml",
    "arduino-topologies.yaml",
}

TOPOLOGY_GENERATED_SOURCE_CHECKS = (
    "test_i2c_primary->set_frequency(100000);",
    "test_i2c_secondary->set_frequency(400000);",
    "test_multiplexer_channel_3->set_parent(test_multiplexer);",
    "test_multiplexer_channel_3->set_channel(3);",
    "test_multiplexer_channel_4->set_parent(test_multiplexer);",
    "test_multiplexer_channel_4->set_channel(4);",
    "feature_hub->set_i2c_bus(test_i2c_secondary);",
    "feature_hub->set_i2c_address(0x61);",
    "feature_hub->set_led_timing_mode(0);",
    "same_bus_hub->set_i2c_bus(test_i2c_secondary);",
    "same_bus_hub->set_i2c_address(0x62);",
    "primary_bus_hub->set_i2c_bus(test_i2c_primary);",
    "primary_bus_hub->set_i2c_address(0x62);",
    "primary_bus_hub->set_led_timing_mode(1);",
    "multiplexed_hub_3->set_i2c_bus(test_multiplexer_channel_3);",
    "multiplexed_hub_3->set_i2c_address(0x61);",
    "multiplexed_hub_4->set_i2c_bus(test_multiplexer_channel_4);",
    "multiplexed_hub_4->set_i2c_address(0x61);",
    "new(topology_pwm) pbhub::PbHubPWMOutput(feature_hub, 11);",
    "new(topology_servo_output) pbhub::PbHubServoOutput(feature_hub, 20);",
)

GENERATED_SOURCE_CHECKS = {
    "multi-hub.yaml": TOPOLOGY_GENERATED_SOURCE_CHECKS,
    "arduino-topologies.yaml": TOPOLOGY_GENERATED_SOURCE_CHECKS,
    "rgb-bounds.yaml": (
        "rgb_always_off_output->set_startup_off(true);",
        "rgb_restore_default_output->set_startup_off(false);",
        "rgb_restore_and_off_output->set_startup_off(true);",
        "rgb_always_on_output->set_startup_off(false);",
        "rgb_one_led->set_default_transition_length(0);",
        "rgb_max_leds->set_default_transition_length(250);",
    ),
}


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


def get_build_name(fixture):
    match = re.search(
        r"(?m)^  name:\s*([a-z0-9][a-z0-9-]*)\s*$",
        fixture.read_text(encoding="utf-8"),
    )
    if match is None:
        raise ValueError(f"cannot determine esphome.name from {fixture}")
    return match.group(1)


def expected_target_package(root, fixture):
    relative = fixture.relative_to(root).as_posix()
    if relative == "tests/positive/multi-hub.yaml":
        return "esp32-idf-topology.yaml"
    if relative == "tests/framework/arduino-hub-only.yaml":
        return "esp32-arduino.yaml"
    if relative == "tests/framework/arduino-topologies.yaml":
        return "esp32-arduino-topology.yaml"
    if relative.startswith("tests/positive/") or relative.startswith(
        "tests/negative/"
    ):
        return "esp32-idf.yaml"
    raise ValueError(f"fixture is outside the target matrix: {relative}")


def expected_framework(root, fixture):
    target = expected_target_package(root, fixture)
    return TARGET_PACKAGES[target]


def check_target_matrix(
    root,
    positive_fixtures,
    negative_fixtures,
    framework_fixtures,
):
    actual_positive_fixtures = {fixture.name for fixture in positive_fixtures}
    if actual_positive_fixtures != POSITIVE_FIXTURES:
        print(
            "[FAIL] positive fixture inventory differs: "
            f"expected {sorted(POSITIVE_FIXTURES)}, "
            f"got {sorted(actual_positive_fixtures)}",
            file=sys.stderr,
        )
        return False

    actual_framework_fixtures = {fixture.name for fixture in framework_fixtures}
    if actual_framework_fixtures != FRAMEWORK_FIXTURES:
        print(
            "[FAIL] framework fixture inventory differs: "
            f"expected {sorted(FRAMEWORK_FIXTURES)}, "
            f"got {sorted(actual_framework_fixtures)}",
            file=sys.stderr,
        )
        return False

    for target_name, framework in TARGET_PACKAGES.items():
        target = root / "tests" / "common" / target_name
        if not target.is_file():
            print(f"[FAIL] missing target package: {target}", file=sys.stderr)
            return False
        text = target.read_text(encoding="utf-8")
        if "  board: esp32dev\n" not in text or f"    type: {framework}\n" not in text:
            print(
                f"[FAIL] target package {target.relative_to(root)} does not "
                f"declare esp32dev/{framework}",
                file=sys.stderr,
            )
            return False

    common_dir = root / "tests" / "common"
    for idf_name, arduino_name in TARGET_PACKAGE_PAIRS:
        idf = (common_dir / idf_name).read_text(encoding="utf-8").replace(
            "type: esp-idf",
            "type: FRAMEWORK",
        )
        arduino = (common_dir / arduino_name).read_text(encoding="utf-8").replace(
            "type: arduino",
            "type: FRAMEWORK",
        )
        if idf != arduino:
            print(
                f"[FAIL] paired target packages differ beyond framework: "
                f"{idf_name}, {arduino_name}",
                file=sys.stderr,
            )
            return False

    fixtures = [*positive_fixtures, *negative_fixtures, *framework_fixtures]
    for fixture in fixtures:
        matches = TARGET_INCLUDE_PATTERN.findall(
            fixture.read_text(encoding="utf-8")
        )
        expected = expected_target_package(root, fixture)
        if matches != [expected]:
            print(
                f"[FAIL] target assignment {fixture.relative_to(root)}: "
                f"expected {[expected]}, got {matches}",
                file=sys.stderr,
            )
            return False


    fixture_by_relative = {
        fixture.relative_to(root).as_posix(): fixture for fixture in fixtures
    }
    for relative, expected_scenario in PAIRED_SCENARIOS.items():
        fixture = fixture_by_relative.get(relative)
        if fixture is None:
            print(f"[FAIL] missing paired fixture: {relative}", file=sys.stderr)
            return False
        matches = SCENARIO_INCLUDE_PATTERN.findall(
            fixture.read_text(encoding="utf-8")
        )
        if matches != [expected_scenario]:
            print(
                f"[FAIL] scenario assignment {relative}: "
                f"expected {[expected_scenario]}, got {matches}",
                file=sys.stderr,
            )
            return False

    print(
        f"[PASS] target matrix: {len(positive_fixtures)} ESP-IDF behavior "
        f"fixtures, {len(negative_fixtures)} ESP-IDF negative fixtures, "
        f"{len(framework_fixtures)} Arduino compile fixtures"
    )
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
    build_name = get_build_name(fixture)
    build_root = fixture.parent / ".esphome" / "build" / build_name
    compile_commands = build_root / "build" / "compile_commands.json"
    if not compile_commands.is_file():
        print(
            f"[FAIL] compile commands not found: "
            f"{compile_commands.relative_to(root)}",
            file=sys.stderr,
        )
        return False

    framework = expected_framework(root, fixture)
    try:
        command_entries = json.loads(compile_commands.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as err:
        print(
            f"[FAIL] cannot read compile commands for "
            f"{fixture.relative_to(root)}: {err}",
            file=sys.stderr,
        )
        return False

    pbhub_commands = []
    for entry in command_entries:
        source = str(entry.get("file", "")).replace("\\", "/")
        if not source.endswith("/esphome/components/pbhub/pbhub.cpp"):
            continue
        if isinstance(entry.get("command"), str):
            pbhub_commands.append(entry["command"])
        elif isinstance(entry.get("arguments"), list):
            pbhub_commands.append(" ".join(str(arg) for arg in entry["arguments"]))

    if len(pbhub_commands) != 1:
        print(
            f"[FAIL] expected one PBHUB compile command for "
            f"{fixture.relative_to(root)}, got {len(pbhub_commands)}",
            file=sys.stderr,
        )
        return False

    commands = pbhub_commands[0]
    missing_markers = [
        marker
        for marker in FRAMEWORK_BUILD_MARKERS[framework]
        if marker not in commands
    ]
    forbidden_markers = [
        marker
        for marker in FRAMEWORK_FORBIDDEN_MARKERS[framework]
        if marker in commands
    ]
    if missing_markers or forbidden_markers:
        print(
            f"[FAIL] framework evidence {fixture.relative_to(root)}: "
            f"missing {missing_markers}, forbidden {forbidden_markers}",
            file=sys.stderr,
        )
        return False
    print(f"[PASS] framework evidence {fixture.relative_to(root)}: {framework}")

    if fixture.name in CORE_ONLY_FIXTURES or fixture.name in FULL_SURFACE_FIXTURES:
        defines_file = build_root / "src" / "esphome" / "core" / "defines.h"
        if not defines_file.is_file():
            print(
                f"[FAIL] generated feature definitions not found: "
                f"{defines_file.relative_to(root)}",
                file=sys.stderr,
            )
            return False
        defines = defines_file.read_text(encoding="utf-8")
        if fixture.name in CORE_ONLY_FIXTURES:
            unexpected = [
                define for define in OPTIONAL_FEATURE_DEFINES if define in defines
            ]
            if unexpected:
                print(
                    f"[FAIL] core-only feature profile "
                    f"{fixture.relative_to(root)} enabled {unexpected}",
                    file=sys.stderr,
                )
                return False
            profile = "core-only"
        else:
            missing = [
                define for define in OPTIONAL_FEATURE_DEFINES if define not in defines
            ]
            if missing:
                print(
                    f"[FAIL] full-surface feature profile "
                    f"{fixture.relative_to(root)} missed {missing}",
                    file=sys.stderr,
                )
                return False
            profile = "full-surface"
        print(f"[PASS] feature profile {fixture.relative_to(root)}: {profile}")

    generated_check = GENERATED_SOURCE_CHECKS.get(fixture.name)
    if generated_check is None:
        return True

    expected_lines = generated_check
    source = build_root / "src" / "main.cpp"
    if not source.is_file():
        print(
            f"[FAIL] generated source not found: {source.relative_to(root)}",
            file=sys.stderr,
        )
        return False
    generated = source.read_text(encoding="utf-8")
    missing = [line for line in expected_lines if line not in generated]
    if missing:
        print(
            f"[FAIL] generated contract {fixture.relative_to(root)} "
            f"missed expected lines: {missing}",
            file=sys.stderr,
        )
        return False
    print(f"[PASS] generated contract {fixture.relative_to(root)}")
    return True


def check_unit_test(cxx, root, source, build_dir):
    executable = build_dir / source.stem
    compile_result = run(
        [
            cxx,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-I",
            str(root / "tests" / "unit" / "stubs"),
            "-I",
            str(root),
            str(source),
            "-o",
            str(executable),
        ],
        root,
    )
    compile_label = f"unit compile {source.relative_to(root)}"
    if compile_result.returncode != 0:
        report_failure(compile_label, compile_result)
        return False
    print(f"[PASS] {compile_label}")

    run_result = run([str(executable)], root)
    run_label = f"unit run {source.relative_to(root)}"
    if run_result.returncode != 0:
        report_failure(run_label, run_result)
        return False
    print(f"[PASS] {run_label}")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Validate PBHUB fixtures with ESPHome 2026.7.0"
    )
    parser.add_argument(
        "--compile",
        action="store_true",
        help="also compile the ESP-IDF behavior and paired-framework matrix",
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

    unit_sources = sorted((root / "tests" / "unit").glob("*_test.cpp"))
    if not unit_sources:
        print("[FAIL] no C++ unit tests found", file=sys.stderr)
        return 1
    configured_cxx = os.environ.get("CXX", "c++")
    cxx = shutil.which(configured_cxx)
    if cxx is None:
        print(f"[FAIL] C++ compiler not found: {configured_cxx}", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="pbhub-unit-") as temp_dir:
        build_dir = Path(temp_dir)
        for source in unit_sources:
            passed = check_unit_test(cxx, root, source, build_dir) and passed

    positive_dir = root / "tests" / "positive"
    positive_fixtures = sorted(positive_dir.glob("*.yaml"))
    if not positive_fixtures:
        print("[FAIL] no positive YAML fixtures found", file=sys.stderr)
        return 1
    framework_dir = root / "tests" / "framework"
    framework_fixtures = sorted(framework_dir.glob("*.yaml"))
    if not framework_fixtures:
        print("[FAIL] no framework YAML fixtures found", file=sys.stderr)
        return 1

    negative_dir = root / "tests" / "negative"
    negative_fixtures = sorted(negative_dir.glob("*.yaml"))
    if not negative_fixtures:
        print("[FAIL] no negative YAML fixtures found", file=sys.stderr)
        return 1
    if not check_target_matrix(
        root,
        positive_fixtures,
        negative_fixtures,
        framework_fixtures,
    ):
        return 1

    for fixture in [*positive_fixtures, *framework_fixtures]:
        passed = check_positive(esphome, root, fixture) and passed

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
            [*positive_fixtures, *framework_fixtures],
            key=lambda fixture: (
                "hub-only" not in fixture.name,
                expected_framework(root, fixture) != "esp-idf",
                fixture.name,
            ),
        )
        for fixture in compile_fixtures:
            passed = compile_fixture(esphome, root, fixture) and passed

    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
