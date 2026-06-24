/******************************************************************************
 * DataStore.cpp
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2026-01-29
 ******************************************************************************/

#pragma once

#include "datalens/DataStore.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

/// <summary>
/// Initialise with column metadata and preallocate rows.
/// </summary>
/// <param name="columns"></param>
/// <param name="preallocRows"></param>
DataStore::DataStore(const std::vector<DataStoreColumnSchema>& columns, size_t preallocRows)
	: mColumns(columns)
{
	// Route through InitializeColumns so column data AND the validity/locked bitmasks are
	// sized consistently (the inline version used to skip the bitmasks).
	InitializeColumns(preallocRows);
}

/// <summary>
/// Initialise with column metadata and pre-load data from a byte array (row-major layout).
/// </summary>
/// <param name="columns"></param>
/// <param name="data"></param>
DataStore::DataStore(const std::vector<DataStoreColumnSchema>& columns, const std::vector<uint8_t>& data)
	: mColumns(columns)
{
	size_t rowStride = GetRowStride();
	if (data.size() % rowStride != 0)
	{
		throw std::runtime_error("Data size is not a multiple of row stride");
	}

	size_t rows = data.size() / rowStride;
	InitializeColumns(rows);
	LoadDataFromRowMajor(data, rows);
}

/// <summary>
/// Initialise with column metadata, pre-load data, and optionally preallocate extra rows.
/// </summary>
/// <param name="columns"></param>
/// <param name="data"></param>
/// <param name="extraRows"></param>
DataStore::DataStore(const std::vector<DataStoreColumnSchema>& columns, const std::vector<uint8_t>& data,
                     size_t extraRows)
	: mColumns(columns)
{
	size_t rowStride = GetRowStride();
	if (data.size() % rowStride != 0)
	{
		throw std::runtime_error("Data size is not a multiple of row stride");
	}

	size_t loadedRows = data.size() / rowStride;
	InitializeColumns(loadedRows + extraRows);
	LoadDataFromRowMajor(data, loadedRows);
}

/// <summary>
/// Load raw row-major data into the datastore.
/// </summary>
/// <param name="src"></param>
void DataStore::LoadRaw(const std::vector<uint8_t>& src)
{
	size_t rowStride = GetRowStride();
	if (rowStride == 0)
	{
		throw std::runtime_error("Cannot load data: row stride is zero (no columns defined)");
	}

	size_t rows = src.size() / rowStride;
	InitializeColumns(rows);
	LoadDataFromRowMajor(src, rows);
}

/// <summary>
/// Dump the current table data in row-major layout.
/// </summary>
/// <returns></returns>
std::vector<uint8_t> DataStore::Dump() const
{
	size_t rowStride = GetRowStride();
	std::vector<uint8_t> out(rowStride * mRowCount);
	for (size_t r = 0; r < mRowCount; ++r)
	{
		size_t offset = r * rowStride;
		for (size_t c = 0; c < mColumns.size(); ++c)
		{
			std::memcpy(out.data() + offset, GetRawCell(r, c), mColumns[c].GetStride());
			offset += mColumns[c].GetStride();
		}
	}
	return out;
}

size_t DataStore::GetRowCount() const { return mRowCount; }

size_t DataStore::GetColumnCount() const { return mColumns.size(); }

size_t DataStore::GetColumnStride(size_t col) const { return mColumns[col].GetStride(); }

const uint8_t* DataStore::GetColumnRaw(size_t col) const
{
	if (col >= mColumnsData.size())
	{
		return nullptr;
	}
	return mColumnsData[col].data();
}

void DataStore::WriteCellRaw(size_t row, size_t col, const uint8_t* src, size_t srcLen)
{
	if (col >= mColumns.size() || row >= mRowCount || src == nullptr)
	{
		return;
	}
	const size_t stride = mColumns[col].GetStride();
	uint8_t* dst = GetRawCellMutable(row, col);
	const size_t n = std::min(srcLen, stride);
	std::memcpy(dst, src, n);
	if (n < stride)
	{
		std::memset(dst + n, 0, stride - n); // short source zero-fills the rest of the cell (§5)
	}
}

namespace
{
	inline unsigned CtzU64(uint64_t x)
	{
#if defined(_MSC_VER)
		unsigned long idx;
		_BitScanForward64(&idx, x);
		return static_cast<unsigned>(idx);
#else
		return static_cast<unsigned>(__builtin_ctzll(x));
#endif
	}
}

