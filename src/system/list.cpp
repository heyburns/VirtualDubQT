// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2004 Avery Lee, All Rights Reserved.
//
// SPDX-License-Identifier: Zlib
//

///////////////////////////////////////////////////////////////////////////
//
//	For those of you who say this looks familiar... it should.  This is
//	the same linked-list style that the Amiga Exec uses, with dummy head
//	and tail nodes.  It's really a very convienent way to implement
//	doubly-linked lists.
//

#include "stdafx.h"
#include <algorithm>
#include <vd2/system/list.h>

List::List() {
	Init();
}

void List::Init() {
	head.next = tail.prev = 0;
	head.prev = &tail;
	tail.next = &head;
}

ListNode *List::RemoveHead() {
	if (head.prev->prev) {
		ListNode *t = head.prev;

		head.prev->Remove();
		return t;
	}

	return 0;
}

ListNode *List::RemoveTail() {
	if (tail.next->next) {
		ListNode *t = tail.next;

		tail.next->Remove();
		return t;
	}

	return 0;
}

void List::Take(List &from) {
	if (from.IsEmpty())
		return;

	head.prev = from.head.prev;
	tail.next = from.tail.next;
	head.prev->next = &head;
	tail.next->prev = &tail;

	from.Init();
}

void List::Swap(List &dst) {
	if (IsEmpty())
		Take(dst);
	else if (dst.IsEmpty())
		dst.Take(*this);
	else {
		std::swap(head.prev, dst.head.prev);
		std::swap(tail.next, dst.tail.next);

		head.prev->next = &head;
		tail.next->prev = &tail;

		dst.head.prev->next = &dst.head;
		dst.tail.next->prev = &dst.tail;
	}
}
