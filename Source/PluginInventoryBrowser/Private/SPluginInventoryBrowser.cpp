// Copyright StateOfRuin, 2026. All Rights Reserved.

#include "SPluginInventoryBrowser.h"
#include "SPluginInventoryTile.h"
#include "SPluginDetailsWindow.h"
#include "OllamaPluginSummaryProvider.h"
#include "Interfaces/IPluginManager.h"
#include "IDirectoryWatcher.h"
#include "DirectoryWatcherModule.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Views/STileView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SPluginInventoryBrowser"

// Debounce: how long to wait after a file-system change before rebuilding (seconds)
static constexpr float RebuildDebounceSeconds = 2.0f;

namespace
{
	FString PluginTypeToString(EPluginType Type)
	{
		switch (Type)
		{
		case EPluginType::Engine:     return TEXT("Engine");
		case EPluginType::Enterprise: return TEXT("Enterprise");
		case EPluginType::Project:    return TEXT("Project");
		case EPluginType::External:   return TEXT("External");
		case EPluginType::Mod:        return TEXT("Mod");
		default:                      return TEXT("Unknown");
		}
	}

	FString LoadedFromToString(EPluginLoadedFrom LoadedFrom)
	{
		switch (LoadedFrom)
		{
		case EPluginLoadedFrom::Engine:  return TEXT("Engine");
		case EPluginLoadedFrom::Project: return TEXT("Project");
		default:                         return TEXT("Unknown");
		}
	}

