#ifndef INCLUDED_TILE_UPLOAD_TRACKER_H
#define INCLUDED_TILE_UPLOAD_TRACKER_H

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

// Exact visible RGBA comparison against the last submitted tile texture.
// TileGenBuffer rows have 512 pixels, but only the first 496 are uploaded.
// Each plane's shadow is heap storage (761,856 bytes), never inline stack data.
// The renderer's GL owner plans, submits, then commits from one stable snapshot.
class TileUploadTracker
{
public:
	static constexpr unsigned Width = 496;
	static constexpr unsigned Height = 384;
	static constexpr unsigned Stride = 512;
	static constexpr unsigned MaxRanges = 4;

	struct Range { unsigned firstRow = 0, rowCount = 0; };
	struct Plan
	{
		std::array<Range, MaxRanges> ranges{};
		unsigned rangeCount = 0, changedRows = 0;
		bool fullUpload = false;
		unsigned UploadedRows() const
		{
			unsigned rows = 0;
			for (unsigned i = 0; i < rangeCount; ++i) rows += ranges[i].rowCount;
			return rows;
		}
	};

	// Call whenever texture storage is created/reinitialized. If the optional
	// shadow allocation fails, retain the original full-upload behavior.
	bool Reset() noexcept
	{
		m_valid = false;
		if (!m_shadow) m_shadow.reset(new (std::nothrow) uint32_t[Width * Height]);
		return bool(m_shadow);
	}

	Plan PlanUpload(const uint32_t* source) const
	{
		Plan plan;
		if (!source) return plan;
		if (!m_valid) {
			plan.changedRows = Height;
			SetFull(plan);
			return plan;
		}
		bool previousChanged = false;
		unsigned groups = 0;
		for (unsigned row = 0; row < Height; ++row) {
			const bool changed = std::memcmp(source + row * Stride, m_shadow.get() + row * Width,
				Width * sizeof(uint32_t)) != 0;
			if (changed) {
				++plan.changedRows;
				if (!previousChanged) {
					++groups;
					if (groups <= MaxRanges) plan.ranges[groups - 1] = { row, 0 };
				}
				if (groups <= MaxRanges) ++plan.ranges[groups - 1].rowCount;
			}
			previousChanged = changed;
		}
		if (groups > MaxRanges || plan.changedRows >= Height / 2) SetFull(plan);
		else plan.rangeCount = groups;
		return plan;
	}

	// Never call before the matching GL uploads. Copy only submitted visible
	// rows, including alpha; skipped/padding rows do not become part of shadow.
	void Commit(const uint32_t* source, const Plan& plan) noexcept
	{
		if (!source || !m_shadow || plan.rangeCount == 0) return;
		for (unsigned i = 0; i < plan.rangeCount; ++i) {
			const auto& range = plan.ranges[i];
			for (unsigned row = range.firstRow; row < range.firstRow + range.rowCount; ++row)
				std::memcpy(m_shadow.get() + row * Width, source + row * Stride, Width * sizeof(uint32_t));
		}
		m_valid = true;
	}

private:
	static void SetFull(Plan& plan)
	{
		plan.rangeCount = 1;
		plan.ranges[0] = { 0, Height };
		plan.fullUpload = true;
	}
	std::unique_ptr<uint32_t[]> m_shadow;
	bool m_valid = false;
};

#endif
