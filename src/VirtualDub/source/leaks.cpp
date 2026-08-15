// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2003 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"

#if defined(_MSC_VER) && defined(_DEBUG)

#include <crtdbg.h>
#include <dbghelp.h>
#include <vd2/system/filesys.h>

// must match CRT internal format -- using different name to avoid
// symbol conflicts
namespace {
	struct CrtBlockHeader {
		CrtBlockHeader*	pNext;
		CrtBlockHeader*	pPrev;
		const char*		pFilename;
		int				line;

		int				type;
		size_t			size;

		long			reqnum;
		unsigned char	redzone_head[4];

		unsigned char	data[1];
	};
}

struct VDDbgHelpDynamicLoaderW32 {
public:
	BOOL (APIENTRY *pSymInitialize)(HANDLE hProcess, PCSTR UserSearchPath, BOOL fInvadeProcess);
	BOOL (APIENTRY *pSymCleanup)(HANDLE hProcess);
	BOOL (APIENTRY *pSymSetSearchPathW)(HANDLE hProcess, PCWSTR SearchPath);
	DWORD64(APIENTRY *pSymLoadModuleExW)(HANDLE hProcess, HANDLE hFile, PCWSTR ImageName, PCWSTR ModuleName, DWORD64 BaseOfDll, DWORD DllSize, PMODLOAD_DATA Data, DWORD Flags);
	BOOL (APIENTRY *pSymFromAddr)(HANDLE hProcess, DWORD64 Address, PDWORD64 Displacement, PSYMBOL_INFO Symbol);
	BOOL (APIENTRY* pSymGetModuleInfo64)(HANDLE hProcess, DWORD64 dwAddr, PIMAGEHLP_MODULE64 ModuleInfo);
	DWORD(APIENTRY *pUnDecorateSymbolName)(PCSTR name, PSTR outputString, DWORD maxStringLength, DWORD flags);

	HMODULE hmodDbgHelp = 0;

	VDDbgHelpDynamicLoaderW32();
	~VDDbgHelpDynamicLoaderW32();

	bool ready() const { return hmodDbgHelp != 0; }
};

VDDbgHelpDynamicLoaderW32::VDDbgHelpDynamicLoaderW32()
{
	// XP DbgHelp doesn't pick up some VC8 symbols -- need DbgHelp 6.2+ for that
	hmodDbgHelp = LoadLibraryW(L"c:\\program files\\debugging tools for windows\\dbghelp");
	if (!hmodDbgHelp) {
		hmodDbgHelp = LoadLibraryW(L"c:\\program files (x86)\\debugging tools for windows\\dbghelp");

		if (!hmodDbgHelp) {
			hmodDbgHelp = LoadLibraryW(L"dbghelp");
		}
	}

	static const char *const sFuncTbl[]={
		"SymInitialize",
		"SymCleanup",
		"SymSetSearchPathW",
		"SymLoadModuleExW",
		"SymFromAddr",
		"SymGetModuleInfo64",
		"UnDecorateSymbolName",
	};
	enum { kFuncs = std::size(sFuncTbl) };

	if (hmodDbgHelp) {
		int i;
		for (i = 0; i < kFuncs; ++i) {
			FARPROC fp = GetProcAddress(hmodDbgHelp, sFuncTbl[i]);
			if (!fp) {
				break;
			}

			((FARPROC*)this)[i] = fp;
		}

		if (i >= kFuncs) {
			return;
		}

		FreeLibrary(hmodDbgHelp);
		hmodDbgHelp = 0;
	}

	for (int j = 0; j < kFuncs; ++j) {
		((FARPROC*)this)[j] = 0;
	}
}

VDDbgHelpDynamicLoaderW32::~VDDbgHelpDynamicLoaderW32()
{
	if (hmodDbgHelp) {
		FreeLibrary(hmodDbgHelp);
		hmodDbgHelp = 0;
	}
}

