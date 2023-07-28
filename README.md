# Unreal Engine plugin for Visual Studio

This project contains an Unreal Editor plugin that works in conjunction with Visual Studio to display information about Blueprints assets in C++ code. 

The plugin can be installed in either the Engine or Game project sources, and it is automatically activated when an Unreal Engine project is opened in Visual Studio.

## Requirements

Before you begin, please make sure you have the following software and tools set up:

1. Visual Studio 2022 has the "IDE features for Unreal Engine" component installed.
   1. The component can be found in the "Game development with C++" workload or as an individual component.
2. Unreal Engine, either installed or built from source.
   1. To learn how to install or build Unreal Engine, please refer to the following guide: [Installing Unreal Engine](https://docs.unrealengine.com/5.0/en-US/installing-unreal-engine).
   1. The source code and instructions have been tested on Unreal Engine versions 4.27, 5.0 and 5.1.

## Building and Installing the Plugin

> If you have Unreal Engine installed and set up through the Epic Games Launcher, and you only want to use the plugin, you can skip the steps below and install it directly from the [Unreal Engine Marketplace](https://aka.ms/vsueplugin).

The most straightforward way to use the plugin is to clone the repo under the `Plugins` folder of your game project or engine source. If you have multiple projects in the same Visual Studio solution, it is recommended to install the plugin at the engine level and share the binaries across the projects.

1. Clone the repo under the project plugin folder by using the following commands:

   ```powershell
   cd <Project or Engine root folder>/Plugins
   git clone https://github.com/microsoft/vc-ue-extensions.git
   ```

2. Optional: Regenerate the Solution for your game project so that the plugin source will be visible in Visual Studio.
3. Rebuild the game project, which will also build the plugin.

After completing these steps, Visual Studio should automatically recognize the plugin when you open a solution or project, and it will start processing Blueprints in your game.

You can also use the option `Rescan UE Blueprints for <Game Project Name>` in the `Project` menu to manually force Visual Studio to invoke the the plugin.

### Cloning outside of engine or project sources

If you prefer to have the plugin's repository located separately from the engine or project sources (for example, if you want to share it between multiple engines), you can follow the instructions provided in the file [Scripts/README.md](Scripts/README.md) to learn how to build and install the plugin in such a scenario.

## Enabling the Plugin (Optional)

By default, the plugin descriptor is already set with `"EnabledByDefault = true"`, so it should function automatically without any additional steps. However, if you encounter difficulties with Unreal Engine building the plugin (e.g., UE fails to build the plugin when building the project), you can enable the plugin explicitly by using one of the following methods:

1. Navigate to the plugin manager in the Unreal Editor and select `VisualStudioTools`.
2. Manually edit the game project's `.uproject` descriptor file by adding an entry for the plugin.

In either case, the end result should be a new entry in the `Plugins` array in the JSON file, as shown below:

```JSON
{
 "FileVersion": 3,
 "Category": "...",
 "Description": "...",
 "Modules": ["..."],
 "Plugins": [
  {
   "Name": "VisualStudioTools",
   "Enabled": true,
  }
 ]
}
```
>Note: To ensure proper activation of the plugin, make sure the correct plugin is selected or the desired changes are made in the `.uproject` file.

## Manually invoking the plugin

The plugin is designed to be used with Visual Studio, and as such, it does not provide any user interfaces, commands, or logs within the Unreal Editor. However, it is still possible to test the plugin's execution by running the **sample** command below: 

```powershell
& "<AbsolutePathToEngine>\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Env:UserProfile\Unreal Projects\EmptyProject\EmptyProject.uproject" -run=VisualStudioTools -output "$Env:Temp\vs-ue-tools.json" [-unattended -noshadercompile -nosound -nullrhi -nocpuprofilertrace -nocrashreports -nosplash]
```

This command will run the plugin for the specified project and save Unreal Engine Blueprint information in the output file. Optional parameters are included to run the command faster.

For more information on the specific command line parameters, you can run the following command in the powershell prompt with `-help`:

```powershell
& "<Editor-Cmd.exe>" "<path_to_uproject>" -run=VisualStudioTools -help [-unattended -noshadercompile -nosound -nullrhi -nocpuprofilertrace -nocrashreports -nosplash]
```

>Note: The executable name is `UE4Editor-cmd.exe` for UE4.x, located under a similar path.

## Troubleshooting

If you encounter any issues when setting up Visual Studio in conjunction with the Unreal Editor plugin, please refer to the [Troubleshooting](https://github.com/microsoft/vc-ue-extensions/blob/main/Docs/Troubleshooting.md) guide in the repository. This guide provides solutions for common issues and is periodically updated to ensure that the latest solutions are available.

To report new issues, provide feedback, or request new features, please use the following options: [Report a Problem](https://aka.ms/feedback/cpp/unrealengine/report) and [Suggest a Feature](https://aka.ms/feedback/cpp/unrealengine/suggest). These options will allow you to submit your issue or feedback directly to our team and help us improve the plugin moving forward.

## Contributing
This project welcomes contributions and suggestions. Check out our [contributing guide](CONTRIBUTING.md) for instructions on how to contribute to the project.

## Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft trademarks or logos is subject to and must follow [Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general).
Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship.
Any use of third-party trademarks or logos are subject to those third-party's policies.