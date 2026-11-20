#ifndef INCLUDED_SCENE_MODEL_BUFFERS_H
#define INCLUDED_SCENE_MODEL_BUFFERS_H

#include <array>
#include <cassert>
#include <cstddef>
#include "Model.h"

namespace New3D {

// Retain only empty vector storage between decoded scenes. Models themselves
// are destroyed at the original scene-clear boundary, including mesh owners and
// diagnostic paths. Fresh Nodes/Models still receive their normal constructors.
// The GL owner uses this pool only while invalidating/building a scene, never
// while a prepared scene is being presented.
class SceneModelBuffers
{
public:
	static constexpr size_t InitialModels = 2048;
	static constexpr size_t MaxBufferModels = 8192;
	static constexpr size_t MaxBuffers = 16;
	static constexpr size_t MaxCachedBytes = 4 * 1024 * 1024;

	void ClearScene(std::vector<Node>& nodes)
	{
		for (auto& node : nodes) {
			auto& models = node.models;
			models.clear();
			const size_t capacity = models.capacity();
			if (capacity < InitialModels || capacity > MaxBufferModels ||
				capacity > (MaxCachedBytes - m_cachedBytes) / sizeof(Model)) continue;
			for (auto& buffer : m_buffers) {
				if (buffer.capacity() != 0) continue;
				buffer.swap(models);
				m_cachedBytes += capacity * sizeof(Model);
				break;
			}
		}
		nodes.clear();
	}

	void PrepareModels(std::vector<Model>& models)
	{
		// Called immediately after constructing a fresh Node, before traversal.
		assert(models.empty() && models.capacity() == 0);
		for (auto& buffer : m_buffers) {
			if (buffer.capacity() == 0) continue;
			m_cachedBytes -= buffer.capacity() * sizeof(Model);
			buffer.swap(models);
			return;
		}
		models.reserve(InitialModels);
	}

	size_t CachedBytes() const { return m_cachedBytes; }
	size_t CachedBuffers() const
	{
		size_t count = 0;
		for (const auto& buffer : m_buffers) if (buffer.capacity() != 0) ++count;
		return count;
	}

private:
	std::array<std::vector<Model>, MaxBuffers> m_buffers;
	size_t m_cachedBytes = 0;
};

} // New3D

#endif
