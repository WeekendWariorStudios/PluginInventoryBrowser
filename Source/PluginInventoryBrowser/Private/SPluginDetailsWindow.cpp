// Copyright StateOfRuin, 2026. All Rights Reserved.

#include "SPluginDetailsWindow.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SPluginDetailsWindow"

// ============================================================================
// Show (static factory)
// ============================================================================

/*static*/ TSharedRef<SWindow> SPluginDetailsWindow::Show(
	const FPluginInventoryEntryPtr& Entry,
	const FString& SelectedModel,
	const TSharedPtr<FOllamaPluginSummaryProvider>& SummaryProvider,
	FOnPluginStateChanged OnPluginStateChanged)
{
	if (!Entry.IsValid())
	{
		return SNew(SWindow)
			.Title(LOCTEXT("WindowTitleInvalid", "Plugin Details"))
			.ClientSize(FVector2D(400.f, 140.f))
			.SizingRule(ESizingRule::UserSized)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.AutoCenter(EAutoCenter::PreferredWorkArea)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InvalidEntry", "No plugin entry was available to display."))
			];
	}

	const FText Title = FText::Format(
		LOCTEXT("WindowTitle", "Plugin Details – {0}"),
		FText::FromString(Entry->FriendlyName));

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(Title)
		.ClientSize(FVector2D(560.f, 720.f))
		.SizingRule(ESizingRule::UserSized)
		.SupportsMaximize(true)
		.SupportsMinimize(false)
		.IsTopmostWindow(false)
		.AutoCenter(EAutoCenter::PreferredWorkArea);

	TSharedRef<SPluginDetailsWindow> Content = SNew(SPluginDetailsWindow)
		.Entry(Entry)
		.SelectedModel(SelectedModel)
		.SummaryProvider(SummaryProvider)
		.OnPluginStateChanged(OnPluginStateChanged);

	Window->SetContent(Content);
	FSlateApplication::Get().AddWindow(Window);
	return Window;
}

// ============================================================================
// Construct
// ============================================================================

