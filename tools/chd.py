# SPDX-License-Identifier: GPL-3.0-or-later
"""MAME CHD images: what one holds, and extracting it with MAME's chdman.

chdman is the one on the PATH, else MAME's flatpak (org.mamedev.MAME), else HIRAKU's own
hiraku-chdcat (runtime/dev/chdcat.cpp: the same bytes, through the CHD reader the games use),
which a release has beside its games, so that ingesting needs no MAME tools.
HK_CHD_TOOL=chdcat uses hiraku-chdcat even where chdman is. Runs are memory-capped with
systemd-run when it is available: chdman's memory use is not bounded (a DVD creation once
reached 9 GB).
"""
from __future__ import annotations

import glob
import os
import re
import shutil
import subprocess
import sys

MEMORY_CAP = os.environ.get("HIRAKU_CHDMAN_MEM", "3G")


class ChdError(Exception):
    pass


def _flatpak_mame() -> bool:
    return bool(shutil.which("flatpak")) and subprocess.run(
        ["flatpak", "info", "org.mamedev.MAME"], capture_output=True).returncode == 0


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def chdcat() -> str | None:
    """hiraku-chdcat: $HK_CHDCAT, else beside the games (a release's bin/, a build's folder)."""
    exe = "hiraku-chdcat.exe" if os.name == "nt" else "hiraku-chdcat"
    for p in (os.environ.get("HK_CHDCAT", ""), os.path.join(ROOT, "bin", exe), os.path.join(ROOT, "build", "cmake", exe)):
        if p and os.path.isfile(p):
            return p
    return shutil.which(exe)


_use_chdcat: bool | None = None


def use_chdcat() -> bool:
    """Whether CHDs go through hiraku-chdcat: asked for, or no chdman to be had."""
    global _use_chdcat
    if _use_chdcat is None:
        forced = os.environ.get("HK_CHD_TOOL", "") == "chdcat"
        _use_chdcat = bool(chdcat()) and (forced or not (shutil.which("chdman") or _flatpak_mame()))
    return _use_chdcat


def chdman_command(dirs: list[str]) -> list[str]:
    """The command that runs chdman, able to read and write `dirs`."""
    if shutil.which("chdman"):
        return ["chdman"]
    if _flatpak_mame():
        fs = []
        for d in sorted({os.path.abspath(d) for d in dirs}):
            fs.append(f"--filesystem={d}")
        return ["flatpak", "run", "--command=chdman", *fs, "org.mamedev.MAME"]
    raise ChdError("neither hiraku-chdcat nor chdman found: install MAME's tools (package mame-tools or mame), "
                   "or MAME's flatpak (flatpak install flathub org.mamedev.MAME)")


_cap_ok: bool | None = None


def capped(cmd: list[str]) -> list[str]:
    """cmd under a memory cap (a systemd user scope), when systemd-run works here."""
    global _cap_ok
    if _cap_ok is None:
        _cap_ok = bool(shutil.which("systemd-run")) and subprocess.run(
            ["systemd-run", "--user", "--scope", "-q", "true"], capture_output=True).returncode == 0
    if not _cap_ok:
        return cmd
    return ["systemd-run", "--user", "--scope", "-q", "-p", f"MemoryMax={MEMORY_CAP}", "-p", "MemorySwapMax=0", *cmd]


class ChdInfo:
    def __init__(self, kind: str, logical: int, tracks: list[dict], sha1: str):
        self.kind = kind          # cd | dvd | hd | raw
        self.logical = logical    # bytes
        self.tracks = tracks      # CD: [{type, frames}]
        self.sha1 = sha1


def _chdcat_info(chd: str) -> dict:
    out = subprocess.run([chdcat(), "--info", chd], capture_output=True, text=True)
    if out.returncode != 0:
        raise ChdError(f"{chd}: hiraku-chdcat --info failed:\n{(out.stdout + out.stderr).strip()}")
    facts = {"track": []}
    for line in out.stdout.splitlines():
        k, _, v = line.strip().partition(" ")
        if k == "track":
            t, f = v.split()
            facts["track"].append({"type": t, "frames": int(f)})
        elif k:
            facts[k] = v
    return facts