	FString CsvEscape(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));

		const bool bNeedsQuotes =
			Escaped.Contains(TEXT(",")) ||
			Escaped.Contains(TEXT("\"")) ||
			Escaped.Contains(TEXT("\n")) ||
			Escaped.Contains(TEXT("\r"));

		return bNeedsQuotes ? FString::Printf(TEXT("\"%s\""), *Escaped) : Escaped;
	}

	FString JoinCsvFields(const TArray<FString>& Fields)
	{
		FString Row;
		for (int32 Index = 0; Index < Fields.Num(); ++Index)
		{
			if (Index > 0)
			{
				Row += TEXT(",");
			}
			Row += CsvEscape(Fields[Index]);
		}
		return Row;
	}

	FString BuildExportDefaultName(SPluginInventoryBrowser::EPluginInventoryExportFormat Format)
	{
		const TCHAR* Extension = (Format == SPluginInventoryBrowser::EPluginInventoryExportFormat::Json) ? TEXT("json") : TEXT("csv");
		return FString::Printf(TEXT("PluginInventoryBrowser_%s.%s"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")), Extension);
	}

	FString BuildExportFileFilter(SPluginInventoryBrowser::EPluginInventoryExportFormat Format)
	{
		return (Format == SPluginInventoryBrowser::EPluginInventoryExportFormat::Json)
			? TEXT("JSON Files (*.json)|*.json|All Files (*.*)|*.*")
			: TEXT("CSV Files (*.csv)|*.csv|All Files (*.*)|*.*");
	}

	FText BuildExportDialogTitle(SPluginInventoryBrowser::EPluginInventoryExportFormat Format)
	{
		return (Format == SPluginInventoryBrowser::EPluginInventoryExportFormat::Json)
			? NSLOCTEXT("SPluginInventoryBrowser", "ExportJsonTitle", "Export Plugin Inventory to JSON")
			: NSLOCTEXT("SPluginInventoryBrowser", "ExportCsvTitle", "Export Plugin Inventory to CSV");
	}

	FText BuildExportSuccessText(SPluginInventoryBrowser::EPluginInventoryExportFormat Format, int32 Count, const FString& FilePath)
	{
		return FText::Format(
			(Format == SPluginInventoryBrowser::EPluginInventoryExportFormat::Json)
				? NSLOCTEXT("SPluginInventoryBrowser", "ExportJsonSuccess", "Exported {0} plugins to {1}")
				: NSLOCTEXT("SPluginInventoryBrowser", "ExportCsvSuccess", "Exported {0} plugins to {1}"),
			FText::AsNumber(Count),
			FText::FromString(FPaths::GetCleanFilename(FilePath)));
	}

	void ShowExportNotification(const FText& Message, SNotificationItem::ECompletionState CompletionState)
	{
		FNotificationInfo Info(Message);
		Info.bFireAndForget = true;
		Info.ExpireDuration = 4.0f;
		Info.FadeOutDuration = 0.25f;
		Info.bUseSuccessFailIcons = true;

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(CompletionState);
		}
	}

	bool SaveTextToFile(const FString& FilePath, const FString& Contents, FString& OutError)
	{
		const FString Directory = FPaths::GetPath(FilePath);
		IFileManager::Get().MakeDirectory(*Directory, true);

		if (!FFileHelper::SaveStringToFile(Contents, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to write %s"), *FilePath);
			return false;
		}

		return true;
	}

	bool SaveInventoryAsJson(const TArray<FPluginInventoryEntryRef>& Entries, const FString& FilePath, FString& OutError)
	{
		TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
		RootObject->SetStringField(TEXT("generatedAtUtc"), FDateTime::UtcNow().ToIso8601());
		RootObject->SetStringField(TEXT("exportScope"), TEXT("Visible"));
		RootObject->SetNumberField(TEXT("entryCount"), Entries.Num());

		TArray<TSharedPtr<FJsonValue>> JsonEntries;
		JsonEntries.Reserve(Entries.Num());

		for (const FPluginInventoryEntryRef& Entry : Entries)
		{
			TSharedRef<FJsonObject> JsonEntry = MakeShared<FJsonObject>();
			JsonEntry->SetStringField(TEXT("Name"), Entry->Name);
			JsonEntry->SetStringField(TEXT("FriendlyName"), Entry->FriendlyName);
			JsonEntry->SetStringField(TEXT("Description"), Entry->Description);
			JsonEntry->SetStringField(TEXT("Category"), Entry->Category);
			JsonEntry->SetStringField(TEXT("CreatedBy"), Entry->CreatedBy);
			JsonEntry->SetStringField(TEXT("EngineVersion"), Entry->EngineVersion);
			JsonEntry->SetStringField(TEXT("VersionName"), Entry->VersionName);
			JsonEntry->SetStringField(TEXT("BaseDir"), Entry->BaseDir);
			JsonEntry->SetStringField(TEXT("DescriptorFileName"), Entry->DescriptorFileName);
			JsonEntry->SetStringField(TEXT("PluginType"), PluginTypeToString(Entry->PluginType));
			JsonEntry->SetStringField(TEXT("LoadedFrom"), LoadedFromToString(Entry->LoadedFrom));
			JsonEntry->SetBoolField(TEXT("IsEnabled"), Entry->bIsEnabled);
			JsonEntry->SetBoolField(TEXT("IsEnabledByDefault"), Entry->bIsEnabledByDefault);
			JsonEntry->SetBoolField(TEXT("IsMounted"), Entry->bIsMounted);
			JsonEntry->SetBoolField(TEXT("IsHidden"), Entry->bIsHidden);
			JsonEntry->SetBoolField(TEXT("IsInstalled"), Entry->bIsInstalled);
			JsonEntry->SetBoolField(TEXT("CanContainContent"), Entry->bCanContainContent);
			JsonEntry->SetBoolField(TEXT("CanContainVerse"), Entry->bCanContainVerse);
			JsonEntry->SetNumberField(TEXT("ModuleCount"), Entry->ModuleCount);
			JsonEntry->SetNumberField(TEXT("DependencyCount"), Entry->DependencyCount);

			TArray<TSharedPtr<FJsonValue>> PlatformValues;
			PlatformValues.Reserve(Entry->SupportedTargetPlatforms.Num());
			for (const FString& Platform : Entry->SupportedTargetPlatforms)
			{
				PlatformValues.Add(MakeShared<FJsonValueString>(Platform));
			}
			JsonEntry->SetArrayField(TEXT("SupportedTargetPlatforms"), PlatformValues);

			JsonEntries.Add(MakeShared<FJsonValueObject>(JsonEntry));
		}

		RootObject->SetArrayField(TEXT("Plugins"), JsonEntries);

		FString Output;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		if (!FJsonSerializer::Serialize(RootObject, Writer))
		{
			OutError = TEXT("Failed to serialize plugin inventory as JSON.");
			return false;
		}

		return SaveTextToFile(FilePath, Output, OutError);
	}

	bool SaveInventoryAsCsv(const TArray<FPluginInventoryEntryRef>& Entries, const FString& FilePath, FString& OutError)
	{
		TArray<FString> Rows;
		Rows.Reserve(Entries.Num() + 1);

		Rows.Add(JoinCsvFields({
			TEXT("Name"),
			TEXT("FriendlyName"),
			TEXT("Description"),
			TEXT("Category"),
			TEXT("CreatedBy"),
			TEXT("EngineVersion"),
			TEXT("VersionName"),
			TEXT("BaseDir"),
			TEXT("DescriptorFileName"),
			TEXT("PluginType"),
			TEXT("LoadedFrom"),
			TEXT("IsEnabled"),
			TEXT("IsEnabledByDefault"),
			TEXT("IsMounted"),
			TEXT("IsHidden"),
			TEXT("IsInstalled"),
			TEXT("CanContainContent"),
			TEXT("CanContainVerse"),
			TEXT("ModuleCount"),
			TEXT("DependencyCount"),
			TEXT("SupportedTargetPlatforms")
		}));

		for (const FPluginInventoryEntryRef& Entry : Entries)
		{
			Rows.Add(JoinCsvFields({
				Entry->Name,
				Entry->FriendlyName,
				Entry->Description,
				Entry->Category,
				Entry->CreatedBy,
				Entry->EngineVersion,
				Entry->VersionName,
				Entry->BaseDir,
				Entry->DescriptorFileName,
				PluginTypeToString(Entry->PluginType),
				LoadedFromToString(Entry->LoadedFrom),
				Entry->bIsEnabled ? TEXT("true") : TEXT("false"),
				Entry->bIsEnabledByDefault ? TEXT("true") : TEXT("false"),
				Entry->bIsMounted ? TEXT("true") : TEXT("false"),
				Entry->bIsHidden ? TEXT("true") : TEXT("false"),
				Entry->bIsInstalled ? TEXT("true") : TEXT("false"),
				Entry->bCanContainContent ? TEXT("true") : TEXT("false"),
				Entry->bCanContainVerse ? TEXT("true") : TEXT("false"),
				FString::FromInt(Entry->ModuleCount),
				FString::FromInt(Entry->DependencyCount),
				FString::Join(Entry->SupportedTargetPlatforms, TEXT(";"))
			}));
		}

		FString Output;
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			Output += Rows[Index];
			if (Index + 1 < Rows.Num())
			{
				Output += TEXT("\r\n");
			}
		}
		Output += TEXT("\r\n");

		return SaveTextToFile(FilePath, Output, OutError);
	}

	bool PromptForExportPath(SPluginInventoryBrowser::EPluginInventoryExportFormat Format, FString& OutFilePath, FString& OutError)
	{
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (!DesktopPlatform)
		{
			OutError = TEXT("Desktop platform support is not available.");
			return false;
		}

		const FString DefaultDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PluginInventoryBrowserExports"));
		IFileManager::Get().MakeDirectory(*DefaultDirectory, true);

		TArray<FString> OutFileNames;
		const FString DefaultFileName = BuildExportDefaultName(Format);
		const FString FileTypes = BuildExportFileFilter(Format);
		void* ParentWindowHandle = const_cast<void*>(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr));
		const bool bSelected = DesktopPlatform->SaveFileDialog(
			ParentWindowHandle,
			*BuildExportDialogTitle(Format).ToString(),
			DefaultDirectory,
			DefaultFileName,
			FileTypes,
			EFileDialogFlags::None,
			OutFileNames);

		if (bSelected && OutFileNames.Num() > 0)
		{
			OutFilePath = OutFileNames[0];
			if (FPaths::GetExtension(OutFilePath).IsEmpty())
			{
				OutFilePath = FPaths::ChangeExtension(OutFilePath, (Format == SPluginInventoryBrowser::EPluginInventoryExportFormat::Json) ? TEXT("json") : TEXT("csv"));
			}
			return true;
		}

		return false;
	}
}

