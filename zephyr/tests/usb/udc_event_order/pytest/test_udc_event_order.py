# SPDX-License-Identifier: Apache-2.0

import time

import pytest
from twister_harness import Shell


def _expect_ok(shell: Shell, command: str, expected: str = "OK") -> None:
    output = shell.exec_command(command)
    assert any(expected in line for line in output), f"{command!r} failed: {output}"


def _assert_events(shell: Shell, *expected: str) -> None:
    command = "events assert " + " ".join(expected)
    output = shell.exec_command(command)
    assert any("PASS" in line for line in output), f"{command!r} failed: {output}"


def _wait_for_vbus(shell: Shell, present: bool) -> None:
    expected = f"VBUS={int(present)}"
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        output = shell.exec_command("events status")
        if any(expected in line for line in output):
            return
        time.sleep(0.05)
    raise AssertionError(f"Device did not report {expected}")


def test_vbus_and_bus_state_events(shells: list[Shell]) -> None:
    assert len(shells) == 2, "Expected the Nucleo H755 host and nRF52840 device"
    host, device = shells

    _expect_ok(host, "host init")
    _expect_ok(host, "host vbus off")
    _wait_for_vbus(device, False)
    time.sleep(0.1)
    _expect_ok(device, "events clear")
    _wait_for_vbus(device, False)

    _expect_ok(host, "host vbus on")
    _wait_for_vbus(device, True)
    _assert_events(device, "vbus_ready")

    _expect_ok(host, "host reset")
    _assert_events(device, "vbus_ready", "reset")

    _expect_ok(device, "events clear")
    _expect_ok(host, "host suspend")
    _assert_events(device, "suspend")
    _expect_ok(host, "host resume")
    _assert_events(device, "suspend", "resume")

    _expect_ok(device, "events clear")
    _expect_ok(host, "host vbus off")
    _assert_events(device, "vbus_removed")

    _expect_ok(device, "events clear")
    _expect_ok(host, "host vbus on")
    _expect_ok(host, "host reset")
    _expect_ok(host, "host suspend")
    _expect_ok(host, "host vbus off")
    _assert_events(device, "vbus_ready", "reset", "suspend", "vbus_removed")

    for _ in range(3):
        _expect_ok(device, "events clear")
        _expect_ok(host, "host vbus on")
        _expect_ok(host, "host reset")
        _expect_ok(host, "host vbus off")
        _assert_events(device, "vbus_ready", "reset", "vbus_removed")