bool DataStore::IsValidRow(size_t row) const
{
	if (row >= mRowCount)
	{
		return false;
	}
	return (mValidBits[row >> 6] >> (row & 63)) & 1ULL;
}

bool DataStore::IsLockedRow(size_t row) const
{
	if (row >= mRowCount)
	{
		return false;
	}
	return (mLockedBits[row >> 6] >> (row & 63)) & 1ULL;
}

void DataStore::SetValid(size_t row, bool valid)
{
	if (row >= mRowCount)
	{
		return;
	}

	uint64_t& word = mValidBits[row >> 6];
	const uint64_t mask = 1ULL << (row & 63);
	const bool was = (word & mask) != 0;

	if (valid && !was)
	{
		word |= mask;
		++mLiveCount;
	}
	else if (!valid && was)
	{
		word &= ~mask;
		--mLiveCount;
	}
}

void DataStore::SetLocked(size_t row, bool locked)
{
	if (row >= mRowCount)
	{
		return;
	}

	uint64_t& word = mLockedBits[row >> 6];
	const uint64_t mask = 1ULL << (row & 63);
	if (locked)
	{
		word |= mask;
	}
	else
	{
		word &= ~mask;
	}
}

void DataStore::SetLod(size_t row, uint8_t level)
{
	if (row >= mRowCount)
	{
		return;
	}
	mLodLevels[row] = level;
}

uint8_t DataStore::GetLod(size_t row) const
{
	if (row >= mRowCount)
	{
		return 0;
	}
	return mLodLevels[row];
}

size_t DataStore::AllocRow()
{
	if (mRowCount == 0)
	{
		return SIZE_MAX;
	}

	const size_t words = mValidBits.size();
	for (size_t i = 0; i < words; ++i)
	{
		const size_t wi = (mAllocCursor + i) % words;
		uint64_t free = ~mValidBits[wi];

		// Mask off bit positions past mRowCount in the final word so we never hand out
		// a row index beyond capacity.
		if (wi == words - 1)
		{
			const size_t bitsInLast = mRowCount - wi * 64; // 1..64
			if (bitsInLast < 64)
			{
				free &= (1ULL << bitsInLast) - 1ULL;
			}
		}

		if (free != 0)
		{
			const unsigned bit = CtzU64(free);
			const size_t row = wi * 64 + bit;
			mValidBits[wi] |= (1ULL << bit);
			mLodLevels[row] = 0; // a freshly allocated row starts at full fidelity
			++mLiveCount;
			mAllocCursor = wi;

			// Seed the row from each column's default (or zero) so a new / reused row starts clean (Â§5).
			for (size_t c = 0; c < mColumns.size(); ++c)
			{
				const size_t stride = mColumns[c].GetStride();
				uint8_t* cell = mColumnsData[c].data() + row * stride;
				const std::vector<uint8_t>& def = mColumns[c].DefaultValue;
				if (!def.empty())
				{
					const size_t n = std::min(stride, def.size());
					std::memcpy(cell, def.data(), n);
					if (n < stride) std::memset(cell + n, 0, stride - n);
				}
				else
				{
					std::memset(cell, 0, stride);
				}
			}

			return row;
		}
	}

	return SIZE_MAX; // at capacity (fixed capacity: no growth)
}

void DataStore::FreeRow(size_t row)
{
	if (row >= mRowCount)
	{
		return;
	}

	const size_t wi = row >> 6;
	const uint64_t mask = 1ULL << (row & 63);

	if (mValidBits[wi] & mask)
	{
		mValidBits[wi] &= ~mask;
		--mLiveCount;
	}
	mLockedBits[wi] &= ~mask; // a freed slot is never locked

	if (wi < mAllocCursor)
	{
		mAllocCursor = wi; // bias the next alloc toward the freed slot
	}
}

size_t DataStore::GetLiveCount() const
{
	return mLiveCount;
}

bool DataStore::IsColumnCacheAligned(size_t col) const
{
	if (col >= mColumnsData.size())
	{
		return false;
	}
	const auto addr = reinterpret_cast<std::uintptr_t>(mColumnsData[col].data());
	return (addr % CacheLineSize()) == 0;
}

/// <summary>
/// Total bytes per row (sum of all column strides).
/// </summary>
/// <returns></returns>
size_t DataStore::GetRowStride() const
{
	size_t stride = 0;
	for (auto& col : mColumns)
	{
		stride += col.GetStride();
	}
	return stride;
}

