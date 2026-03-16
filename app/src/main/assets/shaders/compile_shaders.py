#!/usr/bin/env python3
"""Compile all .glsl shaders in this directory to SPIR-V (.spv) with debug info."""

import glob
import os
import shutil
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
COMPILER = "glslangValidator"


def find_compiler():
    path = shutil.which(COMPILER)
    if path:
        return path
    # Common Windows paths
    for candidate in [
        os.path.expandvars(r"%VULKAN_SDK%\Bin\glslangValidator.exe"),
        os.path.expandvars(r"%VULKAN_SDK%\Bin32\glslangValidator.exe"),
    ]:
        if os.path.isfile(candidate):
            return candidate
    return None


STAGE_MAP = {
    ".vert.glsl": "vert",
    ".frag.glsl": "frag",
    ".comp.glsl": "comp",
    ".geom.glsl": "geom",
    ".tesc.glsl": "tesc",
    ".tese.glsl": "tese",
}


def detect_stage(filename):
    """Return the GLSL stage string for a given filename, or None."""
    for suffix, stage in STAGE_MAP.items():
        if filename.endswith(suffix):
            return stage
    return None


def main():
    compiler = find_compiler()
    if not compiler:
        print(f"ERROR: {COMPILER} not found. Install the Vulkan SDK or add it to PATH.")
        sys.exit(1)

    glsl_files = sorted(glob.glob(os.path.join(SCRIPT_DIR, "*.glsl")))
    if not glsl_files:
        print("No .glsl files found.")
        sys.exit(0)

    failed = []
    for src in glsl_files:
        spv = src.rsplit(".glsl", 1)[0] + ".spv"
        stage = detect_stage(src)
        cmd = [compiler, "-V", "-g", "-Od"]
        if stage:
            cmd += ["-S", stage]
        cmd += [src, "-o", spv]
        print(f"  {os.path.basename(src)} -> {os.path.basename(spv)}")
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"    FAILED:\n{result.stdout}{result.stderr}")
            failed.append(os.path.basename(src))
        elif result.stdout.strip():
            # Print warnings if any
            for line in result.stdout.strip().splitlines():
                if "WARNING" in line.upper():
                    print(f"    {line}")

    print()
    if failed:
        print(f"FAILED ({len(failed)}/{len(glsl_files)}): {', '.join(failed)}")
        sys.exit(1)
    else:
        print(f"OK: {len(glsl_files)} shaders compiled.")


if __name__ == "__main__":
    main()
