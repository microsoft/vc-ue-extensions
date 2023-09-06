# Contributing

This project welcomes contributions and suggestions.  Most contributions require you to agree to a
Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us
the rights to use your contribution. For details, visit <https://cla.opensource.microsoft.com>.

When you submit a pull request, a CLA bot will automatically determine whether you need to provide
a CLA and decorate the PR appropriately (e.g., status check, comment). Simply follow the instructions
provided by the bot. You will only need to do this once across all repos using our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or
contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Code Style Guide

The code in the repo follows the existing code conventions described in the Unreal Engine's [Code Standard document](https://docs.unrealengine.com/INT/epic-cplusplus-coding-standard-for-unreal-engine/). The `.editorconfig` file at the source root is used for Visual Studio to check the conventions and report violations.

## Pull Requests

When submitting a pull request, make sure that it has a clean build using the instructions below. A core contributor will review your pull request and provide feedback. Once all the feedback is addressed and the PR is approved, we will merge the changes.

## Build workflow
The plugin source can be built in isolation using the command below (which wrap the RunUAT.bat script) to ensure it's correct for submition to the Unreal Engine Marketplace.

From a Visual Studio Developer Prompt (or PowerShell Dev Prompt), run the following:

```cmd
> msbuild -p:UnrealEngine=[path_or_version] -p:OutputPath=[absolute_path]
``````

- `UnrealEngine` can be either a path to a source build (e.g. `c:\src\ue`) or a version identifier for an installed engine (e.g. `4.27`, `5.2`).
- `OutputPath` cannot be under the Unreal Engine's folder due to a restriction from `RunUAT.bat`.

> Note: The contents of `OutputPath` will be overwritten!

By default the script will disable Unity Builds in the plugin modules to catch errors from cpp files not including all the required headers. It does not affect the build of other targets and modules.

## Unity build errors

If you get errors due to unity build problems, you get the same errors in Visual Studio by generating the solution with the command below. This will allow Visual Studio to suggest the includes as code fixes. Note that this will overwrite any existing solution and projects that are already present.

```powershell
$env:VSTUE_IsCustomDevBuild=1; & "C:\Program Files\Epic Games\UE_5.2\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="full_path_to_game.uproject" -game
```

The module rules for the plugin check the enviroment variable above to use the more strict include settings.


