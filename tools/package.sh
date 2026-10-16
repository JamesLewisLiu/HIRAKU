#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A copy of a build for another Linux PC (a test laptop):
#
#   tools/package.sh BUILD_DIR OUT.tar.xz [portable]
#
# portable: the package has a folder hiraku/user, which makes it a portable install
# (runtime/shell/userdir.h): its settings and saves stay in that folder, not in ~/.config and
# ~/.local/share, so it moves (or goes on a USB stick) as one.
#
# BUILD_DIR should be configured for it:
#   cmake -S . -B BUILD_DIR -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON \
#     '-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc' '-DCMAKE_BUILD_RPATH=$ORIGIN/lib'
# (CMAKE_BUILD_RPATH, not -Wl,-rpath: ninja hands the flags to a shell, which eats $ORIGIN)
# (no FluidSynth, the C++ runtime linked in, SDL3 looked for beside the binaries).
# Faster with profile-guided optimisation: add -DHK_PGO_DIR=<this PC's build/pgo> (tools/pgo.py)
#
# The archive holds hiraku/bin (the binaries, stripped, with libSDL3 in bin/lib),
# hiraku/games, hiraku/docs and an empty hiraku/dumps: the binaries take the folder
# above them as their root. Game data goes in with tools/pack-data.py.
set -euo pipefail
build=${1:?build dir}
out=${2:?output .tar.xz}
mode=${3:-}
case $mode in ""|portable) ;; *) echo "the third argument is 'portable' or nothing" >&2; exit 2 ;; esac
root=$(cd "$(dirname "$0")/.." && pwd)
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
d=$stage/hiraku
mkdir -p "$d/bin/lib" "$d/dumps"
[ "$mode" = portable ] && mkdir -p "$d/user/config" "$d/user/data"
for f in "$build"/hiraku "$build"/hiraku-*; do
  [ -x "$f" ] && [ -f "$f" ] || continue
  case $(basename "$f") in hiraku-recomp) continue ;; esac
  strip -o "$d/bin/$(basename "$f")" "$f"
  # Konami Python games (runtime/python/) are built in PCSX2 Reliquary's tree and linked
  # into the build dir: they need Reliquary's resources/ beside them, and the libraries
  # built for it outside the system (oracle/pcsx2/build-reliquary.sh's deps/), which
  # they find in bin/lib through their $ORIGIN/lib run path
  real=$(readlink -f "$f")
  if [ -d "$(dirname "$real")/resources" ]; then
    [ -d "$d/bin/resources" ] || cp -r "$(dirname "$real")/resources" "$d/bin/resources"
    ldd "$real" | awk '$3 ~ /^\// && $3 !~ /^\/(usr\/)?lib(64)?\// {print $3}' | while read -r lib; do
      cp -L "$lib" "$d/bin/lib/"
    done
  fi
