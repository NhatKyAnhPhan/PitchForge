#!/usr/bin/env python3
"""Exercise a real Auralis C ABI library; this test never substitutes a mock."""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import platform
import statistics
import sys
import threading
import time
from pathlib import Path
from typing import Callable


STATUS_OK = 0
STATUS_INVALID_ARGUMENT = 1
STATUS_QUEUE_FULL = 4
MAX_EFFECTS = 16
MAX_PARAMETERS = 16
EFFECT_NAMES = (
    "noise_gate",
    "equalizer",
    "de_esser",
    "compressor",
    "pitch_correction",
    "saturation",
    "reverb",
    "delay",
    "limiter",
)
DEFAULT_PARAMETER_EFFECT = 1  # Equalizer
DEFAULT_PARAMETER_ID = 1  # Equalizer::Gain in ParameterIds.h


class AudioConfig(ctypes.Structure):
    _fields_ = [
        ("sample_rate", ctypes.c_double),
        ("buffer_size", ctypes.c_uint32),
        ("input_channels", ctypes.c_uint32),
        ("output_channels", ctypes.c_uint32),
    ]


class DeviceInfo(ctypes.Structure):
    _fields_ = [
        ("index", ctypes.c_uint32),
        ("name", ctypes.c_char * 192),
        ("input_channels", ctypes.c_uint32),
        ("output_channels", ctypes.c_uint32),
        ("is_default", ctypes.c_uint32),
    ]


class Meters(ctypes.Structure):
    _fields_ = [
        ("input_peak_left", ctypes.c_float),
        ("input_peak_right", ctypes.c_float),
        ("input_rms_left", ctypes.c_float),
        ("input_rms_right", ctypes.c_float),
        ("output_peak_left", ctypes.c_float),
        ("output_peak_right", ctypes.c_float),
        ("output_rms_left", ctypes.c_float),
        ("output_rms_right", ctypes.c_float),
        ("clipping", ctypes.c_uint32),
        ("estimated_latency_ms", ctypes.c_double),
    ]


class EffectState(ctypes.Structure):
    _fields_ = [
        ("instance_id", ctypes.c_uint32),
        ("type", ctypes.c_int),
        ("enabled", ctypes.c_uint32),
        ("wet", ctypes.c_float),
        ("parameter_count", ctypes.c_uint32),
        ("parameter_ids", ctypes.c_uint16 * MAX_PARAMETERS),
        ("parameter_values", ctypes.c_float * MAX_PARAMETERS),
    ]


class ChainState(ctypes.Structure):
    _fields_ = [
        ("effect_count", ctypes.c_uint32),
        ("effects", EffectState * MAX_EFFECTS),
    ]


