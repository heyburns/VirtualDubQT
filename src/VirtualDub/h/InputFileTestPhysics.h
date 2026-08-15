// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2007 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_VD2_INPUTFILETESTPHYSICS_H
#define f_VD2_INPUTFILETESTPHYSICS_H

#include <vd2/system/vdstl.h>

#ifdef _MSC_VER
	#pragma once
#endif

struct VDTestVidPhysPartPos {
	sint16 mX;
	sint16 mY;
};

struct VDTestVidPhysFrame {
	float mTriRotation;
	int mFirstParticle;
	int mParticleCount;
};

class VDTestVidPhysVideo {
public:
	vdfastvector<VDTestVidPhysFrame> mFrames;
	vdfastvector<VDTestVidPhysPartPos> mParticles;
};

///////////////////////////////////////////////////////////////////////////////

class VDTestVidPhysSimulator {
public:
	VDTestVidPhysSimulator();
	~VDTestVidPhysSimulator();

	void Step(float dt);

	void EncodeFrame(VDTestVidPhysVideo& video);

protected:
	struct Particle {
		float x;
		float y;
		float vx;
		float vy;
	};

	enum {
		kMaxParticles = 100
	};

	int			mParticleCount;
	float		mTriAngle;
	float		mTriAngVel;
	float		mFirePhase;

	Particle	mParticles[kMaxParticles];
};

#endif
