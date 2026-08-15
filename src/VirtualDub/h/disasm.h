// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#ifndef f_DISASM_H
#define f_DISASM_H

#include <windows.h>


struct VDDisassemblyContext {
	const unsigned char *pRuleBase;
	const unsigned *pRuleSystemOffsets;
	long (*pSymLookup)(VDDisassemblyContext *pctx, unsigned long virtAddr, char *buf, int buf_len);

	bool bSizeOverride;			// 66
	bool bAddressOverride;		// 67
	bool bRepnePrefix;			// F2
	bool bRepePrefix;			// F3
	unsigned char	rex;
	const char *pszSegmentOverride;

	ptrdiff_t physToVirtOffset;

	void	*pRawBlock;
	char	*heap;
	char	*heap_limit;
	ptrdiff_t		*stack;

	void	*pExtraData;
	int		cbExtraData;
};


bool VDDisasmInit(VDDisassemblyContext* pvdc, const wchar_t* pszFilename);
void VDDisasmDeinit(VDDisassemblyContext* pvdc);
const char* VDDisassemble(VDDisassemblyContext* pvdc, const unsigned char* source, int bytes, int& count);



class CodeDisassemblyWindow {
private:
	static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);

	void *code, *rbase, *abase;
	long length;
	void *pFault;

	class lbent {
	public:
		unsigned char *ip;
		int len;
	} *lbents;
	int num_ents;

	HFONT hFontMono;

	char buf[256];

public:
	VDDisassemblyContext vdc;

	CodeDisassemblyWindow(void *code, long, void *, void *);
	~CodeDisassemblyWindow();

	void DoInitListbox(HWND hwndList);
	BOOL DoMeasureItem(LPARAM lParam);
	BOOL DoDrawItem(LPARAM lParam);
	void parse();
	BOOL post(HWND);
	long getInstruction(char *buf, long val);

	void setFaultAddress(void *_pFault) {
		pFault = _pFault;
	}
};

#endif
