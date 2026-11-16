#!/usr/bin/env bash
# Build only in the Ubuntu release builder. Do not import its platform libraries
# into FFmpeg's private directory or bundle separately installed script frameworks.
set -euo pipefail
vdqt_build=${1:?build directory}
vdqt_runtime=${2:?private runtime directory}
vdqt_sources=${3:?matching third-party source directory}
vdqt_output=${4:?absolute output AppImage path}
vdqt_repo=$(CDPATH= cd -- "${BASH_SOURCE[0]%/*}/.." && pwd -P)
vdqt_work=$(mktemp -d "${TMPDIR:-/tmp}/vdqt-appimage-XXXXXXXX")
vdqt_build=$(realpath "$vdqt_build")
vdqt_runtime=$(realpath "$vdqt_runtime")
vdqt_sources=$(realpath "$vdqt_sources")
cd "$vdqt_work"
curl --fail --location --retry 3 -o linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20250213-2/linuxdeploy-x86_64.AppImage
curl --fail --location --retry 3 -o linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x ./*.AppImage
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE=$(command -v qmake6)
export EXTRA_PLATFORM_PLUGINS=libqoffscreen.so
export LD_LIBRARY_PATH="$vdqt_runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Exclude every installed host-platform SONAME, using the very same policy as
# runtime assembly. FFmpeg and AviSynth must not be redeployed into usr/lib by
# linuxdeploy, which would defeat the application's fixed-path validation.
mapfile -t vdqt_host_libraries < <(ldconfig -p | PYTHONPATH="$vdqt_repo/packaging" python3 -c '
import sys
from ffmpeg_runtime_policy import is_host_library
print("\n".join(sorted({line.split()[0] for line in sys.stdin if line.split() and is_host_library(line.split()[0])})))')
vdqt_exclusions=()
for vdqt_library in "${vdqt_host_libraries[@]}" \
    'libavisynth.so*' 'libvapoursynth*.so*' 'libpython*.so*' \
    'libavcodec.so*' 'libavdevice.so*' 'libavfilter.so*' 'libavformat.so*' \
    'libavutil.so*' 'libpostproc.so*' 'libswresample.so*' 'libswscale.so*'; do
    vdqt_exclusions+=(--exclude-library "$vdqt_library")
done
./linuxdeploy-x86_64.AppImage --appdir AppDir \
    --executable "$vdqt_build/VirtualDubQt" \
    --desktop-file "$vdqt_repo/packaging/virtualdubqt.desktop" \
    --icon-file "$vdqt_repo/packaging/virtualdubqt.png" \
    --custom-apprun "$vdqt_repo/packaging/AppRun" --plugin qt "${vdqt_exclusions[@]}"
if [[ -d AppDir/apprun-hooks ]]; then
    mv AppDir/apprun-hooks AppDir/vdqt-apprun-hooks
fi
if [[ -f AppDir/AppRun.wrapped ]]; then
    # This is linuxdeploy's generated wrapper, not user data.
    mv AppDir/AppRun.wrapped "$vdqt_work/generated-AppRun.wrapped"
fi
install -m755 "$vdqt_repo/packaging/AppRun" AppDir/AppRun
mkdir -p AppDir/usr/lib/virtualdubqt
mkdir AppDir/usr/lib/virtualdubqt/ffmpeg-runtime
tar -C "$vdqt_runtime" --exclude='./include' --exclude='./lib/pkgconfig' -cf - . \
    | tar -C AppDir/usr/lib/virtualdubqt/ffmpeg-runtime -xf -
# The application was linked with the native package layout. It gets its private
# backend first, with deployed Qt next; codec-only dependencies stay nested.
patchelf --set-rpath '$ORIGIN/../lib/virtualdubqt/ffmpeg-runtime/lib:$ORIGIN/../lib' AppDir/usr/bin/VirtualDubQt
install -Dm644 "$vdqt_repo/LICENSE" AppDir/usr/share/licenses/virtualdubqt/LICENSE
python3 "$vdqt_repo/packaging/collect-release-sources.py" --payload AppDir \
    --notices AppDir/usr/share/licenses/virtualdubqt/dependencies --sources "$vdqt_sources" \
    --skip-private-runtime
if find AppDir -name 'libavisynth.so*' -o -name 'libvapoursynth*.so*' -o -name 'libpython*.so*' | grep -q .; then
    printf 'A separately installed framework leaked into the AppImage\n' >&2
    exit 1
fi
# Invoke the pinned tool directly after deployment. Running linuxdeploy again
# would rewrite the carefully preserved nested private runtime's ELF paths.
./linuxdeploy-x86_64.AppImage --appimage-extract > /dev/null
ARCH=x86_64 squashfs-root/plugins/linuxdeploy-plugin-appimage/usr/bin/appimagetool AppDir "$vdqt_output"
printf 'AppDir retained for validation: %s/AppDir\n' "$vdqt_work"