// ============================================================================
// Construct
// ============================================================================

void SPluginInventoryBrowser::Construct(const FArguments& InArgs)
{
	// Initialise AI summary provider and restore persisted model
	SummaryProvider = MakeShared<FOllamaPluginSummaryProvider>();

	GConfig->GetString(
		TEXT("PluginInventoryBrowser"),
		TEXT("SelectedOllamaModel"),
		SelectedOllamaModel,
		GEditorPerProjectIni);

	if (SelectedOllamaModel.IsEmpty())
	{
		SelectedOllamaModel = TEXT("qwen3:0.6b");
	}

	// Prime available models from Ollama (best-effort; updates AvailableOllamaModels)
	RefreshAvailableModels();

	RegisterDirectoryWatchers();
	RebuildInventory(); // synchronous on first open

	ChildSlot
	[
		SNew(SVerticalBox)

		// ---- Toolbar --------------------------------------------------------
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildToolbar()
		]

		// ---- Separator ------------------------------------------------------
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// ---- Stats ribbon ---------------------------------------------------
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 4.f)
		[
			SAssignNew(StatsLabel, STextBlock)
			.Text(this, &SPluginInventoryBrowser::GetStatsText)
			.TextStyle(FAppStyle::Get(), "SmallText")
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]

		// ---- Tile grid ------------------------------------------------------
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(4.f)
			[
				SAssignNew(TileViewWidget, STileView<FPluginInventoryEntryRef>)
				.ListItemsSource(&FilteredEntries)
				.OnGenerateTile(this, &SPluginInventoryBrowser::OnGenerateTile)
				.ItemWidth(296.f)
				.ItemHeight(196.f)
				.SelectionMode(ESelectionMode::Single)
				.ScrollbarVisibility(EVisibility::Visible)
			]
		]
	];
}

// ============================================================================
// Destructor
// ============================================================================

SPluginInventoryBrowser::~SPluginInventoryBrowser()
{
	UnregisterDirectoryWatchers();
}

// ============================================================================
// Toolbar
// ============================================================================

