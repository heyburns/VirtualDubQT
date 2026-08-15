// VirtualDub - Video processing and capture application
// Internal filter library
//
// Copyright (C) 1998-2011 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include <vd2/system/vdtypes.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/math.h>
#include <vd2/system/vdstl.h>
#include <vd2/VDXFrame/VideoFilter.h>
#include <vd2/VDLib/Dialog.h>
#include <vd2/VDLib/UIProxies.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/plugin/vdvideoaccel.h>
#include "resource.h"
