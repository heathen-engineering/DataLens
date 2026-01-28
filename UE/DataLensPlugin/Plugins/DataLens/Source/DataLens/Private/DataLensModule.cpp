/******************************************************************************
 * DataLensModule.cpp
 *
 * � 2025 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2025-11-15
 ******************************************************************************/

#include "DataLensModule.h"

#define LOCTEXT_NAMESPACE "FDataLensModule"

void FDataLensModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FDataLensModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDataLensModule, DataLens)
