// Copyright StateOfRuin, 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"

class FPluginInventoryBrowserModule : public IModuleInterface
{
public:
	static const FName InventoryTabName;

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FPluginInventoryBrowserModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FPluginInventoryBrowserModule>("PluginInventoryBrowser");
	}

private:
	TSharedRef<SDockTab> SpawnInventoryTab(const FSpawnTabArgs& Args);
	void RegisterMenus();
};
