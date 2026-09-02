#pragma once

#include <QIcon>

// Returns the canonical VirtualDub2 application icon embedded as generated PNG
// byte arrays in VDQtApplicationIcon.cpp. Keeping the pixels in the executable
// lets desktop/window-manager code use the icon without a runtime asset path.
QIcon vdqtApplicationIcon();
