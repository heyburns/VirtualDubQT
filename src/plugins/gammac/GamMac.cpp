/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

/*
    Some parts may be liberated from Avisynth source code.
    http://avisynth2.cvs.sourceforge.net/avisynth2/

    v1.07, Bug fixed in GuessGamma(), previously found gamma could have been slightly nearer than result.
	       (The binary chop is linear, but results after Gamma mod are not, so we should track best Gamma mod).
	v1.08, GuessGamma(), min,max limited to omin,omax, regression from bug introduced in 1.07.
			Made GetFrame a little easier to understand (better named scalar variables)..
			Correction of oMax limits, from 239->255 to 235->255, ie permissable StudioRGB range was wrong.
    v1.09, GamMax limiting not working proper, fixed.
	       RGB multipliers now not applied when RngLim limited.
    v1.10, Added Dither arg. Messed with RngLim.

*/

// ----------------------------------------------------------------
//	Compiling for Avisynth v2.58 & v2.60/x86, v2.60/x64, under VS2008
//
// For AVS+, also need additional Avisynth+ headers somewhere in an AVS directory,
//
// AVS
//   alignment.h
//   avisynth.h
//   capi.h
//   config.h
//   cpuid.h
//   minmax.h
//   types.h
//   win.h
//
// Point Menu/Tools/Options/Projects and Solutions/VC Directories/ :Include Files: Win32 and x64, to the AVS Parent.
//
// For x64, add '_WIN64' to Preprocessor Definitions (for both C++/Preprocessor & Resources/General).
//        (Do NOT delete any 'WIN32' entry).
// For Avs v2.5, Add 'AVISYNTH_PLUGIN_25' to Preprocessor Definitions (for both C++/Preprocessor & Resources/General).
//   Avs v2.5 includes AVISYNTH_INTERFACE_VERSION=3 as avisynth25.h
//   Avs+ v2.6/x86/x64 includes AVISYNTH_INTERFACE_VERSION=6 as avisynth.h
// ----------------------------------------------------------------


#define GAMAC_VER "1.10"

#include "win_compat.h"
#include <avisynth/avisynth.h>
#include <math.h>
#include <stdio.h>
#include "DDigit.h"

alignas(64) static const BYTE ditherMap[256] = {
#if 0
  // default 0231 recursed table
  0x00, 0x80, 0x20, 0xA0,  0x08, 0x88, 0x28, 0xA8,  0x02, 0x82, 0x22, 0xA2,  0x0A, 0x8A, 0x2A, 0xAA,
  0xC0, 0x40, 0xE0, 0x60,  0xC8, 0x48, 0xE8, 0x68,  0xC2, 0x42, 0xE2, 0x62,  0xCA, 0x4A, 0xEA, 0x6A,
  0x30, 0xB0, 0x10, 0x90,  0x38, 0xB8, 0x18, 0x98,  0x32, 0xB2, 0x12, 0x92,  0x3A, 0xBA, 0x1A, 0x9A,
  0xF0, 0x70, 0xD0, 0x50,  0xF8, 0x78, 0xD8, 0x58,  0xF2, 0x72, 0xD2, 0x52,  0xFA, 0x7A, 0xDA, 0x5A,

  0x0C, 0x8C, 0x2C, 0xAC,  0x04, 0x84, 0x24, 0xA4,  0x0E, 0x8E, 0x2E, 0xAE,  0x06, 0x86, 0x26, 0xA6,
  0xCC, 0x4C, 0xEC, 0x6C,  0xC4, 0x44, 0xE4, 0x64,  0xCE, 0x4E, 0xEE, 0x6E,  0xC6, 0x46, 0xE6, 0x66,
  0x3C, 0xBC, 0x1C, 0x9C,  0x34, 0xB4, 0x14, 0x94,  0x3E, 0xBE, 0x1E, 0x9E,  0x36, 0xB6, 0x16, 0x96,
  0xFC, 0x7C, 0xDC, 0x5C,  0xF4, 0x74, 0xD4, 0x54,  0xFE, 0x7E, 0xDE, 0x5E,  0xF6, 0x76, 0xD6, 0x56,

  0x03, 0x83, 0x23, 0xA3,  0x0B, 0x8B, 0x2B, 0xAB,  0x01, 0x81, 0x21, 0xA1,  0x09, 0x89, 0x29, 0xA9,
  0xC3, 0x43, 0xE3, 0x63,  0xCB, 0x4B, 0xEB, 0x6B,  0xC1, 0x41, 0xE1, 0x61,  0xC9, 0x49, 0xE9, 0x69,
  0x33, 0xB3, 0x13, 0x93,  0x3B, 0xBB, 0x1B, 0x9B,  0x31, 0xB1, 0x11, 0x91,  0x39, 0xB9, 0x19, 0x99,
  0xF3, 0x73, 0xD3, 0x53,  0xFB, 0x7B, 0xDB, 0x5B,  0xF1, 0x71, 0xD1, 0x51,  0xF9, 0x79, 0xD9, 0x59,

  0x0F, 0x8F, 0x2F, 0xAF,  0x07, 0x87, 0x27, 0xA7,  0x0D, 0x8D, 0x2D, 0xAD,  0x05, 0x85, 0x25, 0xA5,
  0xCF, 0x4F, 0xEF, 0x6F,  0xC7, 0x47, 0xE7, 0x67,  0xCD, 0x4D, 0xED, 0x6D,  0xC5, 0x45, 0xE5, 0x65,
  0x3F, 0xBF, 0x1F, 0x9F,  0x37, 0xB7, 0x17, 0x97,  0x3D, 0xBD, 0x1D, 0x9D,  0x35, 0xB5, 0x15, 0x95,
  0xFF, 0x7F, 0xDF, 0x5F,  0xF7, 0x77, 0xD7, 0x57,  0xFD, 0x7D, 0xDD, 0x5D,  0xF5, 0x75, 0xD5, 0x55,
#else
  // improved "equal sum" modified table
  0x00, 0xB0, 0x60, 0xD0,  0x0B, 0xBB, 0x6B, 0xDB,  0x06, 0xB6, 0x66, 0xD6,  0x0D, 0xBD, 0x6D, 0xDD,
  0xC0, 0x70, 0x90, 0x20,  0xCB, 0x7B, 0x9B, 0x2B,  0xC6, 0x76, 0x96, 0x26,  0xCD, 0x7D, 0x9D, 0x2D,
  0x30, 0x80, 0x50, 0xE0,  0x3B, 0x8B, 0x5B, 0xEB,  0x36, 0x86, 0x56, 0xE6,  0x3D, 0x8D, 0x5D, 0xED,
  0xF0, 0x40, 0xA0, 0x10,  0xFB, 0x4B, 0xAB, 0x1B,  0xF6, 0x46, 0xA6, 0x16,  0xFD, 0x4D, 0xAD, 0x1D,

  0x0C, 0xBC, 0x6C, 0xDC,  0x07, 0xB7, 0x67, 0xD7,  0x09, 0xB9, 0x69, 0xD9,  0x02, 0xB2, 0x62, 0xD2,
  0xCC, 0x7C, 0x9C, 0x2C,  0xC7, 0x77, 0x97, 0x27,  0xC9, 0x79, 0x99, 0x29,  0xC2, 0x72, 0x92, 0x22,
  0x3C, 0x8C, 0x5C, 0xEC,  0x37, 0x87, 0x57, 0xE7,  0x39, 0x89, 0x59, 0xE9,  0x32, 0x82, 0x52, 0xE2,
  0xFC, 0x4C, 0xAC, 0x1C,  0xF7, 0x47, 0xA7, 0x17,  0xF9, 0x49, 0xA9, 0x19,  0xF2, 0x42, 0xA2, 0x12,

  0x03, 0xB3, 0x63, 0xD3,  0x08, 0xB8, 0x68, 0xD8,  0x05, 0xB5, 0x65, 0xD5,  0x0E, 0xBE, 0x6E, 0xDE,
  0xC3, 0x73, 0x93, 0x23,  0xC8, 0x78, 0x98, 0x28,  0xC5, 0x75, 0x95, 0x25,  0xCE, 0x7E, 0x9E, 0x2E,
  0x33, 0x83, 0x53, 0xE3,  0x38, 0x88, 0x58, 0xE8,  0x35, 0x85, 0x55, 0xE5,  0x3E, 0x8E, 0x5E, 0xEE,
  0xF3, 0x43, 0xA3, 0x13,  0xF8, 0x48, 0xA8, 0x18,  0xF5, 0x45, 0xA5, 0x15,  0xFE, 0x4E, 0xAE, 0x1E,

  0x0F, 0xBF, 0x6F, 0xDF,  0x04, 0xB4, 0x64, 0xD4,  0x0A, 0xBA, 0x6A, 0xDA,  0x01, 0xB1, 0x61, 0xD1,
  0xCF, 0x7F, 0x9F, 0x2F,  0xC4, 0x74, 0x94, 0x24,  0xCA, 0x7A, 0x9A, 0x2A,  0xC1, 0x71, 0x91, 0x21,
  0x3F, 0x8F, 0x5F, 0xEF,  0x34, 0x84, 0x54, 0xE4,  0x3A, 0x8A, 0x5A, 0xEA,  0x31, 0x81, 0x51, 0xE1,
  0xFF, 0x4F, 0xAF, 0x1F,  0xF4, 0x44, 0xA4, 0x14,  0xFA, 0x4A, 0xAA, 0x1A,  0xF1, 0x41, 0xA1, 0x11,
#endif
};


