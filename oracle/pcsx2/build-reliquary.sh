#!/usr/bin/env bash
# Build PCSX2 Reliquary (pcsx2-qt) on CachyOS/Arch with system packages + a small local prefix.
# Every heavy step runs under a systemd memory cap, -j8.
#
# System packages used (already installed): clang lld cmake ninja qt6-base qt6-tools qt6-svg
#   qt6-imageformats sdl3 shaderc libpng libjpeg-turbo libwebp lz4 zstd freetype2 fontconfig
#   curl libpcap libusb ffmpeg vulkan-headers wayland libx11 libxrandr dbus
# Built locally into $DEPS (not packaged on Arch): libbacktrace, extra-cmake-modules,
#   KDDockWidgets 2.4.1, plutovg 1.3.2, plutosvg 0.0.7, rapidyaml 0.12.1
#
# HIRAKU: `patch` applies reliquary-oracle.patch to the unpacked source (`all` does it before
# configuring): the oracle's per-vsync RAM hashes (PLAN.md §21 PY-0), the keystore kept off
# stdout, and the taps and CMake entry point for HIRAKU's Python core (runtime/python/), which
# this build compiles too, as build/bin/hiraku-<game>. The source is the release zip
# pcsx2-reliquary-1.9.4-reliquary.zip unpacked to $ROOT/pcsx2-reliquary.
#
# Usage: build-reliquary.sh [deps|patch|configure|build|pgo|all]   (default: all; pgo after a build:
#        ~5 min, about 3 % less CPU for pop'n 9, results bit-identical)
set -euo pipefail

ROOT=${HK_PYTHON_WORK:-/run/media/roganis/DATA/Python-work}
SRC=$ROOT/pcsx2-reliquary
BUILD=$SRC/build
DEPS=$ROOT/deps
DLS=$ROOT/deps-src
JOBS=8
CAP=(systemd-run --user --scope --quiet -p MemoryMax=10G -p MemorySwapMax=2G)

HIRAKU=$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)   # this checkout: its Python core is built here too

export CC=clang CXX=clang++
LDF="-fuse-ld=lld"

dl() { # url file sha256
	[ -f "$DLS/$2" ] || curl -sSL --fail -o "$DLS/$2" "$1"
	echo "$3  $DLS/$2" | sha256sum -c --quiet -
}

cm_dep() { # dir extra-args...
	local d=$1; shift
	cmake -S "$d" -B "$d/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$DEPS" -DCMAKE_PREFIX_PATH="$DEPS" \
		-DCMAKE_SHARED_LINKER_FLAGS_INIT=$LDF -DCMAKE_EXE_LINKER_FLAGS_INIT=$LDF "$@"
	"${CAP[@]}" ninja -C "$d/build" -j$JOBS install
}

