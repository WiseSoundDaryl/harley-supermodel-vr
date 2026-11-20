#ifndef INCLUDED_DECODE_VERTEX_BUFFERS_H
#define INCLUDED_DECODE_VERTEX_BUFFERS_H

#include <array>
#include <cassert>
#include <cstddef>
#include "Model.h"

namespace New3D {

// Scratch capacity only: material maps and SortingMesh objects remain local to
// each CacheModel call. A buffer returns here only after its vertices and Mesh
// metadata have been copied to their scene-owned destinations.
class DecodeVertexBuffers
{
public:
	static constexpr size_t MaxBuffers = 32;
	static constexpr size_t MaxBufferBytes = 1024 * 1024;
	static constexpr size_t MaxCachedBytes = 4 * 1024 * 1024;

	void Prepare(std::vector<FVertex>& vertices, size_t requested)
	{
		assert(vertices.empty() && vertices.capacity() == 0);
		if (requested == 0) return;
		size_t selected = MaxBuffers;
		for (size_t i = 0; i < m_buffers.size(); ++i) {
			const size_t capacity = m_buffers[i].capacity();
			if (capacity >= requested &&
				(selected == MaxBuffers || capacity < m_buffers[selected].capacity())) {
				selected = i;
				if (capacity == requested) break; // already the smallest possible adequate fit
			}
		}
		if (selected != MaxBuffers) {
			m_cachedBytes -= m_buffers[selected].capacity() * sizeof(FVertex);
			vertices.swap(m_buffers[selected]);
		}
		else vertices.reserve(requested); // preserve the original model-wide reserve
	}

	void Recycle(std::vector<FVertex>& vertices)
	{
		vertices.clear();
		const size_t capacity = vertices.capacity();
		if (capacity == 0 || capacity > MaxBufferBytes / sizeof(FVertex)) return;
		const size_t available = (MaxCachedBytes - m_cachedBytes) / sizeof(FVertex);
		if (capacity <= available) {
			for (auto& buffer : m_buffers) {
				if (buffer.capacity() != 0) continue;
				buffer.swap(vertices);
				m_cachedBytes += capacity * sizeof(FVertex);
				return;
			}
		}
		// Small early models must not pin every slot forever. Keep the larger
		// empty capacity when replacing one smaller buffer fits the byte limit.
		// Choose the smallest feasible replacement, not necessarily the smallest
		// slot: under byte pressure only a smaller capacity increase may fit.
		size_t selected = MaxBuffers;
		for (size_t i = 0; i < m_buffers.size(); ++i) {
			const size_t oldCapacity = m_buffers[i].capacity();
			if (oldCapacity != 0 && oldCapacity < capacity && capacity - oldCapacity <= available &&
				(selected == MaxBuffers || oldCapacity < m_buffers[selected].capacity())) selected = i;
		}
		if (selected != MaxBuffers) {
			m_cachedBytes += (capacity - m_buffers[selected].capacity()) * sizeof(FVertex);
			vertices.swap(m_buffers[selected]);
			std::vector<FVertex>().swap(vertices); // release displaced empty storage without allocating
		}
	}

	size_t CachedBytes() const { return m_cachedBytes; }
	size_t CachedBuffers() const
	{
		size_t count = 0;
		for (const auto& buffer : m_buffers) if (buffer.capacity() != 0) ++count;
		return count;
	}

private:
	std::array<std::vector<FVertex>, MaxBuffers> m_buffers;
	size_t m_cachedBytes = 0;
};

} // New3D

#endif