// NOT USED
__declspec(align(16)) static const BYTE ditherMap4[16] = {
  0x0, 0xB, 0x6, 0xD,
  0xC, 0x7, 0x9, 0x2,
  0x3, 0x8, 0x5, 0xE,
  0xF, 0x4, 0xA, 0x1,
};


class GamMac : public GenericVideoFilter {
private:
	PClip			dc;
    const int       LockChan;
    const int       Scale;
    const double    RedMul;
    const double    GrnMul;
    const double    BluMul;
    const double    loTh;
    const double    hiTh;
	const double    LockVal;
    const int		RngLim;
    const double    GamMax;
	const int		xx;				// dc coords
	const int		yy;				// dc coords
	const int		ww;				// dc coords
	const int		hh;				// dc coords
	const int		omin;			// output target
	const int		omax;			// output target
    const bool      Show;
    const int       Verbosity;
	const bool		Dither;
    //
	double			rgbMul[3];
    unsigned int    *cnt[3];
    BYTE            *lut[3];
	BYTE            *map[3];
    //
    void   CountDcRGB(int n,IScriptEnvironment* env);
	//
    double ChanAve(unsigned int *cnt,unsigned int Pixels);
    int ChanMin(unsigned int *cnt,double th,unsigned int Pixels);
    int ChanMax(unsigned int *cnt,double th,unsigned int Pixels);
    double ChanAveFromLut(unsigned int *cnt,BYTE *lut,unsigned int Pixels);
    double GuessGamma(int in_min,int in_max,int out_min,int out_max,unsigned int *cnt,double reqAve,unsigned int Pixels,bool *GammErr);
    void   SetGammaLut(int in_min,int in_max,int out_min,int out_max,double gamma,BYTE *lut);
    void   SetDitherMap(int in_min,int in_max,int out_min,int out_max,double gamma,BYTE *map);
    void DrawFString(PVideoFrame &dst,int x,int y,const char *format, ...);
    enum{FMT_BUFSZ = 2048};         // Biggish Buffer size for DrawFString().
    char formatted_buf[FMT_BUFSZ];  // enough for whatever, hopefully
public:
    GamMac(PClip _child,PClip _dc,
			int _LockChan,int _Scale,double _RedMul,double _GrnMul,double _BluMul,
			double _loTh,double _hiTh,double _LockVal,
            int _RngLim,double _GamMax,
            int _xx,int _yy,int _ww,int _hh,int _omin,int _omax,
			bool _Show,int _Verbosity,bool _dither,
            IScriptEnvironment* env);
    ~GamMac();
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
};

