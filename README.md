# Plugin Inventory Browser

Plugin Inventory Browser is an Unreal Engine 5.7 editor plugin for browsing discovered engine, project, enterprise, external, and mod plugins in a live tile grid.

## Requirements

- Unreal Engine 5.7.
- Editor-only use; this plugin is built as an editor module.
- No third-party UI framework is required. The plugin uses Unreal's built-in Slate and ToolMenus systems, so CommonUI is not needed.
- The plugin depends on standard Unreal modules that ship with the engine, including Slate, SlateCore, ToolMenus, Projects, DirectoryWatcher, DesktopPlatform, Json, and JsonUtilities.

## Features

- Search, filter, and sort the full plugin inventory.
- View plugin metadata such as source, category, enabled state, module count, and dependency count.
- Export the currently visible inventory to JSON or CSV from the toolbar.
- Refresh automatically when plugin directories change.

## Notes

- The plugin is built as an editor module.
- Export files are written from the browser's current filtered view so any active filters and sort order are preserved in the output.