void SPluginDetailsWindow::Construct(const FArguments& InArgs)
{
	EntryPtr                  = InArgs._Entry;
	CurrentModel              = InArgs._SelectedModel;
	OnPluginStateChangedDelegate = InArgs._OnPluginStateChanged;
	OllamaProvider            = InArgs._SummaryProvider;

	if (!EntryPtr.IsValid())
	{
		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.Padding(16.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InvalidEntry", "No plugin entry was available to display."))
			]
		];
		return;
	}

	if (!OllamaProvider.IsValid())
	{
		OllamaProvider = MakeShared<FOllamaPluginSummaryProvider>();
	}

	// Load plugin icon
	const FString IconPath = EntryPtr->BaseDir / TEXT("Resources/Icon128.png");
	const FName   BrushKey(*IconPath);
	const FIntPoint IconPx = FSlateApplication::Get().GetRenderer()->GenerateDynamicImageResource(BrushKey);
	if (IconPx.X > 0 && IconPx.Y > 0)
	{
		IconBrush = MakeShared<FSlateDynamicImageBrush>(BrushKey, FVector2D(IconPx.X, IconPx.Y));
	}

	// Load AI icon from PluginInventoryBrowser Resources/ai.png
	TSharedPtr<IPlugin> PIBPlugin = IPluginManager::Get().FindPlugin(TEXT("PluginInventoryBrowser"));
	if (PIBPlugin.IsValid())
	{
		const FString AIIconPath = PIBPlugin->GetBaseDir() / TEXT("Resources/ai.png");
		const FName   AIBrushKey(*AIIconPath);
		const FIntPoint AIIconPx = FSlateApplication::Get().GetRenderer()->GenerateDynamicImageResource(AIBrushKey);
		if (AIIconPx.X > 0 && AIIconPx.Y > 0)
		{
			AIIconBrush = MakeShared<FSlateDynamicImageBrush>(AIBrushKey, FVector2D(16.f, 16.f));
		}
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.f)
		[
			SNew(SScrollBox)

			// =========================================================
			// HEADER
			// =========================================================
			+ SScrollBox::Slot()
			.Padding(16.f, 16.f, 16.f, 8.f)
			[
				SNew(SHorizontalBox)

				// Icon
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				.Padding(0.f, 0.f, 12.f, 0.f)
				[
					SNew(SBox)
					.WidthOverride(80.f)
					.HeightOverride(80.f)
					[
						SNew(SImage)
						.Image(this, &SPluginDetailsWindow::GetIconBrush)
					]
				]

				// Name / version / source column
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(this, &SPluginDetailsWindow::GetFriendlyName)
						.TextStyle(FAppStyle::Get(), "LargeText")
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 4.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(this, &SPluginDetailsWindow::GetVersionText)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 2.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(this, &SPluginDetailsWindow::GetSourceText)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
			]

			// =========================================================
			// SEPARATOR
			// =========================================================
			+ SScrollBox::Slot()
			.Padding(16.f, 0.f)
			[
				SNew(SSeparator)
			]

			// =========================================================
			// OVERVIEW – description
			// =========================================================
			+ SScrollBox::Slot()
			.Padding(16.f, 12.f, 16.f, 4.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OverviewLabel", "Description"))
				.TextStyle(FAppStyle::Get(), "NormalText.Important")
			]

			+ SScrollBox::Slot()
			.Padding(16.f, 0.f, 16.f, 8.f)
			[
				SNew(STextBlock)
				.Text(this, &SPluginDetailsWindow::GetFullDescription)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			// =========================================================
			// OVERVIEW – metadata grid
			// =========================================================
			+ SScrollBox::Slot()
			.Padding(16.f, 4.f, 16.f, 4.f)
			[
				SNew(STextBlock)
				.Text(this, &SPluginDetailsWindow::GetMetadataText)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			// =========================================================
			// URL HYPERLINK (if available)
			// =========================================================
			+ SScrollBox::Slot()
			.Padding(16.f, 2.f, 16.f, 10.f)
			[
				SNew(SBox)
				.Visibility(this, &SPluginDetailsWindow::GetURLVisibility)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 5.f, 0.f)
					[
						SNew(STextBlock)
						.Text(this, &SPluginDetailsWindow::GetURLLabelText)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SHyperlink)
						.Text(this, &SPluginDetailsWindow::GetURLDisplayText)
						.OnNavigate_Lambda([this]()
						{
							const FString URL = GetBestURL();
							if (!URL.IsEmpty())
							{
								FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
							}
						})
					]
				]
			]

			// =========================================================
			// SEPARATOR
			// =========================================================
			+ SScrollBox::Slot()
			.Padding(16.f, 0.f)
			[
				SNew(SSeparator)
			]

			// =========================================================
			// AI SUMMARY
			// =========================================================
			+ SScrollBox::Slot()
			.Padding(16.f, 12.f, 16.f, 4.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AISummaryLabel", "AI Summary"))
					.TextStyle(FAppStyle::Get(), "NormalText.Important")
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.f, 0.f, 0.f, 0.f)
				[
					SAssignNew(SummaryThrobber, SThrobber)
					.Visibility(EVisibility::Collapsed)
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SAssignNew(SummaryRefreshButton, SButton)
					.ToolTipText(LOCTEXT("GenerateSummaryTip", "Generate an AI summary for this plugin using the selected Ollama model."))
					.OnClicked_Lambda([this]() -> FReply
					{
						RequestSummary();
						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.f, 0.f, 5.f, 0.f)
						[
							SNew(SImage)
							.Image_Lambda([this]() -> const FSlateBrush*
							{
								return AIIconBrush.IsValid() ? AIIconBrush.Get() : FAppStyle::Get().GetBrush("Icons.BulletPoint");
							})
							.DesiredSizeOverride(FVector2D(16.f, 16.f))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(LOCTEXT("GenerateSummaryBtn", "Generate AI Summary"))
						]
					]
				]
			]

			+ SScrollBox::Slot()
			.Padding(16.f, 0.f, 16.f, 12.f)
			[
				SAssignNew(SummaryContainer, SBox)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SummaryPending", "Click Generate AI Summary to create one with Ollama."))
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]

			// =========================================================
			// SEPARATOR
			// =========================================================
			+ SScrollBox::Slot()
			.Padding(16.f, 0.f)
			[
				SNew(SSeparator)
			]

			// =========================================================
			// ACTIONS – enable / disable
			// =========================================================
			+ SScrollBox::Slot()
			.Padding(16.f, 12.f, 16.f, 4.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ActionsLabel", "Actions"))
				.TextStyle(FAppStyle::Get(), "NormalText.Important")
			]

			+ SScrollBox::Slot()
			.Padding(16.f, 0.f, 16.f, 4.f)
			[
				SNew(SButton)
				.IsEnabled(this, &SPluginDetailsWindow::CanToggleEnabled)
				.ToolTipText(this, &SPluginDetailsWindow::GetToggleButtonTooltip)
				.OnClicked(this, &SPluginDetailsWindow::OnToggleEnabled)
				[
					SNew(STextBlock)
					.Text(this, &SPluginDetailsWindow::GetToggleButtonText)
				]
			]

			// Restart notice
			+ SScrollBox::Slot()
			.Padding(16.f, 0.f, 16.f, 16.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RestartNotice",
					"A restart of the editor is required for this change to take effect."))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.8f, 0.2f, 1.f)))
				.AutoWrapText(true)
				.Visibility(this, &SPluginDetailsWindow::GetRestartNoticeVisibility)
			]
		]
	];
}