GamMac::GamMac(PClip _child,PClip _dc,
			int _LockChan,int _Scale,double _RedMul,double _GrnMul,double _BluMul,
			double _loTh,double _hiTh,double _LockVal,
            int _RngLim,double _GamMax,
            int _xx,int _yy,int _ww,int _hh,int _omin,int _omax,
			bool _Show,int _Verbosity,bool _dither,
            IScriptEnvironment* env) :
        GenericVideoFilter(_child),
			dc(_dc),
			LockChan(_LockChan),Scale(_Scale),RedMul(_RedMul),GrnMul(_GrnMul),BluMul(_BluMul),
			loTh(_loTh),hiTh(_hiTh),LockVal(_LockVal),
            RngLim(_RngLim),GamMax(_GamMax),
            xx(_xx),yy(_yy),ww(_ww),hh(_hh),omin(_omin),omax(_omax),
			Show(_Show),Verbosity(_Verbosity),Dither(_dither) {
	DPRINTF("Constructor IN")
    const char *myName="GamMac: ";
	rgbMul[0]=RedMul;	rgbMul[1]=GrnMul;	rgbMul[2]=BluMul;
    cnt[0]=cnt[1]=cnt[2] = NULL;
	lut[0]=lut[1]=lut[2] = NULL;
	map[0]=map[1]=map[2] = NULL;
	const int mapsz=(Dither) ? 256*256 : 0;
	DPRINTF("Constructor Alloc MEM")
    if  (
        ((cnt[0] = new unsigned int[256])==NULL) ||
        ((cnt[1] = new unsigned int[256])==NULL) ||
        ((cnt[2] = new unsigned int[256])==NULL) ||
        ((lut[0] = new         BYTE[256])==NULL) ||
        ((lut[1] = new         BYTE[256])==NULL) ||
        ((lut[2] = new         BYTE[256])==NULL) ||
			(
				(Dither) &&
				(((map[0]=new BYTE[mapsz])==NULL)||((map[1]=new BYTE[mapsz])==NULL)||((map[2]=new BYTE[mapsz])==NULL))
			)
        ) {
        if(map[2]!=NULL)  {delete [] map[2]; map[2]=NULL;}
        if(map[1]!=NULL)  {delete [] map[1]; map[1]=NULL;}
        if(map[0]!=NULL)  {delete [] map[0]; map[0]=NULL;}
        if(lut[2]!=NULL)  {delete [] lut[2]; lut[2]=NULL;}
        if(lut[1]!=NULL)  {delete [] lut[1]; lut[1]=NULL;}
        if(lut[0]!=NULL)  {delete [] lut[0]; lut[0]=NULL;}
        if(cnt[2]!=NULL)  {delete [] cnt[2]; cnt[2]=NULL;}
        if(cnt[1]!=NULL)  {delete [] cnt[1]; cnt[1]=NULL;}
        if(cnt[0]!=NULL)  {delete [] cnt[0]; cnt[0]=NULL;}
		env->ThrowError("%sError allocating buffers",myName);
    }
	DPRINTF("Constructor OUT")
}

GamMac::~GamMac() {
	DPRINTF("Destructor IN")
    if(map[2]!=NULL)  {delete [] map[2]; map[2]=NULL;}
    if(map[1]!=NULL)  {delete [] map[1]; map[1]=NULL;}
    if(map[0]!=NULL)  {delete [] map[0]; map[0]=NULL;}
    if(lut[2]!=NULL)  {delete [] lut[2]; lut[2]=NULL;}
    if(lut[1]!=NULL)  {delete [] lut[1]; lut[1]=NULL;}
    if(lut[0]!=NULL)  {delete [] lut[0]; lut[0]=NULL;}
    if(cnt[2]!=NULL)  {delete [] cnt[2]; cnt[2]=NULL;}
    if(cnt[1]!=NULL)  {delete [] cnt[1]; cnt[1]=NULL;}
    if(cnt[0]!=NULL)  {delete [] cnt[0]; cnt[0]=NULL;}
	DPRINTF("Destructor OUT")
}




void GamMac::CountDcRGB(int n,IScriptEnvironment* env) {
	// vi and vi2 are same colorspace and number of frames, so can use vi for n and ColorSpace.
    n = (n<0) ? 0 : (n>= vi.num_frames) ? vi.num_frames - 1 : n;
	DPRINTF("%d ] CountDcRGB IN",n)
	unsigned int *R=cnt[0],*G=cnt[1],*B=cnt[2];
    memset(R,0,sizeof(R[0])*256);	memset(G,0,sizeof(G[0])*256);	memset(B,0,sizeof(B[0])*256);
    PVideoFrame src =   dc->GetFrame(n, env);
    int height      =   src->GetHeight();
    int pitch       =   src->GetPitch();
	const int step  =	(vi.IsRGB32()) ? 4 : 3;
    const BYTE *srcp=   src->GetReadPtr() + (height-1-yy) * pitch + (xx * step);
	const int woff = ww * step;
    if(vi.IsRGB32()) {
        for(int y=hh;--y>=0;) {
            for(int x=woff;(x-=4)>=0;) {
                ++B[srcp[x+0]];
                ++G[srcp[x+1]];
                ++R[srcp[x+2]];
            }
            srcp -= pitch;
        }
    } else {
        for(int y=hh;--y>=0;) {
            for(int x=woff;(x-=3)>=0;) {
                ++B[srcp[x+0]];
                ++G[srcp[x+1]];
                ++R[srcp[x+2]];
            }
            srcp -= pitch;
        }
    }
	DPRINTF("%d ] CountDcRGB OUT",n)
}


double GamMac::ChanAve(unsigned int *cnt,unsigned int Pixels) {
	DPRINTF("ChanAve IN")
    __int64 acc=0;
    for(int i=256;--i>=0;) {acc += cnt[i] * __int64(i);}
	DPRINTF("ChanAve OUT")
    return ((double)acc / Pixels);
}

int GamMac::ChanMin(unsigned int *cnt,double th,unsigned int Pixels) {
	DPRINTF("ChanMin IN")
    const unsigned int lim = (unsigned int)(Pixels * th);       // making smaller than pixels, no overflow
    int rmin=255;
    unsigned int sm=0;
    for(int i=0;i<256;++i) {
        sm += cnt[i];
        if(sm > lim) {rmin=i; break;}
    }
	DPRINTF("ChanMin OUT")
    return rmin;
}

int GamMac::ChanMax(unsigned int *cnt,double th,unsigned int Pixels) {
	DPRINTF("ChanMax IN")
    const unsigned int lim = (unsigned int)(Pixels * th);       // making smaller than Pixels, no overflow
    int rmax=0;
    unsigned int sm=0;
    for(int i=256;--i>=0;) {
        sm += cnt[i];
        if(sm > lim) {rmax=i; break;}
    }
	DPRINTF("ChanMax OUT")
    return rmax;
}

