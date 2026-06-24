/******************************************************************************
 * View.cpp  -  the Core view read (DataLens-Spec.md §6.4).
 ******************************************************************************/

#include "datalens/View.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace datalens
{
    namespace
    {
        // Read up to 8 bytes of a cell, zero-extended, as a u64 (for the index-dereference join).
        std::uint64_t ReadIndex(const DataStore& s, std::size_t col, std::size_t row)
        {
            std::uint64_t v = 0;
            const std::size_t stride = s.GetColumnStride(col);
            const std::uint8_t* cell = s.GetColumnRaw(col) + row * stride;
            std::memcpy(&v, cell, std::min<std::size_t>(sizeof(v), stride));
            return v;
        }

        // Interpret a cell as a sign/zero-extended int64 according to its declared type (for scope).
        std::int64_t ReadAsInt(const std::uint8_t* cell, DataLensValueType type)
        {
            switch (type)
            {
            case DataLensValueType::Bool:   return *reinterpret_cast<const std::uint8_t*>(cell) ? 1 : 0;
            case DataLensValueType::Int8:   return *reinterpret_cast<const std::int8_t*>(cell);
            case DataLensValueType::UInt8:  return *reinterpret_cast<const std::uint8_t*>(cell);
            case DataLensValueType::Int16:  return *reinterpret_cast<const std::int16_t*>(cell);
            case DataLensValueType::UInt16: return *reinterpret_cast<const std::uint16_t*>(cell);
            case DataLensValueType::Int32:  return *reinterpret_cast<const std::int32_t*>(cell);
            case DataLensValueType::UInt32: return *reinterpret_cast<const std::uint32_t*>(cell);
            case DataLensValueType::Int64:  return *reinterpret_cast<const std::int64_t*>(cell);
            case DataLensValueType::UInt64: return static_cast<std::int64_t>(*reinterpret_cast<const std::uint64_t*>(cell));
            default:                        return 0;
            }
        }

        bool CompareInt(std::int64_t cell, DataCompareOp op, std::int64_t thr)
        {
            const std::uint64_t c = static_cast<std::uint64_t>(cell);
            const std::uint64_t t = static_cast<std::uint64_t>(thr);
            switch (op)
            {
            case DataCompareOp::Always:       return true;
            case DataCompareOp::Equal:        return cell == thr;
            case DataCompareOp::NotEqual:     return cell != thr;
            case DataCompareOp::Less:         return cell < thr;
            case DataCompareOp::LessEqual:    return cell <= thr;
            case DataCompareOp::Greater:      return cell > thr;
            case DataCompareOp::GreaterEqual: return cell >= thr;
            case DataCompareOp::HasAllBits:   return (c & t) == t;
            case DataCompareOp::HasAnyBits:   return (c & t) != 0;
            case DataCompareOp::LacksBits:    return (c & t) == 0;
            }
            return false;
        }

        bool CompareDouble(double cell, DataCompareOp op, double thr)
        {
            switch (op)
            {
            case DataCompareOp::Always:       return true;
            case DataCompareOp::Equal:        return cell == thr;
            case DataCompareOp::NotEqual:     return cell != thr;
            case DataCompareOp::Less:         return cell < thr;
            case DataCompareOp::LessEqual:    return cell <= thr;
            case DataCompareOp::Greater:      return cell > thr;
            case DataCompareOp::GreaterEqual: return cell >= thr;
            default:                          return false; // bitmask ops never match a float column
            }
        }

        bool PassesScope(const DataStore& base, std::size_t row, const std::vector<ViewScope>& scope)
        {
            for (const ViewScope& p : scope)
            {
                const std::uint8_t* cell = base.GetColumnRaw(p.Column) + row * base.GetColumnStride(p.Column);
                bool ok;
                if (p.Type == DataLensValueType::Float)
                    ok = CompareDouble(*reinterpret_cast<const float*>(cell), p.Op, p.DValue);
                else if (p.Type == DataLensValueType::Double)
                    ok = CompareDouble(*reinterpret_cast<const double*>(cell), p.Op, p.DValue);
                else
                    ok = CompareInt(ReadAsInt(cell, p.Type), p.Op, p.IValue);
                if (!ok)
                    return false;
            }
            return true;
        }

        // Resolve one join for a base row: the target store row, or View::NoRow when absent / invalid.
        std::size_t ResolveJoin(const std::vector<const DataStore*>& stores,
                                const DataStore& base, std::size_t baseRow, const ViewJoin& join)
        {
            std::size_t targetRow;
            if (join.Aligned)
            {
                targetRow = baseRow;
            }
            else
            {
                const std::uint64_t idx = ReadIndex(base, join.IndexColumn, baseRow);
                if (idx == join.AbsentSentinel)
                    return View::NoRow;
                targetRow = static_cast<std::size_t>(idx);
            }
            const DataStore& t = *stores[join.TargetStore];
            if (targetRow >= t.GetRowCount() || !t.IsValidRow(targetRow))
                return View::NoRow;
            return targetRow;
        }

        // Evaluate one predicate leaf for a base row + its resolved joins. A leaf addresses a cell like a
        // projected column (Source 0 = base, k>=1 = the (k-1)th join's target); a leaf on an absent join
        // row is false. Range leaves are the fused interval test, others use Op against the threshold.
        bool EvalLeaf(const std::vector<const DataStore*>& stores,
                      std::size_t baseStore, const std::vector<ViewJoin>& joins,
                      std::size_t baseRow, const std::vector<std::size_t>& joinRows, const ViewPredicate& p)
        {
            std::size_t storeIdx;
            std::size_t row;
            if (p.Source == 0)
            {
                storeIdx = baseStore;
                row = baseRow;
            }
            else
            {
                const std::size_t jr = joinRows[p.Source - 1];
                if (jr == View::NoRow)
                    return false; // absent dereference -> leaf is false (§6.4.1)
                storeIdx = joins[p.Source - 1].TargetStore;
                row = jr;
            }
            const DataStore& s = *stores[storeIdx];
            const std::uint8_t* cell = s.GetColumnRaw(p.Column) + row * s.GetColumnStride(p.Column);

            if (p.Type == DataLensValueType::Float || p.Type == DataLensValueType::Double)
            {
                const double v = (p.Type == DataLensValueType::Float)
                                     ? static_cast<double>(*reinterpret_cast<const float*>(cell))
                                     : *reinterpret_cast<const double*>(cell);
                return p.Range ? (v >= p.DValue && v <= p.DHi) : CompareDouble(v, p.Op, p.DValue);
            }
            const std::int64_t v = ReadAsInt(cell, p.Type);
            if (p.Range) // branchless interval test: (uint64)(v - lo) <= (uint64)(hi - lo)
                return static_cast<std::uint64_t>(v - p.IValue) <= static_cast<std::uint64_t>(p.IHi - p.IValue);
            return CompareInt(v, p.Op, p.IValue);
        }

        // Evaluate the RPN predicate program for a row. Empty stack defaults to "pass".
        bool EvalProgram(const std::vector<const DataStore*>& stores,
                         std::size_t baseStore, const std::vector<ViewJoin>& joins,
                         std::size_t baseRow, const std::vector<std::size_t>& joinRows,
                         const std::vector<ViewPredicate>& program)
        {
            bool stack[64];
            int sp = 0;
            for (const ViewPredicate& p : program)
            {
                switch (p.Kind)
                {
                case ViewPredicateKind::Leaf:
                    if (sp < 64)
                        stack[sp++] = EvalLeaf(stores, baseStore, joins, baseRow, joinRows, p);
                    break;
                case ViewPredicateKind::Not:
                    if (sp >= 1)
                        stack[sp - 1] = !stack[sp - 1];
                    break;
                case ViewPredicateKind::And:
                    if (sp >= 2) { stack[sp - 2] = stack[sp - 2] && stack[sp - 1]; --sp; }
                    break;
                case ViewPredicateKind::Or:
                    if (sp >= 2) { stack[sp - 2] = stack[sp - 2] || stack[sp - 1]; --sp; }
                    break;
                }
            }
            return sp > 0 ? stack[sp - 1] : true;
        }
    }

    void View::Refresh(const std::vector<const DataStore*>& stores)
    {
        const DataStore& base = *stores[mBaseStore];

        // 1. Type-blind column layout: each projected column's stride comes from its source store.
        mColStrides.resize(mColumns.size());
        mColOffsets.resize(mColumns.size());
        mRowStride = 0;
        for (std::size_t k = 0; k < mColumns.size(); ++k)
        {
            const std::size_t storeIdx =
                (mColumns[k].Source == 0) ? mBaseStore : mJoins[mColumns[k].Source - 1].TargetStore;
            mColStrides[k] = stores[storeIdx]->GetColumnStride(mColumns[k].Column);
            mColOffsets[k] = mRowStride;
            mRowStride += mColStrides[k];
        }

        // 2. Iterate the base store's valid rows, apply scope, resolve joins, copy projected bytes.
        mData.clear();
        mState.clear();
        mBaseRow.clear();
        mJoinRow.clear();
        mRowCount = 0;

        std::vector<std::size_t> joinRows(mJoins.size());
        const std::size_t baseRows = base.GetRowCount();
        for (std::size_t r = 0; r < baseRows; ++r)
        {
            if (!base.IsValidRow(r))
                continue;
            if (!PassesScope(base, r, mScope))
                continue;

            for (std::size_t j = 0; j < mJoins.size(); ++j)
                joinRows[j] = ResolveJoin(stores, base, r, mJoins[j]);

            // Predicate program (the full boolean filter tree) runs after joins resolve, so a leaf may
            // address a dereferenced column. It ANDs with the base-only scope pre-prune above.
            if (!mProgram.empty() &&
                !EvalProgram(stores, mBaseStore, mJoins, r, joinRows, mProgram))
                continue;

            const std::size_t rowOff = mData.size();
            mData.resize(rowOff + mRowStride, 0); // zero-filled: an absent join's columns default to zero

            for (std::size_t k = 0; k < mColumns.size(); ++k)
            {
                const std::size_t src = mColumns[k].Source;
                const std::size_t srcRow = (src == 0) ? r : joinRows[src - 1];
                if (srcRow == NoRow)
                    continue; // absent -> leave the default (zero) bytes
                const std::size_t storeIdx = (src == 0) ? mBaseStore : mJoins[src - 1].TargetStore;
                const DataStore& ss = *stores[storeIdx];
                std::memcpy(mData.data() + rowOff + mColOffsets[k],
                            ss.GetColumnRaw(mColumns[k].Column) + srcRow * ss.GetColumnStride(mColumns[k].Column),
                            mColStrides[k]);
            }

            mState.push_back(ViewRowState::Unchanged);
            mBaseRow.push_back(r);
            for (std::size_t j = 0; j < mJoins.size(); ++j)
                mJoinRow.push_back(joinRows[j]);
            ++mRowCount;
        }
    }

    std::size_t View::AddRow()
    {
        const std::size_t vr = mRowCount;
        mData.resize(mData.size() + mRowStride, 0);
        mState.push_back(ViewRowState::New);
        mBaseRow.push_back(NoRow);
        for (std::size_t j = 0; j < mJoins.size(); ++j)
            mJoinRow.push_back(NoRow);
        ++mRowCount;
        return vr;
    }

    std::size_t View::SourceRowForStore(std::size_t viewRow, std::size_t storeIndex) const
    {
        if (storeIndex == mBaseStore)
            return mBaseRow[viewRow];
        for (std::size_t j = 0; j < mJoins.size(); ++j)
            if (mJoins[j].TargetStore == storeIndex)
                return mJoinRow[viewRow * mJoins.size() + j];
        return NoRow;
    }

    std::size_t View::Commit(std::vector<DataStore*>& stores)
    {
        std::size_t ops = 0;
        for (std::size_t vr = 0; vr < mRowCount; ++vr)
        {
            switch (mState[vr])
            {
            case ViewRowState::Modified:
                for (const ViewWrite& w : mWriteBack.Update)
                {
                    const std::size_t row = SourceRowForStore(vr, w.TargetStore);
                    if (row == NoRow)
                        continue; // can't update an absent record
                    stores[w.TargetStore]->WriteCellRaw(row, w.TargetColumn,
                        RowData(vr) + mColOffsets[w.ViewColumn], mColStrides[w.ViewColumn]);
                    ++ops;
                }
                break;

            case ViewRowState::New:
            {
                // One fresh record per distinct target store referenced by the Insert instruction.
                std::unordered_map<std::size_t, std::size_t> newRows;
                for (const ViewWrite& w : mWriteBack.Insert)
                {
                    std::size_t row;
                    auto it = newRows.find(w.TargetStore);
                    if (it != newRows.end())
                        row = it->second;
                    else
                    {
                        row = stores[w.TargetStore]->AllocRow();
                        newRows[w.TargetStore] = row;
                    }
                    if (row == SIZE_MAX)
                        continue; // store full -> insert fizzles (fallible, Spec §5)
                    stores[w.TargetStore]->WriteCellRaw(row, w.TargetColumn,
                        RowData(vr) + mColOffsets[w.ViewColumn], mColStrides[w.ViewColumn]);
                    ++ops;
                }

                // Linked insert (§6.4.1): wire each dereference join's index column in the new base record
                // to the target store's freshly allocated row, so the catalogue points at the new trait row.
                // (The base record's index column is seeded to the absent sentinel by AllocRow and only made
                // meaningful here, where the target row index is known.)
                {
                    auto baseIt = newRows.find(mBaseStore);
                    if (baseIt != newRows.end() && baseIt->second != SIZE_MAX)
                    {
                        for (const ViewJoin& j : mJoins)
                        {
                            if (j.Aligned)
                                continue;
                            auto tIt = newRows.find(j.TargetStore);
                            if (tIt == newRows.end() || tIt->second == SIZE_MAX)
                                continue;
                            const std::uint64_t idx = static_cast<std::uint64_t>(tIt->second);
                            stores[mBaseStore]->WriteCellRaw(baseIt->second, j.IndexColumn,
                                reinterpret_cast<const std::uint8_t*>(&idx), sizeof(idx));
                            ++ops;
                        }
                    }
                }

                // Reflect the allocated rows in this view row's source map so SourceBaseRow / SourceJoinRow
                // are valid right after an Insert (the consumer reads SourceBaseRow to learn the new record's
                // prime-store index, e.g. a freshly spawned entity's catalog row = its EntityId).
                {
                    auto bIt = newRows.find(mBaseStore);
                    if (bIt != newRows.end() && bIt->second != SIZE_MAX)
                        mBaseRow[vr] = bIt->second;
                    for (std::size_t j = 0; j < mJoins.size(); ++j)
                    {
                        auto tIt = newRows.find(mJoins[j].TargetStore);
                        if (tIt != newRows.end() && tIt->second != SIZE_MAX)
                            mJoinRow[vr * mJoins.size() + j] = tIt->second;
                    }
                }
                break;
            }

            case ViewRowState::Removed:
                for (std::size_t s : mWriteBack.Delete)
                {
                    const std::size_t row = SourceRowForStore(vr, s);
                    if (row == NoRow)
                        continue;
                    stores[s]->FreeRow(row);
                    ++ops;
                }
                break;

            default:
                break; // Unchanged
            }
        }
        return ops;
    }
}
