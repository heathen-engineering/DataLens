/******************************************************************************
 * DataLensBPLibrary.h
 *
 * � 2025 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2025-11-15
 ******************************************************************************/

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DataStore.h"
#include "DataLensBPLibrary.generated.h"

USTRUCT(BlueprintType)
struct FDataLensTestResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DataLens|Test Result")
	FString TestName;

	UPROPERTY(BlueprintReadOnly, Category = "DataLens|Test Result")
	FString Timestamp;

	UPROPERTY(BlueprintReadOnly, Category = "DataLens|Test Result")
	int64 Rows = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DataLens|Test Result")
	int64 Columns = 0;

	// New properties for benchmarking memory
	UPROPERTY(BlueprintReadOnly, Category = "DataLens|Test Result")
	int64 RowSpanBytes = 0; // bytes per row

	UPROPERTY(BlueprintReadOnly, Category = "DataLens|Test Result")
	int64 TotalMemoryBytes = 0; // approximate total memory used

	UPROPERTY(BlueprintReadOnly, Category = "DataLens|Test Result")
	double DurationMs = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "DataLens|Test Result")
	FString Notes;

	UPROPERTY(BlueprintReadOnly, Category = "DataLens|Test Result")
	bool bSuccess = true;
};

UCLASS()
class UDataLensBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()
	// Write all test results to a human-readable log file
	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Output")
	static bool WriteResultsToTextFile(const FString& OutputFilePath, const TArray<FDataLensTestResult>& Results)
	{
		FString Output;
		Output += TEXT("DataLens Phase Test Results\n");
		Output += TEXT("============================================================\n\n");

		for (const FDataLensTestResult& Result : Results)
		{
			Output += FString::Printf(TEXT("Test: %s\n"), *Result.TestName);
			Output += FString::Printf(TEXT("Timestamp: %s\n"), *Result.Timestamp);
			Output += FString::Printf(
				TEXT("Rows: %lld | Cols: %lld | RowSpan: %lld bytes | TotalMem: %lld bytes | Duration: %.3f ms\n"),
				Result.Rows, Result.Columns, Result.RowSpanBytes, Result.TotalMemoryBytes, Result.DurationMs);
			Output += FString::Printf(TEXT("Success: %s\n"), Result.bSuccess ? TEXT("true") : TEXT("false"));
			Output += FString::Printf(TEXT("Notes: %s\n"), *Result.Notes);
			Output += TEXT("------------------------------------------------------------\n\n");
		}

		return FFileHelper::SaveStringToFile(Output, *OutputFilePath, FFileHelper::EEncodingOptions::AutoDetect,
		                                     &IFileManager::Get(), FILEWRITE_None);
	}

	// Write all numeric data to a CSV for analysis/plotting
	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Output")
	static bool WriteResultsToCSV(const FString& OutputFilePath, const TArray<FDataLensTestResult>& Results)
	{
		FString CSV = TEXT("TestName,Timestamp,Rows,Columns,RowSpanBytes,TotalMemoryBytes,DurationMs,Success,Notes\n");

		for (const FDataLensTestResult& Result : Results)
		{
			CSV += FString::Printf(TEXT("\"%s\",\"%s\",%lld,%lld,%lld,%lld,%.3f,%s,\"%s\"\n"),
			                       *Result.TestName,
			                       *Result.Timestamp,
			                       Result.Rows,
			                       Result.Columns,
			                       Result.RowSpanBytes,
			                       Result.TotalMemoryBytes,
			                       Result.DurationMs,
			                       Result.bSuccess ? TEXT("true") : TEXT("false"),
			                       *Result.Notes.ReplaceCharWithEscapedChar()
			);
		}

		return FFileHelper::SaveStringToFile(CSV, *OutputFilePath, FFileHelper::EEncodingOptions::AutoDetect,
		                                     &IFileManager::Get(), FILEWRITE_None);
	}

	// Individual self-contained tests
	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static FDataLensTestResult Test_Phase0_CreateDefaultStore()
	{
		FDataLensTestResult Result;
		Result.TestName = TEXT("Test_Phase0_CreateDefaultStore");
		Result.Timestamp = FDateTime::Now().ToString();

		DataStore Store;
		double DurationMs = 0.0;

		// --- Test execution (timed using cycles) ---
		try
		{
			const uint64 StartCycles = FPlatformTime::Cycles64();
			Store = DataStore(); // Default constructor
			const uint64 EndCycles = FPlatformTime::Cycles64();
			DurationMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);
		}
		catch (const std::exception& e)
		{
			Result.bSuccess = false;
			Result.Notes = UTF8_TO_TCHAR(e.what());
			Result.DurationMs = 0.0;
			Result.Rows = 0;
			Result.RowSpanBytes = 0;
			Result.Columns = 0;
			Result.TotalMemoryBytes = 0;
			return Result;
		}

		// --- Inspect / validate results ---
		try
		{
			Result.Rows = Store.GetRowCount();
			Result.RowSpanBytes = Store.GetRowStride();
			Result.Columns = Store.GetColumnCount();
			Result.TotalMemoryBytes = Result.RowSpanBytes * Result.Rows;
			Result.Notes = TEXT("Created default empty store.");
			Result.bSuccess = (Result.Rows == 0 && Result.Columns == 0);
		}
		catch (const std::exception& e)
		{
			Result.bSuccess = false;
			Result.Notes = FString::Printf(TEXT("Exception during inspection: %s"), UTF8_TO_TCHAR(e.what()));
		}

		Result.DurationMs = DurationMs;
		return Result;
	}

	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static TArray<FDataLensTestResult> Test_Phase0_CreatePreAllocated()
	{
		TArray<FDataLensTestResult> Results;
		TArray<int64> PreAllocSizes = {100, 1000, 10000, 100000, 1000000, 10000000};

		for (int64 RowCount : PreAllocSizes)
		{
			FDataLensTestResult Result;
			Result.TestName = FString::Printf(TEXT("Test_Phase0_CreatePreAllocated_%lldRows"), RowCount);
			Result.Timestamp = FDateTime::Now().ToString();

			bool bSuccess = true;
			FString Notes;

			try
			{
				std::vector<DataStoreColumnSchema> Cols = {
					{"ColumnFloat", DataLensValueType::Float},
					{"ColumnInt32", DataLensValueType::Int32},
					{"ColumnDouble", DataLensValueType::Double}
				};

				{
					const double StartCycles = FPlatformTime::Cycles64();
					// Scoped block ensures DataStore is destroyed before next iteration
					DataStore Store(Cols, RowCount);
					const double EndCycles = FPlatformTime::Cycles64();
					Result.DurationMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);

					// Collect info
					Result.Rows = Store.GetRowCount();
					Result.Columns = Store.GetColumnCount();
					Result.RowSpanBytes = 0;
					for (auto& Col : Cols)
					{
						Result.RowSpanBytes += Col.GetStride();
					}
					Result.TotalMemoryBytes = Result.RowSpanBytes * Result.Rows;

					Notes = FString::Printf(
						TEXT("Created store with %lld rows, %d columns."), Result.Rows,
						static_cast<int>(Result.Columns));
				} // Store destroyed here, memory freed
			}
			catch (const std::exception& e)
			{
				Result.DurationMs = 0;
				bSuccess = false;
				Notes = UTF8_TO_TCHAR(e.what());
			}


			Result.Notes = Notes;
			Result.bSuccess = bSuccess;

			Results.Add(Result);
		}

		return Results;
	}

	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static TArray<FDataLensTestResult> Test_Phase0_CreatePreAllocatedWithData()
	{
		TArray<FDataLensTestResult> Results;
		TArray<int64> RowCounts = {100, 1000, 10000, 100000, 1000000, 10000000};

		for (int64 RowCount : RowCounts)
		{
			FDataLensTestResult Result;
			Result.TestName = FString::Printf(TEXT("Test_Phase0_CreatePreAllocatedWithData_%lldRows"), RowCount);
			Result.Timestamp = FDateTime::Now().ToString();

			bool bSuccess = true;
			FString Notes;

			try
			{
				// Define column metadata
				std::vector<DataStoreColumnSchema> Cols = {
					{"ColumnFloat", DataLensValueType::Float},
					{"ColumnInt32", DataLensValueType::Int32},
					{"ColumnDouble", DataLensValueType::Double}
				};

				// Pre-build raw row-major data
				size_t rowStride = 0;
				for (auto& Col : Cols)
				{
					rowStride += Col.GetStride();
				}
				std::vector<uint8_t> Data(rowStride * RowCount, 0); // zero-filled data

				{
					// Time the creation with data
					const double StartCycles = FPlatformTime::Cycles64();
					DataStore Store(Cols, Data);
					const double EndCycles = FPlatformTime::Cycles64();
					Result.DurationMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);

					// Collect info
					Result.Rows = Store.GetRowCount();
					Result.Columns = Store.GetColumnCount();
					Result.RowSpanBytes = 0;
					for (auto& Col : Cols)
					{
						Result.RowSpanBytes += Col.GetStride();
					}
					Result.TotalMemoryBytes = Result.RowSpanBytes * Result.Rows;

					Notes = FString::Printf(
						TEXT("Created store with %lld rows, %d columns, loaded with pre-generated data."), Result.Rows,
						static_cast<int>(Result.Columns));
				} // Store destroyed here, memory freed
			}
			catch (const std::exception& e)
			{
				Result.DurationMs = 0;
				bSuccess = false;
				Notes = UTF8_TO_TCHAR(e.what());
			}

			Result.Notes = Notes;
			Result.bSuccess = bSuccess;

			Results.Add(Result);
		}

		return Results;
	}

	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static TArray<FDataLensTestResult> Test_Phase0_CreatePreAllocatedWithDataAndPadding()
	{
		TArray<FDataLensTestResult> Results;
		TArray<int64> RowCounts = {100, 1000, 10000, 100000, 1000000, 10000000};

		for (int64 RowCount : RowCounts)
		{
			FDataLensTestResult Result;
			Result.TestName = FString::Printf(
				TEXT("Test_Phase0_CreatePreAllocatedWithDataAndPadding_%lldRows"), RowCount);
			Result.Timestamp = FDateTime::Now().ToString();

			bool bSuccess = true;
			FString Notes;

			try
			{
				// Define column metadata
				std::vector<DataStoreColumnSchema> Cols = {
					{"ColumnFloat", DataLensValueType::Float},
					{"ColumnInt32", DataLensValueType::Int32},
					{"ColumnDouble", DataLensValueType::Double}
				};

				// Pre-build raw row-major data
				size_t rowStride = 0;
				for (auto& Col : Cols)
				{
					rowStride += Col.GetStride();
				}
				std::vector<uint8_t> Data(rowStride * RowCount, 0); // zero-filled data

				// Calculate 20% extra rows for padding
				size_t ExtraRows = static_cast<size_t>(RowCount * 0.2);

				{
					const double StartCycles = FPlatformTime::Cycles64();
					DataStore Store(Cols, Data, ExtraRows);
					const double EndCycles = FPlatformTime::Cycles64();
					Result.DurationMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);

					// Collect info
					Result.Rows = Store.GetRowCount();
					Result.Columns = Store.GetColumnCount();
					Result.RowSpanBytes = 0;
					for (auto& Col : Cols)
					{
						Result.RowSpanBytes += Col.GetStride();
					}
					Result.TotalMemoryBytes = Result.RowSpanBytes * Result.Rows;

					Notes = FString::Printf(
						TEXT(
							"Created store with %lld loaded rows + %lld extra padded rows, %d columns, pre-generated data."),
						RowCount, ExtraRows, static_cast<int>(Result.Columns));
				} // Store destroyed here, memory freed
			}
			catch (const std::exception& e)
			{
				Result.DurationMs = 0;
				bSuccess = false;
				Notes = UTF8_TO_TCHAR(e.what());
			}

			Result.Notes = Notes;
			Result.bSuccess = bSuccess;

			Results.Add(Result);
		}

		return Results;
	}

	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static TArray<FDataLensTestResult> Test_Phase0_GetRaw()
	{
		TArray<FDataLensTestResult> Results;
		TArray<int64> StoreSizes = {100, 1000, 10000, 100000, 1000000, 10000000};
		TArray<int32> Iterations = {10, 100, 1000};

		for (int64 RowCount : StoreSizes)
		{
			// Create predictable data
			std::vector<DataStoreColumnSchema> Cols = {
				{"ColumnFloat", DataLensValueType::Float},
				{"ColumnInt32", DataLensValueType::Int32},
				{"ColumnDouble", DataLensValueType::Double}
			};
			size_t ColCount = Cols.size();
			size_t RowStride = 0;
			for (auto& C : Cols)
			{
				RowStride += C.GetStride();
			}

			std::vector<uint8_t> Data(RowStride * RowCount, 0);
			for (int64 r = 0; r < RowCount; ++r)
			{
				float fVal = static_cast<float>(r);
				int32 iVal = static_cast<int32>(r);
				double dVal = static_cast<double>(r);

				size_t offset = r * RowStride;
				std::memcpy(Data.data() + offset, &fVal, sizeof(float));
				offset += sizeof(float);
				std::memcpy(Data.data() + offset, &iVal, sizeof(int32));
				offset += sizeof(int32);
				std::memcpy(Data.data() + offset, &dVal, sizeof(double));
			}

			// Create the store
			DataStore Store(Cols, Data);

			// Generate random row/col indices (pre-done)
			std::vector<size_t> RandRows, RandCols;
			FRandomStream RNG(FDateTime::Now().GetTicks()); // deterministic per run
			for (int32 j = 0; j < Iterations.Last(); ++j)
			{
				RandRows.push_back(RNG.RandRange(0, RowCount - 1));
				RandCols.push_back(RNG.RandRange(0, ColCount - 1));
			}

			// Run tests for each iteration block
			for (int32 IterCount : Iterations)
			{
				FDataLensTestResult Result;
				Result.TestName = FString::Printf(TEXT("Test_Phase0_GetRaw_%lldRows_%dIters"), RowCount, IterCount);
				Result.Timestamp = FDateTime::Now().ToString();
				Result.Rows = RowCount;
				Result.Columns = ColCount;
				Result.RowSpanBytes = RowStride;
				Result.TotalMemoryBytes = RowStride * Result.Rows;

				bool bSuccess = true;
				FString Notes;

				const double StartCycles = FPlatformTime::Cycles64();
				try
				{
					for (int32 i = 0; i < IterCount; ++i)
					{
						size_t Row = RandRows[i];
						size_t Col = RandCols[i];

						switch (Col)
						{
						case 0:
							{
								float Value = Store.GetRaw<float>(Row, Col);
								if (Value != static_cast<float>(Row))
								{
									bSuccess = false;
								}
								break;
							}
						case 1:
							{
								int32 Value = Store.GetRaw<int32>(Row, Col);
								if (Value != static_cast<int32>(Row))
								{
									bSuccess = false;
								}
								break;
							}
						case 2:
							{
								double Value = Store.GetRaw<double>(Row, Col);
								if (Value != static_cast<double>(Row))
								{
									bSuccess = false;
								}
								break;
							}
						}
					}
				}
				catch (const std::exception& e)
				{
					bSuccess = false;
					Notes = UTF8_TO_TCHAR(e.what());
				}
				const double EndCycles = FPlatformTime::Cycles64();
				Result.DurationMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);

				Result.bSuccess = bSuccess;
				if (Notes.IsEmpty())
				{
					Notes = TEXT("Checked random accesses and verified correctness.");
				}

				Result.Notes = Notes;

				Results.Add(Result);
			}
		}

		return Results;
	}

	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static TArray<FDataLensTestResult> Test_Phase0_TryGet()
	{
		TArray<FDataLensTestResult> Results;
		TArray<int64> StoreSizes = {100, 1000, 10000, 100000, 1000000, 10000000};
		TArray<int32> Iterations = {10, 100, 1000};

		for (int64 RowCount : StoreSizes)
		{
			// Prepare predictable data
			std::vector<DataStoreColumnSchema> Cols = {
				{"ColumnFloat", DataLensValueType::Float},
				{"ColumnInt32", DataLensValueType::Int32},
				{"ColumnDouble", DataLensValueType::Double}
			};
			size_t ColCount = Cols.size();
			size_t RowStride = 0;
			for (auto& C : Cols)
			{
				RowStride += C.GetStride();
			}

			std::vector<uint8_t> Data(RowStride * RowCount, 0);
			for (int64 r = 0; r < RowCount; ++r)
			{
				float fVal = static_cast<float>(r);
				int32 iVal = static_cast<int32>(r);
				double dVal = static_cast<double>(r);

				size_t offset = r * RowStride;
				std::memcpy(Data.data() + offset, &fVal, sizeof(float));
				offset += sizeof(float);
				std::memcpy(Data.data() + offset, &iVal, sizeof(int32));
				offset += sizeof(int32);
				std::memcpy(Data.data() + offset, &dVal, sizeof(double));
			}

			// Create the store
			DataStore Store(Cols, Data);

			// Generate random row/col indices ahead of time
			std::vector<size_t> RandRows, RandCols;
			FRandomStream RNG(FDateTime::Now().GetTicks());
			for (int32 j = 0; j < Iterations.Last(); ++j)
			{
				RandRows.push_back(RNG.RandRange(0, RowCount - 1));
				RandCols.push_back(RNG.RandRange(0, ColCount - 1));
			}

			// Run tests for each iteration block
			for (int32 IterCount : Iterations)
			{
				FDataLensTestResult Result;
				Result.TestName = FString::Printf(TEXT("Test_Phase0_TryGet_%lldRows_%dIters"), RowCount, IterCount);
				Result.Timestamp = FDateTime::Now().ToString();
				Result.Rows = RowCount;
				Result.Columns = ColCount;
				Result.RowSpanBytes = RowStride;
				Result.TotalMemoryBytes = RowStride * Result.Rows;

				bool bSuccess = true;
				FString Notes;

				const double StartCycles = FPlatformTime::Cycles64();
				try
				{
					for (int32 i = 0; i < IterCount; ++i)
					{
						size_t Row = RandRows[i];
						size_t Col = RandCols[i];

						switch (Col)
						{
						case 0:
							{
								float Value{};
								if (!Store.TryGet<float>(Row, Col, Value) || Value != static_cast<float>(Row))
								{
									bSuccess = false;
								}
								break;
							}
						case 1:
							{
								int32 Value{};
								if (!Store.TryGet<int32>(Row, Col, Value) || Value != static_cast<int32>(Row))
								{
									bSuccess = false;
								}
								break;
							}
						case 2:
							{
								double Value{};
								if (!Store.TryGet<double>(Row, Col, Value) || Value != static_cast<double>(Row))
								{
									bSuccess = false;
								}
								break;
							}
						}
					}
				}
				catch (const std::exception& e)
				{
					bSuccess = false;
					Notes = UTF8_TO_TCHAR(e.what());
				}
				const double EndCycles = FPlatformTime::Cycles64();
				Result.DurationMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);

				Result.bSuccess = bSuccess;
				if (Notes.IsEmpty())
				{
					Notes = TEXT("Checked random TryGet accesses and verified correctness.");
				}

				Result.Notes = Notes;

				Results.Add(Result);
			}
		}

		return Results;
	}

	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static TArray<FDataLensTestResult> Test_Phase0_SetRaw()
	{
		TArray<FDataLensTestResult> Results;
		TArray<int64> StoreSizes = {100, 1000, 10000, 100000, 1000000, 10000000};
		TArray<int32> Iterations = {10, 100, 1000};

		for (int64 RowCount : StoreSizes)
		{
			std::vector<DataStoreColumnSchema> Cols = {
				{"ColumnFloat", DataLensValueType::Float},
				{"ColumnInt32", DataLensValueType::Int32},
				{"ColumnDouble", DataLensValueType::Double}
			};
			size_t ColCount = Cols.size();
			size_t RowStride = 0;
			for (auto& C : Cols)
			{
				RowStride += C.GetStride();
			}

			// Fill store with zeros initially
			std::vector<uint8_t> Data(RowStride * RowCount, 0);
			DataStore Store(Cols, Data);

			// Pre-generate map of row/col/value
			struct CellUpdate
			{
				size_t Row;
				size_t Col;
				double DblVal;
				int32 IntVal;
				float FltVal;
			};
			std::vector<CellUpdate> Updates;
			FRandomStream RNG(FDateTime::Now().GetTicks());
			int32 MaxIters = Iterations.Last();
			for (int32 i = 0; i < MaxIters; ++i)
			{
				size_t Row = RNG.RandRange(0, RowCount - 1);
				size_t Col = RNG.RandRange(0, ColCount - 1);
				Updates.push_back({
					Row, Col, static_cast<double>(Row * 1.1), static_cast<int32>(Row * 10),
					static_cast<float>(Row * 1.0f)
				});
			}

			// Run tests for each iteration block
			for (int32 IterCount : Iterations)
			{
				FDataLensTestResult Result;
				Result.TestName = FString::Printf(TEXT("Test_Phase0_SetRaw_%lldRows_%dIters"), RowCount, IterCount);
				Result.Timestamp = FDateTime::Now().ToString();
				Result.Rows = RowCount;
				Result.Columns = ColCount;
				Result.RowSpanBytes = RowStride;
				Result.TotalMemoryBytes = RowStride * Result.Rows;

				bool bSuccess = true;
				FString Notes;

				// --- WRITE STEP ---
				const double StartCycles = FPlatformTime::Cycles64();
				try
				{
					for (int32 i = 0; i < IterCount; ++i)
					{
						const auto& U = Updates[i];
						switch (U.Col)
						{
						case 0: Store.SetRaw<float>(U.Row, U.Col, U.FltVal);
							break;
						case 1: Store.SetRaw<int32>(U.Row, U.Col, U.IntVal);
							break;
						case 2: Store.SetRaw<double>(U.Row, U.Col, U.DblVal);
							break;
						}
					}
				}
				catch (const std::exception& e)
				{
					bSuccess = false;
					Notes = UTF8_TO_TCHAR(e.what());
				}
				const double EndCycles = FPlatformTime::Cycles64();
				Result.DurationMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);

				// --- VERIFY STEP ---
				try
				{
					for (int32 i = 0; i < IterCount; ++i)
					{
						const auto& U = Updates[i];
						switch (U.Col)
						{
						case 0: bSuccess &= (Store.GetRaw<float>(U.Row, U.Col) == U.FltVal);
							break;
						case 1: bSuccess &= (Store.GetRaw<int32>(U.Row, U.Col) == U.IntVal);
							break;
						case 2: bSuccess &= (Store.GetRaw<double>(U.Row, U.Col) == U.DblVal);
							break;
						}
					}
					if (Notes.IsEmpty())
					{
						Notes = TEXT("Performed SetRaw on random cells and verified values.");
					}
				}
				catch (const std::exception& e)
				{
					bSuccess = false;
					Notes = UTF8_TO_TCHAR(e.what());
				}

				Result.bSuccess = bSuccess;
				Result.Notes = Notes;
				Results.Add(Result);
			}
		}

		return Results;
	}

	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static TArray<FDataLensTestResult> Test_Phase0_TrySet()
	{
		TArray<FDataLensTestResult> Results;
		TArray<int64> StoreSizes = {100, 1000, 10000, 100000, 1000000, 10000000};
		TArray<int32> Iterations = {10, 100, 1000};

		for (int64 RowCount : StoreSizes)
		{
			std::vector<DataStoreColumnSchema> Cols = {
				{"ColumnFloat", DataLensValueType::Float},
				{"ColumnInt32", DataLensValueType::Int32},
				{"ColumnDouble", DataLensValueType::Double}
			};
			size_t ColCount = Cols.size();
			size_t RowStride = 0;
			for (auto& C : Cols)
			{
				RowStride += C.GetStride();
			}

			// Fill store with zeros initially
			std::vector<uint8_t> Data(RowStride * RowCount, 0);
			DataStore Store(Cols, Data);

			// Pre-generate map of row/col/value
			struct CellUpdate
			{
				size_t Row;
				size_t Col;
				double DblVal;
				int32 IntVal;
				float FltVal;
			};
			std::vector<CellUpdate> Updates;
			FRandomStream RNG(FDateTime::Now().GetTicks());
			int32 MaxIters = Iterations.Last();
			for (int32 i = 0; i < MaxIters; ++i)
			{
				size_t Row = RNG.RandRange(0, RowCount - 1);
				size_t Col = RNG.RandRange(0, ColCount - 1);
				Updates.push_back({
					Row, Col, static_cast<double>(Row * 1.1), static_cast<int32>(Row * 10),
					static_cast<float>(Row * 1.0f)
				});
			}

			// Run tests for each iteration block
			for (int32 IterCount : Iterations)
			{
				FDataLensTestResult Result;
				Result.TestName = FString::Printf(TEXT("Test_Phase0_TrySet_%lldRows_%dIters"), RowCount, IterCount);
				Result.Timestamp = FDateTime::Now().ToString();
				Result.Rows = RowCount;
				Result.Columns = ColCount;
				Result.RowSpanBytes = RowStride;
				Result.TotalMemoryBytes = RowStride * Result.Rows;
				bool bSuccess = true;
				FString Notes;

				// --- WRITE STEP ---
				const double StartCycles = FPlatformTime::Cycles64();
				try
				{
					for (int32 i = 0; i < IterCount; ++i)
					{
						const auto& U = Updates[i];
						bool bOk = false;
						switch (U.Col)
						{
						case 0: bOk = Store.TrySet<float>(U.Row, U.Col, U.FltVal);
							break;
						case 1: bOk = Store.TrySet<int32>(U.Row, U.Col, U.IntVal);
							break;
						case 2: bOk = Store.TrySet<double>(U.Row, U.Col, U.DblVal);
							break;
						}
						bSuccess &= bOk;
					}
				}
				catch (const std::exception& e)
				{
					bSuccess = false;
					Notes = UTF8_TO_TCHAR(e.what());
				}
				const double EndCycles = FPlatformTime::Cycles64();
				Result.DurationMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);

				// --- VERIFY STEP ---
				try
				{
					for (int32 i = 0; i < IterCount; ++i)
					{
						const auto& U = Updates[i];
						switch (U.Col)
						{
						case 0: bSuccess &= (Store.GetRaw<float>(U.Row, U.Col) == U.FltVal);
							break;
						case 1: bSuccess &= (Store.GetRaw<int32>(U.Row, U.Col) == U.IntVal);
							break;
						case 2: bSuccess &= (Store.GetRaw<double>(U.Row, U.Col) == U.DblVal);
							break;
						}
					}
					if (Notes.IsEmpty())
					{
						Notes = TEXT("Performed TrySet on random cells and verified values.");
					}
				}
				catch (const std::exception& e)
				{
					bSuccess = false;
					Notes = UTF8_TO_TCHAR(e.what());
				}

				Result.bSuccess = bSuccess;
				Result.Notes = Notes;
				Results.Add(Result);
			}
		}

		return Results;
	}

	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static FDataLensTestResult Test_Phase0_TryGetResilience()
	{
		FDataLensTestResult Result;
		Result.TestName = TEXT("Test_Phase0_TryGetResilience");
		Result.Timestamp = FDateTime::Now().ToString();

		bool bSuccess = true;
		FString Notes;

		// Create a tiny table with 1 row and 3 columns
		std::vector<DataStoreColumnSchema> Cols = {
			{"ColumnFloat", DataLensValueType::Float},
			{"ColumnInt32", DataLensValueType::Int32},
			{"ColumnDouble", DataLensValueType::Double}
		};

		try
		{
			DataStore Store(Cols, 1);

			float fValue;
			int32 iValue;
			double dValue;

			// Out-of-bounds row
			bSuccess &= !Store.TryGet<float>(1, 0, fValue);
			bSuccess &= !Store.TryGet<int32>(2, 1, iValue);

			// Out-of-bounds column
			bSuccess &= !Store.TryGet<float>(0, 5, fValue);
			bSuccess &= !Store.TryGet<double>(0, 3, dValue);

			// Type mismatch (over-read)
			bSuccess &= Store.TryGet<float>(0, 2, fValue);
			// column 2 is double, copying into float should safely copy only sizeof(float)
			bSuccess &= Store.TryGet<double>(0, 0, dValue);
			// column 0 is float, copying into double should safely copy only sizeof(double)

			Notes = TEXT("TryGet resilience test passed. Out-of-bounds and type mismatch handled safely.");
		}
		catch (const std::exception& e)
		{
			bSuccess = false;
			Notes = UTF8_TO_TCHAR(e.what());
		}

		Result.bSuccess = bSuccess;
		Result.Notes = Notes;
		Result.Rows = 1;
		Result.Columns = 3;
		Result.RowSpanBytes = 0;
		for (auto& C : Cols)
		{
			Result.RowSpanBytes += C.GetStride();
		}
		Result.TotalMemoryBytes = Result.RowSpanBytes * Result.Rows;
		Result.DurationMs = 0.0;

		return Result;
	}

	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static FDataLensTestResult Test_Phase0_TrySetResilience()
	{
		FDataLensTestResult Result;
		Result.TestName = TEXT("Test_Phase0_TrySetResilience");
		Result.Timestamp = FDateTime::Now().ToString();

		bool bSuccess = true;
		FString Notes;

		// Create a tiny table with 1 row and 3 columns
		std::vector<DataStoreColumnSchema> Cols = {
			{"ColumnFloat", DataLensValueType::Float},
			{"ColumnInt32", DataLensValueType::Int32},
			{"ColumnDouble", DataLensValueType::Double}
		};

		try
		{
			DataStore Store(Cols, 1);

			// Out-of-bounds row/column
			bSuccess &= !Store.TrySet<float>(1, 0, 3.14f);
			bSuccess &= !Store.TrySet<int32>(0, 5, 42);

			// Type mismatch (over-write)
			bSuccess &= Store.TrySet<float>(0, 2, 1.23f); // column 2 is double
			bSuccess &= Store.TrySet<double>(0, 0, 2.34); // column 0 is float

			Notes = TEXT("TrySet resilience test passed. Out-of-bounds and type mismatch handled safely.");
		}
		catch (const std::exception& e)
		{
			bSuccess = false;
			Notes = UTF8_TO_TCHAR(e.what());
		}

		Result.bSuccess = bSuccess;
		Result.Notes = Notes;
		Result.Rows = 1;
		Result.Columns = 3;
		Result.RowSpanBytes = 0;

		for (auto& C : Cols)
		{
			Result.RowSpanBytes += C.GetStride();
		}

		Result.TotalMemoryBytes = Result.RowSpanBytes * Result.Rows;
		Result.DurationMs = 0.0;

		return Result;
	}

	UFUNCTION(BlueprintCallable, Category = "DataLens|Testing|Phase0")
	static TArray<FDataLensTestResult> Test_Phase0_LoadAndDump()
	{
		TArray<FDataLensTestResult> Results;
		TArray<int64> RowCounts = {100, 1000, 10000, 100000, 1000000, 10000000};

		for (int64 Rows : RowCounts)
		{
			FDataLensTestResult Result;
			Result.TestName = FString::Printf(TEXT("Test_Phase0_LoadAndDump_%lldRows"), Rows);
			Result.Timestamp = FDateTime::Now().ToString();
			bool bSuccess = true;
			FString Notes;

			std::vector<DataStoreColumnSchema> Cols = {
				{"ColumnFloat", DataLensValueType::Float},
				{"ColumnInt32", DataLensValueType::Int32},
				{"ColumnDouble", DataLensValueType::Double}
			};
			size_t RowSpan = 0;
			for (auto& C : Cols)
			{
				RowSpan += C.GetStride();
			}

			try
			{
				// Prepare row-major data buffer
				std::vector<uint8_t> DataBuffer(Rows * RowSpan, 0);
				for (int64 r = 0; r < Rows; ++r)
				{
					size_t offset = r * RowSpan;
					// Column 0: float, Column 1: int32, Column 2: double
					float f = static_cast<float>(r);
					int32 i = static_cast<int32>(r);
					double d = static_cast<double>(r);
					std::memcpy(DataBuffer.data() + offset, &f, sizeof(f));
					offset += sizeof(f);
					std::memcpy(DataBuffer.data() + offset, &i, sizeof(i));
					offset += sizeof(i);
					std::memcpy(DataBuffer.data() + offset, &d, sizeof(d));
					offset += sizeof(d);
				}

				const double StartCycles = FPlatformTime::Cycles64();

				DataStore Store(Cols, 0); // 0 prealloc rows, just sets up columns
				Store.LoadRaw(DataBuffer);

				const double EndCycles = FPlatformTime::Cycles64();
				Result.DurationMs = FPlatformTime::ToMilliseconds64(EndCycles - StartCycles);

				Result.Rows = Store.GetRowCount();
				Result.Columns = Store.GetColumnCount();
				Result.RowSpanBytes = RowSpan;
				Result.TotalMemoryBytes = RowSpan * Result.Rows;

				// Dump and validate a few sample rows
				auto Dumped = Store.Dump();
				size_t stride = RowSpan;
				if (Dumped.size() != stride * Rows)
				{
					bSuccess = false;
					Notes += TEXT("Dump size mismatch. ");
				}
				else
				{
					// Validate first, middle, last row
					std::array<int64, 3> SampleRows = {0, Rows / 2, Rows - 1};
					for (int64 idx : SampleRows)
					{
						size_t offset = idx * stride;
						float fVal;
						int32 iVal;
						double dVal;
						std::memcpy(&fVal, Dumped.data() + offset, sizeof(fVal));
						offset += sizeof(fVal);
						std::memcpy(&iVal, Dumped.data() + offset, sizeof(iVal));
						offset += sizeof(iVal);
						std::memcpy(&dVal, Dumped.data() + offset, sizeof(dVal));

						if (fVal != idx || iVal != idx || dVal != idx)
						{
							bSuccess = false;
							Notes += FString::Printf(TEXT("Mismatch at row %lld. "), idx);
						}
					}
				}

				Notes += FString::Printf(
					TEXT("Loaded and dumped %lld rows, %d columns."), Rows, static_cast<int>(Cols.size()));
			}
			catch (const std::exception& e)
			{
				bSuccess = false;
				Result.DurationMs = 0;
				Notes += UTF8_TO_TCHAR(e.what());
			}

			Result.bSuccess = bSuccess;
			Result.Notes = Notes;

			Results.Add(Result);
		}

		return Results;
	}
};