double GamMac::ChanAveFromLut(unsigned int *cnt,BYTE *lut,unsigned int Pixels) {
	DPRINTF("ChanAveFromLut IN")
    __int64 acc=0;
    for(int i=256;--i>=0;) {
        acc += __int64(cnt[i]) * lut[i];
    }
	DPRINTF("ChanAveFromLut OUT")
    return ((double)acc / Pixels);
}

double GamMac::GuessGamma(int in_min,int in_max,int out_min,int out_max,unsigned int *cnt,double reqAve,
		unsigned int Pixels, bool *GamErr) {
	DPRINTF("GuessGamma IN")
    int divisor = in_max - in_min + (in_max == in_min);
    double result=1.0;
	const double orng  = ((double)out_max)-out_min;
	const double round = 0.5 + out_min;
	double PrevAve=-1.0;
	double Bestgmid=GamMax;
	double BestDif=256.0;
	double ghi=GamMax;
	double glo=1.0/ghi;
	double GamMin=glo;
	bool err=false;
	while(glo < ghi) {
		double gmid = (glo + ghi) / 2.0;
		const double igam = 1.0/gmid;
		__int64 acc = 0;
		for(int i=256;--i>=0;) {
			double v = double(i - in_min) / divisor;
			v = pow(min(max(v, 0.0), 1.0), igam);
	        v = (v * orng) + round;
			int val = int(floor(v));                // Round towards -ve infinity
			if (val > out_max)   val = out_max;
			else if(val < out_min) val = out_min;
			acc += __int64(cnt[i]) * val;
		}
		double ave = double(acc) / Pixels;
		double dif=fabs(ave-reqAve);
		if(dif<BestDif) {							// closer gmid not necessarily better match.
			Bestgmid=gmid;							// gmid linear bisect, ave dif result after gamma is not linear.
			BestDif=dif;
		}
		if(dif<0.0001) {
			result = Bestgmid;						// close enough
			break;
		} else if(fabs(ave-PrevAve)<=0.000001)  {
			result = Bestgmid;						// not getting better
			err = (GamMax-ghi < 0.001 || glo-GamMin < 0.001);
			break;
		} else if(ave<reqAve) {
			glo=gmid;
		} else {
			ghi=gmid;
		}
		PrevAve=ave;
	}
	*GamErr=err;
	DPRINTF("GuessGamma OUT")
    return result;
}

void GamMac::SetGammaLut(int in_min,int in_max,int out_min,int out_max,double gamma,BYTE *lut) {
	DPRINTF("SetGammaLut IN")
    if(in_min==0 && in_max==255 && out_min==0 && out_max==255) {
        if(fabs(gamma-1.0)<0.0001) {
            for(int i=256;--i>=0;lut[i]=i);
        } else {
            const double igam = 1.0/gamma;
            for(int i=256;--i>=0;) {
                double v = i / 255.0;					// in_min=0, in_max=255, divisor = 255.0
                v = pow(min(max(v, 0.0), 1.0), igam);
                v = (v * 255.0) + 0.5;					// orng = 255.0, round = 0.5
                int val = int(floor(v));                // Round towards -ve infinity
                if (val > 255)		val = 255;			// out_max = 255
                else if(val < 0)	val = 0;			// out_min = 0
                lut[i] = val;
            }
        }
    } else {
		const double orng  = ((double)out_max)-out_min;
		const double round = 0.5 + out_min;
        const int divisor = in_max - in_min + (in_max == in_min);
        const double igam = 1.0/gamma;
        for(int i=256;--i>=0;) {
            double v = double(i - in_min) / divisor;
            v = pow(min(max(v, 0.0), 1.0), igam);
            v = (v * orng) + round;
            int val = int(floor(v));					// Round towards -ve infinity
            if (val > out_max)		val = out_max;
            else if(val < out_min)	val = out_min;
            lut[i] = val;
        }
    }
	DPRINTF("SetGammaLut OUT")
}



void GamMac::SetDitherMap(int in_min,int in_max,int out_min,int out_max,double gamma,BYTE *map) {
	DPRINTF("SetDitherMap IN")

	const int scale_in_min = in_min * 256;
	const int scale_in_max = in_max * 256;

	const double orng  = ((double)out_max)-out_min;
	const double round = 0.5 + out_min;
    const int divisor = scale_in_max - scale_in_min + (scale_in_max == scale_in_min);
    const double igam = 1.0/gamma;
    for(int i=(256*256);--i>=0;) {
        double v = double(i - scale_in_min - 127.5) / divisor;
        v = pow(min(max(v, 0.0), 1.0), igam);
        v = (v * orng) + round;
        int val = int(floor(v));					// Round towards -ve infinity
        if (val > out_max)		val = out_max;
        else if(val < out_min)	val = out_min;
        map[i] = val;
    }
	DPRINTF("SetDitherMap OUT")
}


