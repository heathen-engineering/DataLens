/******************************************************************************
 * View.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * The Core half of a View (DataLens-Spec.md §6.4): pure, type-blind data movement. A compiled view
 * read = a base store + index-based joins + a column projection + scope predicates, producing a raw,
 * row-major byte payload of the in-scope, glued records with a per-row change-flag slot and a source
 * map for the write-back. Calculated columns / aggregates / type marshalling are a Foundation concern;
 * Core only copies bytes by stride (and interprets bytes only inside a scope predicate).
 ******************************************************************************/

#pragma once

#include "datalens/DataLensSchema.h"
#include "datalens/DataStore.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace datalens
{
    // Per view-row change state (§6.4). The read hydrates every row as Unchanged; the consumer flips
    // rows to Modified / New / Removed to drive the write-back (Phase 3).
    enum class ViewRowState : std::uint8_t { Unchanged = 0, Modified = 1, New = 2, Removed = 3 };

    // How the base store glues to one other store (index-based, §6.4):
    //  - Aligned: target row == base row (no key, zero cost).
    //  - else: a base-store column holds the target row index; AbsentSentinel means "no row" (defaults).
    struct ViewJoin
    {
        std::size_t   TargetStore = 0;                // store index in the table passed to Refresh
        bool          Aligned = false;
        std::size_t   IndexColumn = 0;                // base column holding the target row index (Aligned=false)
        std::uint64_t AbsentSentinel = 0x7FFFFFFFu;   // default int32.Max (HATE catalogue convention)
    };

    // A projected column: a Source (0 = base store; k >= 1 = the (k-1)th join's target) and the column
    // index within that store.
    struct ViewColumn
    {
        std::size_t Source = 0;
        std::size_t Column = 0;
    };

    // A scope predicate gating which base rows hydrate. Typed: Core carries the value type here so the
    // raw cell bytes can be interpreted for the comparison (the one place Core reads a value, §6.4).
    // Legacy flat path: the whole list is AND-composed and evaluated over BASE columns before joins
    // resolve (a cheap pre-prune). The richer boolean filter is the predicate program below.
    struct ViewScope
    {
        std::size_t       Column = 0;                        // base store column index
        DataLensValueType Type   = DataLensValueType::Int32;
        DataCompareOp     Op     = DataCompareOp::Always;
        std::int64_t      IValue = 0;                        // integer / bitmask threshold
        double            DValue = 0.0;                      // float / double threshold
    };

    // A node in a view's predicate PROGRAM (DataLens-Spec §6.4.1): a serialisable RPN (postfix) of leaf
    // comparisons and And/Or/Not connectives, evaluated per base row AFTER its joins resolve (so a leaf
    // may address a dereferenced column). This is how the Foundation's full boolean filter tree compiles
    // to Core. Leaf addresses a cell like a projected column (Source 0 = base, k>=1 = the (k-1)th join);
    // a leaf on an absent join row is false. A Range leaf is the fused branchless interval test
    // (Lo <= cell <= Hi); other leaves use Op against the single threshold.
    enum class ViewPredicateKind : std::uint8_t { Leaf = 0, And = 1, Or = 2, Not = 3 };

    struct ViewPredicate
    {
        ViewPredicateKind Kind   = ViewPredicateKind::Leaf;
        bool              Range  = false;                    // leaf: fused interval test (ignores Op)
        std::size_t       Source = 0;                        // 0 = base, k>=1 = the (k-1)th join's target
        std::size_t       Column = 0;                        // column index within that source store
        DataLensValueType Type   = DataLensValueType::Int32;
        DataCompareOp     Op     = DataCompareOp::Always;
        std::int64_t      IValue = 0;                        // threshold / range lo (integer / bitmask)
        std::int64_t      IHi    = 0;                        // range hi (integer)
        double            DValue = 0.0;                      // threshold / range lo (float / double)
        double            DHi    = 0.0;                      // range hi (float / double)
    };

    // One column write in a write-back instruction: copy a view column's bytes into a store column.
    struct ViewWrite
    {
        std::size_t ViewColumn = 0;
        std::size_t TargetStore = 0;
        std::size_t TargetColumn = 0;
    };

    // The write-back (§6.4): Insert (New rows -> fresh records), Update (Modified rows -> sourced records),
    // Delete (Removed rows -> free the sourced record). Write targets may differ from the read sources, but
    // must be stores the view sources (base or a join target) so the record is resolvable.
    struct ViewWriteBack
    {
        std::vector<ViewWrite>   Insert;
        std::vector<ViewWrite>   Update;
        std::vector<std::size_t> Delete; // target store indices to FreeRow the sourced record from
    };

    class View
    {
    public:
        static constexpr std::size_t NoRow = SIZE_MAX;

        View(std::size_t baseStore,
             std::vector<ViewJoin> joins,
             std::vector<ViewColumn> columns,
             std::vector<ViewScope> scope = {})
            : mBaseStore(baseStore)
            , mJoins(std::move(joins))
            , mColumns(std::move(columns))
            , mScope(std::move(scope))
        {
        }

        // Hydrate from the store table (indexed by store index): iterate the base store's valid rows,
        // apply scope, resolve each join, and copy the projected columns row-major into the payload.
        void Refresh(const std::vector<const DataStore*>& stores);

        std::size_t RowCount()    const { return mRowCount; }
        std::size_t ColumnCount() const { return mColumns.size(); }
        std::size_t RowStride()   const { return mRowStride; }
        std::size_t ByteSize()    const { return mData.size(); }
        const std::uint8_t* Data() const { return mData.data(); }

        std::size_t ColumnOffset(std::size_t col) const { return mColOffsets[col]; }
        std::size_t ColumnStride(std::size_t col) const { return mColStrides[col]; }
        const std::uint8_t* RowData(std::size_t viewRow) const { return mData.data() + viewRow * mRowStride; }
        std::uint8_t* MutableRowData(std::size_t viewRow) { return mData.data() + viewRow * mRowStride; }

        // Write a projected cell in the payload (the read/write surface). Stride-bounded.
        template <typename T>
        void Set(std::size_t viewRow, std::size_t col, const T& value)
        {
            std::memcpy(MutableRowData(viewRow) + mColOffsets[col], &value,
                        std::min(sizeof(T), mColStrides[col]));
        }

        // Append a zero-filled row in the New state (for an Insert write-back). Returns the view row.
        // Call after Refresh (the column layout must be known).
        std::size_t AddRow();

        // Typed read of a projected cell (zero-extended) — a consumer/test convenience.
        template <typename T>
        T Get(std::size_t viewRow, std::size_t col) const
        {
            T v{};
            std::memcpy(&v, RowData(viewRow) + mColOffsets[col],
                        std::min(sizeof(T), mColStrides[col]));
            return v;
        }

        ViewRowState State(std::size_t viewRow) const { return mState[viewRow]; }
        void SetState(std::size_t viewRow, ViewRowState s) { mState[viewRow] = s; }

        void SetWriteBack(ViewWriteBack wb) { mWriteBack = std::move(wb); }

        // Set the predicate program (RPN of leaves + And/Or/Not). Empty = no program (use the legacy
        // ViewScope list, if any). When present it ANDs with the scope list: a row hydrates only if it
        // passes the base-only scope pre-prune AND the program (evaluated after its joins resolve).
        void SetScopeProgram(std::vector<ViewPredicate> program) { mProgram = std::move(program); }

        // Apply the write-back to the stores by each row's change flag (Modified->Update, New->Insert,
        // Removed->Delete, Unchanged->nothing). Returns the number of store ops applied.
        std::size_t Commit(std::vector<DataStore*>& stores);

        // The store row this view row sources from a given store (base or a join target), or NoRow.
        std::size_t SourceRowForStore(std::size_t viewRow, std::size_t storeIndex) const;

        // Source map for the write-back: the base row, and each join's resolved target row (NoRow if absent).
        std::size_t SourceBaseRow(std::size_t viewRow) const { return mBaseRow[viewRow]; }
        std::size_t SourceJoinRow(std::size_t viewRow, std::size_t join) const
        {
            return mJoinRow[viewRow * mJoins.size() + join];
        }

    private:
        std::size_t mBaseStore;
        std::vector<ViewJoin> mJoins;
        std::vector<ViewColumn> mColumns;
        std::vector<ViewScope> mScope;
        std::vector<ViewPredicate> mProgram;

        std::size_t mRowCount = 0;
        std::size_t mRowStride = 0;
        std::vector<std::size_t> mColOffsets;
        std::vector<std::size_t> mColStrides;
        std::vector<std::uint8_t> mData;       // row-major payload
        std::vector<ViewRowState> mState;
        std::vector<std::size_t> mBaseRow;     // view row -> base store row
        std::vector<std::size_t> mJoinRow;     // [viewRow * joinCount + join] -> target row, or NoRow
        ViewWriteBack mWriteBack;
    };
}
