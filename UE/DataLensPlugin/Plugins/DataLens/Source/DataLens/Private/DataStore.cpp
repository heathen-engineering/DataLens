/******************************************************************************
 * DataStore.cpp
 *
 * (c) 2025-2026 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2026-01-29
 ******************************************************************************/

#pragma once

#include "DataStore.h"
#include <cstring>

/// <summary>
/// Initialise with column metadata and preallocate rows.
/// </summary>
/// <param name="columns"></param>
/// <param name="preallocRows"></param>
DataStore::DataStore(const std::vector<DataStoreColumnSchema>& columns, size_t preallocRows)
	: mColumns(columns), mRowCount(preallocRows)
{
	mColumnsData.resize(columns.size());
	for (size_t c = 0; c < columns.size(); ++c)
	{
		mColumnsData[c].resize(columns[c].GetStride() * preallocRows);
	}
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

bool DataStore::IsValidRow(size_t row) const
{
	// is the 1st bit of the 1st byte of this row 1 = true, 0 = false
	if (mColumns.empty() || row >= mRowCount)
	{
		return false;
	}

	uint8_t flag = *GetRawCell(row, 0);
	return (flag & 0x01) != 0;
}

bool DataStore::IsLockedRow(size_t row) const
{
	// is the 2nd bit of the 1st byte of this row 1 = true, 0 = false
	if (mColumns.empty() || row >= mRowCount)
	{
		return false;
	}

	uint8_t flag = *GetRawCell(row, 0);
	return (flag & 0x02) != 0;
}

void DataStore::SetValid(size_t row, bool valid)
{
	// set the 1st bit of the 1st byte 1 if valid == true, else 0
	if (mColumns.empty() || row >= mRowCount)
	{
		return;
	}

	uint8_t* flagCell = GetRawCellMutable(row, 0);

	if (valid)
	{
		*flagCell |= 0x01;
	}
	else
	{
		*flagCell &= ~0x01;
	}
}

void DataStore::SetLocked(size_t row, bool locked)
{
	// set the 2nd bit of the 1st byte 1 if locked == true, else 0
	if (mColumns.empty() || row >= mRowCount)
	{
		return;
	}

	uint8_t* flagCell = GetRawCellMutable(row, 0);

	if (locked)
	{
		*flagCell |= 0x02;
	}
	else
	{
		*flagCell &= ~0x02;
	}
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
	std::vector<std::vector<uint8_t>> newColumnsData;
	newColumnsData.reserve(newSchema.Columns.size());

	size_t rowCount = mRowCount;

	for (const DataStoreColumnSchema& newCol : newSchema.Columns)
	{
		size_t newStride = newCol.GetStride();
		std::vector<uint8_t> newColData(rowCount * newStride, 0); // zero-initialize

		auto it = std::find_if(mColumns.begin(), mColumns.end(),
		                       [&newCol](const DataStoreColumnSchema& oldCol) { return oldCol.Name == newCol.Name; });

		if (it != mColumns.end())
		{
			size_t oldStride = it->GetStride();
			size_t colIndex = std::distance(mColumns.begin(), it);
			const std::vector<uint8_t>& oldData = mColumnsData[colIndex];

			for (size_t row = 0; row < rowCount; ++row)
			{
				const uint8_t* src = oldData.data() + row * oldStride;
				uint8_t* dst = newColData.data() + row * newStride;

				DataLensValueTypeUtils::ConvertData(src, it->Type, dst, newCol.Type);
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

bool DataStore::CompareCells(size_t rowA, size_t columnA, const DataStore& other, size_t rowB, size_t columnB) const
{
	if (columnA >= mColumns.size() || columnB >= other.mColumns.size())
	{
		throw std::out_of_range("Column index out of range");
	}

	const DataStoreColumnSchema& colA = mColumns[columnA];
	const DataStoreColumnSchema& colB = other.mColumns[columnB];

	uint8_t bufferA[16]; // max GUID
	uint8_t bufferB[16];

	const void* srcA = GetCellPointer(rowA, columnA);
	const void* srcB = other.GetCellPointer(rowB, columnB);

	DataLensValueTypeUtils::ConvertData(srcA, colA.Type, bufferA, colA.Type);
	DataLensValueTypeUtils::ConvertData(srcB, colB.Type, bufferB, colA.Type);

	return std::memcmp(bufferA, bufferB, colA.GetStride()) == 0;
}

void DataStore::CopyCellToFlatRow(size_t rowIndex, size_t columnIndex, void* dst) const
{
	if (columnIndex >= mColumns.size())
	{
		throw std::out_of_range("Column index out of range");
	}

	const DataStoreColumnSchema& col = mColumns[columnIndex];
	const void* src = GetCellPointer(rowIndex, columnIndex);

	DataLensValueTypeUtils::ConvertData(src, col.Type, dst, col.Type);
}

bool DataStore::MatchesPredicate(size_t rowIndex, const DataQueryPredicate& pred) const
{
	if (rowIndex >= mRowCount || pred.ColumnIndex >= mColumns.size())
	{
		throw std::out_of_range("Row or column index out of range");
	}

	const DataStoreColumnSchema& col = mColumns[pred.ColumnIndex];

	// Allocate temporary storage for conversion
	uint8_t buffer[16]; // max size for GUID

	const void* src = GetRawCell(rowIndex, pred.ColumnIndex);

	// Convert cell to the type of the predicate
	DataLensValueTypeUtils::ConvertData(src, col.Type, buffer, pred.ColumnType);

	switch (pred.ColumnType)
	{
	case DataLensValueType::Bool:
	case DataLensValueType::Int8:
	case DataLensValueType::Int16:
	case DataLensValueType::Int32:
	case DataLensValueType::Int64:
	case DataLensValueType::UInt8:
	case DataLensValueType::UInt16:
	case DataLensValueType::UInt32:
	case DataLensValueType::UInt64:
		{
			int64_t cellValue = 0;
			std::memcpy(&cellValue, buffer, sizeof(cellValue));

			switch (pred.Op)
			{
			case DataQueryOperator::Equals: return cellValue == pred.IntValue;
			case DataQueryOperator::NotEquals: return cellValue != pred.IntValue;
			case DataQueryOperator::Less: return cellValue < pred.IntValue;
			case DataQueryOperator::LessOrEqual: return cellValue <= pred.IntValue;
			case DataQueryOperator::Greater: return cellValue > pred.IntValue;
			case DataQueryOperator::GreaterOrEqual: return cellValue >= pred.IntValue;
			case DataQueryOperator::Range: return cellValue >= pred.IntValue && cellValue <= pred.IntValueHigh;
			default: throw std::logic_error("Unsupported operator for integer type");
			}
		}

	case DataLensValueType::Float:
	case DataLensValueType::Double:
		{
			double cellValue = 0.0;
			std::memcpy(&cellValue, buffer, sizeof(cellValue));

			switch (pred.Op)
			{
			case DataQueryOperator::Equals: return cellValue == pred.DoubleValue;
			case DataQueryOperator::NotEquals: return cellValue != pred.DoubleValue;
			case DataQueryOperator::Less: return cellValue < pred.DoubleValue;
			case DataQueryOperator::LessOrEqual: return cellValue <= pred.DoubleValue;
			case DataQueryOperator::Greater: return cellValue > pred.DoubleValue;
			case DataQueryOperator::GreaterOrEqual: return cellValue >= pred.DoubleValue;
			case DataQueryOperator::Range: return cellValue >= pred.DoubleValue && cellValue <= pred.DoubleValueHigh;
			default: throw std::logic_error("Unsupported operator for floating type");
			}
		}

	case DataLensValueType::GUID:
		{
			if (pred.Op != DataQueryOperator::Equals && pred.Op != DataQueryOperator::NotEquals)
			{
				throw std::logic_error("GUID supports only Equals/NotEquals");
			}

			bool equal = std::memcmp(buffer, &pred.UIntValue, 16) == 0;
			return pred.Op == DataQueryOperator::Equals ? equal : !equal;
		}

	default:
		throw std::logic_error("Unsupported DataType in predicate evaluation");
	}
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