PVideoFrame __stdcall GamMac::GetFrame(int n, IScriptEnvironment* env) {
    n = (n<0) ? 0 : (n>= vi.num_frames) ? vi.num_frames - 1 : n;			// valid range limit frame n.
	DPRINTF("%d ] GetFrame IN",n)

	double gamma[3]={1.0,1.0,1.0};
	const int OutRng=omax-omin;

	unsigned int dcPixels=ww*hh;

	int	   lockch   = -1;					// These vary with frame content.
    double lockval  = -1.0;
	int    frmScale = Scale;

	int  raw_Min[3];						// ChanMin(cnt[i],max(loTh,0.0));
	int  raw_Max[3];						// ChanMax(cnt[i],max(hiTh,0.0));
	bool raw_Bad[3];						// (raw_Max[i]-raw_Min[i]<RngLim);
	int  in_Min[3];							// in_Min[i]=(loTh>=0.0) ? raw_Min[i] : 0;
	int  in_Max[3];							// in_Max[i]=(hiTh>=0.0) ? raw_Max[i] : 255;
	int  in_Rng[3];							// in_Max[i]-in_Min[i]

    double in_Ave[3],scaleAve[3];			// In Init, in_Ave[i]=scaleAve[i]=ChanAve(cnt[i];
	double reqAve[3];
	bool gamErr[3]={false,false,false};

	CountDcRGB(n,env);

	int i;
	for(i=0;i<3;++i) {						// Init
		raw_Min[i]=ChanMin(cnt[i],max(loTh,0.0),dcPixels);
		raw_Max[i]=ChanMax(cnt[i],max(hiTh,0.0),dcPixels);
		raw_Bad[i]=(raw_Max[i]-raw_Min[i]<RngLim);
		in_Min[i]=(loTh>=0.0) ? raw_Min[i] : 0;
		in_Max[i]=(hiTh>=0.0) ? raw_Max[i] : 255;
		in_Rng[i]=in_Max[i]-in_Min[i];
		in_Ave[i]=scaleAve[i]=reqAve[i]=ChanAve(cnt[i],dcPixels);
	}

	bool raw_AtLeastOneBad  = (raw_Bad[0] || raw_Bad[1] || raw_Bad[2]);
	bool raw_AllBad 		= (raw_Bad[0] && raw_Bad[1] && raw_Bad[2]);
	int in_Min_Minimum		= min(min(in_Min[0],in_Min[1]),in_Min[2]);
	int in_Max_Maximum		= max(max(in_Max[0],in_Max[1]),in_Max[2]);


	if(loTh>=0.0 && frmScale!=2) in_Min[0]=in_Min[1]=in_Min[2]=in_Min_Minimum;
	if(hiTh>=0.0 && frmScale!=2) in_Max[0]=in_Max[1]=in_Max[2]=in_Max_Maximum;
	int input_Rng = in_Max_Maximum - in_Min_Minimum;

	if(frmScale==2) {
		// If at least one range bad OR all mins and max the same,
		//     then Knock down to Scale=1
		if(
			raw_AtLeastOneBad ||
			(in_Min[0]==in_Min[1]&&in_Min[0]==in_Min[2] && in_Max[0]==in_Max[1]&&in_Max[0]==in_Max[2])
		  ) {
			frmScale=1;
		}
	}

	// If Full Range and scale==1, then knock down to Scale=0
	if(frmScale==1 && input_Rng==255) {
		frmScale=0;
	}

	if(raw_AllBad) {													// All bad range
		frmScale=0;														// Scale = 0, No scale
		for(i=0;i<3;++i) {
			SetGammaLut(0,255,omin,omax, 1.0 ,lut[i]);					// Linear
		}
	} else {
		if(frmScale==2){		// Scale R,G,B individually in_Rng[x] cannot be 0
			double in_scaleR = (in_Rng[0]==0) ? 1.0 : 255.0 / in_Rng[0];
			scaleAve[0] = min(max((in_Ave[0] - in_Min[0]) * in_scaleR,0.0),255.0);
			double in_scaleG = (in_Rng[1]==0) ? 1.0 : 255.0 / in_Rng[1];
			scaleAve[1] = min(max((in_Ave[1] - in_Min[1]) * in_scaleG,0.0),255.0);
			double in_scaleB = (in_Rng[2]==0) ? 1.0 : 255.0 / in_Rng[2];
			scaleAve[2] = min(max((in_Ave[2] - in_Min[2]) * in_scaleB,0.0),255.0);
		} else if(frmScale==1) {			// Scale all of R,G,B similarly by in_Rng[x]
			double in_scale = (input_Rng==0) ? 1.0 : 255.0 / input_Rng;		// input_Rng cannot be 0
			scaleAve[0] = min(max((in_Ave[0] - in_Min[0]) * in_scale,0.0),255.0);
			scaleAve[1] = min(max((in_Ave[1] - in_Min[1]) * in_scale,0.0),255.0);
			scaleAve[2] = min(max((in_Ave[2] - in_Min[2]) * in_scale,0.0),255.0);
		}

		if(LockChan>=0)			{ lockval=scaleAve[lockch=LockChan]; }						// Use LockChan 0->2
		else if(LockChan==-1)	{ lockval =LockVal; }										// Use LockVar
		else if(LockChan==-2)   { lockval=((scaleAve[0]+scaleAve[1]+scaleAve[2])/3.0); }	// ScaleAve Average
		else {																				// Median
			if(scaleAve[1]>=scaleAve[0]) {
				if(scaleAve[0]>=scaleAve[2])		{ lockval=scaleAve[lockch=0]; }		// R
				else if(scaleAve[1]>=scaleAve[2])	{ lockval=scaleAve[lockch=2]; }		// B
				else								{ lockval=scaleAve[lockch=1]; }		// G
			} else {
				if(scaleAve[1]>=scaleAve[2])		{ lockval=scaleAve[lockch=1]; }		// G
				else if(scaleAve[0]>=scaleAve[2])	{ lockval=scaleAve[lockch=2]; }		// B
				else								{ lockval=scaleAve[lockch=0]; }		// R
			}
		}

		for(i=0;i<3;++i) {
			reqAve[i]= lockval*rgbMul[i];
			bool guess = (OutRng!=255 || in_Rng[i]!=255 || fabs(scaleAve[i]-reqAve[i])>=0.0001);
			gamma[i] = (!guess)?1.0:GuessGamma(in_Min[i],in_Max[i],omin,omax,cnt[i],reqAve[i],dcPixels,&gamErr[i]);
			SetGammaLut(in_Min[i],in_Max[i],omin,omax, gamma[i],lut[i]);
			if(Dither) SetDitherMap(in_Min[i],in_Max[i],omin,omax, gamma[i],map[i]);
		}
	}

	DPRINTF("%d ] GetFrame Render Frame",n)

    PVideoFrame src =   child->GetFrame(n, env);
    PVideoFrame dst =   env->NewVideoFrame(vi);
    int rowsize     =   src->GetRowSize();
    int height      =   src->GetHeight();
    int pitch       =   src->GetPitch();
    int dpitch      =   dst->GetPitch();
    const BYTE *srcp=   src->GetReadPtr();
    BYTE *dstp      =   dst->GetWritePtr();
    int x,y;
    // We process from bottom to top (weird RGB order)
	if(Dither && !raw_AllBad) {
		if(vi.IsRGB32()) {
			for(y=height;--y>=0;) {
		        const int _y = (y << 4) & 0xf0;
				for(x=vi.width;--x>=0;) {
					const int xx=x*4;
					const int _dither = ditherMap[(x&0x0f)|_y];
					dstp[xx+0] = map[2][srcp[xx+0]<<8 | _dither ];
					dstp[xx+1] = map[1][srcp[xx+1]<<8 | _dither ];
					dstp[xx+2] = map[0][srcp[xx+2]<<8 | _dither ];
					dstp[xx+3] = srcp[xx+3];				// copy Alpha
				}
				srcp += pitch;
				dstp += dpitch;
			}
		} else {
			for(y=height;--y>=0;) {
		        const int _y = (y << 4) & 0xf0;
				for(x=vi.width;--x>=0;) {
					const int xx=x*3;
					const int _dither = ditherMap[(x&0x0f)|_y];
					dstp[xx+0] = map[2][srcp[xx+0]<<8 | _dither ];
					dstp[xx+1] = map[1][srcp[xx+1]<<8 | _dither ];
					dstp[xx+2] = map[0][srcp[xx+2]<<8 | _dither ];
				}
				srcp += pitch;
				dstp += dpitch;
			}
		}
	} else {
		if(vi.IsRGB32()) {
			for(y=height;--y>=0;) {
				for(x=rowsize;(x-=4)>=0;) {
					dstp[x+0] = lut[2][srcp[x+0]];
					dstp[x+1] = lut[1][srcp[x+1]];
					dstp[x+2] = lut[0][srcp[x+2]];
					dstp[x+3] = srcp[x+3];				// copy Alpha
				}
				srcp += pitch;
				dstp += dpitch;
			}
		} else {
			for(y=height;--y>=0;) {
				for(x=rowsize;(x-=3)>=0;) {
					dstp[x+0] = lut[2][srcp[x+0]];
					dstp[x+1] = lut[1][srcp[x+1]];
					dstp[x+2] = lut[0][srcp[x+2]];
				}
				srcp += pitch;
				dstp += dpitch;
			}
		}
	}

    if(Show) {
        int yoff=0;
		const char *FC=(LockChan==0)	?"\a20"		// Red 0
			:(LockChan==1)		?"\a41"		// Grn 1
			:(LockChan==2)		?"\a12"		// Blu 2
			:(LockChan==-1)  	?"V"		// LockVar
			:(LockChan==-2)		?"A"		// Mean
			:					((lockch==0)?"\a20":(lockch==1)?"\a41":"\a12");		// Median chan color and No
		int FS = "M-!"[frmScale];													// 0=gray,1=white,2=Orange
		int FL = "2M"[raw_AtLeastOneBad?0:1];										// Range flag Red if any bad, else Gray
		double ig=1.0/GamMax;
		const char *glim[3];
		glim[0]= (gamErr[0]) ? "!":"-";
		glim[1]= (gamErr[1]) ? "!":"-";
		glim[2]= (gamErr[2]) ? "!":"-";
		bool glimit=gamErr[0] || gamErr[1] || gamErr[2];

        if(Verbosity>=5) {
			DrawFString(dst,0,yoff*20,"\aEGamMac v%s",GAMAC_VER);
	        ++yoff;
		}
		if(lockval<0.0) {
			DrawFString(dst,0,yoff*20,"%d] \aTFlags:-\a- %s\a%cS\a%cR\a%sG \a-LockVal=\a2???",n,FC,FS,FL,(glimit)?"!":"M");
		} else {
			DrawFString(dst,0,yoff*20,"%d] \aTFlags:-\a- %s\a%cS\a%cR\a%sG \a-LockVal=%.3f",n,FC,FS,FL,(glimit)?"!":"M",lockval);
		}

        ++yoff;
		if(Verbosity>=1) {
			DrawFString(dst,(11*10),yoff*20,"\a2R         \a4G         \a1B\a-",n);		// R,G,B channels in color
			yoff+=1;
			int FskipR=(raw_Bad[0]) ? '2' : '-';										// Red inputs if bad range else white
			int FskipG=(raw_Bad[1]) ? '2' : '-';
			int FskipB=(raw_Bad[2]) ? '2' : '-';
			if(Verbosity>=4 || raw_AtLeastOneBad || (loTh>=0.0 || hiTh>=0.0)) {
				if(Verbosity>=4 || (raw_AtLeastOneBad && (
					raw_Min[0]!=in_Min[0] || raw_Min[1]!=in_Min[1] || raw_Min[2]!=in_Min[2] ||
					raw_Max[0]!=in_Max[0] || raw_Max[1]!=in_Max[1] || raw_Max[2]!=in_Max[2]
					))) {
					DrawFString(dst,0,yoff*20,"RAW:    \a%c%3d,%-3d\a-   \a%c%3d,%-3d\a-   \a%c%3d,%-3d\a-",
						FskipR,raw_Min[0],raw_Max[0],FskipG,raw_Min[1],raw_Max[1],FskipB,raw_Min[2],raw_Max[2]);
					yoff+=1;
				}
				DrawFString(dst,0,yoff*20,"IN:     \a%c%3d,%-3d\a-   \a%c%3d,%-3d\a-   \a%c%3d,%-3d\a-",
					FskipR,in_Min[0],in_Max[0],FskipG,in_Min[1],in_Max[1],FskipB,in_Min[2],in_Max[2]);
				yoff+=1;
			}
			DrawFString(dst,0,yoff*20,"IN_AVE: \a%c%7.3f\a- : \a%c%7.3f\a- : \a%c%7.3f\a-",
					FskipR,in_Ave[0],FskipG,in_Ave[1],FskipB,in_Ave[2]);
			++yoff;
			if(Verbosity>=4 || FS!='M') {
				DrawFString(dst,0,yoff*20,"SCALED: %7.3f : %7.3f : %7.3f",scaleAve[0],scaleAve[1],scaleAve[2]);
				++yoff;
			}

			DrawFString(dst,0,yoff*20,"GAMMA:  \a%s%7.3f\a- : \a%s%7.3f\a- : \a%s%7.3f",
				glim[0],gamma[0],glim[1],gamma[1],glim[2],gamma[2]);
			++yoff;

			double outAveR=ChanAveFromLut(cnt[0],lut[0],dcPixels);
			double outAveG=ChanAveFromLut(cnt[1],lut[1],dcPixels);
			double outAveB=ChanAveFromLut(cnt[2],lut[2],dcPixels);

			DrawFString(dst,0,yoff*20,"OUTAVE: %7.3f : %7.3f : %7.3f",outAveR,outAveG,outAveB);
			++yoff;

			if(Verbosity>0) {
				yoff=0;
				if(Verbosity>=3) {
					++yoff;
					const char *g = GamMax<10.0 ? "!":"-";
					DrawFString(dst,0,vi.height-(yoff*20),
						"RngLim=%d \a%sGamMax=%6.3f\a- oMin=%d oMax=%d\n",RngLim,g,GamMax,omin,omax);
				}
				yoff+=1;
				double lt=(loTh<0.0) ? loTh : loTh*100.0;
				double ht=(hiTh<0.0) ? hiTh : hiTh*100.0;
				const char *locol=(lt<0.0)?"!":(lt==0.0)?"-":"E";
				const char *hicol=(ht<0.0)?"!":(ht==0.0)?"-":"E";
				const char *col=(LockChan==0)?"2":(LockChan==1)?"4":(LockChan==2)?"1":"-";
				DrawFString(dst,0,vi.height-(yoff*20),"LockChan=\a%s%d\a- Scale=%d loTh=\a%s%.2f\a- hiTh=\a%s%.2f",
					col,LockChan,Scale,locol,lt,hicol,ht);
				if(Verbosity>=4 || (Verbosity>=2 && (RedMul!=1.0 || GrnMul!=1.0 || BluMul!=1.0))) {
					++yoff;
					const char *rcol=(RedMul!=1.0)?"!":"-";
					const char *gcol=(GrnMul!=1.0)?"!":"-";
					const char *bcol=(BluMul!=1.0)?"!":"-";
					DrawFString(dst,0,vi.height-(yoff*20),"RedMul=\a%s%5.3f\a- GrnMul=\a%s%5.3f\a- BluMul=\a%s%5.3f",
						rcol,RedMul,gcol,GrnMul,bcol,BluMul);
				}
			}
		}
    }
	DPRINTF("%d ] GetFrame OUT",n)
    return dst;
}

