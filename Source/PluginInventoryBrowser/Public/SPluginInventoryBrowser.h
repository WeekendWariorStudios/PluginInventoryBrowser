// Copyright StateOfRuin, 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"
#include "IDirectoryWatcher.h"
#include "DirectoryWatcherModule.h"
#include "PluginInventoryEntry.h"
#include "PluginFilterState.h"
#include "OllamaPluginSummaryProvider.h"

class SWindow;

/**
 * SPluginInventoryBrowser
 *
 * Main dockable panel widget.
 *
 * Layout:
 *   ┌─ Toolbar ─────────────────────────────────────────────────────────┐
 *   │  [🔍 Search]  [Source▼]  [Type▼]  [Category▼]  [Status▼]  [More▼] [Export▼] [⟳] [✕]  │
 *   ├─ Stats ribbon ─────────────────────────────────────────────────────┤
 *   │  Showing 42 / 805 plugins                                          │
 *   ├─ Tile grid (STileView) ────────────────────────────────────────────┤
 *   │  ┌───────┐ ┌───────┐ ┌───────┐ ...                                │
 *   │  │ Tile  │ │ Tile  │ │ Tile  │                                     │
 *   └──────────────────────────────────────────────────────────────────-─┘
 */
class SPluginInventoryBrowser : public SCompoundWidget
{
public:
	enum class EPluginInventoryExportFormat : uint8
	{
		Json,
		Csv,
	};

	SLATE_BEGIN_ARGS(SPluginInventoryBrowser) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SPluginInventoryBrowser() override;

	/** Called by directory watcher or timer to rebuild the full inventory then refilter. */
	void RebuildInventory();

	/** Re-apply current filters to the existing AllEntries and refresh the tile view. */
	void RebuildFilteredList();

	/** Schedule a deferred inventory rebuild (debounced, 2 s). */
	void ScheduleRebuild();

	/** Schedule an immediate filter refresh on the next tick. */
	void ScheduleFilterRefresh();

private:
	// ---- Data ---------------------------------------------------------------
	/** Full unfiltered inventory built from IPluginManager. */
	TArray<FPluginInventoryEntryRef> AllEntries;

	/** Filtered and sorted subset shown in the tile view. */
	TArray<FPluginInventoryEntryRef> FilteredEntries;

	/** Shared filter / sort state. */
	FPluginFilterState FilterState;

	// ---- Widgets ------------------------------------------------------------
	TSharedPtr<STileView<FPluginInventoryEntryRef>> TileViewWidget;
	TSharedPtr<class SSearchBox>                     SearchBoxWidget;
	TSharedPtr<class STextBlock>                     StatsLabel;

	// ---- AI / Details -------------------------------------------------------
	/** Shared Ollama provider (lives as long as this browser). */
	TSharedPtr<FOllamaPluginSummaryProvider> SummaryProvider;

	/** Currently selected Ollama model name (persisted in editor user config). */
	FString SelectedOllamaModel;

	/** Available model names fetched from Ollama (may be empty if offline). */
	TArray<FString> AvailableOllamaModels;

	/** Weak ref to the currently open details window (at most one at a time). */
	TWeakPtr<SWindow> ActiveDetailsWindow;

	/** Ollama brand icon loaded from plugin Resources/ollama.png. */
	TSharedPtr<FSlateBrush> OllamaIconBrush;

	// ---- Active timers ------------------------------------------------------
	TSharedPtr<FActiveTimerHandle> RebuildTimerHandle;
	TSharedPtr<FActiveTimerHandle> FilterTimerHandle;
	bool bRebuildPending = false;
	bool bFilterPending  = false;

	// ---- Directory watching -------------------------------------------------
	TMap<FString, FDelegateHandle> WatchHandles;
	void RegisterDirectoryWatchers();
	void UnregisterDirectoryWatchers();
	void OnPluginDirectoryChanged(const TArray<FFileChangeData>& Changes);

	// ---- Timer callbacks ----------------------------------------------------
	EActiveTimerReturnType TimerRebuildInventory(double InCurrentTime, float InDeltaTime);
	EActiveTimerReturnType TimerRefreshFilter   (double InCurrentTime, float InDeltaTime);

	// ---- Tile view callbacks ------------------------------------------------
	TSharedRef<ITableRow> OnGenerateTile(FPluginInventoryEntryRef Item,
	                                     const TSharedRef<STableViewBase>& OwnerTable);
	void OnTileDoubleClicked(FPluginInventoryEntryPtr Item);

	// ---- Model picker -------------------------------------------------------
	TSharedRef<SWidget> BuildModelPickerWidget();
	TSharedRef<SWidget> BuildModelPickerMenu();
	void OnOllamaModelSelected(FString ModelName);
	void OnAvailableModelsFetched(const TArray<FString>& Models);
	FText GetSelectedModelText() const;
	void RefreshAvailableModels();

	// ---- Filter bar builders ------------------------------------------------
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildSourceFilterMenu();
	TSharedRef<SWidget> BuildTypeFilterMenu();
	TSharedRef<SWidget> BuildCategoryFilterMenu();
	TSharedRef<SWidget> BuildStatusFilterMenu();
	TSharedRef<SWidget> BuildMoreFiltersMenu();
	TSharedRef<SWidget> BuildExportMenu();

	// ---- Filter bar callbacks -----------------------------------------------
	void OnSearchTextChanged(const FText& NewText);
	void OnSearchTextCommitted(const FText& NewText, ETextCommit::Type CommitType);
	FReply OnRefreshClicked();
	FReply OnClearFiltersClicked();
	void ExportInventory(EPluginInventoryExportFormat Format);

	void ToggleTypeFilter      (EPluginType Type);
	bool IsTypeFilterActive    (EPluginType Type) const;
	void ToggleLoadedFromFilter(EPluginLoadedFrom From);
	bool IsLoadedFromFilterActive(EPluginLoadedFrom From) const;
	void SetEnabledFilter      (EPluginEnabledFilter Value);
	bool IsEnabledFilterActive (EPluginEnabledFilter Value) const;
	void ToggleCategoryFilter  (FString Category);
	bool IsCategoryFilterActive(const FString& Category) const;

	// ---- Sort callbacks -----------------------------------------------------
	void SetSortField(EPluginSortField Field);
	bool IsSortFieldActive(EPluginSortField Field) const;
	void ToggleSortDirection();

	// ---- Stats label --------------------------------------------------------
	FText GetStatsText() const;

	// ---- Helpers ------------------------------------------------------------
	/** Collect unique categories from AllEntries for the Category dropdown. */
	TArray<FString> CollectCategories() const;
};
