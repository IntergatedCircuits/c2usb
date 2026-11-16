# SPDX-License-Identifier: Apache-2.0

from twister_harness import Shell

_STATE_CODES = {
    "L0_ON": "0",
    "L1_SLEEP": "1",
    "L2_SUSPEND": "2",
    "L3_OFF": "3",
}


def _expect_ok(shell: Shell, command: str, expected: str = "OK") -> None:
    output = shell.exec_command(command)
    assert any(expected in line for line in output), f"{command!r} failed: {output}"


def _assert_states(shell: Shell, *expected: str) -> None:
    command = "power assert " + " ".join(_STATE_CODES[state] for state in expected)
    output = shell.exec_command(command, timeout=10.0)
    assert any("PASS" in line for line in output), f"{command!r} failed: {output}"


def test_c2usb_power_state_transitions(shells: list[Shell]) -> None:
    assert len(shells) == 2, "Expected the Nucleo H755 host and nRF52840 c2usb device"
    host, device = shells

    _expect_ok(host, "host init")
    _expect_ok(device, "power state", "state=L3_OFF")
    _expect_ok(device, "power clear")

    for _ in range(3):
        _expect_ok(device, "power clear")
        _expect_ok(host, "host vbus on")
        _expect_ok(host, "host vbus off")
        _assert_states(device, "L2_SUSPEND", "L0_ON", "L3_OFF")