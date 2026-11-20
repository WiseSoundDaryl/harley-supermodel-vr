#ifndef SUPERMODEL_NEW3D_ROM_GEOMETRY_CACHE_H
#define SUPERMODEL_NEW3D_ROM_GEOMETRY_CACHE_H

#include "Model.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace New3D {
namespace RomGeometryCache {

using Cache = std::unordered_map<UINT32, std::shared_ptr<std::vector<Mesh>>>;

enum class Status
{
  Compacted,
  InvalidRange,
  InvalidModel,
  CapacityExceeded,
  AllocationFailed
};

struct Result
{
  Status status = Status::InvalidModel;
  size_t oldVertices = 0;
  size_t liveVertices = 0;
  size_t oldCachedModels = 0;
  size_t retainedModels = 0;
  size_t uniqueMeshVectors = 0;
};

inline const char* StatusName(Status status)
{
  switch (status)
  {
  case Status::Compacted: return "compacted";
  case Status::InvalidRange: return "invalid_range";
  case Status::InvalidModel: return "invalid_model";
  case Status::CapacityExceeded: return "capacity_exceeded";
  case Status::AllocationFailed: return "allocation_failed";
  }
  return "unknown";
}

// Keep every static mesh referenced by the decoded scene, including geometry
// hidden in presentation passes. Call on the renderer's owning thread, with no
// concurrent readers of mesh offsets. The caller must reset its uploaded-ROM
// byte count and upload the resulting vector before drawing the retained scene.
// Failure leaves the ROM vector, cache, and all shared mesh offsets unchanged.
inline Result Compact(std::vector<FVertex>& rom, Cache& cache,
                      const std::vector<Node>& nodes, size_t capacity)
{
  Result result;
  result.oldVertices = rom.size();
  result.oldCachedModels = cache.size();

  struct Move
  {
    Mesh* mesh;
    size_t source;
    size_t destination;
    size_t count;
  };

  try
  {
    Cache retained;
    std::unordered_set<const std::vector<Mesh>*> unique;
    std::vector<Move> moves;

    for (const Node& node : nodes)
    {
      for (const Model& model : node.models)
      {
        if (model.dynamic)
          continue;
        if (!model.meshes)
          return result;

        const auto entry = retained.emplace(model.sourceAddress, model.meshes);
        if (!entry.second && entry.first->second != model.meshes)
          return result; // One source cannot name two different cached models.
        result.retainedModels = retained.size();

        if (!unique.insert(model.meshes.get()).second)
          continue;
        result.uniqueMeshVectors = unique.size();

        for (Mesh& mesh : *model.meshes)
        {
          if (mesh.vboOffset < 0 || mesh.vertexCount < 0)
          {
            result.status = Status::InvalidRange;
            return result;
          }
          const size_t source = static_cast<size_t>(mesh.vboOffset);
          const size_t count = static_cast<size_t>(mesh.vertexCount);
          if (source > rom.size() || count > rom.size() - source)
          {
            result.status = Status::InvalidRange;
            return result;
          }
          if (count > std::numeric_limits<size_t>::max() - result.liveVertices)
          {
            result.status = Status::CapacityExceeded;
            return result;
          }
          moves.push_back({ &mesh, source, result.liveVertices, count });
          result.liveVertices += count;
        }
      }
    }

    // RAM ranges are in a different VBO partition. An inconsistent scene that
    // aliases one mesh vector as both RAM and ROM must not move its RAM offsets.
    for (const Node& node : nodes)
      for (const Model& model : node.models)
        if (model.dynamic && unique.count(model.meshes.get()) != 0)
          return result;

    // The endpoint is exclusive: exactly filling the ROM partition is valid.
    // Mesh offsets and draw indices use int even if the host vector uses size_t.
    if (result.liveVertices > capacity ||
        result.liveVertices > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
      result.status = Status::CapacityExceeded;
      return result;
    }

    std::vector<FVertex> compacted;
    compacted.reserve(result.liveVertices);
    for (const Move& move : moves)
      compacted.insert(compacted.end(), rom.begin() + move.source,
                       rom.begin() + move.source + move.count);

    // Commit only after every validation and allocation succeeds. These integer
    // assignments and default-allocator container swaps cannot throw.
    for (const Move& move : moves)
      move.mesh->vboOffset = static_cast<int>(move.destination);
    rom.swap(compacted);
    cache.swap(retained);
    result.status = Status::Compacted;
  }
  catch (const std::bad_alloc&)
  {
    result.status = Status::AllocationFailed;
  }
  catch (const std::length_error&)
  {
    result.status = Status::AllocationFailed;
  }
  return result;
}

} // namespace RomGeometryCache
} // namespace New3D

#endif
