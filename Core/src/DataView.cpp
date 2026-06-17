/******************************************************************************
 * DataView.cpp
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Materialisation for the read-only DataView (A5).
 ******************************************************************************/

#include "datalens/DataView.h"

namespace datalens
{
    void DataView::PrepareRefresh(const DataStore& store, bool useBand,
                                  std::uint8_t minLod, std::uint8_t maxLod)
    {
        const std::size_t storeCols = store.GetColumnCount();

        // Recompute the row layout from the store (the schema may have changed since last refresh).
        // An out-of-range source column collapses to zero width and is skipped during copy, so a stale
        // selection degrades gracefully rather than throwing.
        mColStrides.assign(mSourceColumns.size(), 0);
        mColOffsets.assign(mSourceColumns.size(), 0);
        mRowStride = 0;
        for (std::size_t i = 0; i < mSourceColumns.size(); ++i)
        {
            const std::size_t sc = mSourceColumns[i];
            const std::size_t stride = (sc < storeCols) ? store.GetColumnStride(sc) : 0;
            mColStrides[i] = stride;
            mColOffsets[i] = mRowStride;
            mRowStride += stride;
        }

        // Gather the live rows (optionally within the LOD band). This compaction is inherently serial
        // (output order depends on the scan), but it is a tight validity-bit scan — the heavy
        // per-column copy is the parallelisable part (MaterializeRange).
        mSourceRows.clear();
        const std::size_t rows = store.GetRowCount();
        for (std::size_t r = 0; r < rows; ++r)
        {
            if (!store.IsValidRow(r))
                continue;
            if (useBand)
            {
                const std::uint8_t lv = store.GetLod(r);
                if (lv < minLod || lv > maxLod)
                    continue;
            }
            mSourceRows.push_back(r);
        }
        mRowCount = mSourceRows.size();
        mData.assign(mRowCount * mRowStride, 0);
    }

    void DataView::MaterializeRange(const DataStore& store, std::size_t viewRowBegin, std::size_t viewRowEnd)
    {
        if (viewRowEnd > mRowCount)
            viewRowEnd = mRowCount;

        // Materialise row-major: each view row is the selected columns concatenated. The view always
        // mirrors store columns with identical type (it selects, never retypes), so we copy raw bytes
        // directly — no per-cell type dispatch. Column base pointers are hoisted out of the row loop;
        // the inner copy is a tiny fixed-size memcpy the compiler lowers to a load/store. Each call
        // writes only view rows [begin, end), so disjoint ranges are race-free across threads.
        const std::size_t colCount = mSourceColumns.size();
        for (std::size_t i = 0; i < colCount; ++i)
        {
            const std::size_t stride = mColStrides[i];
            if (stride == 0)
                continue; // out-of-range / zero-width column: nothing to copy
            const std::uint8_t* colBase = store.GetColumnRaw(mSourceColumns[i]);
            const std::size_t dstOffset = mColOffsets[i];
            for (std::size_t vr = viewRowBegin; vr < viewRowEnd; ++vr)
            {
                std::memcpy(mData.data() + vr * mRowStride + dstOffset,
                            colBase + mSourceRows[vr] * stride, stride);
            }
        }
    }
}