TSharedRef<SWidget> SPluginInventoryBrowser::BuildToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.f, 6.f))
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4.f, 4.f))

			// Search
			+ SWrapBox::Slot()
			.FillLineWhenSizeLessThan(600.f)
			.FillEmptySpace(true)
			[
				SNew(SBox)
				.MinDesiredWidth(200.f)
				[
					SAssignNew(SearchBoxWidget, SSearchBox)
					.HintText(LOCTEXT("SearchHint", "Search plugins…"))
					.OnTextChanged(this, &SPluginInventoryBrowser::OnSearchTextChanged)
					.OnTextCommitted(this, &SPluginInventoryBrowser::OnSearchTextCommitted)
				]
			]

			// Source filter
			+ SWrapBox::Slot()
			[
				SNew(SComboButton)
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("SourceBtn", "Source"))
				]
				.MenuContent()
				[
					BuildSourceFilterMenu()
				]
			]

			// Type filter
			+ SWrapBox::Slot()
			[
				SNew(SComboButton)
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("TypeBtn", "Type"))
				]
				.MenuContent()
				[
					BuildTypeFilterMenu()
				]
			]

			// Category filter
			+ SWrapBox::Slot()
			[
				SNew(SComboButton)
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("CategoryBtn", "Category"))
				]
				.MenuContent()
				[
					BuildCategoryFilterMenu()
				]
			]

			// Status filter
			+ SWrapBox::Slot()
			[
				SNew(SComboButton)
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("StatusBtn", "Status"))
				]
				.MenuContent()
				[
					BuildStatusFilterMenu()
				]
			]

			// More filters (technical facets)
			+ SWrapBox::Slot()
			[
				SNew(SComboButton)
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("MoreBtn", "More Filters"))
				]
				.MenuContent()
				[
					BuildMoreFiltersMenu()
				]
			]

			// Export menu
			+ SWrapBox::Slot()
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("ExportTip", "Export the currently visible inventory to JSON or CSV."))
				.ButtonContent()
				[
					SNew(STextBlock).Text(LOCTEXT("ExportBtn", "Export"))
				]
				.MenuContent()
				[
					BuildExportMenu()
				]
			]

			// Refresh button
			+ SWrapBox::Slot()
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("RefreshTip", "Re-scan all plugin directories and refresh the inventory."))
				.OnClicked(this, &SPluginInventoryBrowser::OnRefreshClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("RefreshBtn", "⟳ Refresh"))
				]
			]

			// Clear filters button
			+ SWrapBox::Slot()
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("ClearTip", "Clear all active filters."))
				.OnClicked(this, &SPluginInventoryBrowser::OnClearFiltersClicked)
				[
					SNew(STextBlock).Text(LOCTEXT("ClearBtn", "✕ Clear"))
				]
			]

			// AI Model picker
			+ SWrapBox::Slot()
			[
				BuildModelPickerWidget()
			]
		];
}

// ---- Source filter menu -------------------------------------------------

TSharedRef<SWidget> SPluginInventoryBrowser::BuildSourceFilterMenu()
{
	FMenuBuilder MB(true, nullptr);

	auto MakeLoadedFromEntry = [&](EPluginLoadedFrom From, const FText& Label)
	{
		MB.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SPluginInventoryBrowser::ToggleLoadedFromFilter, From),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SPluginInventoryBrowser::IsLoadedFromFilterActive, From)),
			NAME_None, EUserInterfaceActionType::ToggleButton);
	};
	MakeLoadedFromEntry(EPluginLoadedFrom::Engine,  LOCTEXT("FromEngine",  "Engine"));
	MakeLoadedFromEntry(EPluginLoadedFrom::Project, LOCTEXT("FromProject", "Project"));
	return MB.MakeWidget();
}

// ---- Type filter menu ---------------------------------------------------

TSharedRef<SWidget> SPluginInventoryBrowser::BuildTypeFilterMenu()
{
	FMenuBuilder MB(true, nullptr);

	auto MakeTypeEntry = [&](EPluginType Type, const FText& Label)
	{
		MB.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SPluginInventoryBrowser::ToggleTypeFilter, Type),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SPluginInventoryBrowser::IsTypeFilterActive, Type)),
			NAME_None, EUserInterfaceActionType::ToggleButton);
	};
	MakeTypeEntry(EPluginType::Engine,     LOCTEXT("TypeEngine",     "Engine"));
	MakeTypeEntry(EPluginType::Enterprise, LOCTEXT("TypeEnterprise", "Enterprise"));
	MakeTypeEntry(EPluginType::Project,    LOCTEXT("TypeProject",    "Project"));
	MakeTypeEntry(EPluginType::External,   LOCTEXT("TypeExternal",   "External"));
	MakeTypeEntry(EPluginType::Mod,        LOCTEXT("TypeMod",        "Mod"));
	return MB.MakeWidget();
}

// ---- Category filter menu -----------------------------------------------

