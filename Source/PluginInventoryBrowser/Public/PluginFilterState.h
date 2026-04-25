// Copyright StateOfRuin, 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "PluginInventoryEntry.h"

/**
 * ESortField
 * Columns available for sorting the inventory grid.
 */
enum class EPluginSortField : uint8
{
	Name,
	Category,
	Source,
	ModuleCount,
	DependencyCount,
	EnabledState,
};

/**
 * EPluginEnabledFilter
 * Which enable-state bucket is visible.
 */
enum class EPluginEnabledFilter : uint8
{
	All,
	EnabledOnly,
	DisabledOnly,
};

/**
 * FPluginFilterState
 *
 * Shared, composable filter/sort configuration owned by SPluginInventoryBrowser.
 * Each predicate is independent; all active predicates must pass for an entry to be visible.
 * Observers call SetNeedsRebuild() on the browser whenever a facet changes.
 */
struct FPluginFilterState
{
	// ---- Text search --------------------------------------------------------
	/** Lower-cased raw text entered by the user. Empty = no text filter. */
	FString SearchText;

	// ---- Enable-state bucket ------------------------------------------------
	EPluginEnabledFilter EnabledFilter = EPluginEnabledFilter::All;

	// ---- Source / type facets (empty set = any) -----------------------------
	/** Active plugin types. If empty, all types pass. */
	TSet<EPluginType> AllowedTypes;

	/** Active loaded-from values. If empty, all pass. */
	TSet<EPluginLoadedFrom> AllowedLoadedFrom;

	// ---- Category filter (empty = any) --------------------------------------
	TSet<FString> AllowedCategories;

	// ---- Technical facets ---------------------------------------------------
	/** If true, hidden plugins are shown; otherwise they are filtered out. */
	bool bShowHidden            = false;

	/** If true, only plugins that can contain content are shown. */
	bool bRequireContent        = false;

	/** If true, only plugins that can contain Verse code are shown. */
	bool bRequireVerse          = false;

	/** If true, only installed (bInstalled) plugins are shown. */
	bool bInstalledOnly         = false;

	/** Min/max module counts. -1 = no bound. */
	int32 MinModules            = -1;
	int32 MaxModules            = -1;

	/** Min/max dependency counts. -1 = no bound. */
	int32 MinDependencies       = -1;
	int32 MaxDependencies       = -1;

	/** Platform filter: if non-empty, plugin must list at least one of these in SupportedTargetPlatforms. */
	TSet<FString> RequiredPlatforms;

	// ---- Sort ---------------------------------------------------------------
	EPluginSortField SortField      = EPluginSortField::Name;
	bool             bSortAscending = true;

	// -------------------------------------------------------------------------
	// Reset helpers
	// -------------------------------------------------------------------------
	void ResetTextSearch()    { SearchText.Empty(); }
	void ResetAllFilters()
	{
		SearchText.Empty();
		EnabledFilter     = EPluginEnabledFilter::All;
		AllowedTypes.Empty();
		AllowedLoadedFrom.Empty();
		AllowedCategories.Empty();
		bShowHidden       = false;
		bRequireContent   = false;
		bRequireVerse     = false;
		bInstalledOnly    = false;
		MinModules        = -1;
		MaxModules        = -1;
		MinDependencies   = -1;
		MaxDependencies   = -1;
		RequiredPlatforms.Empty();
	}

	bool HasActiveFilters() const
	{
		return !SearchText.IsEmpty()
			|| EnabledFilter != EPluginEnabledFilter::All
			|| AllowedTypes.Num() > 0
			|| AllowedLoadedFrom.Num() > 0
			|| AllowedCategories.Num() > 0
			|| bShowHidden
			|| bRequireContent
			|| bRequireVerse
			|| bInstalledOnly
			|| MinModules != -1
			|| MaxModules != -1
			|| MinDependencies != -1
			|| MaxDependencies != -1
			|| RequiredPlatforms.Num() > 0;
	}

	// -------------------------------------------------------------------------
	// PassesFilter — returns true when all active predicates are satisfied
	// -------------------------------------------------------------------------
	bool PassesFilter(const FPluginInventoryEntry& Entry) const
	{
		// Hidden plugins
		if (!bShowHidden && Entry.bIsHidden)
		{
			return false;
		}

		// Text search (case-insensitive via pre-lowercased SearchText + SearchTokens)
		if (!SearchText.IsEmpty() && !Entry.SearchText.Contains(SearchText))
		{
			return false;
		}

		// Enabled state
		if (EnabledFilter == EPluginEnabledFilter::EnabledOnly  && !Entry.bIsEnabled) { return false; }
		if (EnabledFilter == EPluginEnabledFilter::DisabledOnly &&  Entry.bIsEnabled) { return false; }

		// Plugin type
		if (AllowedTypes.Num() > 0 && !AllowedTypes.Contains(Entry.PluginType))
		{
			return false;
		}

		// Loaded-from
		if (AllowedLoadedFrom.Num() > 0 && !AllowedLoadedFrom.Contains(Entry.LoadedFrom))
		{
			return false;
		}

		// Category
		if (AllowedCategories.Num() > 0 && !AllowedCategories.Contains(Entry.Category))
		{
			return false;
		}

		// Technical facets
		if (bRequireContent  && !Entry.bCanContainContent) { return false; }
		if (bRequireVerse    && !Entry.bCanContainVerse)   { return false; }
		if (bInstalledOnly   && !Entry.bIsInstalled)       { return false; }

		// Module count range
		if (MinModules != -1 && Entry.ModuleCount < MinModules) { return false; }
		if (MaxModules != -1 && Entry.ModuleCount > MaxModules) { return false; }

		// Dependency count range
		if (MinDependencies != -1 && Entry.DependencyCount < MinDependencies) { return false; }
		if (MaxDependencies != -1 && Entry.DependencyCount > MaxDependencies) { return false; }

		// Platform filter: at least one required platform must be in the plugin's supported list
		if (RequiredPlatforms.Num() > 0)
		{
			bool bFound = false;
			for (const FString& Plat : Entry.SupportedTargetPlatforms)
			{
				if (RequiredPlatforms.Contains(Plat))
				{
					bFound = true;
					break;
				}
			}
			// If the plugin has no platform restriction (empty list) we treat it as "all platforms"
			if (!bFound && Entry.SupportedTargetPlatforms.Num() > 0)
			{
				return false;
			}
		}

		return true;
	}

	// -------------------------------------------------------------------------
	// SortLess — comparator for TArray::Sort
	// -------------------------------------------------------------------------
	bool SortLess(const FPluginInventoryEntryRef& A, const FPluginInventoryEntryRef& B) const
	{
		auto Compare = [&]() -> int32
		{
			switch (SortField)
			{
			case EPluginSortField::Category:      return A->Category.Compare(B->Category);
			case EPluginSortField::Source:        return static_cast<int32>(A->PluginType) - static_cast<int32>(B->PluginType);
			case EPluginSortField::ModuleCount:   return A->ModuleCount - B->ModuleCount;
			case EPluginSortField::DependencyCount: return A->DependencyCount - B->DependencyCount;
			case EPluginSortField::EnabledState:  return (A->bIsEnabled ? 0 : 1) - (B->bIsEnabled ? 0 : 1);
			default: /* Name */                   return A->FriendlyName.Compare(B->FriendlyName);
			}
		};
		int32 Cmp = Compare();
		if (Cmp == 0) { Cmp = A->FriendlyName.Compare(B->FriendlyName); } // secondary: always name
		return bSortAscending ? (Cmp < 0) : (Cmp > 0);
	}
};