// DrawFString() Draw a Formatted string of text at pixel position, a user defined member function.
// As with all class members defined outside of the class, it is preceded by the class name and scope operator.

void GamMac::DrawFString(PVideoFrame &dst,int x,int y,const char *format, ...) {
    // NOTE, Frame MUST be writable
    // This member function adds BOTH string and screen/layout formatting,
    // eg '%f' for string and '\n' for screen/layout.

    // Firstly, string formatting, into the buffer implemented in the class instance, formatted_buf[FMT_BUFSZ].
    va_list args;
    va_start(args, format);
    _vsnprintf(formatted_buf,FMT_BUFSZ-1, format, args);
    va_end(args);
    //
    //   Formatting control codes (as provided by InfoF.h):-
    //   '\n', Newline, positioning cursor 1 line down and at left edge of screen.
    //   '\r', Newline Special, moves 1 line down and positions cursor at on-entry X position.
    //   '\b', Backspace, obvious, not sure how useful this will be.
    //   '\f', Forward Space, again obvious, re-uses formfeed code for this. Again, maybe not so useful.
    //   '\t', Tab, @ character positions, every 4 characters.(relative screen LHS).
    //
    if(vi.IsRGB24()) {DrawStringRGB24(dst,x,y,formatted_buf);}
    else             {DrawStringRGB32(dst,x,y,formatted_buf);}
}


