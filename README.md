# Unreal Engine extensions for Visual Studio

This project contains an Unreal Editor plug-in used by Visual Studio to display information about Blueprints assets in the C++ code.

It can be installed in the Engine or Game project sources and is automatically invoked by VS when opening an Unreal Engine C++ project.

## Prerequisites

1. Visual Studio 2022, with the "IDE features for Unreal Engine" component installed in the Gaming Workload.
2. Unreal Engine, either installed or built from source.
   1. See the following link for instructions on how to install or build the Engine: [Installing Unreal Engine](https://docs.unrealengine.com/5.0/en-US/installing-unreal-engine).
   1. We have tested the source and instructions below using Unreal Engine versions 4.27, 5.0 and 5.1.

## Building and installing

> Have an engine installed with the Epic Game Launcher and just want to use the plugin? You can skip the steps bellow and install from the Epic Marketplace using this link: https://aka.ms/vsueplugin.

The simplest way to use the plugin is to clone this repo under the `Plugins` folder of your game project or the engine source.
If you have multiple projects in the same Visual Studio solution, it's recommended to use install the plugin at the engine level, and share the binaries across the projects.

1. Clone the repo under the project plugin folder. 

```powershell
cd <Project or Engine root folder>/Plugins
git clone <repo url>
```

2. Optional: Regenerate the Solution for your game project, so the plugin source will be visible in Visual Studio.
3. Rebuild the game project, which will build the plugin.

After that, Visual Studio should detect the plugin when opening a solution or project and start processing the blueprints in the game.

You can also force VS to invoke the commandlet in the plugin using the option `Rescan UE Blueprints for <Game Project Name>` in the `Project` menu.

### Cloning outside of engine or project sources

If you want to keep the plugin repo outside of the engine/project sources (e.g., share it for multiple engines) check [these instructions](Scripts/README.md) for some tools that help building and installing the plugin in those cases.

## Optional: Enabling the plugin

The plugin descriptor comes with `"EnabledByDefault = true"` set, so it should work without having to enable it for every game project. 
If that does not work (e.g. UE is not building the plugin when building the project), you can enabe the plugin explicitly with one of the following options.

1. Using the plugin manager in the Unreal Editor and selecting `VisualStudioTools`.
2. Manually editing the `.uproject` descriptor for the game project and add an entry for the plugin.

## Manually invoking the plugin

The plugin is intended to be used by Visual Studio, so it does not add any UI, commands, or logs to the Unreal Editor. 
It still possible to test it's execution, but running the following command:

The command bellow will run the plugin for the specified project and save the Unreal Engine blueprints information in the output file. 
The optional params help to run the command faster.

```powershell
& "C:\Program Files\Epic Games\UE_5.0\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Env:UserProfile\Unreal Projects\EmptyProject\EmptyProject.uproject" -run=VisualStudioTools -output "$Env:Temp\vs-ue-tools.json" [-unattended -noshadercompile -nosound -nullrhi -nocpuprofilertrace -nocrashreports -nosplash]
```

For more information about the command line parameters of the commandlet, run it with the `-help` switch.

```powershell
& "<Editor-Cmd.exe>" "<path_to_uproject>" -run=VisualStudioTools -help [-unattended -noshadercompile -nosound -nullrhi -nocpuprofilertrace -nocrashreports -nosplash]
```

> For UE4.x, the executable is named `UE4Editor-cmd.exe`, under a similar path.

## Contributing

This project welcomes contributions and suggestions.  Most contributions require you to agree to a
Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us
the rights to use your contribution. For details, visit <https://cla.opensource.microsoft.com>.

When you submit a pull request, a CLA bot will automatically determine whether you need to provide
a CLA and decorate the PR appropriately (e.g., status check, comment). Simply follow the instructions
provided by the bot. You will only need to do this once across all repos using our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or
contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

### Code Style Guide

The code in the repo follows the existing conventions for Unreal Engine code described in this [page](https://docs.unrealengine.com/5.1/en-US/epic-cplusplus-coding-standard-for-unreal-engine/).

## Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft trademarks or logos is subject to and must follow [Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general).
Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship.
Any use of third-party trademarks or logos are subject to those third-party's policies.