class NativeApi:
    """Loads and binds the exported C API from a real shared library."""

    REQUIRED_SYMBOLS = (
        "auralis_api_version",
        "auralis_engine_create",
        "auralis_engine_destroy",
        "auralis_engine_start",
        "auralis_engine_stop",
        "auralis_engine_is_running",
        "auralis_engine_get_meters",
        "auralis_device_count",
        "auralis_device_get_info",
        "auralis_engine_select_device",
        "auralis_engine_effect_add",
        "auralis_engine_effect_remove",
        "auralis_engine_effect_move",
        "auralis_engine_effect_set_enabled",
        "auralis_engine_effect_set_wet",
        "auralis_engine_effect_set_parameter",
        "auralis_engine_get_chain_state",
    )

    def __init__(self, library_path: Path) -> None:
        if not library_path.is_file():
            raise FileNotFoundError(f"real engine library not found: {library_path}")
        self.library_path = library_path.resolve()
        self.library = ctypes.CDLL(str(self.library_path))
        missing = [
            name
            for name in self.REQUIRED_SYMBOLS
            if not hasattr(self.library, name)
        ]
        if missing:
            raise RuntimeError(
                "library is missing required Auralis C ABI exports: "
                + ", ".join(missing)
            )

        self.api_version = self._bind("auralis_api_version", [], ctypes.c_uint32)
        self.engine_create = self._bind(
            "auralis_engine_create", [], ctypes.c_void_p
        )
        self.engine_destroy = self._bind(
            "auralis_engine_destroy", [ctypes.c_void_p], None
        )
        self.engine_start = self._bind(
            "auralis_engine_start",
            [ctypes.c_void_p, ctypes.POINTER(AudioConfig)],
            ctypes.c_int,
        )
        self.engine_stop = self._bind(
            "auralis_engine_stop", [ctypes.c_void_p], None
        )
        self.engine_is_running = self._bind(
            "auralis_engine_is_running", [ctypes.c_void_p], ctypes.c_uint32
        )
        self.engine_get_meters = self._bind(
            "auralis_engine_get_meters",
            [ctypes.c_void_p, ctypes.POINTER(Meters)],
            ctypes.c_int,
        )
        self.device_count = self._bind(
            "auralis_device_count", [ctypes.c_uint32], ctypes.c_uint32
        )
        self.device_get_info = self._bind(
            "auralis_device_get_info",
            [ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(DeviceInfo)],
            ctypes.c_int,
        )
        self.engine_select_device = self._bind(
            "auralis_engine_select_device",
            [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32],
            ctypes.c_int,
        )
        self.effect_add = self._bind(
            "auralis_engine_effect_add",
            [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32, ctypes.c_uint32],
            ctypes.c_int,
        )
        self.effect_remove = self._bind(
            "auralis_engine_effect_remove",
            [ctypes.c_void_p, ctypes.c_uint32],
            ctypes.c_int,
        )
        self.effect_move = self._bind(
            "auralis_engine_effect_move",
            [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32],
            ctypes.c_int,
        )
        self.effect_set_enabled = self._bind(
            "auralis_engine_effect_set_enabled",
            [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32],
            ctypes.c_int,
        )
        self.effect_set_wet = self._bind(
            "auralis_engine_effect_set_wet",
            [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_float],
            ctypes.c_int,
        )
        self.effect_set_parameter = self._bind(
            "auralis_engine_effect_set_parameter",
            [
                ctypes.c_void_p,
                ctypes.c_uint32,
                ctypes.c_uint16,
                ctypes.c_float,
            ],
            ctypes.c_int,
        )
        self.get_chain_state = self._bind(
            "auralis_engine_get_chain_state",
            [ctypes.c_void_p, ctypes.POINTER(ChainState)],
            ctypes.c_int,
        )

    def _bind(
        self, name: str, argtypes: list[object], restype: object
    ) -> object:
        function = getattr(self.library, name)
        function.argtypes = argtypes
        function.restype = restype
        return function


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check_abi_layout() -> dict[str, int]:
    expected_sizes = {
        "AuralisAudioConfig": 24,
        "AuralisDeviceInfo": 208,
        "AuralisMeters": 48,
        "AuralisEffectState": 116,
        "AuralisChainState": 1860,
    }
    actual_sizes = {
        "AuralisAudioConfig": ctypes.sizeof(AudioConfig),
        "AuralisDeviceInfo": ctypes.sizeof(DeviceInfo),
        "AuralisMeters": ctypes.sizeof(Meters),
        "AuralisEffectState": ctypes.sizeof(EffectState),
        "AuralisChainState": ctypes.sizeof(ChainState),
    }
    require(
        actual_sizes == expected_sizes,
        f"C ABI struct sizes differ: expected {expected_sizes}, got {actual_sizes}",
    )
    return actual_sizes


def check_api_version(api: NativeApi) -> int:
    version = int(api.api_version())
    require(version == 1, f"expected API version 1, got {version}")
    return version


def check_null_validation(api: NativeApi) -> None:
    config = AudioConfig(48000.0, 256, 1, 2)
    meters = Meters()
    chain = ChainState()
    device = DeviceInfo()
    engine = api.engine_create()
    require(bool(engine), "engine_create returned a null handle")
    try:
        checks = {
            "start(null engine)": api.engine_start(None, ctypes.byref(config)),
            "start(null config)": api.engine_start(engine, None),
            "meters(null engine)": api.engine_get_meters(None, ctypes.byref(meters)),
            "meters(null destination)": api.engine_get_meters(engine, None),
            "device info(null destination)": api.device_get_info(
                0, 0, None
            ),
            "select device(null engine)": api.engine_select_device(None, 0, 0),
            "effect add(null engine)": api.effect_add(None, 1, 1, 0),
            "effect remove(null engine)": api.effect_remove(None, 1),
            "effect move(null engine)": api.effect_move(None, 1, 0),
            "set enabled(null engine)": api.effect_set_enabled(None, 1, 1),
            "set wet(null engine)": api.effect_set_wet(None, 1, 0.5),
            "set parameter(null engine)": api.effect_set_parameter(
                None, 1, 0, 0.5
            ),
            "chain snapshot(null engine)": api.get_chain_state(
                None, ctypes.byref(chain)
            ),
            "chain snapshot(null destination)": api.get_chain_state(engine, None),
        }
        for name, status in checks.items():
            require(
                status == STATUS_INVALID_ARGUMENT,
                f"{name} returned status {status}, expected INVALID_ARGUMENT",
            )
    finally:
        api.engine_destroy(engine)


