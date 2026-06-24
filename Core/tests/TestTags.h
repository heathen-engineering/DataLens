#pragma once

#include "datalens/DataLensSchema.h"
#include <string_view>

// Tests-only helper. Core is pure u64 (it never sees strings); these substrate tests, however, are
// easier to read with names, so this hashes a name to a stable, globally-unique-enough DataLensId.
// FNV-1a; never returns 0 (which is the reserved DataLensRowFlagsTag).
inline DataLensId Tag(std::string_view name)
{
    DataLensId h = 1469598103934665603ULL;
    for (char c : name)
    {
        h ^= static_cast<unsigned char>(c);
        h *= 1099511628211ULL;
    }
    return h == DataLensRowFlagsTag ? 1u : h;
}
