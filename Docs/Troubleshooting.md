# Troubleshooting guide

This document describes some of the errors that might happen in the integration with the Unreal Engine and potential ways to mitigate them.

The integration works by Visual Studio being able to invoke the `VisualStudioTools` plugin using the Unreal Editor executable in commandlet mode. That means the following must be true:

- The `Visual Studio Tools for Unreal Engine` component from Visual Studio must be installed. You can find it under the "Game Development with C++" workload in the VS Installer.
- The game project must be built in a Editor target (e.g., `"Development_Editor"`).
- The `VisualStudioTools` plugin must be enabled for the project, either explicitly in the .uproject descriptor file or be enabled by default to all projects if installed at the as an engine plugin (via the `"EnabledByDefault=true"` entry in the .uplugin file).
- Starting on version 17.5, Visual Studio will wait to scan the game project until a file with the `UCLASS/UPROPERTY/UFUNCTION` macros is opened and the Code Lens hints are requested.
- At the moment, the Code Lens hints will only be displayed for game projects. In particular, the files in the _engine_ project of the solution (with name like "UE4" or "UE5") will not display the hints yet.

## Code Lens are not visible

### Verify the required VS component is installed

In recent versions of UE, the generated solution comes with a `.vsconfig` file, which allows right-clicking on the Solution in VS and selecting "Install Missing Feature(s)". The component is part of the "Game Development with C++" workload.

You can also see this [help page](https://learn.microsoft.com/en-us/visualstudio/install/install-visual-studio?view=vs-2022#step-4---choose-workloads) about installing features using the Visual Studio installer.

### Check if the opened documents have any class decorated with the Unreal macros

For real world projects, scanning the blueprints information might take several seconds and be expensive in terms of machine resources. Visual Studio will only start the operation when the Code Lens are rendered. That means it will wait until a file from the game project with the Unreal macros is opened in the editor.

### Check if a `cpp.hint` file is redefining the relevant Unreal macros

Some projects might have a cpp.hint file that includes the `UCLASS`, `UPROPERTY`, `UFUNCTION` macros. That might suppress the new logic in Visual Studio that uses the macros to display the Code Lens hints.

If that is the case, you can remove those macros from the hint file, save it and try reloading the project.

Note that other macros in the hint file can be left as-is and do not affect the Code Lens hints.

### Ensure the C++ Database is enabled

In Tools > Options > Text Editor > C/C++ > Advanced > Browsing/Navigation, the setting "Disable Database" should be set to "False". This is the default value of this setting.

## Errors showing up in the Output Window and/or Task Center notification

### Message "LogInit: Error: VisualStudioToolsCommandlet looked like a commandlet, but we could not find the class."

Possible causes are the plugin not being installed correctly or installed but not yet enabled for the game project (which is required on installation from the Marketplace).

- See [this section](../README.md#building-and-installing) for installation instructions.

- See [this section](../README.md#optional-enabling-the-plugin) for instructions on how to enable the plugin.

### Message "Command finished with exit code 1" without other errors

Either the game project or the plugin DLL is not yet built. Rebuilding the project should ensure they are available. Then manually rescan the game project using the `Project > Rescan UE Blueprints` menu.

### Task Center error: "Your task failed with the message: Could not find a part of the path...'

This was a known issue when trying to locate the path the Unreal Editor executable, fixed in Visual Studio 17.5-Preview3. This usually happens when the selected Configuration in VS is not one with an "Editor" target.

A workaround is to switch to such configuration and manually rescan the game project using the `Project > Rescan UE Blueprints` menu.
