/******************************************************************************
 * DataLens.h
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * High-performance, column-oriented in-memory data table with dynamic
 * per-column stride. Supports both raw and safe access to table cells.
 *
 * Author: James McGhee
 * Date:   2025-11-14
 ******************************************************************************/

#pragma once

#include <vector>
#include "DataStore.h"

class DataLens
{
public:
	explicit DataLens(const Schema& schema)
		: mSchema(schema)
	{ }

	std::vector<uint8_t> Serialize() const;

	void Deserialize(const std::vector<uint8_t>& data);
	// GetView
	// HasView
	// RemoveView
	// Query
	// Update
	// CommitAll
	// Commit
private:
	Schema mSchema; 
	std::vector<DataStore> mStores;
	// storage for our pending update commands
	// storage for our registered views
	// CommitViews
	// RefreshViews

	static void WriteString(std::vector<uint8_t>& out, const std::string& str);
	static std::string ReadString(const uint8_t* data, size_t& offset, size_t dataSize);
};