// ============================================================================
// Summary
// ============================================================================

void SPluginDetailsWindow::RequestSummary()
{
	if (!SummaryContainer.IsValid() || !OllamaProvider.IsValid() || !EntryPtr.IsValid())
	{
		return;
	}

	bSummaryPending = true;
	SummaryContainer->SetContent(
		SNew(STextBlock)
		.Text(LOCTEXT("SummaryGenerating", "Generating AI summary…"))
		.AutoWrapText(true)
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
	);
	if (SummaryThrobber.IsValid())
	{
		SummaryThrobber->SetVisibility(EVisibility::Visible);
	}
	if (SummaryRefreshButton.IsValid())
	{
		SummaryRefreshButton->SetEnabled(false);
	}

	FOllamaPluginSummaryProvider::FOnSummaryReady Delegate;
	Delegate.BindSP(this, &SPluginDetailsWindow::OnSummaryReady);
	OllamaProvider->RequestSummary(EntryPtr.ToSharedRef(), CurrentModel, Delegate);
}

void SPluginDetailsWindow::OnSummaryReady(const FString& /*PluginName*/, const FString& Summary, bool /*bWasAI*/)
{
	bSummaryPending = false;
	if (SummaryContainer.IsValid())
	{
		SummaryContainer->SetContent(BuildMarkdownWidget(Summary));
	}
	if (SummaryThrobber.IsValid())
	{
		SummaryThrobber->SetVisibility(EVisibility::Collapsed);
	}
	if (SummaryRefreshButton.IsValid())
	{
		SummaryRefreshButton->SetEnabled(true);
	}
}

// ============================================================================
// Enable / Disable
// ============================================================================

FReply SPluginDetailsWindow::OnToggleEnabled()
{
	const bool bNewEnabled = !EntryPtr->bIsEnabled;
	FText FailReason;

	IProjectManager& PM = IProjectManager::Get();
	if (!PM.SetPluginEnabled(EntryPtr->Name, bNewEnabled, FailReason))
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("EnableFailFmt", "Could not change plugin state: {0}"),
			FailReason));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 6.0f;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> Note = FSlateNotificationManager::Get().AddNotification(Info);
		if (Note.IsValid()) { Note->SetCompletionState(SNotificationItem::CS_Fail); }
		return FReply::Handled();
	}

	FText SaveFailReason;
	const bool bSaved = PM.SaveCurrentProjectToDisk(SaveFailReason);

	// Update our local copy so the button reflects the new state immediately
	EntryPtr->bIsEnabled = bNewEnabled;
	bRestartRequired = true;

	if (!bSaved)
	{
		FNotificationInfo Info(FText::Format(
			LOCTEXT("SaveFailFmt", "Plugin state changed, but saving the project failed: {0}"),
			SaveFailReason));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 6.0f;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> Note = FSlateNotificationManager::Get().AddNotification(Info);
		if (Note.IsValid()) { Note->SetCompletionState(SNotificationItem::CS_Fail); }
	}

	// Notify the browser to refresh
	OnPluginStateChangedDelegate.ExecuteIfBound();

	return FReply::Handled();
}