def check_device_enumeration(api: NativeApi) -> dict[str, int]:
    counts: dict[str, int] = {}
    for for_input, label in ((1, "input"), (0, "output")):
        count = int(api.device_count(for_input))
        require(count <= 4096, f"implausible {label} device count: {count}")
        counts[label] = count
        for index in range(count):
            info = DeviceInfo()
            status = api.device_get_info(for_input, index, ctypes.byref(info))
            require(
                status == STATUS_OK,
                f"{label} device {index} query returned status {status}",
            )
            require(info.index == index, f"{label} device index mismatch")
            require(info.is_default in (0, 1), f"{label} default flag is not boolean")
            raw_name = ctypes.string_at(
                ctypes.addressof(info) + DeviceInfo.name.offset, 192
            )
            require(
                b"\0" in raw_name,
                f"{label} device {index} name is not NUL terminated",
            )
        invalid = DeviceInfo()
        status = api.device_get_info(for_input, count, ctypes.byref(invalid))
        require(
            status == STATUS_INVALID_ARGUMENT,
            f"out-of-range {label} device index returned status {status}",
        )
    return counts


def snapshot(api: NativeApi, engine: int) -> list[EffectState]:
    state = ChainState()
    status = api.get_chain_state(engine, ctypes.byref(state))
    require(status == STATUS_OK, f"chain snapshot returned status {status}")
    require(
        state.effect_count <= MAX_EFFECTS,
        f"snapshot effect count exceeds {MAX_EFFECTS}: {state.effect_count}",
    )
    effects = [state.effects[i] for i in range(state.effect_count)]
    for effect in effects:
        require(
            effect.parameter_count <= MAX_PARAMETERS,
            f"effect {effect.instance_id} parameter count exceeds {MAX_PARAMETERS}",
        )
    return effects


def wait_until(
    predicate: Callable[[], bool], message: str, timeout_seconds: float = 8.0
) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise AssertionError(f"timed out waiting for {message}")


def find_effect(effects: list[EffectState], instance_id: int) -> EffectState | None:
    return next((effect for effect in effects if effect.instance_id == instance_id), None)