// -----------------------------------------------------
// -----------------------------------------------------
// -----------------------------------------------------

class Coords : public GenericVideoFilter {
	const int		xx,yy,ww,hh;
public:
    Coords(PClip _child,int _xx,int _yy,int _ww,int _hh,IScriptEnvironment* env)
		   : GenericVideoFilter(_child),xx(_xx),yy(_yy),ww(_ww),hh(_hh){};
    ~Coords(){};
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
};


PVideoFrame __stdcall Coords::GetFrame(int n, IScriptEnvironment* env) {
    n = (n<0) ? 0 : (n>= vi.num_frames) ? vi.num_frames - 1 : n;
	DPRINTF("%d ] Coords::GetFrame IN",n)
    PVideoFrame dst =   child->GetFrame(n, env);
	env->MakeWritable(&dst);
    int rowsize     =   dst->GetRowSize();
    int height      =   dst->GetHeight();
    int pitch		=   dst->GetPitch();
    BYTE *dstp      =   dst->GetWritePtr();
	const int xstep=(vi.IsRGB32()) ?4:3;
	const int xstepz=xstep*3;
	const int pxz=pitch*3;

	const int x1=xx;
	const int x2=xx+ww-1;
	const int y1=yy;
	const int y2=yy+hh-1;
	int xr,xr2,yr;
	BYTE *rp = dstp + (height-1-y1)*pitch;					// Top Horizontal
	BYTE col=255;
	for(xr=rowsize;(xr-=xstepz)>=0;) {
		rp[xr]	 = col;
		rp[xr+1] = col;
		rp[xr+2] = col;
		col = ~col;
	}

	rp = dstp + (height-1-y2)*pitch;						// Bot Horizontal
	col=255;
	for(xr=rowsize;(xr-=xstepz)>=0;) {
		rp[xr]  = col;
		rp[xr+1]= col;
		rp[xr+2]= col;
		col = ~col;
	}

	rp = dstp + (height-1)*pitch;							// Verticals
	xr = x1*xstep;	xr2=x2*xstep;
	col=255;
	for(yr=height;(yr-=3)>=0;) {
		rp[xr]   = col;
		rp[xr+1] = col;
		rp[xr+2] = col;
		rp[xr2]  = col;
		rp[xr2+1]= col;
		rp[xr2+2]= col;
		col = ~col;
		rp -= pxz;
	}
	char bf[256];
	sprintf(bf,"%d,%d,%d,%d (%dx%d)",x1,y1,x2,y2,ww,hh);
	int x = max((vi.width - (int(strlen(bf)) * 10)) / 2, 0);
	int y = max(vi.height / 2 - 10,0);
    if(vi.IsRGB24()) {DrawStringRGB24(dst,x,y,bf);}
    else             {DrawStringRGB32(dst,x,y,bf);}
	DPRINTF("%d ] Coords::GetFrame OUT",n)
    return dst;
}