bool SPluginDetailsWindow::CanToggleEnabled() const
{
	// Engine plugins loaded from engine are not toggleable from the project
	if (EntryPtr->LoadedFrom == EPluginLoadedFrom::Engine &&
		EntryPtr->PluginType == EPluginType::Engine)
	{
		return false;
	}

	return IPluginManager::Get().CanEnablePluginInCurrentTarget(EntryPtr->Name);
}

FText SPluginDetailsWindow::GetToggleButtonText() const
{
	return EntryPtr->bIsEnabled
		? LOCTEXT("DisablePlugin", "Disable Plugin")
		: LOCTEXT("EnablePlugin",  "Enable Plugin");
}

FText SPluginDetailsWindow::GetToggleButtonTooltip() const
{
	if (!CanToggleEnabled())
	{
		return LOCTEXT("ToggleTipDisabled",
			"This plugin cannot be toggled in the current target configuration.");
	}

	return EntryPtr->bIsEnabled
		? LOCTEXT("DisableTip", "Disable this plugin in the project descriptor. Requires an editor restart.")
		: LOCTEXT("EnableTip",  "Enable this plugin in the project descriptor. Requires an editor restart.");
}

EVisibility SPluginDetailsWindow::GetRestartNoticeVisibility() const
{
	return bRestartRequired ? EVisibility::Visible : EVisibility::Collapsed;
}

// ============================================================================
// Attribute helpers
// ============================================================================

const FSlateBrush* SPluginDetailsWindow::GetIconBrush() const
{
	if (IconBrush.IsValid()) { return IconBrush.Get(); }
	return FAppStyle::Get().GetBrush("Plugins.TabIcon");
}

FText SPluginDetailsWindow::GetFriendlyName() const
{
	return FText::FromString(EntryPtr->FriendlyName);
}

FText SPluginDetailsWindow::GetVersionText() const
{
	FString Ver = EntryPtr->VersionName;
	if (!EntryPtr->EngineVersion.IsEmpty())
	{
		Ver += FString::Printf(TEXT("  ·  Engine %s"), *EntryPtr->EngineVersion);
	}
	return FText::FromString(Ver);
}

FText SPluginDetailsWindow::GetSourceText() const
{
	return EntryPtr->GetSourceLabel();
}

FText SPluginDetailsWindow::GetFullDescription() const
{
	return EntryPtr->Description.IsEmpty()
		? LOCTEXT("NoDescription", "(No description provided)")
		: FText::FromString(EntryPtr->Description);
}

