#ifndef INCLUDED_DECODE_MATERIAL_ARENA_H
#define INCLUDED_DECODE_MATERIAL_ARENA_H

#include <cstddef>
#include <memory_resource>
#include <unordered_map>
#include "Model.h"

namespace New3D {

// One model decode owns this scratch arena. Declare the map after the arena so
// its SortingMesh objects are destroyed before the storage they occupy. Only
// map nodes/buckets use the arena; vertex vectors retain their own allocator.
// The ordinary hash/equality and bucket policy remain unchanged, with no reserve.
class DecodeMaterialArena
{
public:
	static constexpr size_t InlineBytes = 8192;
	using Map = std::pmr::unordered_map<UINT64, SortingMesh>;

	explicit DecodeMaterialArena(std::pmr::memory_resource* upstream = std::pmr::new_delete_resource())
		: m_resource(m_storage, sizeof(m_storage), upstream) {}

	std::pmr::memory_resource* Resource() { return &m_resource; }

private:
	// Intentionally uninitialized: allocation constructs every map object.
	// Large maps fall back to the heap; 8 KiB is inline capacity, not a limit.
	alignas(std::max_align_t) std::byte m_storage[InlineBytes];
	std::pmr::monotonic_buffer_resource m_resource;
};

} // New3D

#endif
