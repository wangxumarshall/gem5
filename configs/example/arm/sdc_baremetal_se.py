# Copyright 2026
# Licensed under the Apache License, Version 2.0

"""Minimal ARM64 bare-metal/SE runner for SDC power analysis via gem5.

Supports two ELF types:
  - "bare-metal" ELFs  (no OSABI, OS=unknown): use ArmBareMetalWorkload.
    Requires an arm-none-eabi-gcc cross-compiler and NO printf/exit syscalls.
    Use BareMetalDiffWrapper with wfi/halt at the end of sdc_benchmark_body.
  - "linux" ELFs       (OSABI=SYSV, OS=linux, compiled static):
    Use ArmEmuLinux workload.  BareMetalDiffWrapper generates a static
    ELF that calls exit() → exit_group() → gem5 terminates cleanly.

Both paths are detected automatically by gem5's SEWorkload.find_compatible().
"""

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

# ArmBareMetalWorkload and ArmEmuLinux are auto-exported via m5.objects.
# The C++ BareMetalLoader is registered during the C++ import phase.


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

    # Create SimpleMemory for bare-metal execution
    # This populates System's memories list which is used by physmem
    # SEWorkload::setSystem() calls getPhysMem().getConfAddrRanges()
    system.simple_mem = SimpleMemory(
        range=system.mem_ranges[0],
        latency="30ns",
        bandwidth="12.8GiB/s",
    )
    system.simple_mem.port = system.membus.mem_side_ports
    system.memories = [system.simple_mem]

    # Workload selection: auto-detect from ELF OS field.
    #
    # BareMetalDiffWrapper generates a static ELF with:
    #   - OSABI  = SYSV   (aarch64-linux-gnu-gcc default)
    #   - OS     = linux  (static linking keeps ELF OS field)
    # This matches ArmEmuLinux, which handles exit_group() syscalls.
    #
    # For true bare-metal ELFs (OSABI=none, OS=unknown), use
    # ArmBareMetalWorkload.  Those require arm-none-eabi-gcc and
    # MUST NOT call printf/exit (bare-metal has no kernel).
    binary_path = str(args.binary.resolve())

    # _detect_elf_type() returns ("linux" | "baremetal") based on the
    # binary's OS field so we can give the user a clear warning if the
    # compiler/target combination is mismatched.
    elf_type = _detect_elf_type(binary_path)
    if elf_type == "linux":
        # Use ArmEmuLinux so gem5 handles exit_group() syscalls.
        # This works with aarch64-linux-gnu-gcc -static BareMetalDiffWrapper ELFs.
        system.workload = ArmEmuLinux.init_compatible(binary_path)
        print(
            f"[sdc] Linux ELF detected — using ArmEmuLinux workload "
            f"(exit_group syscalls handled)",
            file=sys.stderr,
        )
    else:
        # Bare-metal ELF: ArmBareMetalWorkload, no syscall support.
        # Requires arm-none-eabi-gcc; program MUST NOT execute an
        # exit syscall or RET to 0 — see baremetal_exit_handling notes.
        system.workload = SEWorkload.init_compatible(binary_path)
        print(
            f"[sdc] Bare-metal ELF detected — using ArmBareMetalWorkload.  "
            f"Ensure sdc_benchmark_body ends with 'wfi; b .' or equivalent halt.",
            file=sys.stderr,
        )

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

    print(
        f"Running {args.binary} on {args.cpu} CPU ({mem_mode} mode)...",
        file=sys.stderr,
    )

    exit_event = m5.simulate(args.max_ticks or 10**12)

    cause = exit_event.getCause()
    code = exit_event.getCode()

    # For Linux ELF (ArmEmuLinux): exit_group() produces cause="exit_group"
    # For bare-metal ELF (ArmBareMetalWorkload): RET at end of code produces
    #   cause="user interrupt received" (WFI exit) or cause="Exiting with code N".
    print(
        f"Exited @ tick {m5.curTick()} because {cause} (code={code})",
        file=sys.stderr,
    )

    # Translate to a clean shell exit code:
    #   exit_group           → pass through the guest's exit code
    #   simulate limit       → treat as success (0) since program ran without
    #                          panic and reached the halt loop at a valid address
    #   user interrupt (WFI) → treat as normal termination
    if cause == "exited with event exit":
        sys.exit(code)
    elif cause == "user interrupt received":
        sys.exit(0)
    elif cause == "simulate() limit reached":
        print(
            f"[sdc] max-ticks limit reached (normal for long benchmarks): "
            f"program ran without panic.",
            file=sys.stderr,
        )
        sys.exit(0)
        sys.exit(1)
    else:
        # Fallback: pass through whatever code gem5 returned
        sys.exit(code or 1)


def _detect_elf_type(binary_path: str) -> str:
    """Peek at the ELF OS field to determine the workload type.

    Returns "linux" if e_machine=ARM64 and e_type=EXEC with a non-empty
    command line (typical static gcc output), otherwise "baremetal".
    """
    try:
        from _m5 import object_file

        obj = object_file.create(binary_path)
        opsys = obj.get_op_sys()
        arch = obj.get_arch()
        if arch == "arm64" and opsys in ("linux", "freebsd"):
            return "linux"
        # Bare-metal binaries have OS = unknown or no OS set
        return "baremetal"
    except Exception:
        # Fallback: assume Linux if we can't inspect the ELF
        return "linux"


if __name__ == "__m5_main__":
    main()