TSharedRef<SWidget> SPluginInventoryBrowser::BuildCategoryFilterMenu()
{
	FMenuBuilder MB(true, nullptr);

	const TArray<FString> Categories = CollectCategories();
	for (const FString& Cat : Categories)
	{
		const FString CatCopy = Cat;
		MB.AddMenuEntry(
			FText::FromString(Cat), FText::GetEmpty(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SPluginInventoryBrowser::ToggleCategoryFilter, CatCopy),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, CatCopy]() { return IsCategoryFilterActive(CatCopy); })),
			NAME_None, EUserInterfaceActionType::ToggleButton);
	}
	return MB.MakeWidget();
}

// ---- Status filter menu -------------------------------------------------

TSharedRef<SWidget> SPluginInventoryBrowser::BuildStatusFilterMenu()
{
	FMenuBuilder MB(true, nullptr);

	auto MakeEntry = [&](EPluginEnabledFilter Val, const FText& Label)
	{
		MB.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SPluginInventoryBrowser::SetEnabledFilter, Val),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SPluginInventoryBrowser::IsEnabledFilterActive, Val)),
			NAME_None, EUserInterfaceActionType::RadioButton);
	};
	MakeEntry(EPluginEnabledFilter::All,          LOCTEXT("StatusAll",      "All"));
	MakeEntry(EPluginEnabledFilter::EnabledOnly,  LOCTEXT("StatusEnabled",  "Enabled Only"));
	MakeEntry(EPluginEnabledFilter::DisabledOnly, LOCTEXT("StatusDisabled", "Disabled Only"));
	return MB.MakeWidget();
}

// ---- More filters menu --------------------------------------------------

TSharedRef<SWidget> SPluginInventoryBrowser::BuildMoreFiltersMenu()
{
	FMenuBuilder MB(true, nullptr);

	MB.BeginSection("Visibility", LOCTEXT("VisSection", "Visibility"));
	MB.AddMenuEntry(
		LOCTEXT("ShowHidden", "Show Hidden Plugins"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]{ FilterState.bShowHidden = !FilterState.bShowHidden; ScheduleFilterRefresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]{ return FilterState.bShowHidden; })),
		NAME_None, EUserInterfaceActionType::ToggleButton);
	MB.EndSection();

	MB.BeginSection("Capabilities", LOCTEXT("CapSection", "Capabilities"));
	MB.AddMenuEntry(
		LOCTEXT("ContentOnly", "Content Capable Only"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]{ FilterState.bRequireContent = !FilterState.bRequireContent; ScheduleFilterRefresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]{ return FilterState.bRequireContent; })),
		NAME_None, EUserInterfaceActionType::ToggleButton);
	MB.AddMenuEntry(
		LOCTEXT("VerseOnly", "Verse Capable Only"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]{ FilterState.bRequireVerse = !FilterState.bRequireVerse; ScheduleFilterRefresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]{ return FilterState.bRequireVerse; })),
		NAME_None, EUserInterfaceActionType::ToggleButton);
	MB.AddMenuEntry(
		LOCTEXT("InstalledOnly", "Installed (Marketplace) Only"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]{ FilterState.bInstalledOnly = !FilterState.bInstalledOnly; ScheduleFilterRefresh(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([this]{ return FilterState.bInstalledOnly; })),
		NAME_None, EUserInterfaceActionType::ToggleButton);
	MB.EndSection();

	MB.BeginSection("Sort", LOCTEXT("SortSection", "Sort By"));
	auto MakeSortEntry = [&](EPluginSortField Field, const FText& Label)
	{
		MB.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SPluginInventoryBrowser::SetSortField, Field),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SPluginInventoryBrowser::IsSortFieldActive, Field)),
			NAME_None, EUserInterfaceActionType::RadioButton);
	};
	MakeSortEntry(EPluginSortField::Name,            LOCTEXT("SortName",    "Name"));
	MakeSortEntry(EPluginSortField::Category,        LOCTEXT("SortCat",     "Category"));
	MakeSortEntry(EPluginSortField::Source,          LOCTEXT("SortSrc",     "Source"));
	MakeSortEntry(EPluginSortField::ModuleCount,     LOCTEXT("SortMod",     "Module Count"));
	MakeSortEntry(EPluginSortField::DependencyCount, LOCTEXT("SortDep",     "Dependency Count"));
	MakeSortEntry(EPluginSortField::EnabledState,    LOCTEXT("SortEnabled", "Enabled State"));

	MB.AddMenuEntry(
		LOCTEXT("ToggleDir", "Toggle Sort Direction"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SPluginInventoryBrowser::ToggleSortDirection)));
	MB.EndSection();

	return MB.MakeWidget();
}

// ---- Export menu ---------------------------------------------------------