done
# The launcher (bin/hiraku) with its own Qt, so a PC without Qt 6.12 runs it: the Qt libraries
# and everything they need but the system's own (glibc, the graphics and windowing stack, fonts,
# D-Bus, glib, OpenSSL, sound) in bin/qt/lib, the plugins it uses in bin/qt/plugins (qt.conf).
# No run paths can be written into copied libraries here (no patchelf), so bin/hiraku is a
# script that sets LD_LIBRARY_PATH for bin/hiraku.bin, which puts the user's own back once Qt is
# up (launcher/main.cpp): the games and tools it starts never see the bundled Qt.
bundle_qt() {
  local qtplugins=/usr/lib/qt6/plugins
  [ -d "$qtplugins" ] || qtplugins=$(dirname "$(ldd "$d/bin/hiraku.bin" | awk '/libQt6Core/ {print $3}')")/qt6/plugins
  mkdir -p "$d/bin/qt/lib" "$d/bin/qt/plugins"
  local p
  for p in platforms/libqwayland.so platforms/libqxcb.so platforms/libqoffscreen.so \
           wayland-shell-integration/libxdg-shell.so wayland-decoration-client/libbradient.so \
           wayland-graphics-integration-client/libqt-plugin-wayland-egl.so \
           xcbglintegrations/libqxcb-egl-integration.so xcbglintegrations/libqxcb-glx-integration.so \
           platformthemes/libqxdgdesktopportal.so platforminputcontexts/libcomposeplatforminputcontextplugin.so \
           tls/libqopensslbackend.so tls/libqcertonlybackend.so; do
    [ -f "$qtplugins/$p" ] || continue
    mkdir -p "$d/bin/qt/plugins/$(dirname "$p")"
    cp -L "$qtplugins/$p" "$d/bin/qt/plugins/$p"
  done
  { ldd "$d/bin/hiraku.bin"; find "$d/bin/qt/plugins" -name '*.so' -exec ldd {} \; ; } |
    awk '$2 == "=>" && $3 ~ /^\// {print $3}' | sort -u | while read -r lib; do
      local n; n=$(basename "$lib")
      case $n in
        libxcb-icccm*|libxcb-image*|libxcb-keysyms*|libxcb-render-util*|libxcb-util*|libxcb-cursor*|libxcb-ewmh*) ;;
        ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|libutil.so*|libanl.so*|libmvec.so*) continue ;;
        libGL*|libGLX*|libOpenGL*|libEGL*|libGLES*|libdrm*|libgbm*|libvulkan*) continue ;;
        libwayland-*|libX*|libxcb*|libxkbcommon*|libxshmfence*) continue ;;
        libfontconfig*|libfreetype*|libharfbuzz*|libgraphite2*|libexpat*|libz.so*|libffi*) continue ;;
        libdbus-1*|libsystemd*|libudev*|libglib-2.0*|libgobject-2.0*|libgio-2.0*|libgmodule-2.0*|libgthread-2.0*) continue ;;
        libSDL3*) continue ;;   # in bin/lib, for the games too
        libssl*|libcrypto*|libasound*|libpulse*|libselinux*|libmount*|libblkid*|libuuid*|libcap.so*) continue ;;
      esac
      cp -L "$lib" "$d/bin/qt/lib/$n"
    done
  printf '[Paths]\nPrefix = qt\nPlugins = plugins\n' > "$d/bin/qt.conf"
  cat > "$d/bin/hiraku" <<'WRAP'
#!/bin/sh
# HIRAKU's launcher with the Qt in bin/qt (tools/package.sh; launcher/main.cpp puts these back)
here=$(dirname "$(readlink -f "$0")")
HK_SAVED_ENV=1; export HK_SAVED_ENV
[ "${LD_LIBRARY_PATH+set}" = set ] && { HK_SAVED_LD_LIBRARY_PATH=$LD_LIBRARY_PATH; export HK_SAVED_LD_LIBRARY_PATH; }
[ "${QT_PLUGIN_PATH+set}" = set ] && { HK_SAVED_QT_PLUGIN_PATH=$QT_PLUGIN_PATH; export HK_SAVED_QT_PLUGIN_PATH; }
LD_LIBRARY_PATH="$here/qt/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"; export LD_LIBRARY_PATH
unset QT_PLUGIN_PATH
exec "$here/hiraku.bin" "$@"
WRAP
  chmod +x "$d/bin/hiraku"
}
if [ -f "$d/bin/hiraku" ]; then
  mv "$d/bin/hiraku" "$d/bin/hiraku.bin"
  bundle_qt
fi
sdl=""
for f in "$build"/hiraku "$build"/hiraku-*; do   # the first binary that has SDL (hiraku-eamuse hasn't)
  [ -n "$sdl" ] || sdl=$(ldd "$f" 2>/dev/null | awk '/libSDL3/ {print $3}')
