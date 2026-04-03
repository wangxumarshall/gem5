"""Helpers for recording gem5-side SDC fault-injection requests."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Optional, Sequence


@dataclass(frozen=True)
class FaultRequest:
    fault_model: str = "none"
    fault_target: str = ""
    fault_bit: Optional[int] = None
    fault_tick: Optional[int] = None
    implemented_in_script: bool = False
    binary: str = ""
    binary_args: tuple[str, ...] = ()
    cpu_type: str = "o3"
    num_cores: int = 1
    clk_freq: str = "2.6GHz"

    def is_active(self) -> bool:
        return self.fault_model != "none"

    def to_dict(self) -> dict:
        payload = asdict(self)
        payload["binary_args"] = list(self.binary_args)
        return payload


def build_fault_request(
    *,
    fault_model: str,
    fault_target: str,
    fault_bit: Optional[int],
    fault_tick: Optional[int],
    implemented_in_script: bool,
    binary: Path,
    binary_args: Sequence[str],
    cpu_type: str,
    num_cores: int,
    clk_freq: str,
) -> FaultRequest:
    return FaultRequest(
        fault_model=fault_model,
        fault_target=fault_target,
        fault_bit=fault_bit,
        fault_tick=fault_tick,
        implemented_in_script=implemented_in_script,
        binary=str(binary.resolve()),
        binary_args=tuple(binary_args),
        cpu_type=cpu_type,
        num_cores=num_cores,
        clk_freq=clk_freq,
    )


def write_fault_request(request: FaultRequest, manifest_path: Path) -> Path:
    manifest_path = manifest_path.resolve()
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(request.to_dict(), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return manifest_path
