/******************************************************************************
 * DataView.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * A read-only materialised view (A5): a row-major SNAPSHOT of selected columns
 * of a store's live rows. It is a COPY, never an alias of the store, so gameplay
 * can read a view on any thread while the Lens mutates the underlying store —
 * the snapshot-isolation model from DataLens-Spec.md §7.1.1. Refresh re-builds
 * the snapshot from the current store state (the "view refresh" half of a Tick).
 *
 * This first slice: column selection + live-row gather (optionally within a LOD
 * band), materialised row-major (cache-friendly for whole-entity reads). Joins,
 * predicates, calc/virtual columns, dirty-tracking and double buffering are later
 * A5 slices.
 ******************************************************************************/

#pragma once

#include "datalens/DataStore.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace datalens
{
    class DataView
    {
    public:
        /// Build a view that mirrors the given source columns of a store (view column i == the store's
        /// sourceColumns[i]). Call Refresh to materialise. The view holds no store reference.
        explicit DataView(std::vector<std::size_t> sourceColumns)
            : mSourceColumns(std::move(sourceColumns)) {}

        /// Re-materialise from the current store state: gather every live row and copy the selected
        /// columns into a fresh row-major snapshot. A copy — it never aliases the store.
        void Refresh(const DataStore& store) { RefreshImpl(store, false, 0, 255); }

        /// As Refresh, but only include rows whose Simulation LOD is within [minLod, maxLod].
        void RefreshInLodBand(const DataStore& store, std::uint8_t minLod, std::uint8_t maxLod)
        {
            RefreshImpl(store, true, minLod, maxLod);
        }

        /// Two-phase refresh for PARALLEL materialisation (driven by the Lens, which owns a pool).
        /// PrepareRefresh does the column layout + live-row selection and sizes the buffer (serial,
        /// cheap); MaterializeRange then fills view rows [begin, end) — safe to call concurrently on
        /// disjoint ranges (each writes only its own rows). RowCount() is valid after PrepareRefresh.
        void PrepareRefresh(const DataStore& store, bool useBand = false,
                            std::uint8_t minLod = 0, std::uint8_t maxLod = 255);
        void MaterializeRange(const DataStore& store, std::size_t viewRowBegin, std::size_t viewRowEnd);

        std::size_t RowCount() const { return mRowCount; }
        std::size_t ColumnCount() const { return mSourceColumns.size(); }
        std::size_t RowStride() const { return mRowStride; }

        /// The store column a view column mirrors.
        std::size_t SourceColumn(std::size_t viewCol) const { return mSourceColumns[viewCol]; }
        /// The store row a view row was materialised from (the snapshot keeps this mapping).
        std::size_t SourceRow(std::size_t viewRow) const { return mSourceRows[viewRow]; }

        /// Pointer to a view row's contiguous row-major bytes (whole-entity read).
        const std::uint8_t* RowData(std::size_t viewRow) const
        {
            return mData.data() + viewRow * mRowStride;
        }

        /// Base pointer + size of the whole row-major snapshot, for bulk/zero-copy reads (e.g. a single
        /// marshalled copy into a managed array). Valid until the next Refresh.
        const std::uint8_t* Data() const { return mData.data(); }
        std::size_t ByteSize() const { return mData.size(); }

        /// Typed read of a cell from the snapshot (unchecked, like DataStore::GetRaw).
        template <typename T>
        T Get(std::size_t viewRow, std::size_t viewCol) const
        {
            T value{};
            std::memcpy(&value, mData.data() + viewRow * mRowStride + mColOffsets[viewCol], sizeof(T));
            return value;
        }

    private:
        void RefreshImpl(const DataStore& store, bool useBand, std::uint8_t minLod, std::uint8_t maxLod)
        {
            PrepareRefresh(store, useBand, minLod, maxLod);
            MaterializeRange(store, 0, mRowCount);
        }

        std::vector<std::size_t> mSourceColumns; // store column index per view column
        std::vector<std::size_t> mColOffsets;    // byte offset of each view column within a row
        std::vector<std::size_t> mColStrides;    // byte width of each view column
        std::size_t mRowStride = 0;              // bytes per view row
        std::vector<std::uint8_t> mData;         // row-major snapshot
        std::vector<std::size_t> mSourceRows;    // view row -> store row
        std::size_t mRowCount = 0;
    };
}