done
[ -n "$sdl" ] && cp -L "$sdl" "$d/bin/lib/"
cp -r "$root/games" "$root/docs" "$root/shaders" "$d/"
cp -r "$root/oracle/scripts" "$d/scripts"
mkdir -p "$d/tools"
cp "$root"/tools/{ingest.py,chd.py,fbexe.py,pack-data.py,regress.py,unlock.py,pythonfs.py,chdread.py,mameset.py,catalogue.py} "$d/tools/"
# MAME's BEMANI sets as data: ingest tells a set by its contents (tools/mameset.py)
cp -r "$root/tools/data" "$d/tools/"
# e-AMUSEMENT (Konami Python): the server checker and the game's calls (docs/EAMUSE3.md)
mkdir -p "$d/tools/eamuse" && cp "$root"/tools/eamuse/{check.py,corpus.json} "$d/tools/eamuse/"
mkdir -p "$d/tools/eamuse/bemaniutils" && cp "$root"/tools/eamuse/bemaniutils/{README.md,popn9-python.patch} "$d/tools/eamuse/bemaniutils/"
# Konami Python ingest reads disks in place through libchdr (tools/chdread.py)
[ -f "$root/build/libchdr_mini.so" ] && mkdir -p "$d/build" && cp "$root/build/libchdr_mini.so" "$d/build/"
cp "$root/LICENSE" "$d/" 2>/dev/null || true
# the licences of the code taken from other projects, where Help > About HIRAKU reads them
for f in $(grep -o '"third_party/[^"]*"' "$root/launcher/aboutdialog.cpp" | tr -d '"'); do
	mkdir -p "$d/$(dirname "$f")" && cp "$root/$f" "$d/$f"
done
cat > "$d/README.txt" <<'EOF'
HIRAKU, a build for another Linux PC.

Needs glibc 2.38 or later (Ubuntu 24.04, Fedora 39, Debian 13, Arch and newer).
SDL3 is included, and the launcher's Qt (bin/qt: bin/hiraku is a script that starts
bin/hiraku.bin with it). From the system: the graphics drivers, X11 or Wayland, fontconfig,
D-Bus, glib and OpenSSL, which every desktop has.

Game data, either way:
- ingest here, as on the main PC (Python 3.11+, 7z, and chdman for CHDs):
      tools/ingest.py --help
- or copy it from a PC that has it ingested:
      tools/pack-data.py /path/to/this/hiraku GAME...
  (dumps/GAME/ and its disc images, with relative paths in machine.txt). Saved settings and NVRAM live in
~/.local/share/hiraku/GAME (copy them over for games that need a first-boot setup), and the
launcher's settings in ~/.config/hiraku.

A portable install: with a folder named user in this folder (tools/package.sh ... portable makes
one; or create it), the saves go in user/data/GAME and the launcher's settings in user/config, and
this folder always runs as itself wherever it is moved: copy it to another PC or a USB stick and
everything comes along. To bring an existing setup in, copy ~/.local/share/hiraku/* to user/data/
and ~/.config/hiraku/* to user/config/.

Konami Python games (bin/hiraku-popn9 … hiraku-popn14): Vulkan or OpenGL drivers from the system;
--renderer vk (or gl) draws on the GPU, the default software renderer on the CPU.
Their e-AMUSEMENT (pop'n music 9: cards, players, rankings) runs offline inside the game
by default; bin/hiraku-eamuse --port 8080 is the same server for several PCs, which then run
with --eamuse http://that-pc:8080/ (docs/EAMUSE3.md; tools/eamuse/check.py checks a server).

Run a game:
    bin/hiraku-popn8 --window          (--window 3 for 3x, --fullscreen)
    bin/hiraku-popn8 --window --picture light   (a CRT: light or full; F10 cycles off/light/full)
    bin/hiraku-popn8 --help            (all options)
Each run prints its frame times when it ends ("frametime:": host work per frame,
mean, percentiles and the worst frame). A repeatable test, no window:
    bin/hiraku-bm1stmix --frames 12600 --script scripts/bm1stmix-play.his
    bin/hiraku-popn8 --frames 7200
A run without a window starts from blank NVRAM unless given --nvram DIR (kbm, ppp
and pop'n 6 need theirs: --nvram ~/.local/share/hiraku/kbm, or user/data/kbm when portable).

Smooth frames (display sync, on by default): on a 60, 120 or 240 Hz display every frame
stays up the same number of refreshes, and pop'n runs 2.6 % faster to match (docs/PLAY.md,
"Frame pacing"). The console says what it chose ("display sync: on: 60.00 Hz display, ...");
the exit report's "video: display sync" line says how it went ("held their N refreshes",
"irregular"). --display-sync off: the exact speed, for a variable-refresh display.
EOF
# xz with a window wider than one binary: the binaries share most of their code,
# so the archive is a quarter of a .tar.gz (about 14 MB)
tar -C "$stage" -cf - hiraku | xz -9e -T1 --lzma2=preset=9e,dict=192MiB > "$out"
ls -la "$out"