TSharedRef<SWidget> SPluginInventoryBrowser::BuildExportMenu()
{
	FMenuBuilder MB(true, nullptr);

	MB.BeginSection("Export", LOCTEXT("ExportSection", "Export Visible Inventory"));
	MB.AddMenuEntry(
		LOCTEXT("ExportJsonLabel", "Export JSON..."),
		LOCTEXT("ExportJsonTip", "Export the currently visible plugin inventory to a JSON file."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SPluginInventoryBrowser::ExportInventory, EPluginInventoryExportFormat::Json)));
	MB.AddMenuEntry(
		LOCTEXT("ExportCsvLabel", "Export CSV..."),
		LOCTEXT("ExportCsvTip", "Export the currently visible plugin inventory to a CSV file."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SPluginInventoryBrowser::ExportInventory, EPluginInventoryExportFormat::Csv)));
	MB.EndSection();

	return MB.MakeWidget();
}

// ============================================================================
// Inventory build
// ============================================================================

void SPluginInventoryBrowser::RebuildInventory()
{
	IPluginManager::Get().RefreshPluginsList();

	AllEntries.Reset();
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
	{
		AllEntries.Add(FPluginInventoryEntry::FromPlugin(Plugin));
	}

	RebuildFilteredList();
}

void SPluginInventoryBrowser::RebuildFilteredList()
{
	FilteredEntries.Reset();
	FilteredEntries.Reserve(AllEntries.Num());

	for (const FPluginInventoryEntryRef& Entry : AllEntries)
	{
		if (FilterState.PassesFilter(*Entry))
		{
			FilteredEntries.Add(Entry);
		}
	}

	FilteredEntries.Sort([this](const FPluginInventoryEntryRef& A, const FPluginInventoryEntryRef& B)
	{
		return FilterState.SortLess(A, B);
	});

	if (TileViewWidget.IsValid())
	{
		TileViewWidget->RequestListRefresh();
	}
}

// ============================================================================
// Directory watching
// ============================================================================

void SPluginInventoryBrowser::RegisterDirectoryWatchers()
{
	TArray<FString> Dirs;
	Dirs.Add(FPaths::EnginePluginsDir());

	if (FPaths::DirectoryExists(FPaths::EnterprisePluginsDir()))
	{
		Dirs.Add(FPaths::EnterprisePluginsDir());
	}
	if (FApp::HasProjectName())
	{
		const FString ProjPluginsDir = FPaths::ProjectPluginsDir();
		if (FPaths::DirectoryExists(ProjPluginsDir))
		{
			Dirs.Add(ProjPluginsDir);
		}
		const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
		if (Project)
		{
			for (const FString& ExtraPath : Project->GetAdditionalPluginDirectories())
			{
				if (FPaths::DirectoryExists(ExtraPath))
				{
					Dirs.Add(ExtraPath);
				}
			}
		}
	}

	FDirectoryWatcherModule& DWM = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	for (const FString& Dir : Dirs)
	{
		FDelegateHandle Handle;
		if (DWM.Get()->RegisterDirectoryChangedCallback_Handle(
			Dir,
			IDirectoryWatcher::FDirectoryChanged::CreateSP(this, &SPluginInventoryBrowser::OnPluginDirectoryChanged),
			Handle,
			IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges))
		{
			WatchHandles.Add(Dir, Handle);
		}
	}
}

void SPluginInventoryBrowser::UnregisterDirectoryWatchers()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("DirectoryWatcher")))
	{
		FDirectoryWatcherModule& DWM = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		for (auto& Pair : WatchHandles)
		{
			DWM.Get()->UnregisterDirectoryChangedCallback_Handle(Pair.Key, Pair.Value);
		}
	}
	WatchHandles.Empty();
}

void SPluginInventoryBrowser::OnPluginDirectoryChanged(const TArray<FFileChangeData>& /*Changes*/)
{
	ScheduleRebuild();
}

// ============================================================================
// Timer scheduling
// ============================================================================

void SPluginInventoryBrowser::ScheduleRebuild()
{
	if (!bRebuildPending)
	{
		bRebuildPending = true;
		RebuildTimerHandle = RegisterActiveTimer(
			RebuildDebounceSeconds,
			FWidgetActiveTimerDelegate::CreateSP(this, &SPluginInventoryBrowser::TimerRebuildInventory));
	}
}

void SPluginInventoryBrowser::ScheduleFilterRefresh()
{
	if (!bFilterPending)
	{
		bFilterPending = true;
		FilterTimerHandle = RegisterActiveTimer(
			0.f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SPluginInventoryBrowser::TimerRefreshFilter));
	}
}

EActiveTimerReturnType SPluginInventoryBrowser::TimerRebuildInventory(double /*T*/, float /*DT*/)
{
	bRebuildPending = false;
	RebuildInventory();
	return EActiveTimerReturnType::Stop;
}

EActiveTimerReturnType SPluginInventoryBrowser::TimerRefreshFilter(double /*T*/, float /*DT*/)
{
	bFilterPending = false;
	RebuildFilteredList();
	return EActiveTimerReturnType::Stop;
}

// ============================================================================
// Tile row generator
// ============================================================================

