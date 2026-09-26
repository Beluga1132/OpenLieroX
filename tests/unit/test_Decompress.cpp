/*
 *  Unit tests for Decompress, which also unpacks data sent by peers.
 */

#include "unittest.h"
#include "StringUtils.h"

void test_DecompressRoundtrip() {
	const std::string text = "some level data, some level data, some level data";
	std::string packed, unpacked;
	CHECK(Compress(text, &packed));
	CHECK(Decompress(packed, &unpacked));
	CHECK(unpacked == text);
}

// A few KB that inflate to megabytes must be refused past the limit,
// instead of growing the output without bound.
void test_DecompressRejectsBomb() {
	const std::string zeros(4 * 1024 * 1024, '\0');
	std::string packed, unpacked;
	CHECK(Compress(zeros, &packed));
	CHECK(packed.size() < 64 * 1024);

	CHECK(!Decompress(packed, &unpacked, 1024 * 1024));
	CHECK(unpacked.empty());

	// The same data within the limit still works.
	CHECK(Decompress(packed, &unpacked, zeros.size()));
	CHECK(unpacked == zeros);
}
