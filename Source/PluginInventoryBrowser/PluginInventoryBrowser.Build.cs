// Copyright StateOfRuin, 2026. All Rights Reserved.

using UnrealBuildTool;

public class PluginInventoryBrowser : ModuleRules
{
	public PluginInventoryBrowser(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"WorkspaceMenuStructure",
			"Projects",            // IPluginManager, IPlugin, FPluginDescriptor
			"DirectoryWatcher",   // IDirectoryWatcher for live reload
			"ToolMenus",          // Window menu entry
			"EditorStyle",        // FEditorStyle (legacy), FAppStyle
			"ApplicationCore",
			"DesktopPlatform",    // Save file dialogs for export
			"Json",
			"JsonUtilities",
		});
	}
}
