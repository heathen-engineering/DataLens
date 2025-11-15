/******************************************************************************
 * DataLensModule.h
 *
 * © 2025 Heathen Engineering. All rights reserved.
 *
 * Author: James McGhee
 * Date:   2025-11-04 - 2025-11-15
 ******************************************************************************/

#pragma once

#include "Modules/ModuleManager.h"

class FDataLensModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