TSharedRef<ITableRow> SPluginInventoryBrowser::OnGenerateTile(
	FPluginInventoryEntryRef Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<FPluginInventoryEntryRef>, OwnerTable)
		[
			SNew(SPluginInventoryTile)
			.Entry(Item)
			.OnDoubleClicked(this, &SPluginInventoryBrowser::OnTileDoubleClicked)
		];
}

void SPluginInventoryBrowser::OnTileDoubleClicked(FPluginInventoryEntryPtr Item)
{
	if (!Item.IsValid() || Item->Name.IsEmpty())
	{
		return;
	}

	SPluginDetailsWindow::FOnPluginStateChanged StateChangedDelegate;
	StateChangedDelegate.BindSP(this, &SPluginInventoryBrowser::RebuildInventory);

	TSharedRef<SWindow> NewWindow = SPluginDetailsWindow::Show(
		Item.ToSharedRef(),
		SelectedOllamaModel,
		SummaryProvider,
		StateChangedDelegate);

	ActiveDetailsWindow = NewWindow;
}

// ============================================================================
// Filter callbacks
// ============================================================================

void SPluginInventoryBrowser::OnSearchTextChanged(const FText& NewText)
{
	FilterState.SearchText = NewText.ToString().ToLower();
	ScheduleFilterRefresh();
}

void SPluginInventoryBrowser::OnSearchTextCommitted(const FText& NewText, ETextCommit::Type /*Type*/)
{
	FilterState.SearchText = NewText.ToString().ToLower();
	ScheduleFilterRefresh();
}

FReply SPluginInventoryBrowser::OnRefreshClicked()
{
	RebuildInventory();
	return FReply::Handled();
}

FReply SPluginInventoryBrowser::OnClearFiltersClicked()
{
	FilterState.ResetAllFilters();
	if (SearchBoxWidget.IsValid())
	{
		SearchBoxWidget->SetText(FText::GetEmpty());
	}
	ScheduleFilterRefresh();
	return FReply::Handled();
}

void SPluginInventoryBrowser::ExportInventory(EPluginInventoryExportFormat Format)
{
	FString SelectedFilePath;
	FString ErrorMessage;
	if (!PromptForExportPath(Format, SelectedFilePath, ErrorMessage))
	{
		if (!ErrorMessage.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("Plugin inventory export failed: %s"), *ErrorMessage);
			ShowExportNotification(FText::FromString(ErrorMessage), SNotificationItem::CS_Fail);
		}
		return;
	}

	const bool bSucceeded = (Format == EPluginInventoryExportFormat::Json)
		? SaveInventoryAsJson(FilteredEntries, SelectedFilePath, ErrorMessage)
		: SaveInventoryAsCsv(FilteredEntries, SelectedFilePath, ErrorMessage);

	if (bSucceeded)
	{
		UE_LOG(LogTemp, Log, TEXT("Exported plugin inventory to %s"), *SelectedFilePath);
		ShowExportNotification(BuildExportSuccessText(Format, FilteredEntries.Num(), SelectedFilePath), SNotificationItem::CS_Success);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Plugin inventory export failed: %s"), *ErrorMessage);
	ShowExportNotification(FText::FromString(ErrorMessage), SNotificationItem::CS_Fail);
}

void SPluginInventoryBrowser::ToggleTypeFilter(EPluginType Type)
{
	if (FilterState.AllowedTypes.Contains(Type)) { FilterState.AllowedTypes.Remove(Type); }
	else { FilterState.AllowedTypes.Add(Type); }
	ScheduleFilterRefresh();
}

bool SPluginInventoryBrowser::IsTypeFilterActive(EPluginType Type) const
{
	return FilterState.AllowedTypes.Contains(Type);
}

void SPluginInventoryBrowser::ToggleLoadedFromFilter(EPluginLoadedFrom From)
{
	if (FilterState.AllowedLoadedFrom.Contains(From)) { FilterState.AllowedLoadedFrom.Remove(From); }
	else { FilterState.AllowedLoadedFrom.Add(From); }
	ScheduleFilterRefresh();
}

bool SPluginInventoryBrowser::IsLoadedFromFilterActive(EPluginLoadedFrom From) const
{
	return FilterState.AllowedLoadedFrom.Contains(From);
}

void SPluginInventoryBrowser::SetEnabledFilter(EPluginEnabledFilter Value)
{
	FilterState.EnabledFilter = Value;
	ScheduleFilterRefresh();
}

bool SPluginInventoryBrowser::IsEnabledFilterActive(EPluginEnabledFilter Value) const
{
	return FilterState.EnabledFilter == Value;
}

void SPluginInventoryBrowser::ToggleCategoryFilter(FString Category)
{
	if (FilterState.AllowedCategories.Contains(Category)) { FilterState.AllowedCategories.Remove(Category); }
	else { FilterState.AllowedCategories.Add(Category); }
	ScheduleFilterRefresh();
}

bool SPluginInventoryBrowser::IsCategoryFilterActive(const FString& Category) const
{
	return FilterState.AllowedCategories.Contains(Category);
}

