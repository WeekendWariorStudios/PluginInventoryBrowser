// Copyright StateOfRuin, 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "PluginInventoryEntry.h"
#include "OllamaPluginSummaryProvider.h"

class STextBlock;
class SMultiLineEditableText;
class SButton;
class SThrobber;

/**
 * SPluginDetailsWindow
 *
 * Floating, non-modal details window opened by double-clicking an inventory tile.
 *
 * Sections:
 *  1. Header  – icon, friendly name, version, source
 *  2. Overview – full description + all metadata
 *  3. AI Summary – Ollama-backed text with an explicit generate button
 *  4. Actions – enable / disable toggle with save & restart notice
 */
class SPluginDetailsWindow : public SCompoundWidget
{
public:
	/** Fired when the user toggles the plugin enabled state so the browser can refresh. */
	DECLARE_DELEGATE(FOnPluginStateChanged);

	SLATE_BEGIN_ARGS(SPluginDetailsWindow)
		: _Entry()
		, _SelectedModel(TEXT("qwen3:0.6b"))
	{}
		SLATE_ARGUMENT(FPluginInventoryEntryRef, Entry)
		SLATE_ARGUMENT(FString, SelectedModel)
		SLATE_EVENT(FOnPluginStateChanged, OnPluginStateChanged)
		/** The Ollama summary provider shared from the browser (may be null, in which case a local one is used). */
		SLATE_ARGUMENT(TSharedPtr<FOllamaPluginSummaryProvider>, SummaryProvider)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Create and show a standalone floating window hosting this widget.
	 * Returns the window so the caller can keep a weak reference.
	 */
	static TSharedRef<SWindow> Show(
		const FPluginInventoryEntryRef& Entry,
		const FString& SelectedModel,
		const TSharedPtr<FOllamaPluginSummaryProvider>& SummaryProvider,
		FOnPluginStateChanged OnPluginStateChanged);

private:
	FPluginInventoryEntryRef EntryPtr;
	FString CurrentModel;
	FOnPluginStateChanged OnPluginStateChangedDelegate;
	TSharedPtr<FOllamaPluginSummaryProvider> OllamaProvider;

	// ---- Icon brush ---------------------------------------------------------
	TSharedPtr<FSlateBrush> IconBrush;

	// ---- Summary UI ---------------------------------------------------------
	TSharedPtr<STextBlock>            SummaryText;
	TSharedPtr<SThrobber>             SummaryThrobber;
	TSharedPtr<SButton>               SummaryRefreshButton;
	bool                              bSummaryPending = false;
	bool                              bRestartRequired = false;

	// ---- Helpers ------------------------------------------------------------
	void RequestSummary();

	void OnSummaryReady(const FString& PluginName, const FString& Summary, bool bWasAI);

	// ---- Enable / Disable ---------------------------------------------------
	FReply OnToggleEnabled();
	bool   CanToggleEnabled() const;
	FText  GetToggleButtonText() const;
	FText  GetToggleButtonTooltip() const;
	EVisibility GetRestartNoticeVisibility() const;

	// ---- Attribute helpers --------------------------------------------------
	const FSlateBrush* GetIconBrush() const;
	FText  GetFriendlyName() const;
	FText  GetVersionText() const;
	FText  GetSourceText() const;
	FText  GetFullDescription() const;
	FText  GetMetadataText() const;
};