AVSValue __cdecl Create_GamMac(AVSValue args, void* user_data, IScriptEnvironment* env) {
	DPRINTF("Create_GamMac IN")
    const char *myName="GamMac: ";
	PClip	child	 = args[0].AsClip();
	const VideoInfo &vi=child->GetVideoInfo();
    if(!vi.IsRGB())                         env->ThrowError("%sRGB Only",myName);
    if(vi.width==0||vi.num_frames<=0)       env->ThrowError("%sClip has no video",myName);
    int     LockChan = args[1 ].AsInt(1);
    int     Scale    = args[2 ].AsInt(1);
    double  RedMul   = args[3 ].AsFloat(1.0);
    double  GrnMul   = args[4 ].AsFloat(1.0);
    double  BluMul   = args[5 ].AsFloat(1.0);
    float   Th       = (float)args[6 ].AsFloat(0.00);
    double  loTh     = args[7 ].AsFloat(Th);
    double  hiTh     = args[8 ].AsFloat(Th);
    double  LockVal  = args[9 ].AsFloat(128.0);
    int		RngLim   = args[10].AsInt(11);
    double  GamMax   = args[11].AsFloat(10.0);
	PClip   dc		 = (args[12].IsClip()) ? args[12].AsClip() : child;
	int		xx		 = args[13].AsInt(20);
	int		yy		 = args[14].AsInt(20);
	int		ww		 = args[15].AsInt(-20);
	int		hh		 = args[16].AsInt(-20);
	int		omin     = args[17].AsInt(0);
    int     omax     = args[18].AsInt(255);
    bool    Show     = args[19].AsBool(true);
    int     Verbosity= args[20].AsInt(2);
    bool    coords   = args[21].AsBool(false);
    bool    dither   = args[22].AsBool(false);
	const VideoInfo &vi2=dc->GetVideoInfo();
    if(vi2.width==0||vi2.num_frames<=0)     env->ThrowError("%sDetect Clip has no video",myName);
    if(!vi.IsSameColorspace(vi2))           env->ThrowError("%sDetect Clip ColorSpace differs",myName);
    if(vi.num_frames != vi2.num_frames)     env->ThrowError("%sDetect Clip FrameCount differs",myName);
    if(LockChan< -3 || LockChan>2)          env->ThrowError("%sLockChan -3 to 2 only(%d)",myName,LockChan);
    if(Scale<0||Scale>2)    env->ThrowError("%sScale 0 to 2 only(%d)",myName,Scale);
    if(RedMul < 0.1f|| RedMul>10.0f)        env->ThrowError("%s0.1 <= RedMul <= 10.0(%f)",myName,RedMul);
    if(GrnMul < 0.1f|| GrnMul>10.0f)        env->ThrowError("%s0.1 <= GrnMul <= 10.0(%f)",myName,GrnMul);
    if(BluMul < 0.1f|| BluMul>10.0f)        env->ThrowError("%s0.1 <= BluMul <= 10.0(%f)",myName,BluMul);
    if(loTh<0.0f)   loTh = -1.0f;
    if(hiTh<0.0)    hiTh = -1.0f;
    if(loTh>1.0f)							env->ThrowError("%sloTh -1.0, or 0.0 to 1.0 only(%f)",myName,loTh);
    if(hiTh>1.0f)							env->ThrowError("%shiTh -1.0, or 0.0 to 1.0 only(%f)",myName,hiTh);
    if(loTh>0.0) loTh /= 100.0;
    if(hiTh>0.0) hiTh /= 100.0;
    if(LockVal<= 0.0 || LockVal>=255.0)     env->ThrowError("%s0.0 < LockVal < 255.0(%f)",myName,LockVal);
    if(RngLim < 1 || RngLim>255)				env->ThrowError("%s1 <= RngLim <= 32(%d)",myName,RngLim);
    if(GamMax<=1.0f || GamMax > 10.0f)		env->ThrowError("%s1.1.0 < GamMax <= 10.0(%f)",myName,GamMax);
    if(Verbosity<0 ||Verbosity>5)			Verbosity=5;
	if(xx <  0 || xx >=vi2.width)			env->ThrowError("%sInvalid X coord(%d)",myName,xx);
	if(yy <  0 || yy >=vi2.height)			env->ThrowError("%sInvalid Y coord(%d)",myName,yy);
	if(ww <= 0) ww += vi2.width  - xx;
	if(hh <= 0) hh += vi2.height - yy;
	if(ww <= 0 || xx + ww > vi2.width)		env->ThrowError("%sInvalid W coord(%d)",myName,ww);
	if(hh <= 0 || yy + hh > vi2.height)		env->ThrowError("%sInvalid H coord(%d)",myName,hh);
	if(omin< 0 || omin >16) 				env->ThrowError("%s 0 <= omin <=16(%d)",myName,omin);
	if(omax < 235 || omax > 255)			env->ThrowError("%s 235 <= omax <= 255(%d)",myName,omax);
	AVSValue ret;
	if(coords) {
		DPRINTF("Create_GamMac Calling new Coords")
		ret = new Coords(dc,xx,yy,ww,hh,env);
	} else {
		DPRINTF("Create_GamMac Calling new GamMac")
		ret = new GamMac(child,dc,
			LockChan,Scale,RedMul,GrnMul,BluMul,
			loTh,hiTh,LockVal,RngLim,GamMax,
			xx,yy,ww,hh,omin,omax,
			Show,Verbosity,dither,
			env);
	}
	DPRINTF("Create_GamMac OUT")
	return ret;
}




// The following function is the function that actually registers the filter in AviSynth
// It is called automatically, when the plugin is loaded to see which functions this filter contains.
__attribute__((visibility("hidden"))) const AVS_Linkage *AVS_linkage = nullptr;

extern "C" __attribute__((visibility("default"))) const char* __stdcall
AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {
	AVS_linkage = vectors;
	// The AddFunction has the following paramters:
	// AddFunction(Filtername , Arguments, Function to call,0);
    env->AddFunction("GamMac"  ,
		"c[LockChan]i[Scale]i[RedMul]f[GrnMul]f[BluMul]f"
		"[Th]f[loTh]f[hiTh]f[LockVal]f[RngLim]i[GamMax]f"
		"[dc]c"
		"[x]i[y]i[w]i[h]i[omin]i[omax]i"
		"[Show]b[Verbosity]i[Coords]b[Dither]b"
        ,Create_GamMac  , 0);
	// Arguments is a string that defines the types and optional nicknames of the arguments for you filter.
	// c - Video Clip
	// i - Integer number
	// f - Float number
	// s - String
	// b - boolean
	// . - Any type (dot)
	//      Array Specifiers
	// i* - Integer Array, zero or more
	// i+ - Integer Array, one or more
	// .* - Any type Array, zero or more
	// .+ - Any type Array, one or more
	//      Etc
    return "`GamMac' GamMac";
	// A freeform name of the plugin.
}