// ============================================================================
// Sort callbacks
// ============================================================================

void SPluginInventoryBrowser::SetSortField(EPluginSortField Field)
{
	if (FilterState.SortField == Field)
	{
		FilterState.bSortAscending = !FilterState.bSortAscending;
	}
	else
	{
		FilterState.SortField      = Field;
		FilterState.bSortAscending = true;
	}
	ScheduleFilterRefresh();
}

bool SPluginInventoryBrowser::IsSortFieldActive(EPluginSortField Field) const
{
	return FilterState.SortField == Field;
}

void SPluginInventoryBrowser::ToggleSortDirection()
{
	FilterState.bSortAscending = !FilterState.bSortAscending;
	ScheduleFilterRefresh();
}

// ============================================================================
// Stats text
// ============================================================================

FText SPluginInventoryBrowser::GetStatsText() const
{
	return FText::Format(
		LOCTEXT("StatsFmt", "Showing {0} of {1} plugins"),
		FText::AsNumber(FilteredEntries.Num()),
		FText::AsNumber(AllEntries.Num()));
}

// ============================================================================
// Helpers
// ============================================================================

TArray<FString> SPluginInventoryBrowser::CollectCategories() const
{
	TSet<FString> Cats;
	for (const FPluginInventoryEntryRef& E : AllEntries)
	{
		Cats.Add(E->Category);
	}
	TArray<FString> Sorted = Cats.Array();
	Sorted.Sort();
	return Sorted;
}

// ============================================================================
// AI Model picker
// ============================================================================

TSharedRef<SWidget> SPluginInventoryBrowser::BuildModelPickerWidget()
{
	return SNew(SComboButton)
		.ToolTipText(LOCTEXT("ModelPickerTip",
			"Select the local Ollama model used to generate AI plugin summaries."))
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text(this, &SPluginInventoryBrowser::GetSelectedModelText)
		]
		.MenuContent()
		[
			BuildModelPickerMenu()
		];
}

TSharedRef<SWidget> SPluginInventoryBrowser::BuildModelPickerMenu()
{
	FMenuBuilder MB(true, nullptr);

	MB.BeginSection("Models", LOCTEXT("ModelSection", "Ollama Model"));

	// Always include the selected model and the default in the list
	TSet<FString> ShownModels;
	ShownModels.Add(TEXT("qwen3:0.6b"));
	ShownModels.Add(SelectedOllamaModel);
	for (const FString& M : AvailableOllamaModels)
	{
		ShownModels.Add(M);
	}

	TArray<FString> SortedModels = ShownModels.Array();
	SortedModels.Sort();

	for (const FString& ModelName : SortedModels)
	{
		const FString ModelNameCopy = ModelName;
		MB.AddMenuEntry(
			FText::FromString(ModelName), FText::GetEmpty(), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SPluginInventoryBrowser::OnOllamaModelSelected, ModelNameCopy),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, ModelNameCopy]
				{
					return SelectedOllamaModel == ModelNameCopy;
				})),
			NAME_None, EUserInterfaceActionType::RadioButton);
	}

	MB.EndSection();

	MB.BeginSection("Actions", LOCTEXT("ModelActionsSection", "Actions"));
	MB.AddMenuEntry(
		LOCTEXT("RefreshModels", "Refresh model list"),
		LOCTEXT("RefreshModelsTip", "Re-query the local Ollama instance for available models."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SPluginInventoryBrowser::RefreshAvailableModels)));
	MB.EndSection();

	return MB.MakeWidget();
}

void SPluginInventoryBrowser::OnOllamaModelSelected(FString ModelName)
{
	if (SelectedOllamaModel == ModelName)
	{
		return;
	}

	if (SummaryProvider.IsValid())
	{
		SummaryProvider->InvalidateCacheForModel(SelectedOllamaModel);
	}

	SelectedOllamaModel = ModelName;

	GConfig->SetString(
		TEXT("PluginInventoryBrowser"),
		TEXT("SelectedOllamaModel"),
		*SelectedOllamaModel,
		GEditorPerProjectIni);
}

FText SPluginInventoryBrowser::GetSelectedModelText() const
{
	return FText::Format(LOCTEXT("ModelPickerLabel", "AI: {0}"),
		FText::FromString(SelectedOllamaModel));
}

void SPluginInventoryBrowser::RefreshAvailableModels()
{
	if (!SummaryProvider.IsValid())
	{
		return;
	}

	FOllamaPluginSummaryProvider::FOnModelsReady Delegate;
	Delegate.BindSP(this, &SPluginInventoryBrowser::OnAvailableModelsFetched);
	SummaryProvider->FetchAvailableModels(Delegate);
}

void SPluginInventoryBrowser::OnAvailableModelsFetched(const TArray<FString>& Models)
{
	AvailableOllamaModels = Models;
}

#undef LOCTEXT_NAMESPACE
