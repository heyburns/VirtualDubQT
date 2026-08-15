// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2005 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef BACKFACE_H
#define BACKFACE_H

#include <vd2/system/VDString.h>

#ifdef _DEBUG
	#define VD_BACKFACE_ENABLED 1
#endif

#if VD_BACKFACE_ENABLED
	class VDBackfaceService;
	class VDBackfaceClass;
	class VDBackfaceObjectBase;

	///////////////////////////////////////////////////////////////////////////
	class IVDBackfaceOutput {
	public:
		virtual void operator<<(const char *s) = 0;
		virtual void operator()(const char *format, ...) = 0;
		virtual VDStringA GetTag(VDBackfaceObjectBase *p) = 0;
		virtual VDStringA GetBlurb(VDBackfaceObjectBase *p) = 0;
	};

	///////////////////////////////////////////////////////////////////////////
	class VDBackfaceObjectNode {
	public:
		VDBackfaceObjectNode *mpObjPrev, *mpObjNext;
	};

	class VDBackfaceObjectBase : private VDBackfaceObjectNode {
		friend VDBackfaceService;
	public:
		VDBackfaceObjectBase() {}
		VDBackfaceObjectBase(const VDBackfaceObjectBase&);
		~VDBackfaceObjectBase();

	protected:
		VDBackfaceClass *BackfaceInitClass(const char *shortname, const char *longname);
		void BackfaceInitObject(VDBackfaceClass *);

	private:
		virtual void BackfaceDumpObject(IVDBackfaceOutput&);
		virtual void BackfaceDumpBlurb(IVDBackfaceOutput&);

		VDBackfaceClass *mpClass;
		uint32	mInstance;
	};

	template<class T>
	class VDBackfaceObject : public VDBackfaceObjectBase {
	public:
		VDBackfaceObject() {
			static VDBackfaceClass *spClass = BackfaceInitClass(T::BackfaceGetShortName(), T::BackfaceGetLongName());

			BackfaceInitObject(spClass);
		}
	};

	///////////////////////////////////////////////////////////////////////////

	void VDBackfaceOpenConsole();
#else
	class IVDBackfaceStream {};
	struct VDBackfaceObjectBase {};
	template<class T> class VDBackfaceObject : public VDBackfaceObjectBase{};

	void VDBackfaceOpenConsole();
#endif

#endif