def info(chd: str) -> ChdInfo:
    if use_chdcat():
        f = _chdcat_info(chd)
        return ChdInfo(f.get("kind", "raw"), int(f.get("logical", 0)), f["track"], f.get("sha1", ""))
    out = subprocess.run(chdman_command([os.path.dirname(chd)]) + ["info", "-i", chd],
                         capture_output=True, text=True)
    text = out.stdout + out.stderr
    if out.returncode != 0:
        raise ChdError(f"{chd}: chdman info failed:\n{text.strip()}")
    m = re.search(r"Logical size:\s+([\d,]+)", text)
    logical = int(m.group(1).replace(",", "")) if m else 0
    m = re.search(r"^SHA1:\s+([0-9a-f]{40})", text, re.M)
    sha1 = m.group(1) if m else ""
    tags = re.findall(r"Tag='(....)'", text)
    tracks = [{"type": t, "frames": int(f)} for t, f in re.findall(r"TYPE:(\S+).*?FRAMES:(\d+)", text)]
    if any(t in ("CHT2", "CHTR", "CHCD", "CHGT", "CHGD") for t in tags):
        kind = "cd"
    elif "DVD " in tags:
        kind = "dvd"
    elif "GDDD" in tags:
        kind = "hd"
    else:
        kind = "raw"
    return ChdInfo(kind, logical, tracks, sha1)


def geometry(chd: str) -> tuple[int, int, int] | None:
    """A hard disk CHD's geometry from its GDDD metadata: (cylinders, heads, sectors), or None."""
    if use_chdcat():
        g = _chdcat_info(chd).get("geometry")
        return tuple(int(x) for x in g.split()) if g else None
    out = subprocess.run(chdman_command([os.path.dirname(chd)]) + ["info", "-v", "-i", chd],
                         capture_output=True, text=True)
    m = re.search(r"CYLS:(\d+),HEADS:(\d+),SECS:(\d+)", out.stdout + out.stderr)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else None


def _run_with_progress(cmd: list[str], label: str) -> str:
    """Run chdman (or hiraku-chdcat), printing its progress every 10 %; return what it printed."""
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    seen, last = [], -10
    buf = b""
    while True:
        chunk = p.stdout.read(256)
        if not chunk:
            break
        buf += chunk
        for m in re.finditer(rb"(\d+(?:\.\d+)?)% complete", chunk):
            pct = float(m.group(1))
            if pct >= last + 10:
                last = pct - pct % 10
                print(f"{'extract' if use_chdcat() else 'chdman':<8} {label}: {int(last)}%", flush=True)
        seen.append(chunk)
    p.wait()
    text = b"".join(seen).decode(errors="replace")
    if p.returncode != 0 or "complete" not in text.lower():
        raise ChdError(f"{'hiraku-chdcat' if use_chdcat() else 'chdman'} failed on {label} (exit {p.returncode}):\n{text[-600:].strip()}")
    return text


def _link(src: str, dst: str) -> None:
    """A hard link to another game's copy; a symbolic one to where it really is when that is on
    another filesystem (a big disk kept elsewhere, dumps/<game>/<disk> a link to it)."""
    try:
        os.link(src, dst)
    except OSError:
        os.symlink(os.path.realpath(src), dst)


