# Copyright 2026
# Licensed under the Apache License, Version 2.0

"""Minimal bare-metal ARM64 SE runner for SDC power analysis via gem5."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import m5
from m5.objects import *
from m5.util import addToPath

m5.util.addToPath("../../")

from common import ObjectList, MemConfig
from common.cores.arm import O3_ARM_v7a
import devices
from devices import L1I, L1D, L2, SimpleSeSystem, ArmCpuCluster

# ArmBareMetalWorkload is exported via m5.objects automatically
# BareMetalLoader is auto-registered when C++ code is imported
# No explicit import needed - gem5's Python binding exports it


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Run an ARM64 bare-metal static ELF in gem5 SE mode."
    )
    p.add_argument("binary", type=Path, help="Path to the ARM64 ELF to run.")
    p.add_argument("--cpu", choices=["atomic", "timing", "o3", "minor"], default="atomic")
    p.add_argument("--cpu-freq", default="2.6GHz")
    p.add_argument("--num-cores", type=int, default=1)
    p.add_argument("--mem-size", default="4GiB")
    p.add_argument("--mem-channels", type=int, default=1)
    p.add_argument("--mem-type", default="DDR3_1600_8x8")
    p.add_argument("--mem-ranks", type=int, default=None)
    p.add_argument("--max-ticks", type=int, default=None)
    p.add_argument(
        "-P", "--param", action="append", default=[],
        help="Set SimObject params.",
    )
    return p


def _cpu_type_info(cpu_name: str):
    lut = {
        "atomic": (AtomicSimpleCPU, None, None, None),
        "timing": (TimingSimpleCPU, L1I, L1D, L2),
        "o3": (O3_ARM_v7a.O3_ARM_v7a_3, L1I, L1D, L2),
        "minor": (MinorCPU, L1I, L1D, L2),
    }
    return lut[cpu_name]


def main() -> None:
    args = _build_parser().parse_args()

    cpu_cls, l1i_cls, l1d_cls, l2_cls = _cpu_type_info(args.cpu)
    mem_mode = cpu_cls.memory_mode()
    want_caches = mem_mode == "timing"

    # System
    system = SimpleSeSystem(mem_mode=mem_mode)

    # CPU cluster
    system.cpu_cluster = ArmCpuCluster(
        system,
        args.num_cores,
        args.cpu_freq,
        "1.2V",
        cpu_cls, l1i_cls, l1d_cls, l2_cls,
    )
    system.addCaches(want_caches, last_cache_level=2)

    system.mem_ranges = [AddrRange(start=0, size=args.mem_size)]

    MemConfig.config_mem(args, system)
    system.connect()

    # Bare-metal workload
    binary_path = str(args.binary.resolve())

    # Let gem5 auto-detect workload type based on ELF OS field
    # BareMetalLoader in C++ will be matched for UnknownOpSys ARM64 binaries
    system.workload = SEWorkload.init_compatible(binary_path)

    # Create single process and assign to all CPUs
    process = Process()
    process.executable = binary_path
    process.cmd = [binary_path]

    # Assign same process to each CPU (ArmCpuCluster already handles threading)
    for cpu in system.cpu_cluster.cpus:
        cpu.workload = process

    root = Root(full_system=False)
    root.system = system
    root.apply_config(args.param)

    m5.instantiate()

    # BareMetalWorkload::initState() (C++) sets PC to ELF entry and SP to 0x80010000
    # No manual PC/SP setup needed — the C++ layer handles it.
    pass

    print(
        f"Running {args.binary} on {args.cpu} CPU ({mem_mode} mode)...",
        file=sys.stderr,
    )

    exit_event = m5.simulate(args.max_ticks or 10**12)

    print(
        f"Exited @ tick {m5.curTick()} "
        f"because {exit_event.getCause()} ({exit_event.getCode()})",
        file=sys.stderr,
    )


if __name__ == "__m5_main__":
    main()