def check_audio_chain_and_stress(api: NativeApi, args: argparse.Namespace) -> dict:
    engine = api.engine_create()
    require(bool(engine), "engine_create returned a null handle")
    config = AudioConfig(
        args.sample_rate,
        args.buffer_size,
        args.input_channels,
        args.output_channels,
    )
    metrics: dict[str, object] = {
        "configuration": {
            "sample_rate": args.sample_rate,
            "buffer_size": args.buffer_size,
            "input_channels": args.input_channels,
            "output_channels": args.output_channels,
        }
    }
    try:
        for device_index, is_input in (
            (args.input_device, 1),
            (args.output_device, 0),
        ):
            if device_index is not None:
                status = api.engine_select_device(engine, is_input, device_index)
                require(
                    status == STATUS_OK,
                    f"select {'input' if is_input else 'output'} device "
                    f"{device_index} returned status {status}",
                )

        status = api.engine_start(engine, ctypes.byref(config))
        require(status == STATUS_OK, f"engine_start returned status {status}")
        require(api.engine_is_running(engine) == 1, "engine did not enter running state")

        meters = Meters()
        status = api.engine_get_meters(engine, ctypes.byref(meters))
        require(status == STATUS_OK, f"meter snapshot returned status {status}")
        meter_values = [
            meters.input_peak_left,
            meters.input_peak_right,
            meters.input_rms_left,
            meters.input_rms_right,
            meters.output_peak_left,
            meters.output_peak_right,
            meters.output_rms_left,
            meters.output_rms_right,
            meters.estimated_latency_ms,
        ]
        require(
            all(math.isfinite(value) for value in meter_values),
            "meter snapshot contains a non-finite value",
        )

        # Use distinct instance IDs so a failure cannot be hidden by an existing
        # effect with the same identity.
        primary_id = 0x7F000001
        secondary_id = 0x7F000002
        require(
            all(
                effect.instance_id not in (primary_id, secondary_id)
                for effect in snapshot(api, engine)
            ),
            "test instance ID already exists in the new engine",
        )
        primary_type = args.parameter_effect
        secondary_type = (
            0 if primary_type != 0 else 1
        )  # noise gate / EQ are only used as a second chain node

        status = api.effect_add(engine, primary_type, primary_id, 0)
        require(status == STATUS_OK, f"first effect add returned status {status}")
        status = api.effect_add(engine, secondary_type, secondary_id, 1)
        require(status == STATUS_OK, f"second effect add returned status {status}")
        wait_until(
            lambda: [item.instance_id for item in snapshot(api, engine)]
            == [primary_id, secondary_id],
            "two queued effects to appear in order",
        )

        for label, status in (
            ("wet/dry", api.effect_set_wet(engine, primary_id, 0.375)),
            ("bypass", api.effect_set_enabled(engine, primary_id, 0)),
            (
                "parameter",
                api.effect_set_parameter(
                    engine,
                    primary_id,
                    args.parameter_id,
                    args.parameter_value,
                ),
            ),
        ):
            require(status == STATUS_OK, f"set {label} returned status {status}")

        def primary_values_applied() -> bool:
            effect = find_effect(snapshot(api, engine), primary_id)
            if effect is None or effect.enabled != 0 or abs(effect.wet - 0.375) > 1e-4:
                return False
            for index in range(effect.parameter_count):
                if effect.parameter_ids[index] == args.parameter_id:
                    return abs(
                        effect.parameter_values[index] - args.parameter_value
                    ) <= 1e-4
            return False

        wait_until(primary_values_applied, "wet, bypass, and parameter updates")

        status = api.effect_move(engine, secondary_id, 0)
        require(status == STATUS_OK, f"effect move returned status {status}")
        wait_until(
            lambda: [item.instance_id for item in snapshot(api, engine)]
            == [secondary_id, primary_id],
            "effect reorder",
        )
        status = api.effect_remove(engine, primary_id)
        require(status == STATUS_OK, f"effect remove returned status {status}")
        wait_until(
            lambda: [item.instance_id for item in snapshot(api, engine)]
            == [secondary_id],
            "effect removal",
        )

        call_latencies_ns: list[int] = []
        status_counts = {STATUS_OK: 0, STATUS_QUEUE_FULL: 0}
        failures: list[str] = []
        lock = threading.Lock()

        def enqueue_stress(worker_index: int) -> None:
            local_latencies: list[int] = []
            local_counts = {STATUS_OK: 0, STATUS_QUEUE_FULL: 0}
            for call_index in range(args.stress_calls):
                wet = ((worker_index * args.stress_calls + call_index) % 100) / 99.0
                started = time.perf_counter_ns()
                result = int(api.effect_set_wet(engine, secondary_id, wet))
                local_latencies.append(time.perf_counter_ns() - started)
                if result not in local_counts:
                    with lock:
                        failures.append(
                            f"stress command returned unexpected status {result}"
                        )
                    continue
                local_counts[result] += 1
            with lock:
                call_latencies_ns.extend(local_latencies)
                for result, count in local_counts.items():
                    status_counts[result] += count

        workers = [
            threading.Thread(target=enqueue_stress, args=(index,))
            for index in range(args.stress_threads)
        ]
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join()
        require(not failures, "; ".join(failures))
        expected_calls = args.stress_threads * args.stress_calls
        require(
            len(call_latencies_ns) == expected_calls,
            f"stress completed {len(call_latencies_ns)} of {expected_calls} calls",
        )
        sorted_latencies = sorted(call_latencies_ns)
        p99_index = min(
            len(sorted_latencies) - 1,
            math.ceil(len(sorted_latencies) * 0.99) - 1,
        )
        metrics["concurrent_control_stress"] = {
            "threads": args.stress_threads,
            "calls_per_thread": args.stress_calls,
            "status_ok": status_counts[STATUS_OK],
            "status_queue_full": status_counts[STATUS_QUEUE_FULL],
            "call_latency_us_mean": round(statistics.mean(call_latencies_ns) / 1000, 2),
            "call_latency_us_p99": round(sorted_latencies[p99_index] / 1000, 2),
            "call_latency_us_max": round(max(call_latencies_ns) / 1000, 2),
        }
        return metrics
    finally:
        api.engine_stop(engine)
        api.engine_destroy(engine)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run C ABI checks against a real built Auralis library. Live chain "
            "and callback stress require explicit --audio opt-in."
        )
    )
    parser.add_argument(
        "--library",
        type=Path,
        required=True,
        help="path to the real engine DLL/shared library (no mock fallback)",
    )
    parser.add_argument(
        "--audio",
        action="store_true",
        help="open an actual audio stream and run chain/parameter/stress checks",
    )
    parser.add_argument("--sample-rate", type=float, default=48000.0)
    parser.add_argument("--buffer-size", type=int, default=256)
    parser.add_argument("--input-channels", type=int, default=1)
    parser.add_argument("--output-channels", type=int, default=2)
    parser.add_argument("--input-device", type=int)
    parser.add_argument("--output-device", type=int)
    parser.add_argument(
        "--parameter-effect",
        type=int,
        choices=range(len(EFFECT_NAMES)),
        default=DEFAULT_PARAMETER_EFFECT,
        help="effect type whose supported parameter is exercised (default: equalizer)",
    )
    parser.add_argument(
        "--parameter-id",
        type=int,
        choices=range(0x10000),
        default=DEFAULT_PARAMETER_ID,
        help="effect-specific ID (default: Equalizer::Gain from ParameterIds.h)",
    )
    parser.add_argument("--parameter-value", type=float, default=0.5)
    parser.add_argument("--stress-threads", type=int, default=4)
    parser.add_argument("--stress-calls", type=int, default=250)
    parser.add_argument(
        "--json-report",
        type=Path,
        help="optional destination for machine-readable results",
    )
    args = parser.parse_args()
    if args.audio:
        if (
            args.buffer_size <= 0
            or not math.isfinite(args.sample_rate)
            or args.sample_rate <= 0
        ):
            parser.error("sample rate and buffer size must be positive")
        if args.input_channels <= 0 or args.output_channels <= 0:
            parser.error("input and output channel counts must be positive")
        if args.stress_threads <= 0 or args.stress_calls <= 0:
            parser.error("stress thread and call counts must be positive")
        if not 0.0 <= args.parameter_value <= 1.0:
            parser.error("parameter value must be normalized to [0, 1]")
    return args


