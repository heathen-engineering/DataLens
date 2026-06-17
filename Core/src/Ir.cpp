/******************************************************************************
 * Ir.cpp
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Serialisation for the Query/Update IR (A4). Format is a small fixed header
 * followed by the raw op array; little-endian / same-architecture for v1.
 ******************************************************************************/

#include "datalens/Ir.h"

#include <cstring>

namespace datalens
{
    namespace
    {
        struct IrHeader
        {
            uint32_t magic;
            uint32_t version;
            uint64_t opCount;
        };
    }

    std::vector<uint8_t> IrProgram::Serialize() const
    {
        IrHeader header{kMagic, kVersion, static_cast<uint64_t>(mSystems.size())};

        const std::size_t opBytes = mSystems.size() * sizeof(IrSystemOp);
        std::vector<uint8_t> out(sizeof(IrHeader) + opBytes);

        std::memcpy(out.data(), &header, sizeof(IrHeader));
        if (opBytes != 0)
            std::memcpy(out.data() + sizeof(IrHeader), mSystems.data(), opBytes);

        return out;
    }

    bool IrProgram::Deserialize(const uint8_t* data, std::size_t size, IrProgram& out)
    {
        out.Clear();

        if (data == nullptr || size < sizeof(IrHeader))
            return false;

        IrHeader header{};
        std::memcpy(&header, data, sizeof(IrHeader));

        if (header.magic != kMagic || header.version != kVersion)
            return false;

        // Guard against a truncated or impossibly large op count before sizing anything.
        const std::size_t remaining = size - sizeof(IrHeader);
        if (header.opCount > remaining / sizeof(IrSystemOp))
            return false;
        if (remaining != header.opCount * sizeof(IrSystemOp))
            return false; // trailing garbage / wrong size

        out.mSystems.resize(static_cast<std::size_t>(header.opCount));
        if (header.opCount != 0)
            std::memcpy(out.mSystems.data(), data + sizeof(IrHeader),
                        static_cast<std::size_t>(header.opCount) * sizeof(IrSystemOp));

        return true;
    }
}
