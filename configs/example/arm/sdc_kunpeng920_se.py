# Copyright 2026
# Licensed under the Apache License, Version 2.0

"""ARM64/Kunpeng920-like SE runner for SDC differential execution."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import m5
from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_cache_hierarchy import (
    PrivateL1PrivateL2CacheHierarchy,
)
from gem5.components.memory import DualChannelDDR4_2400, SingleChannelDDR4_2400
from gem5.components.processors.cpu_types import (
    get_cpu_type_from_str,
    get_cpu_types_str_set,
)
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import Simulator

from sdc_fault_request import build_fault_request, write_fault_request


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run an ARM64 static ELF in gem5 SE mode using a Kunpeng920-like "
            "cache/memory profile and record any requested fault metadata."
        )
    )
    parser.add_argument("binary", type=Path, help="Path to the ARM64 ELF to run.")
    parser.add_argument(
        "--binary-arg",
        action="append",
        default=[],
        help="Argument passed to the guest binary. Repeat as needed.",
    )
    parser.add_argument(
        "--cpu-type",
        choices=sorted(get_cpu_types_str_set()),
        default="o3",
        help="gem5 CPU model used for the SE run.",
    )
    parser.add_argument("--clk-freq", default="2.6GHz", help="Board clock frequency.")
    parser.add_argument("--num-cores", type=int, default=1, help="Number of cores.")
    parser.add_argument("--l1i-size", default="64KiB", help="Private L1I size.")
    parser.add_argument("--l1d-size", default="64KiB", help="Private L1D size.")
    parser.add_argument("--l2-size", default="512KiB", help="Private L2 size.")
    parser.add_argument("--mem-size", default="4GiB", help="Total modeled memory size.")
    parser.add_argument(
        "--mem-channels",
        type=int,
        choices=[1, 2],
        default=2,
        help="Modeled memory-channel count supported by this lightweight script.",
    )
    parser.add_argument(
        "--stdout-file",
        type=Path,
        default=None,
        help="Guest stdout capture path. Defaults to <outdir>/guest.stdout.",
    )
    parser.add_argument(
        "--stderr-file",
        type=Path,
        default=None,
        help="Guest stderr capture path. Defaults to <outdir>/guest.stderr.",
    )
    parser.add_argument(
        "--fault-model",
        default="none",
        help="Fault model name to record for downstream injection hooks.",
    )
    parser.add_argument(
        "--fault-target",
        default="",
        help="Fault target identifier to record for downstream injection hooks.",
    )
    parser.add_argument(
        "--fault-bit",
        type=int,
        default=None,
        help="Optional bit index for downstream injection hooks.",
    )
    parser.add_argument(
        "--fault-tick",
        type=int,
        default=None,
        help="Optional tick at which the external injector should trigger.",
    )
    parser.add_argument(
        "--max-ticks",
        type=int,
        default=None,
        help="Optional maximum ticks passed to the simulator run.",
    )
    return parser


def _default_output_path(requested: Path | None, filename: str) -> Path:
    if requested is not None:
        return requested.resolve()
    return (Path(m5.options.outdir) / filename).resolve()


def _build_memory(mem_channels: int, mem_size: str):
    if mem_channels == 1:
        return SingleChannelDDR4_2400(mem_size)
    return DualChannelDDR4_2400(mem_size)


def _write_fault_manifest(args: argparse.Namespace) -> Path:
    outdir = Path(m5.options.outdir).resolve()
    manifest_path = outdir / "sdc_fault_request.json"
    request = build_fault_request(
        fault_model=args.fault_model,
        fault_target=args.fault_target,
        fault_bit=args.fault_bit,
        fault_tick=args.fault_tick,
        binary=args.binary,
        binary_args=args.binary_arg,
        cpu_type=args.cpu_type,
        num_cores=args.num_cores,
        clk_freq=args.clk_freq,
    )
    return write_fault_request(request, manifest_path)


def main() -> None:
    args = _build_parser().parse_args()
    outdir = Path(m5.options.outdir).resolve()
    outdir.mkdir(parents=True, exist_ok=True)

    cache_hierarchy = PrivateL1PrivateL2CacheHierarchy(
        l1d_size=args.l1d_size,
        l1i_size=args.l1i_size,
        l2_size=args.l2_size,
    )
    processor = SimpleProcessor(
        cpu_type=get_cpu_type_from_str(args.cpu_type),
        isa=ISA.ARM,
        num_cores=args.num_cores,
    )
    board = SimpleBoard(
        clk_freq=args.clk_freq,
        processor=processor,
        memory=_build_memory(args.mem_channels, args.mem_size),
        cache_hierarchy=cache_hierarchy,
    )

    stdout_file = _default_output_path(args.stdout_file, "guest.stdout")
    stderr_file = _default_output_path(args.stderr_file, "guest.stderr")
    binary = BinaryResource(
        local_path=str(args.binary.resolve()),
        architecture=ISA.ARM,
    )
    board.set_se_binary_workload(
        binary,
        arguments=args.binary_arg,
        stdout_file=stdout_file,
        stderr_file=stderr_file,
    )

    fault_manifest = _write_fault_manifest(args)
    if args.fault_model != "none":
        print(
            "[sdc] fault metadata recorded at %s; "
            "an external injector must consume this request."
            % fault_manifest,
            file=sys.stderr,
        )

    simulator = Simulator(board=board)
    simulator.run(max_ticks=args.max_ticks)
    print(
        "Exiting @ tick {} because {}.".format(
            simulator.get_current_tick(), simulator.get_last_exit_event_cause()
        )
    )


if __name__ == "__m5_main__":
    main()
