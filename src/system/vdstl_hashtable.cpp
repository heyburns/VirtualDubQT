// VirtualDub - Video processing and capture application
// System library component
//
// Copyright (C) 1998-2010 Avery Lee, All Rights Reserved.
// Copyright (C) 2025 v0lt
//
// SPDX-License-Identifier: Zlib
//

#include <stdafx.h>
#include <vd2/system/vdstl_hashtable.h>

vdhashtable_base_node *const vdhashtable_base::sEmptyBucket = {NULL};

vdhashtable_base::vdhashtable_base()
	: mBucketCount(0)
	, mElementCount(0)
	, mpBucketStart(const_cast<vdhashtable_base_node **>(&sEmptyBucket))
	, mpBucketEnd(const_cast<vdhashtable_base_node **>(&sEmptyBucket))
{
}

vdhashtable_base::size_type vdhashtable_base::bucket_count() const {
	return mpBucketEnd - mpBucketStart;
}

vdhashtable_base::size_type vdhashtable_base::max_bucket_count() const {
	return (size_type)-1 >> 1;
}

vdhashtable_base::size_type vdhashtable_base::bucket_size(size_type n) const {
	VDASSERT(n < (size_type)(mpBucketEnd - mpBucketStart));

	size_type len = 0;
	for(vdhashtable_base_node *p = mpBucketStart[n]; p; p = p->mpHashNext)
		++len;

	return len;
}

vdhashtable_base::size_type vdhashtable_base::compute_bucket_count(size_type n) {
	static const size_t kBucketSizes[]={
		11,
		17, 37, 67, 131,
		257, 521, 1031, 2049,
		4099, 8209, 16411, 32771,
		65537, 131101, 262147, 524309,
		1048583, 2097169, 4194319, 8388617,
		16777259, 33554467, 67108879, 134217757,
		268435459, 536870923, 1073741827
	};

	unsigned i = 0;
	size_type buckets;

	while(i < std::size(kBucketSizes)) {
		buckets = kBucketSizes[i];

		if (n <= buckets)
			break;

		++i;
	}

	return buckets;
}