namespace {
	template<class T>
	class heapvector {
	public:
		typedef	T *					pointer_type;
		typedef	const T *			const_pointer_type;
		typedef T&					reference_type;
		typedef const T&			const_reference_type;
		typedef pointer_type		iterator;
		typedef	const_pointer_type	const_iterator;
		typedef size_t				size_type;
		typedef	ptrdiff_t			difference_type;

		heapvector() : pStart(0), pEnd(0), pEndAlloc(0) {}
		~heapvector() {
			if (pStart)
				HeapFree(GetProcessHeap(), 0, pStart);
		}

		iterator begin() { return pStart; }
		const_iterator begin() const { return pStart; }
		iterator end() { return pEnd; }
		const_iterator end() const { return pEnd; }

		reference_type operator[](size_type i) { return pStart[i]; }
		const_reference_type operator[](size_type i) const { return pStart[i]; }

		bool empty() const { return pEnd == pStart; }
		size_type size() const { return pEnd-pStart; }
		size_type capacity() const { return pEndAlloc-pStart; }

		void resize(size_type s) {
			if (capacity() < s)
				reserve(std::min<size_type>(size()*2, s));

			pEnd = pStart + s;
		}

		void reserve(size_type s) {
			if (s > capacity()) {
				HANDLE h = GetProcessHeap();
				size_type siz = size();
				T *pNewBlock = (T*)HeapAlloc(h, 0, s * sizeof(T));

				if (pStart) {
					memcpy(pNewBlock, pStart, (char *)pEnd - (char *)pStart);
					HeapFree(h, 0, pStart);
				}

				pStart = pNewBlock;
				pEnd = pStart + siz;
				pEndAlloc = pStart + s;
			}
		}

		void push_back(const T& x) {
			if (pEnd == pEndAlloc)
				reserve(pEndAlloc==pStart ? 16 : size()*2);

			*pEnd++ = x;
		}

	protected:
		T *pStart, *pEnd, *pEndAlloc;

		union trivial_check { T x; };
	};

	struct BlockInfo {
		const CrtBlockHeader *pBlock;
		bool marked;
	};

	bool operator<(const BlockInfo& x, const BlockInfo& y) {
		return (uintptr)x.pBlock < (uintptr)y.pBlock;
	}

	bool operator<(uintptr x, const BlockInfo& y) {
		return x < (uintptr)y.pBlock;
	}

	bool operator<(const BlockInfo& x, uintptr y) {
		return (uintptr)x.pBlock < y;
	}
}