void DataStore::ConvertToSchema(const DataStoreSchema& newSchema)
{
	std::vector<ColumnBuffer> newColumnsData;
	newColumnsData.reserve(newSchema.Columns.size());

	size_t rowCount = mRowCount;

	for (const DataStoreColumnSchema& newCol : newSchema.Columns)
	{
		size_t newStride = newCol.GetStride();
		ColumnBuffer newColData(rowCount * newStride, 0); // zero-initialize (cache-line-aligned)

		auto it = std::find_if(mColumns.begin(), mColumns.end(),
		                       [&newCol](const DataStoreColumnSchema& oldCol) { return oldCol.Tag == newCol.Tag; });

		if (it != mColumns.end())
		{
			size_t oldStride = it->GetStride();
			size_t colIndex = std::distance(mColumns.begin(), it);
			const ColumnBuffer& oldData = mColumnsData[colIndex];
			size_t copySize = std::min(oldStride, newStride);

			for (size_t row = 0; row < rowCount; ++row)
			{
				// Type-blind migration: copy the raw bytes, stride-bounded (newColData is zero-filled, so
				// a wider new stride zero-extends and a narrower one truncates the low bytes).
				std::memcpy(newColData.data() + row * newStride, oldData.data() + row * oldStride, copySize);
			}
		}
		else if (!newCol.DefaultValue.empty())
		{
			size_t copySize = std::min(newStride, newCol.DefaultValue.size());
			for (size_t row = 0; row < rowCount; ++row)
			{
				std::memcpy(newColData.data() + row * newStride, newCol.DefaultValue.data(), copySize);
				if (copySize < newStride)
				{
					std::memset(newColData.data() + row * newStride + copySize, 0, newStride - copySize);
				}
			}
		}
		// else: column missing and no default, already zeroed above

		newColumnsData.push_back(std::move(newColData));
	}

	mColumns = newSchema.Columns;
	mColumnsData = std::move(newColumnsData);
}

/// <summary>
/// Get a pointer to a raw cell (unchecked).
/// </summary>
/// <param name="row"></param>
/// <param name="col"></param>
/// <returns></returns>
uint8_t* DataStore::GetRawCellMutable(size_t row, size_t col)
{
	return mColumnsData[col].data() + row * mColumns[col].GetStride();
}

/// <summary>
/// Get a pointer to a raw cell (unchecked).
/// </summary>
/// <param name="row"></param>
/// <param name="col"></param>
/// <returns></returns>
const uint8_t* DataStore::GetRawCell(size_t row, size_t col) const
{
	return mColumnsData[col].data() + row * mColumns[col].GetStride();
}

void DataStore::InitializeColumns(size_t rows)
{
	mRowCount = rows;
	mColumnsData.resize(mColumns.size());
	for (size_t c = 0; c < mColumns.size(); ++c)
	{
		mColumnsData[c].resize(mColumns[c].GetStride() * rows);
	}

	const size_t words = (rows + 63) / 64;
	mValidBits.assign(words, 0);   // all rows start invalid (free)
	mLockedBits.assign(words, 0);
	mLodLevels.assign(rows, 0);    // every row starts at LOD 0 (highest fidelity)
	mLiveCount = 0;
	mAllocCursor = 0;
}

void DataStore::LoadDataFromRowMajor(const std::vector<uint8_t>& src, size_t rows)
{
	size_t rowStride = GetRowStride();
	for (size_t r = 0; r < rows; ++r)
	{
		size_t srcOffset = r * rowStride;
		size_t dstOffset = 0;
		for (size_t c = 0; c < mColumns.size(); ++c)
		{
			std::memcpy(mColumnsData[c].data() + r * mColumns[c].GetStride(),
			            src.data() + srcOffset + dstOffset,
			            mColumns[c].GetStride());
			dstOffset += mColumns[c].GetStride();
		}
	}
}

bool DataStore::IsValid(size_t row, size_t col) const
{
	return row < mRowCount && col < mColumns.size();
}

const void* DataStore::GetCellPointer(size_t rowIndex, size_t columnIndex) const
{
	if (columnIndex >= mColumnsData.size() || rowIndex >= mRowCount)
	{
		throw std::out_of_range("Invalid row or column index");
	}

	const DataStoreColumnSchema& col = mColumns[columnIndex];
	size_t stride = col.GetStride();
	return mColumnsData[columnIndex].data() + rowIndex * stride;
}
