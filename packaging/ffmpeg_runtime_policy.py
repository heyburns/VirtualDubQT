"""Libraries that must remain coherent with the host desktop and AVS plugins.

FFmpeg and codec libraries are private. Graphics/font dispatchers and the C++
runtime are not: Linux reuses an already loaded SONAME even when a later plugin
asks for a newer implementation. A private libva plus the host's libva-x11, for
example, prevents L-SMASH from loading. These are ordinary runtime dependencies,
not permission to use a system FFmpeg executable or FFmpeg library.
"""

import re


SYSTEM_LIBRARY = re.compile(
    r"^(?:ld-linux[^/]*|lib(?:c|m|pthread|dl|rt|resolv|util|nss_[^/]+)\.so(?:\..*)?)$")
HOST_PLATFORM_LIBRARY = re.compile(
    r"^lib(?:va(?:-[^.]+)?|drm(?:_[^.]+)?|"
    r"fontconfig|freetype|cairo(?:-[^.]+)?|pixman-1|pango[^/]*|harfbuzz[^/]*|"
    r"X[A-Za-z0-9_-]+|xcb(?:-[^.]+)?|wayland(?:-[^.]+)?|"
    r"EGL|GL(?:X|dispatch)?|OpenGL|gbm|vulkan|"
    r"stdc\+\+|gcc_s|asound|pulse(?:-[^.]+)?|pipewire-0\.3|jack)\.so(?:\..*)?$")


def is_host_library(name):
    return bool(SYSTEM_LIBRARY.fullmatch(name) or HOST_PLATFORM_LIBRARY.fullmatch(name))