def main() -> int:
    args = parse_args()
    results: dict[str, object] = {
        "os": platform.platform(),
        "python": platform.python_version(),
        "library": str(args.library.resolve()),
        "audio_stream_requested": args.audio,
        "checks": [],
        "blockers": [],
    }

    try:
        api = NativeApi(args.library)
        results["api_version"] = check_api_version(api)
        results["abi_layout"] = check_abi_layout()
        check_null_validation(api)
        device_counts = check_device_enumeration(api)
        results["device_counts"] = device_counts
        results["checks"] = [
            "required exports and real library load",
            "API version",
            "fixed C ABI structure sizes",
            "null-pointer status validation",
            "device enumeration and out-of-range indices",
        ]
        print("PASS: real library loaded; API version and fixed layouts validated")
        print("PASS: pointer/index validation and device enumeration")

        if not args.audio:
            results["blockers"].append(
                "Live chain/parameter/callback stress not run; pass --audio to "
                "explicitly open a hardware stream."
            )
            print(
                "BLOCKED: chain, parameter, and callback stress checks require "
                "--audio; this is not a full integration pass."
            )
            exit_code = 2
        else:
            audio_metrics = check_audio_chain_and_stress(api, args)
            audio_metrics["parameter_effect"] = EFFECT_NAMES[args.parameter_effect]
            audio_metrics["parameter_id"] = args.parameter_id
            results["audio_integration"] = audio_metrics
            results["checks"].append(
                "live engine chain add/move/remove, bypass, wet/dry, parameter, "
                "concurrent command stress"
            )
            results["blockers"].extend(
                [
                    "Preset save/load has no C ABI entry points in the current contract.",
                    "Stress records control-call latency, not audio callback duration, "
                    "allocations, underruns, or physical round-trip latency.",
                ]
            )
            print("PASS: live chain, parameter, and concurrent command stress")
            print(
                "BLOCKED: preset round-trip and direct callback timing/allocation "
                "instrumentation are not exposed by the current public API."
            )
            print(
                "NOTE: reported stress latency is control-call latency, not callback "
                "performance."
            )
            exit_code = 0
    except (AssertionError, FileNotFoundError, OSError, RuntimeError) as error:
        results["failure"] = str(error)
        print(f"FAIL: {error}", file=sys.stderr)
        exit_code = 1

    results["exit_code"] = exit_code
    if args.json_report:
        args.json_report.parent.mkdir(parents=True, exist_ok=True)
        args.json_report.write_text(
            json.dumps(results, indent=2) + os.linesep, encoding="utf-8"
        )
        print(f"Report: {args.json_report}")
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
