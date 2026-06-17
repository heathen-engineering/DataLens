/******************************************************************************
 * Ir.h
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * The Query/Update IR (A4): the flat, index-addressed, POINTER-FREE, serialisable
 * operation set the core ingests (DataLens-Spec.md §6.1). There is no parser or
 * query language in the core — authoring front-ends are per-platform (Stream E)
 * and all emit this same validated IR.
 *
 * This first slice covers the System (update) op: the same column transform as
 * SystemDesc, but referencing its store by INDEX into a store table supplied at
 * execution time rather than by raw pointer. That makes a program
 * serialisable/relocatable (the networking + distributed-topology seam) and lets
 * the Lens validate it once before running it via its batch/wave executor.
 ******************************************************************************/

#pragma once

#include "datalens/DataStore.h"

#include <cstdint>
#include <vector>

namespace datalens
{
    /// <summary>Kind of IR operation. Only RunSystem exists in this slice; query/view ops follow.</summary>
    enum class IrOpKind : uint32_t
    {
        RunSystem = 0,
    };

    /// <summary>
    /// One System (update) operation, fully described as data with NO pointers. The store is
    /// referenced by index into the table passed to <see cref="Lens::Execute"/>. Scalar operand and
    /// threshold are carried as double and cast to the element type at execution (Int32 fits exactly).
    /// Fixed-layout POD so an array of these serialises by raw copy.
    /// </summary>
    struct IrSystemOp
    {
        IrOpKind          kind            = IrOpKind::RunSystem;
        uint32_t          storeIndex      = 0;
        DataLensValueType elemType        = DataLensValueType::Int32; // Int32 / Float supported
        uint32_t          targetCol       = 0;
        DataSystemOp      op              = DataSystemOp::Set;
        uint32_t          operandIsColumn = 0; // bool
        uint32_t          operandCol      = 0;
        uint32_t          hasPredicate    = 0; // bool
        uint32_t          compareCol      = 0;
        DataCompareOp     cmp             = DataCompareOp::Always;
        uint8_t           minLod          = 0;
        uint8_t           maxLod          = 255;
        uint16_t          _pad            = 0;
        double            operand         = 0.0;
        double            threshold       = 0.0;

        // ── Typed builder helpers (mirror SystemDesc factories, but store-by-index) ──
        static IrSystemOp Scalar(uint32_t storeIndex, DataLensValueType elem, uint32_t targetCol,
                                 DataSystemOp op, double operand)
        {
            IrSystemOp o;
            o.storeIndex = storeIndex; o.elemType = elem; o.targetCol = targetCol;
            o.op = op; o.operand = operand;
            return o;
        }

        static IrSystemOp Column(uint32_t storeIndex, DataLensValueType elem, uint32_t targetCol,
                                 DataSystemOp op, uint32_t operandCol)
        {
            IrSystemOp o;
            o.storeIndex = storeIndex; o.elemType = elem; o.targetCol = targetCol;
            o.op = op; o.operandIsColumn = 1; o.operandCol = operandCol;
            return o;
        }

        IrSystemOp& WithPredicate(uint32_t compareColumn, DataCompareOp compareOp, double thr)
        {
            hasPredicate = 1; compareCol = compareColumn; cmp = compareOp; threshold = thr;
            return *this;
        }

        IrSystemOp& WithLodBand(uint8_t lo, uint8_t hi) { minLod = lo; maxLod = hi; return *this; }
    };

    /// <summary>
    /// An ordered IR program: a list of operations the Lens executes in order (with the same
    /// non-conflicting-Systems-run-concurrently semantics as a batch). Serialisable to/from a flat
    /// byte buffer for storage, transport, or replay.
    /// </summary>
    class IrProgram
    {
    public:
        void Add(const IrSystemOp& op) { mSystems.push_back(op); }
        std::size_t Count() const { return mSystems.size(); }
        const std::vector<IrSystemOp>& Systems() const { return mSystems; }
        void Clear() { mSystems.clear(); }

        /// Serialise to a self-describing byte buffer: header (magic, version, count) + raw ops.
        /// Little-endian / same-architecture for v1 (matches the store's current LE assumption);
        /// an endianness-free encoding is future work (A6).
        std::vector<uint8_t> Serialize() const;

        /// Parse a buffer produced by Serialize. Returns false (and leaves out empty) on a bad magic,
        /// unsupported version, or truncated/oversized buffer.
        static bool Deserialize(const uint8_t* data, std::size_t size, IrProgram& out);

        static constexpr uint32_t kMagic   = 0x52494C44u; // 'DLIR' little-endian
        static constexpr uint32_t kVersion = 1u;

    private:
        std::vector<IrSystemOp> mSystems;
    };
}
