# Helper Scripts 

This folder contains a few scripts intended to use when testing the plugin against different projects. 
It's useful for cases where one wants to test changes in projects targetting different UE versions.
Instead of having the plugin in the project/engine source, it uses UE's `RunUAT` tool to build the plugin 
and copy the results to a target project or engine installation.

## Building and installing

You can build and install the plugin using the `Build-Plugin.ps1` script in the `Scripts` folder.

The script has 4 parameters:

1. `Engine`: the path to the Unreal Engine to use.
   - For example, the default installation for UE5 is usually at `C:\Program Files\Epic Games\UE_5.0\Engine`.
   - This parameter works for installed engines and source builds
2. `InstalledVersion`: a version number of an intalled build of the engine, e.g., "4.27", "5.0" or "5.1"
   - This will probe the information from the Registry under the `HKLM:\SOFTWARE\EpicGames\Unreal Engine` path.
3. `Project`: the path to the `.uproject` descriptor of the game project to install.
   - If the parameter is omitted, the plugin will be installed at the engine level.
   - Note that the engine plugin requires a source build of the engine. See this [link](https://docs.unrealengine.com/5.0/en-US/building-unreal-engine-from-source/) for details.
4. `CopyToTarget`: wheter to copy the resuling files to a project/engine `Plugins` folder.
   - Defaults to `Yes`. This option will not overwrite the destination folder if it already exists.
   - Use `Force` to overwrite the destination.

The `Engine` and `InstalledVersion` parameters are mutually exclusive.

By default, the script will copy binaries to a `Plugins\VisualStudioTools` folder of either the game project or engine directories. 

> If building for Unreal Engine 4.x, you can first run the script `Patch-Descriptor.ps1` to update the `.uplugin` file to the schema used by that version.

### Examples

- Use the Unreal Engine 5.0 installed in the default location, and target a project called `EmptyProject`.

```powershell
./Build-Plugin.ps1 -Engine "C:\Program Files\Epic Games\UE_5.0\Engine" -Project "$Env:UserProfile\Unreal Projects\EmptyProject\EmptyProject.uproject"
```

- Build and install as an engine plugin using a source build of UE.

```powershell
./Build-Plugin.ps1 -Engine "C:\dev\UnrealEngine\Engine"
```

- Build and install the plugin for the project, using an intalled build with the version 5.0.

```powershell
./Build-Plugin.ps1 -EngineVersion 5.0 -Project "$Env:UserProfile\Unreal Projects\EmptyProject\EmptyProject.uproject"
```

To see more usage examples, run the following command in PowerShell: `get-help build-plugin.ps1 -detailed`.
