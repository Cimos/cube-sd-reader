#!/usr/bin/env python3
# AP_FLAKE8_CLEAN
"""Development commands; intentionally no upload or flash operation."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "upstream" / "ardupilot"
LOCK = json.loads((ROOT / "dependencies.json").read_text())
OVERLAY = Path("libraries/AP_HAL/examples/CubeSDCardReader")


def run(args, cwd=ROOT):
    subprocess.run(args, cwd=cwd, check=True)


def capture(args, cwd=ROOT):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def verify():
    revision = capture(["git", "rev-parse", "HEAD"], UPSTREAM)
    if revision != LOCK["ardupilot"]["commit"]:
        raise RuntimeError("Upstream revision differs from dependencies.json")
    modules = capture(["git", "submodule", "status", "--recursive"], UPSTREAM)
    if any(line.startswith(("-", "+", "U")) for line in modules.splitlines()):
        raise RuntimeError("Missing or mismatched upstream submodules; run bootstrap")
    if capture(["git", "diff", "HEAD", "--name-only"], UPSTREAM):
        raise RuntimeError("Tracked upstream edits detected; review before reader build")


def bootstrap():
    run(["git", "submodule", "update", "--init", "upstream/ardupilot"])
    run(["git", "submodule", "update", "--init", "--recursive", "--jobs", "4"], UPSTREAM)
    verify()
    run([sys.executable, '-m', 'venv', str(ROOT / '.venv')])
    run([str(ROOT / '.venv/bin/python'), '-m', 'pip', 'install', '-r', str(ROOT / 'requirements-dev.txt')])


def sync():
    verify()
    source = ROOT / "firmware" / "CubeSDCardReader"
    destination = UPSTREAM / OVERLAY
    state_path = ROOT / "work" / "overlay-state.json"
    state = json.loads(state_path.read_text()) if state_path.exists() else {}
    pending = []
    for item in sorted(source.iterdir()):
        if not item.is_file():
            continue
        target = destination / item.name
        data = item.read_bytes()
        if target.exists() and target.read_bytes() != data:
            current = hashlib.sha256(target.read_bytes()).hexdigest()
            if state.get(item.name) != current:
                raise RuntimeError(f"Refusing to overwrite local upstream edit: {target}")
        pending.append((item.name, target, data))
    # Remove copies this tool generated whose source was deleted or renamed; they
    # would otherwise still be compiled into the firmware.
    stale = sorted(set(state) - {name for name, _, _ in pending})
    for name in stale:
        target = destination / name
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() != state[name]:
            raise RuntimeError(f"Refusing to delete local upstream edit: {target}")
    for name in stale:
        (destination / name).unlink(missing_ok=True)
        del state[name]
    destination.mkdir(parents=True, exist_ok=True)
    for name, target, data in pending:
        target.write_bytes(data)
        state[name] = hashlib.sha256(data).hexdigest()
    state_path.parent.mkdir(exist_ok=True)
    state_path.write_text(json.dumps(state, indent=2) + "\n")
    print(f"Synced reader sources to {destination}")


def doctor():
    verify()
    for command in ("git", "python3", "arm-none-eabi-gcc", "arm-none-eabi-size", "make"):
        location = shutil.which(command)
        if not location:
            raise RuntimeError(f"Missing executable: {command}")
        print(f"{command}: {location}")
    print(capture(["arm-none-eabi-gcc", "--version"]).splitlines()[0])
    print(f"Board: {LOCK['board']}; upstream: {LOCK['ardupilot']['commit']}")


def build(jobs):
    venv_bin = ROOT / '.venv' / 'bin'
    if not (venv_bin / 'python').exists():
        raise RuntimeError('Run bootstrap to create the build environment')
    os.environ['PATH'] = str(venv_bin) + os.pathsep + os.environ.get('PATH', '')
    doctor()
    sync()
    run(["./waf", "configure", "--board", LOCK["board"], "--extra-hwdef", str(ROOT / "firmware/reader.hwdef")], UPSTREAM)
    run(["./waf", "--targets", LOCK["target"], "-j", str(jobs)], UPSTREAM)
    run([str(venv_bin / "python"), "-m", "unittest", "discover", "-s", "tests", "-v"])
    binary = UPSTREAM / "build" / LOCK["board"] / LOCK["target"]
    if not binary.is_file():
        raise RuntimeError(f"Expected ELF missing: {binary}")
    from validate_artifacts import validate
    build_dir = UPSTREAM / "build" / LOCK["board"]
    native = build_dir / "bin" / "CubeSDCardReader"
    report = validate(binary, native.with_suffix(".apj"), native.with_suffix(".bin"))
    output = ROOT / "artifacts" / "reader-dev"
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, output / "CubeSDCardReader.elf")
    shutil.copy2(native.with_suffix(".apj"), output / "CubeSDCardReader.apj")
    shutil.copy2(native.with_suffix(".bin"), output / "CubeSDCardReader.bin")
    shutil.copy2(build_dir / "Linker.map", output / "CubeSDCardReader.map")
    (output / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    manifest = {
        "status": "development firmware; automated checks passed; see docs/TESTING.md for hardware evidence",
        "upstream": LOCK["ardupilot"]["commit"],
        "submodules": capture(["git", "submodule", "status", "--recursive"], UPSTREAM).splitlines(),
        "compiler": capture(["arm-none-eabi-gcc", "--version"]).splitlines()[0],
        "board": LOCK["board"],
        "source_sha256": {
            str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for base in ("firmware", "tools", "tests")
            for p in (ROOT / base).rglob("*") if p.is_file() and "__pycache__" not in p.parts
        },
        "artifact_sha256": {
            p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in output.iterdir() if p.is_file() and p.name not in ("manifest.json", "SHA256SUMS")
        },
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    sums = [hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.name
            for p in sorted(output.iterdir()) if p.is_file() and p.name != "SHA256SUMS"]
    (output / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    print(f"Validated development firmware: {output}")



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("bootstrap", "doctor", "sync", "build"))
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        if args.command == "build":
            build(args.jobs)
        else:
            {"bootstrap": bootstrap, "doctor": doctor, "sync": sync}[args.command]()
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