void VDDumpMemoryLeaksVC()
{
	_CrtMemState msNow;

	// disable CRT tracking of memory blocks
	_CrtSetDbgFlag(_CrtSetDbgFlag(0) & ~_CRTDBG_ALLOC_MEM_DF);

	VDDbgHelpDynamicLoaderW32 dbghelp;

	if (!dbghelp.ready()) {
		_CrtDumpMemoryLeaks();
		return;
	}

	HANDLE hProc = GetCurrentProcess();

	BOOL ret = dbghelp.pSymInitialize(hProc, NULL, FALSE);

	wchar_t filename[MAX_PATH], path[MAX_PATH];
	GetModuleFileNameW(NULL, filename, std::size(filename));

	wcscpy(path, filename);
	*VDFileSplitPath(path) = 0;

	ret = dbghelp.pSymSetSearchPathW(hProc, path);
	SetCurrentDirectoryW(path);
	DWORD64 dwBaseAddr = dbghelp.pSymLoadModuleExW(hProc, NULL, filename, nullptr, 0, 0, nullptr, 0);

	IMAGEHLP_MODULE64 modinfo = {sizeof(IMAGEHLP_MODULE64)};
	ret = dbghelp.pSymGetModuleInfo64(hProc, dwBaseAddr, &modinfo);

	// checkpoint the current memory layout
	_CrtMemCheckpoint(&msNow);

	// traverse memory

	typedef heapvector<BlockInfo> tHeapInfo;
	tHeapInfo heapinfo;

	const CrtBlockHeader *pHdr = (const CrtBlockHeader *)msNow.pBlockHeader;
	for(; pHdr; pHdr = pHdr->pNext) {
		const int type = (pHdr->type & 0xffff);

		if (type != _CLIENT_BLOCK && type != _NORMAL_BLOCK) {
			continue;
		}

		BlockInfo info = {
			pHdr,
			false
		};

		heapinfo.push_back(info);
	}

	if (!heapinfo.empty()) {
		_RPT0(0, "\n\n===== MEMORY LEAKS DETECTED =====\n\n");

		std::sort(heapinfo.begin(), heapinfo.end());

		tHeapInfo::iterator itBase(heapinfo.begin());
		for (tHeapInfo::iterator it(itBase), itEnd(heapinfo.end()); it != itEnd; ++it) {
			BlockInfo& blk = *it;
			size_t pointers = blk.pBlock->size / sizeof(void*);
			uintptr* pp = (uintptr*)blk.pBlock->data;

			for (size_t i = 0; i < pointers; ++i) {
				uintptr ip = pp[i];

				tHeapInfo::iterator itTarget(std::upper_bound(itBase, itEnd, ip));

				if (itTarget != itBase) {
					BlockInfo& blk2 = *--itTarget;

					if (ip - (uintptr)blk2.pBlock->data < blk2.pBlock->size) {
						blk2.marked = true;
					}
				}
			}
		}

		struct {
			SYMBOL_INFO hdr;
			CHAR nameext[511];
		} sym = {};

		sym.hdr.SizeOfStruct = sizeof(SYMBOL_INFO);
		sym.hdr.MaxNameLen = std::size(sym.nameext);

		for(int pass = 0; pass < 2; ++pass) {
			bool test = pass ? true : false;

			if (test) {
				_RPT0(0, "\nSecondary leaks:\n\n");
			} else {
				_RPT0(0, "\nPrimary leaks:\n\n");
			}

			for(tHeapInfo::iterator it(heapinfo.begin()), itEnd(heapinfo.end()); it!=itEnd; ++it) {
				BlockInfo& blk = *it;

				if (blk.marked != test) {
					continue;
				}

				pHdr = blk.pBlock;

				char buf[1024], *s = buf;

				s += wsprintfA(buf, "    #%-5d %p (%8ld bytes)", pHdr->reqnum, pHdr->data, (long)pHdr->size);

				if (pHdr->pFilename && !strcmp(pHdr->pFilename, "stack trace")) {
					DWORD64 dwAddress = dwBaseAddr + pHdr->line;

					ret = dbghelp.pSymFromAddr(hProc, dwAddress, nullptr, &sym.hdr);
					if (ret) {
						s += wsprintfA(s, "  Allocator: %p [%s]", (void*)dwAddress, sym.hdr.Name);
					} else {
						s += wsprintfA(s, "  Allocator: %p", (void*)dwAddress);
					}
				}

				if (pHdr->size >= sizeof(void*)) {
					void* vtbl = *(void**)pHdr->data;

					if (vtbl >= (char*)modinfo.BaseOfImage && vtbl < (char*)modinfo.BaseOfImage + modinfo.ImageSize) {
						ret = dbghelp.pSymFromAddr(hProc, (DWORD64)vtbl, nullptr, &sym.hdr);
						if (ret) {
							char* t = strstr(sym.hdr.Name, "::`vftable'");
							if (t) {
								*t = 0;
								s += wsprintfA(s, " [Type: %s]", sym.hdr.Name);
							}
						}
					}
				}

				*s = 0;

				_RPT1(0, "%s\n", buf);
			}
		}

		_RPT0(0, "\nEnd of leak dump.\n");
	}

	dbghelp.pSymCleanup(hProc);
}

#pragma section(".CRT$XPB",long,read)

extern "C" static __declspec(allocate(".CRT$XPB")) void (__cdecl *g_leaktrap)() = VDDumpMemoryLeaksVC;

#endif
