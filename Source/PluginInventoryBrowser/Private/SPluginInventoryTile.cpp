// Copyright StateOfRuin, 2026. All Rights Reserved.

#include "SPluginInventoryTile.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "SPluginInventoryTile"

static const FLinearColor EnabledColor   = FLinearColor(0.15f, 0.80f, 0.35f, 1.f);
static const FLinearColor DisabledColor  = FLinearColor(0.60f, 0.60f, 0.60f, 1.f);
static const float TileWidth  = 280.f;
static const float TileHeight = 180.f;
static const float IconSize   = 64.f;

void SPluginInventoryTile::Construct(const FArguments& InArgs)
{
	EntryPtr = InArgs._Entry;
	if (!EntryPtr.IsValid())
	{
		return;
	}

	// Try to load plugin icon from <PluginDir>/Resources/Icon128.png
	const FString IconPath = EntryPtr->BaseDir / TEXT("Resources/Icon128.png");
	const FName   BrushKey(*IconPath);
	const FIntPoint IconPx = FSlateApplication::Get().GetRenderer()->GenerateDynamicImageResource(BrushKey);
	if (IconPx.X > 0 && IconPx.Y > 0)
	{
		IconBrush = MakeShared<FSlateDynamicImageBrush>(BrushKey, FVector2D(IconPx.X, IconPx.Y));
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.f))
		[
			SNew(SBox)
			.WidthOverride(TileWidth)
			.HeightOverride(TileHeight)
			[
				SNew(SVerticalBox)

				// ---- Row 1: icon + name + source/category badges ----------------
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					// Icon
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Top)
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SBox)
						.WidthOverride(IconSize)
						.HeightOverride(IconSize)
						[
							SNew(SImage)
							.Image(this, &SPluginInventoryTile::GetIconBrush)
						]
					]

					// Name + meta column
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Top)
					[
						SNew(SVerticalBox)

						// Friendly name
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(this, &SPluginInventoryTile::GetFriendlyName)
							.TextStyle(FAppStyle::Get(), "NormalText.Important")
							.AutoWrapText(true)
						]

						// Category | Source
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.f, 2.f, 0.f, 0.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(STextBlock)
								.Text(this, &SPluginInventoryTile::GetCategoryText)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.TextStyle(FAppStyle::Get(), "SmallText")
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(4.f, 0.f)
							[
								SNew(STextBlock)
								.Text(FText::FromString(TEXT("|")))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.TextStyle(FAppStyle::Get(), "SmallText")
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(STextBlock)
								.Text(this, &SPluginInventoryTile::GetSourceText)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.TextStyle(FAppStyle::Get(), "SmallText")
							]
						]
					]
				]

				// ---- Row 2: description -----------------------------------------
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				.Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(this, &SPluginInventoryTile::GetDescText)
					.TextStyle(FAppStyle::Get(), "SmallText")
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]

				// ---- Row 3: status + counts --------------------------------------
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)

					// Enabled badge
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(this, &SPluginInventoryTile::GetStatusText)
						.ColorAndOpacity(this, &SPluginInventoryTile::GetStatusColor)
						.TextStyle(FAppStyle::Get(), "SmallText")
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)

					// Module count
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock)
						.Text(this, &SPluginInventoryTile::GetModuleText)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.TextStyle(FAppStyle::Get(), "SmallText")
					]

					// Dependency count
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(this, &SPluginInventoryTile::GetDepsText)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.TextStyle(FAppStyle::Get(), "SmallText")
					]
				]
			]
		]
	];
}

// ---- Attribute helpers -------------------------------------------------------

FText SPluginInventoryTile::GetFriendlyName() const
{
	return EntryPtr.IsValid() ? FText::FromString(EntryPtr->FriendlyName) : FText::GetEmpty();
}

FText SPluginInventoryTile::GetCategoryText() const
{
	return EntryPtr.IsValid() ? FText::FromString(EntryPtr->Category) : FText::GetEmpty();
}

FText SPluginInventoryTile::GetSourceText() const
{
	return EntryPtr.IsValid() ? EntryPtr->GetSourceLabel() : FText::GetEmpty();
}

FText SPluginInventoryTile::GetStatusText() const
{
	return EntryPtr.IsValid() ? EntryPtr->GetStatusLabel() : FText::GetEmpty();
}

FText SPluginInventoryTile::GetModuleText() const
{
	if (!EntryPtr.IsValid()) { return FText::GetEmpty(); }
	return FText::Format(LOCTEXT("ModuleFmt", "{0} mod"), FText::AsNumber(EntryPtr->ModuleCount));
}

FText SPluginInventoryTile::GetDepsText() const
{
	if (!EntryPtr.IsValid()) { return FText::GetEmpty(); }
	return FText::Format(LOCTEXT("DepFmt", "{0} dep"), FText::AsNumber(EntryPtr->DependencyCount));
}

FText SPluginInventoryTile::GetDescText() const
{
	if (!EntryPtr.IsValid()) { return FText::GetEmpty(); }
	// Truncate very long descriptions to keep the tile tidy
	const FString& Desc = EntryPtr->Description;
	return FText::FromString(Desc.Len() > 140 ? Desc.Left(137) + TEXT("…") : Desc);
}

FSlateColor SPluginInventoryTile::GetStatusColor() const
{
	if (EntryPtr.IsValid() && EntryPtr->bIsEnabled)
	{
		return FSlateColor(EnabledColor);
	}
	return FSlateColor(DisabledColor);
}

const FSlateBrush* SPluginInventoryTile::GetIconBrush() const
{
	if (IconBrush.IsValid())
	{
		return IconBrush.Get();
	}
	return FAppStyle::Get().GetBrush("Plugins.TabIcon");
}

#undef LOCTEXT_NAMESPACE
