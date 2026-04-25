// Copyright StateOfRuin, 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "PluginInventoryEntry.h"

/**
 * SPluginInventoryTile
 *
 * A single tile rendered inside the STileView inventory grid.
 * Shows: icon (128px), friendly name, source label, category, enabled badge,
 *        module/dependency counts, and a short description.
 */
class SPluginInventoryTile : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPluginInventoryTile)
		: _Entry()
	{}
		SLATE_ARGUMENT(FPluginInventoryEntryPtr, Entry)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FPluginInventoryEntryPtr EntryPtr;

	/** Dynamic image brush loaded from <PluginDir>/Resources/Icon128.png */
	TSharedPtr<FSlateBrush> IconBrush;

	// ---- Attribute helpers --------------------------------------------------
	FText  GetFriendlyName()  const;
	FText  GetCategoryText()  const;
	FText  GetSourceText()    const;
	FText  GetStatusText()    const;
	FText  GetModuleText()    const;
	FText  GetDepsText()      const;
	FText  GetDescText()      const;
	FSlateColor GetStatusColor() const;

	const FSlateBrush* GetIconBrush() const;
};
