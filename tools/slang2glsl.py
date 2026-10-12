#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert a libretro slang shader preset to the GLSL 3.30 that HIRAKU's window runs.

    tools/slang2glsl.py PRESET.slangp OUT_DIR [--vendor DIR]

It does what RetroArch does at load time, once, ahead of time:
- each pass's .slang is read with its #includes; its #pragma lines (parameter, name, format)
  are taken out, and it is split into its vertex and fragment stages;
- glslangValidator compiles each stage to SPIR-V and SPIRV-Cross writes it back as GLSL 3.30
  (the interface between the stages renamed by location: GLSL 3.30 matches them by name).

OUT_DIR gets passN.vert / passN.frag, preset.slangp (the preset, pointing at those, with the
pragmas' aliases and formats as aliasN / formatN), parameters.txt (one line per parameter:
name, default, minimum, maximum, step, label; the preset's own values as defaults) and the
preset's textures. runtime/host/picture.cpp reads that folder.

--vendor DIR copies every source file read (and the LICENSE/README files beside them) under
DIR, keeping their paths relative to the slang-shaders checkout: the preferred form for
modifying the generated files.

Needs glslangValidator (glslang) and spirv-cross on the PATH, or SPIRV_CROSS=/path/to/spirv-cross.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

PASS_KEYS = ("filter_linear", "wrap_mode", "mipmap_input", "float_framebuffer", "srgb_framebuffer",
             "scale_type", "scale_type_x", "scale_type_y", "scale", "scale_x", "scale_y",
             "frame_count_mod", "alias")


def parse_preset(path: str) -> dict[str, str]:
    out: dict[str, str] = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r'^\s*#reference\s+"?([^"]+)"?', line)
            if m:
                out.update(parse_preset(os.path.join(os.path.dirname(path), m.group(1))))
                continue
            line = re.sub(r'(^|\s)#.*$', "", line) if '"' not in line else re.sub(r'("[^"]*")\s*#.*$', r"\1", line)
            m = re.match(r'^\s*([A-Za-z0-9_]+)\s*=\s*"?([^"]*?)"?\s*$', line)
            if m:
                out[m.group(1)] = m.group(2)
    return out


class Source:
    """One pass's .slang with its includes expanded and its pragmas taken out."""

    def __init__(self, path: str, read: set[str]):
        self.params: list[tuple[str, str, float, float, float, float]] = []
        self.name = ""
        self.format = ""
        self.lines = self._expand(os.path.abspath(path), read, 0)

    def _expand(self, path: str, read: set[str], depth: int) -> list[str]:
        if depth > 16:
            sys.exit(f"{path}: #include nested too deep")
        read.add(path)
        out = []
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.rstrip("\n").rstrip("\r")
                m = re.match(r'^\s*#\s*include\s+"([^"]+)"', line)
                if m:
                    out += self._expand(os.path.normpath(os.path.join(os.path.dirname(path), m.group(1))), read, depth + 1)
                    continue
                m = re.match(r'^\s*#pragma\s+parameter\s+(\w+)\s+"([^"]*)"\s+(\S+)\s+(\S+)\s+(\S+)(?:\s+(\S+))?', line)
                if m:
                    step = float(m.group(6)) if m.group(6) else 0.0
                    self.params.append((m.group(1), m.group(2).strip(), float(m.group(3)), float(m.group(4)),
                                        float(m.group(5)), step))
                    out.append("")
                    continue
                m = re.match(r'^\s*#pragma\s+name\s+(\w+)', line)
                if m:
                    self.name = m.group(1)
                    out.append("")
                    continue
                m = re.match(r'^\s*#pragma\s+format\s+(\w+)', line)
                if m:
                    self.format = m.group(1)
                    out.append("")
                    continue
                out.append(line)
        return out

    def stages(self) -> tuple[str, str]:
        common, vert, frag, cur = [], [], [], None
        for line in self.lines:
            m = re.match(r'^\s*#pragma\s+stage\s+(\w+)', line)
            if m:
                cur = vert if m.group(1) == "vertex" else frag
                continue
            (cur if cur is not None else common).append(line)
        return "\n".join(common + vert) + "\n", "\n".join(common + frag) + "\n"


def run(cmd: list[str]) -> str:
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(f"{' '.join(cmd)}:\n{p.stdout}{p.stderr}")
    return p.stdout


