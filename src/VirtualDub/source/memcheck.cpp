// VirtualDub - Video processing and capture application
//
// Copyright (C) 1998-2001 Avery Lee
//
// SPDX-License-Identifier: GPL-2.0-or-later
//

#include "stdafx.h"
#include <windows.h>

#if 0

void *allocmem(size_t siz) {
	void *mem;
	size_t rsiz = (siz + 4095 + 8) & -4096;

	mem = VirtualAlloc(NULL, rsiz, MEM_COMMIT, PAGE_READWRITE);

	if (!mem)
		return NULL;

	mem = (char *)mem + rsiz - ((siz+7)&-8) - 8;

	((long *)mem)[0] = siz;
	((long *)mem)[1] = 0x12345678;

	return (char *)mem + 8;
}

void freemem(void *block) {
	if (!block)
		return;

	block = (void *)((char *)block - 8);

	if (((long *)block)[1] != 0x12345678)
		__asm int 3

	VirtualFree((void *)((long)block & -4096), 0, MEM_RELEASE);
}

void *reallocmem(void *block, size_t siz) {
	void *nblock = allocmem(siz);

	if (!nblock)
		return NULL;

	if (!block)
		return nblock;

	if (((long *)block)[-1] != 0x12345678)
		__asm int 3

	if (siz < ((long *)block)[-2])
		siz = ((long *)block)[-2];

	memcpy(nblock, block, siz);

	return nblock;
}

void *callocmem(size_t s1, size_t s2) {
	void *mem = allocmem(s1 * s2);

	if (!mem)
		return NULL;

	memset(mem, 0, s1*s2);

	return mem;
}

void *operator new(size_t siz) {
	return allocmem(siz);
}
void operator delete(void *block) {
	freemem(block);
}


#else

void *allocmem(size_t siz) {
	return malloc(siz);
}

void freemem(void *block) {
	free(block);
}

void *reallocmem(void *block, size_t siz) {
	return realloc(block, siz);
}

void *callocmem(size_t s1, size_t s2) {
	return calloc(s1, s2);
}

#endif
