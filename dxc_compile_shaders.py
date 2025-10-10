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
parser.add_argument("--sm", type=str, default="6_7", help="Shader Model suffix (default 6_7)")
parser.add_argument("--dbg", action="store_true", help="Enable debug info")
parser.add_argument("--opt", type=str, default=None, help="Optimization level (O0, O1, O2, O3). If not set, no optimization flag is used")
parser.add_argument("--vk", type=str, default="vulkan1.3", help="Vulkan target environment (default vulkan1.4)")
args = parser.parse_args()

SRC_DIR = Path(args.src)
OUT_DIR = Path(args.out)
OUT_DIR.mkdir(parents=True, exist_ok=True)

# -------------------
# Shader detection
# -------------------
ENTRY_PATTERN = re.compile(
    r'\[\s*shader\s*\(\s*"?(\w+)"?\s*\)\s*\]\s*[\w\s\*:<>]+\s+(\w+)\s*\('
)

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
print("Using DXC executable:", DXC)

# -------------------
# Compilation
# -------------------
start_time = time.time()
compiled_count = 0

for shader_path in SRC_DIR.glob("*.hlsl"):
    code = shader_path.read_text()
    for stage, entry in ENTRY_PATTERN.findall(code):
        prefix = STAGE_TO_TARGET_PREFIX.get(stage.lower())
        if not prefix:
            print(f"Skipping unknown stage '{stage}' in {shader_path}")
            continue

        target = f"{prefix}_{args.sm}"
        out_file = OUT_DIR / f"{shader_path.stem}_{entry}.spv"

        cmd = [
            DXC,
            "-spirv",
            f"-fspv-target-env={args.vk}",
            "-fvk-use-dx-layout",
            "-fvk-use-scalar-layout",
            "-E", entry,
            "-T", target,
            str(shader_path),
            "-Fo", str(out_file)
        ]

        if args.dbg:
            cmd += ["-Zi"]
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