def to_glsl(src: str, stage: str, tmp: str, spirv_cross: str) -> str:
    """Vulkan GLSL -> SPIR-V -> GLSL 3.30, the varyings named HK_VARY<location>."""
    ext = {"vertex": "vert", "fragment": "frag"}[stage]
    vk = os.path.join(tmp, "s." + ext)
    spv = os.path.join(tmp, "s.spv")
    with open(vk, "w") as f:
        f.write(src)
    run(["glslangValidator", "-V", "--target-env", "vulkan1.0", "-o", spv, vk])
    refl = json.loads(run([spirv_cross, spv, "--reflect"]))
    rename = []
    side = "outputs" if stage == "vertex" else "inputs"
    for io in refl.get(side, []):
        rename += ["--rename-interface-variable", "out" if stage == "vertex" else "in",
                   str(io["location"]), f"HK_VARY{io['location']}"]
    return run([spirv_cross, spv, "--version", "330", "--no-es", "--no-420pack-extension", *rename])


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("preset")
    ap.add_argument("out")
    ap.add_argument("--vendor", help="copy the sources read under this folder")
    a = ap.parse_args()
    spirv_cross = os.environ.get("SPIRV_CROSS") or shutil.which("spirv-cross") or sys.exit("spirv-cross not found")
    preset_path = os.path.abspath(a.preset)
    base = os.path.dirname(preset_path)
    p = parse_preset(preset_path)
    n = int(p["shaders"])
    os.makedirs(a.out, exist_ok=True)
    read: set[str] = {preset_path}
    params: dict[str, list] = {}
    lines = [f"# Generated by tools/slang2glsl.py from {os.path.basename(preset_path)}", f"shaders = {n}"]
    with tempfile.TemporaryDirectory() as tmp:
        for i in range(n):
            src = Source(os.path.join(base, p[f"shader{i}"]), read)
            vert, frag = src.stages()
            for stage, text, ext in (("vertex", vert, "vert"), ("fragment", frag, "frag")):
                with open(os.path.join(a.out, f"pass{i}.{ext}"), "w") as f:
                    f.write(f"// pass {i}: {p[f'shader{i}']}, converted by tools/slang2glsl.py\n")
                    f.write(to_glsl(text, stage, tmp, spirv_cross))
            for name, label, d, lo, hi, step in src.params:
                params.setdefault(name, [d, lo, hi, step, label])
            lines.append(f"shader{i} = pass{i}")
            if src.name and f"alias{i}" not in p:
                lines.append(f"alias{i} = {src.name}")
            if src.format:
                lines.append(f"format{i} = {src.format}")
            for k in PASS_KEYS:
                if f"{k}{i}" in p:
                    lines.append(f"{k}{i} = {p[f'{k}{i}']}")
            print(f"pass {i}: {p[f'shader{i}']}")
    if p.get("textures"):
        names = [t for t in p["textures"].split(";") if t]
        lines.append("textures = " + ";".join(names))
        os.makedirs(os.path.join(a.out, "lut"), exist_ok=True)
        for t in names:
            src = os.path.normpath(os.path.join(base, p[t]))
            read.add(src)
            shutil.copy(src, os.path.join(a.out, "lut", os.path.basename(src)))
            lines.append(f"{t} = lut/{os.path.basename(src)}")
            for k in ("_linear", "_mipmap", "_wrap_mode"):
                if t + k in p:
                    lines.append(f"{t}{k} = {p[t + k]}")
    for k, v in p.items():   # the preset's own parameter values
        if k in params:
            params[k][0] = float(v)
    with open(os.path.join(a.out, "preset.slangp"), "w") as f:
        f.write("\n".join(lines) + "\n")
    with open(os.path.join(a.out, "parameters.txt"), "w") as f:
        f.write("# name\tdefault\tminimum\tmaximum\tstep\tlabel (generated by tools/slang2glsl.py)\n")
        for name, (d, lo, hi, step, label) in params.items():
            f.write(f"{name}\t{d:g}\t{lo:g}\t{hi:g}\t{step:g}\t{label}\n")
    if a.vendor:
        root = base
        while root != "/" and not os.path.isdir(os.path.join(root, ".git")):
            root = os.path.dirname(root)
        extra = set()
        for f in read:
            for name in ("LICENSE", "LICENSE.TXT", "LICENSE.md", "README.TXT", "README.md", "THANKS.TXT"):
                if os.path.exists(os.path.join(os.path.dirname(f), name)):
                    extra.add(os.path.join(os.path.dirname(f), name))
        for f in sorted(read | extra):
            dst = os.path.join(a.vendor, os.path.relpath(f, root))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy(f, dst)
        print(f"{len(read | extra)} source files under {a.vendor}")
    print(f"{n} passes, {len(params)} parameters -> {a.out}")


if __name__ == "__main__":
    main()