deps() {
	mkdir -p "$DLS" "$DEPS"
	dl https://github.com/ianlancetaylor/libbacktrace/archive/ad106d5fdd5d960bd33fae1c48a351af567fd075/libbacktrace-ad106d5fdd5d960bd33fae1c48a351af567fd075.tar.gz \
		libbacktrace-ad106d5fdd5d960bd33fae1c48a351af567fd075.tar.gz 96e5c2d7f2c482a60d5804da48a2eb9a0db0719b2c65dcc169fbfdcf37f3a45d
	dl https://github.com/KDE/extra-cmake-modules/archive/refs/tags/v6.19.0.tar.gz \
		extra-cmake-modules-6.19.0.tar.gz a7f59fc5dcaa6927f0dafea427dd5734d00ea100ad8ddceb2a8c640291d7f527
	dl https://github.com/KDAB/KDDockWidgets/archive/v2.4.1/KDDockWidgets-2.4.1.tar.gz \
		KDDockWidgets-2.4.1.tar.gz 5cd9495d9316cf6cc937bb328a7fd491c3248ef2469cfa42511f1039f6148aca
	dl https://github.com/sammycage/plutovg/archive/v1.3.2/plutovg-1.3.2.tar.gz \
		plutovg-1.3.2.tar.gz 7bd4e79ce18b1d47517e7e91fbb7cf19d4f01942804a519bc7c0bf32b6325dd5
	dl https://github.com/sammycage/plutosvg/archive/v0.0.7/plutosvg-0.0.7.tar.gz \
		plutosvg-0.0.7.tar.gz 78561b571ac224030cdc450ca2986b4de915c2ba7616004a6d71a379bffd15f3
	dl https://github.com/biojppm/rapidyaml/releases/download/v0.12.1/rapidyaml-0.12.1-src.tgz \
		rapidyaml-0.12.1-src.tgz e9efcdd17f86287748793cf21d106e461fcad8d103a3e5a23632afe93828660d

	local W=$DLS/work
	rm -rf "$W" && mkdir -p "$W" && cd "$W"
	for f in "$DLS"/*.tar.gz "$DLS"/*.tgz; do tar xf "$f"; done

	(cd libbacktrace-ad106d5fdd5d960bd33fae1c48a351af567fd075 &&
		./configure --prefix="$DEPS" --with-pic &&
		"${CAP[@]}" make -j$JOBS && make install)
	cm_dep extra-cmake-modules-6.19.0 -DBUILD_TESTING=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF
	cm_dep KDDockWidgets-2.4.1 -DKDDockWidgets_QT6=true -DKDDockWidgets_EXAMPLES=false -DKDDockWidgets_FRONTENDS=qtwidgets -DKDDockWidgets_TESTS=false
	cm_dep plutovg-1.3.2 -DBUILD_SHARED_LIBS=ON -DPLUTOVG_BUILD_EXAMPLES=OFF
	cm_dep plutosvg-0.0.7 -DBUILD_SHARED_LIBS=ON -DPLUTOSVG_ENABLE_FREETYPE=OFF -DPLUTOSVG_BUILD_EXAMPLES=OFF
	cm_dep rapidyaml-0.12.1-src -DBUILD_SHARED_LIBS=ON
	cd "$ROOT" && rm -rf "$W"
}

patch_src() {
	local P
	P=$(dirname "$(readlink -f "$0")")/reliquary-oracle.patch
	if (cd "$SRC" && patch -p1 --dry-run -R -s -f < "$P" >/dev/null 2>&1); then
		echo "oracle patch already applied"
	else
		(cd "$SRC" && patch -p1 < "$P")
	fi
}

# Profile-guided optimisation (PLAN.md section 21, PY-3): `pgo` builds the Python games
# instrumented in build-pgogen/, plays each game's oracle/scripts/<game>-play.his with both
# GS renderers, and merges the counts into $PGO_PROF; `configure` uses the profile whenever
# it exists (HK_PY_PGO=0: not). Derived from the games, so it stays out of the repo.
PGO_PROF=$ROOT/pgo/python.profdata
PGO_USE=(-Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-profile-instr-missing)

configure() {
	local flags=()
	if [ -f "$PGO_PROF" ] && [ "${HK_PY_PGO:-1}" != 0 ]; then
		flags=("-DCMAKE_C_FLAGS=-fprofile-instr-use=$PGO_PROF ${PGO_USE[*]}" "-DCMAKE_CXX_FLAGS=-fprofile-instr-use=$PGO_PROF ${PGO_USE[*]}")
		echo "profile-guided: $PGO_PROF"
	else
		flags=("-DCMAKE_C_FLAGS=" "-DCMAKE_CXX_FLAGS=")
	fi
	configure_dir "$BUILD" "${flags[@]}"
}

configure_dir() {
	local dir=$1; shift
	# The source tarball has an empty pcsx2/GS/parallel-gs (uninitialised git submodule);
	# CMake only checks IS_DIRECTORY, so drop the empty dir to build without the optional paraLLEl-GS renderer.
	rmdir "$SRC/pcsx2/GS/parallel-gs" 2>/dev/null || true
	cmake -S "$SRC" -B "$dir" -G Ninja "$@" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
		-DCMAKE_EXE_LINKER_FLAGS_INIT=$LDF -DCMAKE_MODULE_LINKER_FLAGS_INIT=$LDF -DCMAKE_SHARED_LINKER_FLAGS_INIT=$LDF \
		-DCMAKE_PREFIX_PATH="$DEPS" \
		-DCMAKE_BUILD_RPATH="$DEPS/lib;\$ORIGIN/lib" \
		-DUSE_LINKED_FFMPEG=OFF \
		-DENABLE_TESTS=OFF \
		-DWAYLAND_SCANNER=OFF \
		-DHIRAKU_DIR="$HIRAKU"
	# ^ $ORIGIN/lib: a packaged copy (tools/package.sh) finds the local deps beside the binary.
	#   FFmpeg is loaded at run time, only for video capture, so the binaries start without it.
	#   pcsx2-qt takes its Wayland pointer-lock protocol XMLs from the (absent) parallel-gs submodule;
	#   hiding wayland-scanner skips only that optional relative-pointer feature.
}

python_games() {
	sed -n 's/^hiraku_python_game(\(.*\))$/hiraku-\1/p' "$HIRAKU/runtime/python/CMakeLists.txt"
}

pgo() {
	local gen=$SRC/build-pgogen raw=$ROOT/pgo/raw g id script
	rm -rf "$raw" && mkdir -p "$raw"
	configure_dir "$gen" -DCMAKE_C_FLAGS=-fprofile-instr-generate -DCMAKE_CXX_FLAGS=-fprofile-instr-generate \
		-DCMAKE_EXE_LINKER_FLAGS=-fprofile-instr-generate
	"${CAP[@]}" ninja -C "$gen" -j$JOBS $(python_games)
	ln -sfn "$BUILD/bin/resources" "$gen/bin/resources"   # the main build's (pcsx2-qt puts them there)
	for g in $(python_games); do
		id=${g#hiraku-}
		script=$HIRAKU/oracle/scripts/$id-play.his
		[ -f "$script" ] || { echo "pgo: no $script, $id not trained"; continue; }
		for r in sw vk; do
			echo "pgo: training $id ($r)"
			LLVM_PROFILE_FILE="$raw/$id-$r-%p.profraw" "${CAP[@]}" "$gen/bin/$g" --renderer $r --script "$script" \
				--out "$raw/out-$id-$r" > "$raw/$id-$r.log" 2>&1
		done
	done
	llvm-profdata merge -o "$PGO_PROF" "$raw"/*.profraw
	echo "pgo: $PGO_PROF ($(du -h "$PGO_PROF" | cut -f1)); rebuilding with it"
	configure && build
}

build() {
	# pcsx2-qt is the oracle; hiraku-<game> are HIRAKU's Python core binaries (runtime/python/)
	local games
	games=$(python_games)
	"${CAP[@]}" ninja -C "$BUILD" -j$JOBS pcsx2-qt $games
	# where the launcher and tools/regress.py look for game binaries (the link keeps them
	# next to Reliquary's resources/, which they find through /proc/self/exe)
	mkdir -p "$HIRAKU/build/cmake"
	for g in $games; do ln -sf "$BUILD/bin/$g" "$HIRAKU/build/cmake/$g"; done
}

case "${1:-all}" in
	deps) deps ;;
	patch) patch_src ;;
	pgo) pgo ;;
	configure) configure ;;
	build) build ;;
	all) deps && patch_src && configure && build ;;
	*) echo "usage: $0 [deps|patch|configure|build|pgo|all]"; exit 1 ;;
esac
