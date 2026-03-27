import importlib.util
import json
from pathlib import Path
import sys


HELPER_PATH = (
    Path(__file__).resolve().parents[1]
    / "configs"
    / "example"
    / "arm"
    / "sdc_fault_request.py"
)
SPEC = importlib.util.spec_from_file_location("sdc_fault_request", HELPER_PATH)
assert SPEC is not None and SPEC.loader is not None
sdc_fault_request = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = sdc_fault_request
SPEC.loader.exec_module(sdc_fault_request)


def test_build_fault_request_captures_runner_context(tmp_path):
    request = sdc_fault_request.build_fault_request(
        fault_model="register_bit_flip",
        fault_target="x0",
        fault_bit=7,
        fault_tick=1234,
        binary=tmp_path / "case.elf",
        binary_args=["--size", "64"],
        cpu_type="o3",
        num_cores=1,
        clk_freq="2.6GHz",
    )

    assert request.is_active() is True
    assert request.binary_args == ("--size", "64")
    assert request.binary.endswith("case.elf")


def test_write_fault_request_emits_json_manifest(tmp_path):
    request = sdc_fault_request.FaultRequest(
        fault_model="none",
        fault_target="",
        fault_bit=None,
        fault_tick=None,
        implemented_in_script=False,
        binary=str((tmp_path / "guest.elf").resolve()),
    )

    manifest = sdc_fault_request.write_fault_request(
        request, tmp_path / "out" / "sdc_fault_request.json"
    )

    payload = json.loads(manifest.read_text(encoding="utf-8"))
    assert payload["fault_model"] == "none"
    assert payload["binary"].endswith("guest.elf")
    assert payload["binary_args"] == []
