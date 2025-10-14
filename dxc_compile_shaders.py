import re
import os
import subprocess
from pathlib import Path
import argparse
import time

# -------------------
# Argument parsing
# -------------------
parser = argparse.ArgumentParser(description="Compile HLSL shaders to SPIR-V")
parser.add_argument("--src", type=str, default="Shaders", help="Source folder with HLSL files")
parser.add_argument("--out", type=str, default="bin/SpirV", help="Output folder for SPIR-V")
parser.add_argument("--sm", type=str, default="6_9", help="Shader Model suffix (default 6_9)")
parser.add_argument("--dbg", type=bool, default=True, help="Enable debug info")
parser.add_argument("--opt", type=str, default=None, help="Optimization level (O0, O1, O2, O3). If not set, no optimization flag is used")
parser.add_argument("--vk", type=str, default="vulkan1.3", help="Vulkan target environment (default vulkan1.3)")
args = parser.parse_args()

SRC_DIR = Path(args.src)
OUT_DIR = Path(args.out)
OUT_DIR.mkdir(parents=True, exist_ok=True)

# -------------------
# Shader detection
# -------------------
STAGE_REGEX = re.compile(r'\[\s*shader\s*\(\s*"(\w+)"\s*\)\s*\]')
ENTRY_POINT_REGEX = re.compile(r'\b(\w*Main\w*)\s*\(')


STAGE_TO_TARGET_PREFIX = {
    "vertex": "vs",
    "pixel": "ps",
    "compute": "cs",
    "mesh": "ms",
    "ampl": "as",
    "raygeneration": "rgs",
    "intersection": "is",
    "anyhit": "ahs",
    "closesthit": "chs",
    "miss": "ms",
}

DXC = os.environ.get("DXC")
if not DXC:
    raise RuntimeError("DXC environment variable not set")

print("Using DXC executable:", DXC)
subprocess.run([DXC, "--version"], capture_output=False, text=True)

# -------------------
# Compilation
# -------------------
start_time = time.time()
compiled_count = 0

for shader_path in SRC_DIR.glob("*.hlsl"):
    code = shader_path.read_text()
    stage_matches = STAGE_REGEX.findall(code)
    entry_matches = ENTRY_POINT_REGEX.findall(code)

    # Check there is exactly one match for each
    if len(stage_matches) != 1:
        raise RuntimeError(f"Expected exactly one [shader(...)] attribute, found {len(stage_matches)}")

    if len(entry_matches) != 1:
        raise RuntimeError(f"Expected exactly one entry function matching *Main*, found {len(entry_matches)}")

    stage = stage_matches[0]
    entry = entry_matches[0]

    prefix = STAGE_TO_TARGET_PREFIX.get(stage)
    if not prefix:
        print(f"Skipping unknown stage '{stage}' in {shader_path}")
        continue

    target = f"{prefix}_{args.sm}"
    out_file = OUT_DIR / f"{stage}_{entry}"

    cmd = [
        DXC,
        "-spirv",
        f"-fspv-target-env={args.vk}",
        "-fspv-use-vulkan-memory-model",
        #"-fvk-use-dx-layout",
        "-fvk-use-scalar-layout",
        "-enable-16bit-types",
        "-E", entry,
        "-T", target,
        str(shader_path),
        "-Fo", f"{str(out_file)}.spirv"
    ]

    if args.dbg:
        cmd += ["-Zi", "-Qembed_debug", "-fspv-debug=vulkan-with-source"]
    if args.opt:
        cmd += [f"-{args.opt}"]

    print(f"\nCompiling {shader_path} [{entry}] ...")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.stdout.strip():
            print(f"[stdout]\n{result.stdout}")
        if result.stderr.strip():
            print(f"[stderr]\n{result.stderr}")
        compiled_count += 1
    except Exception as e:
        print(f"Exception running DXC on {shader_path} [{entry}]: {e}")

end_time = time.time()
duration = end_time - start_time
print(f"\nCompiled {compiled_count} shaders in {duration:.2f} seconds.")
