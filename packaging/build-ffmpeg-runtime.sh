#!/usr/bin/env bash
# Maintainer-only build: tools and application libraries come from one source
# configuration. Never install into /usr or replace a user's system FFmpeg.
# Framework builds are private SDK/test fixtures, NOT deployed bundle contents.
set -euo pipefail

vdqt_script_dir=$(CDPATH= cd -- "${BASH_SOURCE[0]%/*}" && pwd -P)
vdqt_manifest="$vdqt_script_dir/ffmpeg-runtime.json"
vdqt_work_dir=
vdqt_prefix=
vdqt_jobs=3
while (($#)); do
    case "$1" in
        --work-dir) vdqt_work_dir=${2:?}; shift 2 ;;
        --prefix) vdqt_prefix=${2:?}; shift 2 ;;
        --jobs) vdqt_jobs=${2:?}; shift 2 ;;
        --help)
            printf 'Usage: bash %s --work-dir ABSOLUTE_PATH --prefix ABSOLUTE_PATH [--jobs 3]\n' "$0"
            exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done
if [[ "$vdqt_work_dir" != /* || "$vdqt_prefix" != /* || "$vdqt_work_dir" == / || "$vdqt_prefix" == / || ! "$vdqt_jobs" =~ ^[1-9][0-9]*$ ]]; then
    printf 'Supply task-specific absolute build/install paths and a positive job count.\n' >&2
    exit 2
fi
for vdqt_tool in git cmake make autoreconf pkg-config python3; do
    command -v "$vdqt_tool" >/dev/null || { printf 'Missing build tool: %s\n' "$vdqt_tool" >&2; exit 1; }
done
mkdir -p "$vdqt_work_dir" "$vdqt_prefix"
vdqt_work_dir=$(CDPATH= cd -- "$vdqt_work_dir" && pwd -P)
vdqt_prefix=$(CDPATH= cd -- "$vdqt_prefix" && pwd -P)
if [[ "$vdqt_prefix" == /usr || "$vdqt_prefix" == /usr/local || "$vdqt_prefix" == / ]]; then
    printf 'The runtime must have a private install prefix, not %s.\n' "$vdqt_prefix" >&2
    exit 2
fi

# Installing a new build over a different recorded configuration can leave
# obsolete DSOs around. Refuse that case rather than cleaning a broad directory.
python3 - "$vdqt_manifest" "$vdqt_prefix" <<'PY'
import json, pathlib, sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text())
record = pathlib.Path(sys.argv[2]) / 'ffmpeg-runtime-manifest.json'
if record.exists() and json.loads(record.read_text()) != manifest:
    raise SystemExit('Existing prefix belongs to another configuration; choose a new prefix.')
PY

vdqt_checkout() {
    local vdqt_name=$1 vdqt_url vdqt_tag vdqt_commit vdqt_actual
    mapfile -t vdqt_fields < <(python3 - "$vdqt_manifest" "$vdqt_name" <<'PY'
import json, sys
with open(sys.argv[1]) as stream:
    source = json.load(stream)['sources'][sys.argv[2]]
for key in ('url', 'tag', 'commit'):
    print(source[key])
PY
    )
    vdqt_url=${vdqt_fields[0]}; vdqt_tag=${vdqt_fields[1]}; vdqt_commit=${vdqt_fields[2]}
    if [[ ! -d "$vdqt_work_dir/$vdqt_name/.git" ]]; then
        git clone --depth 1 --branch "$vdqt_tag" "$vdqt_url" "$vdqt_work_dir/$vdqt_name"
    fi
    vdqt_actual=$(git -C "$vdqt_work_dir/$vdqt_name" rev-parse HEAD)
    [[ "$vdqt_actual" == "$vdqt_commit" ]] || { printf 'Unexpected %s revision: %s\n' "$vdqt_name" "$vdqt_actual" >&2; exit 1; }
    if [[ "$vdqt_name" == ffmpeg ]] && git -C "$vdqt_work_dir/ffmpeg" apply --reverse --check "$vdqt_script_dir/ffmpeg-vapoursynth-optional.patch" 2>/dev/null; then
        # Verify the entire tracked diff, not just the patched hunks. Extra
        # source edits cannot silently become part of this reproducible build.
        local vdqt_expected_diff
        vdqt_expected_diff=$(git -c core.abbrev=7 -C "$vdqt_work_dir/ffmpeg" diff --binary --no-ext-diff --no-textconv --src-prefix=a/ --dst-prefix=b/ HEAD)
        [[ "$vdqt_expected_diff" == "$(<"$vdqt_script_dir/ffmpeg-vapoursynth-optional.patch")" ]] || { printf 'Unexpected FFmpeg source changes.\n' >&2; exit 1; }
    else
        [[ -z $(git -C "$vdqt_work_dir/$vdqt_name" diff --name-only HEAD) ]] || { printf 'Tracked %s source changes are not permitted.\n' "$vdqt_name" >&2; exit 1; }
    fi
}
vdqt_checkout avisynth
vdqt_checkout vapoursynth
vdqt_checkout ffmpeg
if ! git -C "$vdqt_work_dir/ffmpeg" apply --reverse --check "$vdqt_script_dir/ffmpeg-vapoursynth-optional.patch" 2>/dev/null; then
    git -C "$vdqt_work_dir/ffmpeg" apply "$vdqt_script_dir/ffmpeg-vapoursynth-optional.patch"
fi

export PKG_CONFIG_PATH="$vdqt_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$vdqt_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CPPFLAGS="-I$vdqt_prefix/include${CPPFLAGS:+ $CPPFLAGS}"

cmake -S "$vdqt_work_dir/avisynth" -B "$vdqt_work_dir/avisynth-build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$vdqt_prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib -DENABLE_PLUGINS=OFF
cmake --build "$vdqt_work_dir/avisynth-build" --parallel "$vdqt_jobs"
cmake --install "$vdqt_work_dir/avisynth-build"

vdqt_cython_version=$(python3 - "$vdqt_manifest" <<'PY'
import json, sys
with open(sys.argv[1]) as stream:
    print(json.load(stream)['build_tools']['cython'])
PY
)
if [[ ! -x "$vdqt_work_dir/build-tools/bin/cython" ]] || \
    ! PYTHONPATH="$vdqt_work_dir/build-tools" python3 -c 'import Cython, sys; sys.exit(Cython.__version__ != sys.argv[1])' "$vdqt_cython_version"; then
    python3 -m pip install --no-deps --upgrade --target "$vdqt_work_dir/build-tools" "Cython==$vdqt_cython_version"
fi
# R70 reads VAPOURSYNTH_VERSION relative to make's working directory. Use its
# task-owned source checkout, as upstream's recipe does, rather than generating
# an empty VS_CURRENT_RELEASE macro in an unsupported out-of-tree build.
(export PYTHONPATH="$vdqt_work_dir/build-tools${PYTHONPATH:+:$PYTHONPATH}"; \
    cd "$vdqt_work_dir/vapoursynth" && autoreconf -fi && \
    ./configure --prefix="$vdqt_prefix" --libdir="$vdqt_prefix/lib" \
        --enable-core --enable-vsscript --enable-python-module --disable-vspipe --disable-static \
        --with-cython="$vdqt_work_dir/build-tools/bin/cython" && \
    make -j"$vdqt_jobs" && make install)

mapfile -t vdqt_configure < <(python3 - "$vdqt_manifest" <<'PY'
import json, sys
with open(sys.argv[1]) as stream:
    print('\n'.join(json.load(stream)['configure']))
PY
)
mkdir -p "$vdqt_work_dir/ffmpeg-build"
(cd "$vdqt_work_dir/ffmpeg-build" && \
    "$vdqt_work_dir/ffmpeg/configure" --prefix="$vdqt_prefix" --libdir="$vdqt_prefix/lib" \
        --extra-cflags="-I$vdqt_prefix/include" --extra-ldflags="-L$vdqt_prefix/lib" \
        "${vdqt_configure[@]}" && \
    make -j"$vdqt_jobs" && make install)
install -m 0644 "$vdqt_manifest" "$vdqt_prefix/ffmpeg-runtime-manifest.json"
install -m 0644 "$vdqt_script_dir/ffmpeg-vapoursynth-optional.patch" "$vdqt_prefix/ffmpeg-vapoursynth-optional.patch"
"$vdqt_prefix/bin/ffmpeg" -version
"$vdqt_prefix/bin/ffprobe" -version
printf 'Matched runtime installed at %s. Assemble and validate it before deployment.\n' "$vdqt_prefix"
