# SPDX-License-Identifier: MIT
from sys import path
from west.commands import WestCommand
from west import log
from west.util import west_topdir
from pathlib import Path
from textwrap import dedent
import json
import yaml
import subprocess
import re

class WestMcux(WestCommand):
    def __init__(self):
        super().__init__(
            'mcux',
            'generate MCUXpresso for VSCode project files',
            dedent('''
            This command generates VSCode project files for MCUXpresso projects.
            ''')
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name, help=self.help, description=self.description)
        parser.add_argument(
            '-d', '--project-dir', required=True, metavar='DIR',
            help='Project directory of the MCUXpresso project')
        parser.add_argument(
            '-b', '--board', required=True, metavar='BOARD',
            help='Name of the board to use for the project.')
        return parser

    def do_run(self, args, unknown_args):
        project_dir = Path(args.project_dir).resolve()

        # create the .vscode directory in the project directory
        vscode_dotdir = project_dir / '.vscode'
        vscode_dotdir.mkdir(parents=True, exist_ok=True)

        # use git describe in manifest_int to find the git tag of the release
        manifest_int_path = Path(west_topdir()) / 'mcuxsdk' / 'manifest_int'
        try:
            git_tag = subprocess.check_output(
                ['git', 'describe', '--tags'],
                cwd=manifest_int_path,
                stderr=subprocess.DEVNULL,
                text=True
            ).strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            git_tag = "unknown"

        # find out the device id from mcuxsdk/examples/_boards/{board}/variable.cmake
        variable_cmake_path = Path(west_topdir()) / 'mcuxsdk' / 'examples' / '_boards' / args.board / 'variable.cmake'
        if not variable_cmake_path.exists():
            log.die(f"variable.cmake not found for board {args.board} in {variable_cmake_path}.")

        device_regex = re.compile(r'\(device\s+([A-Z0-9]+)\)')
        soc_series_regex = re.compile(r'\(soc_series\s+([A-Z0-9]+)\)')
        device_id = None
        soc_series = None
        with open(variable_cmake_path, 'r') as f:
            content = f.read()
            match = device_regex.search(content)
            if match:
                device_id = match.group(1)
            match = soc_series_regex.search(content)
            if match:
                soc_series = match.group(1)
        if not device_id:
            log.die(f"Could not find device id in {variable_cmake_path}.")
        if not soc_series:
            log.die(f"Could not find soc_series in {variable_cmake_path}.")

        log.inf(f"Found device id: {device_id}, soc_series: {soc_series} for board {args.board}.")
        # find subdirectory under mcuxsdk/devices/{soc_series}/{device_id}
        devices_dir = Path(west_topdir()) / 'mcuxsdk' / 'devices'
        device_dir = None
        for subdir in devices_dir.iterdir():
            if subdir.is_dir():
                for subsubdir in subdir.iterdir():
                    if subsubdir.is_dir() and soc_series in subsubdir.name:
                        device_subdir = subsubdir / device_id
                        if device_subdir.exists() and device_subdir.is_dir():
                            device_dir = device_subdir
                            break
                if device_dir:
                    break
        if not device_dir:
            log.die(f"Could not find device directory for device id {device_id} in {devices_dir}.")

        # open and parse chip.yml in device_dir to find the core id
        chip_yml_path = device_dir / 'chip.yml'
        if not chip_yml_path.exists():
            log.die(f"chip.yml not found in {device_dir}.")
        with open(chip_yml_path, 'r') as f:
            chip_yml = yaml.safe_load(f)
            core_id = chip_yml.get('device.hardware_data', {}).get('contents', {}).get('devices', [])[0].get('core', [])[0].get('id', None)
        if not core_id:
            log.die(f"Could not find core id in {chip_yml_path}.")

        # create mcuxpresso-tools.json
        mcuxpresso_tools_json_path = vscode_dotdir / 'mcuxpresso-tools.json'
        mcuxpresso_tools_json = {
            "version": f"{git_tag}",
            "toolchainPath": "$env{ARM_GCC_DIR}",
            "linkedProjects": [],
            "trustZoneType": "none",
            "multicoreType": "none",
            "projectType": "sdk-v2-repository",
            "sdk": {
                "version": f"{git_tag}",
                "path": f"{west_topdir()}",
                "boardId": f"{args.board}",
                "deviceId": f"{device_id}",
                "coreId": f"{core_id}"
            }
        }
        with open(mcuxpresso_tools_json_path, 'w') as f:
            json.dump(mcuxpresso_tools_json, f, indent=4)

        # generate settings.json
        settings_json_path = vscode_dotdir / 'settings.json'
        settings_json = {
            "cmake.configureOnOpen": False,
            "C_Cpp.errorSquiggles": "disabled",
            "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
            "cmake.useCMakePresets": "always",
            "cmake.buildTask": True,
            "cmake.skipConfigureIfCachePresent": True,
            "cmake.sourceDirectory": "${workspaceFolder}",
            "mcuxpresso.toolchainCompatibilityRecommendations": False
        }
        with open(settings_json_path, 'w') as f:
            json.dump(settings_json, f, indent=4)

        # generate tasks.json
        tasks_json_path = vscode_dotdir / 'tasks.json'
        tasks_json = {
            "version": "2.0.0",
            "tasks": [
                {
                "type": "cmake",
                "label": "CMake: build",
                "command": "build",
                "targets": [
                    "all"
                ],
                "group": {
                    "kind": "build",
                    "isDefault": True
                },
                "problemMatcher": [
                    "$gcc"
                ],
                "detail": "CMake template build task",
                "preset": "release"
                },
                {
                "type": "cmake",
                "label": "CMake: clean",
                "command": "clean",
                "problemMatcher": [
                    "$gcc"
                ],
                "detail": "CMake template clean task",
                "preset": "release"
                },
                {
                "type": "cmake",
                "label": "CMake: configure",
                "command": "configure",
                "problemMatcher": [
                    "$gcc"
                ],
                "detail": "CMake template configure task",
                "preset": "release"
                }
            ]
        }
        with open(tasks_json_path, 'w') as f:
            json.dump(tasks_json, f, indent=4)

        # generate mcux_include.json
        mcux_include_json_path = project_dir / 'mcux_include.json'
        mcux_include_json = {
            "version": 7,
            "cmakeMinimumRequired": {
                "major": 3
            },
            "configurePresets": [
                {
                    "name": "shared-env",
                    "hidden": True,
                    "environment": {
                        "ARMGCC_DIR": "$env{ARM_GCC_DIR}",
                        "SdkRootDirPath": f"{west_topdir()}",
                        "BOARD": f"{args.board}",
                        "MCUX_VENV_PATH": "",
                        "PATH": "$env{MCUX_VENV_PATH}${pathListSep}$penv{PATH}",
                        "POSTPROCESS_UTILITY": "",
                        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
                    },
                    "cacheVariables": {
                        "CPM_SOURCE_CACHE": "$env{SdkRootDirPath}/cpm"
                    }
                },
                {
                    "name": "debug-env",
                    "displayName": "debug-env",
                    "hidden": True,
                    "inherits": [
                        "shared-env"
                    ]
                },
                {
                    "name": "release-env",
                    "displayName": "release-env",
                    "hidden": True,
                    "inherits": [
                        "shared-env"
                    ]
                }
            ],
            "buildPresets": []
        }
        with open(mcux_include_json_path, 'w') as f:
            json.dump(mcux_include_json, f, indent=4)

        # generate CMakePresets.json
        cmake_presets_json_path = project_dir / 'CMakePresets.json'
        cmake_presets_json = {
            "version": 7,
            "cmakeMinimumRequired": {
                "major": 3
            },
            "configurePresets": [
                {
                    "name": "debug",
                    "displayName": "debug",
                    "generator": "Ninja",
                    "binaryDir": "${fileDir}/build/${presetName}",
                    "toolchainFile": "$env{SdkRootDirPath}/mcuxsdk/cmake/toolchain/armgcc.cmake",
                    "cacheVariables": {
                        "APP_DIR": {
                        "value": "${fileDir}",
                        "type": "PATH"
                        },
                        "CMAKE_BUILD_TYPE": "debug",
                        "SdkRootDirPath": "$env{SdkRootDirPath}/mcuxsdk",
                        "CONFIG_TOOLCHAIN": "armgcc",
                        "board": "$env{BOARD}",
                        "CMAKE_RUNTIME_OUTPUT_DIRECTORY": "$env{binaryDir}",
                        "CMAKE_LIBRARY_OUTPUT_DIRECTORY": "$env{binaryDir}",
                        "CMAKE_ARCHIVE_OUTPUT_DIRECTORY": "$env{binaryDir}"
                    },
                    "inherits": [
                        "debug-env"
                    ]
                },
                {
                    "name": "release",
                    "displayName": "release",
                    "generator": "Ninja",
                    "binaryDir": "${fileDir}/build/${presetName}",
                    "toolchainFile": "$env{SdkRootDirPath}/mcuxsdk/cmake/toolchain/armgcc.cmake",
                    "cacheVariables": {
                        "APP_DIR": {
                        "value": "${fileDir}",
                        "type": "PATH"
                        },
                        "CMAKE_BUILD_TYPE": "release",
                        "SdkRootDirPath": "$env{SdkRootDirPath}/mcuxsdk",
                        "CONFIG_TOOLCHAIN": "armgcc",
                        "board": "$env{BOARD}",
                        "CMAKE_RUNTIME_OUTPUT_DIRECTORY": "$env{binaryDir}",
                        "CMAKE_LIBRARY_OUTPUT_DIRECTORY": "$env{binaryDir}",
                        "CMAKE_ARCHIVE_OUTPUT_DIRECTORY": "$env{binaryDir}"
                    },
                    "inherits": [
                        "release-env"
                    ]
                }
            ],
            "buildPresets": [
                {
                    "name": "debug",
                    "displayName": "debug",
                    "configurePreset": "debug"
                },
                {
                    "name": "release",
                    "displayName": "release",
                    "configurePreset": "release"
                }
            ],
            "include": [
                "mcux_include.json"
            ]
        }
        with open(cmake_presets_json_path, 'w') as f:
            json.dump(cmake_presets_json, f, indent=4)

        log.inf(f"Project configuration files generated in {project_dir}.")
