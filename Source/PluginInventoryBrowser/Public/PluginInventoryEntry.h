// Copyright StateOfRuin, 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"

/**
 * FPluginInventoryEntry
 *
 * Normalized, flat view-model for a single discovered plugin.
 * Built once from IPlugin and cached so filter/sort passes never hit the manager again.
 */
struct FPluginInventoryEntry
{
	// --- Identity ---
	FString Name;
	FString FriendlyName;
	FString Description;
	FString Category;
	FString CreatedBy;
	FString CreatedByURL;
	FString EngineVersion;
	FString VersionName;

	// --- URLs ---
	FString MarketplaceURL;
	FString DocsURL;
	FString SupportURL;

	// --- Location ---
	FString BaseDir;
	FString DescriptorFileName;
	EPluginType PluginType       = EPluginType::Engine;
	EPluginLoadedFrom LoadedFrom = EPluginLoadedFrom::Engine;

	// --- State ---
	bool bIsEnabled            = false;
	bool bIsEnabledByDefault   = false;
	bool bIsMounted            = false;
	bool bIsHidden             = false;
	bool bIsInstalled          = false; // Descriptor.bInstalled

	// --- Capabilities ---
	bool bCanContainContent    = false;
	bool bCanContainVerse      = false;

	// --- Modules / Deps (counts only; full lists are in the descriptor) ---
	int32 ModuleCount          = 0;
	int32 DependencyCount      = 0;

	// --- Platforms ---
	TArray<FString> SupportedTargetPlatforms;

	// --- Search tokens (pre-flattened for fast substring search) ---
	FString SearchText; // name + friendlyname + description + category + createdby + basedir

	// --------------------------------------------------------------------------
	// Factory: build from a live IPlugin reference
	// --------------------------------------------------------------------------
	static TSharedRef<FPluginInventoryEntry> FromPlugin(const TSharedRef<IPlugin>& Plugin)
	{
		TSharedRef<FPluginInventoryEntry> Entry = MakeShared<FPluginInventoryEntry>();

		const FPluginDescriptor& Desc = Plugin->GetDescriptor();

		Entry->Name                    = Plugin->GetName();
		Entry->FriendlyName            = Plugin->GetFriendlyName();
		Entry->Description             = Desc.Description;
		Entry->Category                = Desc.Category.IsEmpty() ? TEXT("Other") : Desc.Category;
		Entry->CreatedBy               = Desc.CreatedBy;
		Entry->CreatedByURL            = Desc.CreatedByURL;
		Entry->EngineVersion           = Desc.EngineVersion;
		Entry->VersionName             = Desc.VersionName;
		Entry->MarketplaceURL          = Desc.MarketplaceURL;
		Entry->DocsURL                 = Desc.DocsURL;
		Entry->SupportURL              = Desc.SupportURL;
		Entry->BaseDir                 = Plugin->GetBaseDir();
		Entry->DescriptorFileName      = Plugin->GetDescriptorFileName();
		Entry->PluginType              = Plugin->GetType();
		Entry->LoadedFrom              = Plugin->GetLoadedFrom();
		Entry->bIsEnabled              = Plugin->IsEnabled();
		Entry->bIsEnabledByDefault     = Plugin->IsEnabledByDefault(true);
		Entry->bIsMounted              = Plugin->IsMounted();
		Entry->bIsHidden               = Plugin->IsHidden();
		Entry->bIsInstalled            = Desc.bInstalled;
		Entry->bCanContainContent      = Plugin->CanContainContent();
		Entry->bCanContainVerse        = Plugin->CanContainVerse();
		Entry->ModuleCount             = Desc.Modules.Num();
		Entry->DependencyCount         = Desc.Plugins.Num();
		Entry->SupportedTargetPlatforms = Desc.SupportedTargetPlatforms;

		// Build search text (all lowercase for case-insensitive matching)
		Entry->SearchText = (Entry->Name + TEXT(" ") +
			Entry->FriendlyName + TEXT(" ") +
			Entry->Description + TEXT(" ") +
			Entry->Category + TEXT(" ") +
			Entry->CreatedBy + TEXT(" ") +
			Entry->BaseDir).ToLower();

		return Entry;
	}

	// --------------------------------------------------------------------------
	// Human-readable source label
	// --------------------------------------------------------------------------
	FText GetSourceLabel() const
	{
		switch (PluginType)
		{
		case EPluginType::Engine:     return FText::FromString(TEXT("Engine"));
		case EPluginType::Enterprise: return FText::FromString(TEXT("Enterprise"));
		case EPluginType::Project:    return FText::FromString(TEXT("Project"));
		case EPluginType::External:   return FText::FromString(TEXT("External"));
		case EPluginType::Mod:        return FText::FromString(TEXT("Mod"));
		default:                      return FText::FromString(TEXT("Unknown"));
		}
	}

	// --------------------------------------------------------------------------
	// Status badge text and colour hint
	// --------------------------------------------------------------------------
	FText GetStatusLabel() const
	{
		if (bIsEnabled)
		{
			return FText::FromString(TEXT("Enabled"));
		}
		return FText::FromString(TEXT("Disabled"));
	}
};

/** Alias used throughout the plugin */
using FPluginInventoryEntryRef = TSharedRef<FPluginInventoryEntry>;
using FPluginInventoryEntryPtr = TSharedPtr<FPluginInventoryEntry>;