def _cue_layout(cue: str) -> list[tuple[str, int]]:
    """A one-FILE cue sheet's tracks as chdman's info names them: (TYPE, FRAMES)."""
    names = {"MODE1/2048": "MODE1", "MODE1/2352": "MODE1_RAW", "MODE2/2336": "MODE2", "MODE2/2048": "MODE2_FORM1",
             "MODE2/2324": "MODE2_FORM2", "MODE2/2352": "MODE2_RAW", "AUDIO": "AUDIO"}
    tracks, starts = [], []
    with open(cue) as fh:
        for line in fh:
            w = line.split()
            if w[:1] == ["TRACK"] and len(w) >= 3:
                tracks.append(names.get(w[2], w[2]))
            elif w[:2] == ["INDEX", "01"] and len(w) >= 3:
                m, sec, f = (int(x) for x in w[2].split(":"))
                starts.append((m * 60 + sec) * 75 + f)
    binf = cue[:-4] + ".bin"
    if len(tracks) != len(starts) or not os.path.exists(binf):
        return []
    sizes = {"MODE1": 2048, "MODE1_RAW": 2352, "MODE2": 2336, "MODE2_FORM1": 2048, "MODE2_FORM2": 2324,
             "MODE2_RAW": 2352, "AUDIO": 2352}
    total = os.path.getsize(binf)
    out = []
    for k, t in enumerate(tracks):
        if k + 1 < len(tracks):
            out.append((t, starts[k + 1] - starts[k]))
        else:
            used = sum(f * sizes.get(tt, 2352) for tt, f in out)
            out.append((t, (total - used) // sizes.get(t, 2352)))
    return out


def extract(chd: str, outdir: str) -> tuple[str, str]:
    """Extract chd into outdir: (path, kind). A CD gives NAME.cue + NAME.bin, a DVD
    NAME.iso, a hard disk NAME.img. Kept if already there and the right size."""
    ci = info(chd)
    os.makedirs(outdir, exist_ok=True)
    name = os.path.splitext(os.path.basename(chd))[0]
    base = os.path.join(outdir, name)
    cmd0 = None if use_chdcat() else capped(chdman_command([os.path.dirname(os.path.abspath(chd)), outdir]))
    if ci.kind == "cd":
        cue, binf = base + ".cue", base + ".bin"
        if os.path.exists(cue) and os.path.exists(binf) and os.path.getsize(binf) > 0:
            print(f"have     {os.path.basename(cue)}")
            return cue, "cd"
        # Sets that share a disc (a revision's Video CD is its parent's): link the other game's copy
        # when the CHD's track layout (TYPE, FRAMES) is the same as its cue sheet's
        for other in glob.glob(os.path.join(os.path.dirname(os.path.abspath(outdir)), "*", name + ".cue")):
            obin = other[:-4] + ".bin"
            if os.path.exists(obin) and _cue_layout(other) == [(t["type"], t["frames"]) for t in ci.tracks]:
                _link(obin, binf)
                shutil.copyfile(other, cue)
                print(f"linked   {os.path.basename(cue)} (same disc as {os.path.basename(os.path.dirname(other))})")
                return cue, "cd"
        tmp = base + ".part"
        os.makedirs(tmp, exist_ok=True)
        if cmd0 is None:
            # hiraku-chdcat: the .bin, and the cue sheet (which names the CHD) beside it as .bin.cue
            _run_with_progress([chdcat(), chd, os.path.join(tmp, name + ".bin")], os.path.basename(chd))
            with open(os.path.join(tmp, name + ".bin.cue")) as fh:
                lines = fh.read().splitlines()
            lines[0] = f'FILE "{name}.bin" BINARY'
            with open(os.path.join(tmp, name + ".cue"), "w", newline="\n") as fh:
                fh.write("\n".join(lines) + "\n")
            os.remove(os.path.join(tmp, name + ".bin.cue"))
        else:
            _run_with_progress(cmd0 + ["extractcd", "-f", "-i", chd, "-o", os.path.join(tmp, name + ".cue"),
                                       "-ob", os.path.join(tmp, name + ".bin")], os.path.basename(chd))
        for f in (name + ".cue", name + ".bin"):
            os.replace(os.path.join(tmp, f), os.path.join(outdir, f))
        shutil.rmtree(tmp, ignore_errors=True)
        return cue, "cd"
    verb, ext = {"dvd": ("extractdvd", ".iso"), "hd": ("extracthd", ".img")}.get(ci.kind, ("extractraw", ".bin"))
    out = base + ext
    if os.path.exists(out) and os.path.getsize(out) == ci.logical:
        print(f"have     {os.path.basename(out)}")
        return out, ci.kind
    # Sets that share a disk (bm3rdmix/bm3rdmixe, bmcompm2/hmcompm2...): link the other game's copy.
    for other in glob.glob(os.path.join(os.path.dirname(os.path.abspath(outdir)), "*", name + ext)):
        if os.path.getsize(other) == ci.logical:
            _link(other, out)
            print(f"linked   {os.path.basename(out)} (same disk as {os.path.basename(os.path.dirname(other))})")
            return out, ci.kind
    free = shutil.disk_usage(outdir).free
    if ci.logical > free:
        raise ChdError(f"{chd}: needs {ci.logical / 1e9:.1f} GB, {free / 1e9:.1f} GB free in {outdir}")
    tmp = out + ".part"
    if cmd0 is None:
        _run_with_progress([chdcat(), chd, tmp], os.path.basename(chd))
    else:
        _run_with_progress(cmd0 + [verb, "-f", "-i", chd, "-o", tmp], os.path.basename(chd))
    if os.path.getsize(tmp) != ci.logical:
        raise ChdError(f"{chd}: extracted {os.path.getsize(tmp)} bytes, expected {ci.logical}")
    if ci.kind == "hd":
        # Hard disks are mostly zeros (DJ Main ~80 %, beatmania III ~40 %): store those as holes.
        if shutil.which("fallocate"):
            subprocess.run(["fallocate", "--dig-holes", tmp], check=False)
    os.replace(tmp, out)
    return out, ci.kind


if __name__ == "__main__":
    for path in sys.argv[1:]:
        i = info(path)
        print(f"{path}: {i.kind}, {i.logical} bytes, sha1 {i.sha1}")
