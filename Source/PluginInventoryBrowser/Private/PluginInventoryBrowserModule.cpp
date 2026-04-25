// Copyright StateOfRuin, 2026. All Rights Reserved.

#include "PluginInventoryBrowserModule.h"
#include "SPluginInventoryBrowser.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FPluginInventoryBrowserModule"

const FName FPluginInventoryBrowserModule::InventoryTabName = TEXT("PluginInventoryBrowser");

void FPluginInventoryBrowserModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		InventoryTabName,
		FOnSpawnTab::CreateRaw(this, &FPluginInventoryBrowserModule::SpawnInventoryTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Plugin Inventory"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Browse all discovered engine and project plugins in a live tile grid."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Plugins"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPluginInventoryBrowserModule::RegisterMenus));
}

void FPluginInventoryBrowserModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(InventoryTabName);
}

TSharedRef<SDockTab> FPluginInventoryBrowserModule::SpawnInventoryTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SPluginInventoryBrowser)
		];
}

void FPluginInventoryBrowserModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu.Window");
	FToolMenuSection& Section = Menu->FindOrAddSection("WindowGlobalTabSpawners");
	Section.AddMenuEntry(
		"OpenPluginInventoryBrowser",
		LOCTEXT("MenuEntryLabel", "Plugin Inventory Browser"),
		LOCTEXT("MenuEntryTooltip", "Open the Plugin Inventory Browser to explore all discovered plugins."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Plugins"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FPluginInventoryBrowserModule::InventoryTabName);
		})));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPluginInventoryBrowserModule, PluginInventoryBrowser)