FText SPluginDetailsWindow::GetMetadataText() const
{
	TArray<FString> Lines;

	if (!EntryPtr->Category.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Category: %s"), *EntryPtr->Category));
	}
	if (!EntryPtr->CreatedBy.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Author: %s"), *EntryPtr->CreatedBy));
	}

	Lines.Add(FString::Printf(TEXT("Modules: %d"), EntryPtr->ModuleCount));
	Lines.Add(FString::Printf(TEXT("Dependencies: %d"), EntryPtr->DependencyCount));

	if (EntryPtr->SupportedTargetPlatforms.Num() > 0)
	{
		Lines.Add(TEXT("Platforms: ") + FString::Join(EntryPtr->SupportedTargetPlatforms, TEXT(", ")));
	}

	TArray<FString> Flags;
	if (EntryPtr->bCanContainContent) { Flags.Add(TEXT("Content")); }
	if (EntryPtr->bCanContainVerse)   { Flags.Add(TEXT("Verse")); }
	if (EntryPtr->bIsInstalled)       { Flags.Add(TEXT("Installed")); }
	if (Flags.Num() > 0)
	{
		Lines.Add(TEXT("Flags: ") + FString::Join(Flags, TEXT(", ")));
	}

	Lines.Add(FString::Printf(TEXT("Base dir: %s"), *EntryPtr->BaseDir));

	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

// ============================================================================
// Markdown renderer
// ============================================================================

TSharedRef<SWidget> SPluginDetailsWindow::BuildMarkdownWidget(const FString& Markdown)
{
	TSharedRef<SVerticalBox> VBox = SNew(SVerticalBox);

	TArray<FString> Lines;
	Markdown.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);

	for (FString Line : Lines)
	{
		// Strip stray inline markers the model may emit despite instructions
		Line.ReplaceInline(TEXT("**"), TEXT(""));
		Line.ReplaceInline(TEXT("__"), TEXT(""));

		if (Line.StartsWith(TEXT("## ")))
		{
			VBox->AddSlot()
			.AutoHeight()
			.Padding(0.f, 10.f, 0.f, 3.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Line.Mid(3)))
				.TextStyle(FAppStyle::Get(), "NormalText.Important")
				.AutoWrapText(true)
			];
		}
		else if (Line.StartsWith(TEXT("### ")))
		{
			VBox->AddSlot()
			.AutoHeight()
			.Padding(0.f, 7.f, 0.f, 2.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Line.Mid(4)))
				.TextStyle(FAppStyle::Get(), "NormalText.Important")
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		}
		else if (Line.StartsWith(TEXT("- ")) || Line.StartsWith(TEXT("* ")))
		{
			VBox->AddSlot()
			.AutoHeight()
			.Padding(12.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("\u2022 ") + Line.Mid(2)))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		}
		else if (Line.TrimStartAndEnd().IsEmpty())
		{
			VBox->AddSlot()
			.AutoHeight()
			.Padding(0.f, 4.f)
			[
				SNew(SBox).HeightOverride(1.f)
			];
		}
		else
		{
			VBox->AddSlot()
			.AutoHeight()
			.Padding(0.f, 1.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Line))
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		}
	}

	return VBox;
}

// ============================================================================
// URL helpers
// ============================================================================

FString SPluginDetailsWindow::GetBestURL() const
{
	if (!EntryPtr.IsValid()) return FString();
	if (!EntryPtr->MarketplaceURL.IsEmpty()) return EntryPtr->MarketplaceURL;
	if (!EntryPtr->DocsURL.IsEmpty())        return EntryPtr->DocsURL;
	if (!EntryPtr->SupportURL.IsEmpty())     return EntryPtr->SupportURL;
	if (!EntryPtr->CreatedByURL.IsEmpty())   return EntryPtr->CreatedByURL;
	return FString();
}

FText SPluginDetailsWindow::GetURLLabelText() const
{
	if (!EntryPtr.IsValid()) return FText::GetEmpty();
	if (!EntryPtr->MarketplaceURL.IsEmpty()) return LOCTEXT("URLLabelMarketplace", "Marketplace:");
	if (!EntryPtr->DocsURL.IsEmpty())        return LOCTEXT("URLLabelDocs",        "Docs:");
	if (!EntryPtr->SupportURL.IsEmpty())     return LOCTEXT("URLLabelSupport",     "Support:");
	if (!EntryPtr->CreatedByURL.IsEmpty())   return LOCTEXT("URLLabelAuthor",      "Author:");
	return FText::GetEmpty();
}

FText SPluginDetailsWindow::GetURLDisplayText() const
{
	return FText::FromString(GetBestURL());
}

EVisibility SPluginDetailsWindow::GetURLVisibility() const
{
	return (EntryPtr.IsValid() && !GetBestURL().IsEmpty())
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
