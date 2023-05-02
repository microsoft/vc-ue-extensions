# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

<#
.SYNOPSIS
    Builds and installs the VisualStudioTools plugin for Unreal Engine.

.DESCRIPTION
    Build-Plugin.ps1 will build the plugin from source using the specified Engine and copy the binaries to install it.
    It can be installed at both the Engine and Game Project levels.
    Note that Engine plugins require a source build of UE. See 'UE Plugins' under 'Related Links' for details.

.EXAMPLE
PS> Build-Plugin.ps1 -Engine "C:\Program Files\Epic Games\UE_5.0\Engine" -Project "$Env:UserProfile\Projects\EmptyProject\EmptyProject.uproject"
Build and install the plugin in the game project called 'EmptyProject', using the default install path for Unreal Engine 5.

.EXAMPLE
PS> Build-Plugin.ps1 -Engine "C:\dev\UnrealEngine\Engine"
Build and install as an engine plugin using a source build of UE.

.EXAMPLE
PS> Build-Plugin.ps1 -EngineVersion 5.0 -Project "$Env:UserProfile\Projects\EmptyProject\EmptyProject.uproject"
Build and install the plugin for the project, using an intalled build with the version 5.0.

.LINK
UE Plugins: https://docs.unrealengine.com/5.0/en-US/plugins-in-unreal-engine/
#>

param(
    [Parameter(Mandatory=$true, ParameterSetName='EnginePath')]
    [string]
    # The path to the Unreal Engine to use. 
    # For example, the default installation for UE5 is usually at "C:\Program Files\Epic Games\UE_5.0\Engine".
    # This parameter works for installed engines and source builds.
    $Engine,

    [Parameter(Mandatory=$true, ParameterSetName='InstalledVersion')]
    [string]
    # Use a installed version of the engine. Must be a string in the "Major.Minor" version format (e.g, 4.27 or 5.0).
    # This will probe the settings in the Registry to find where the engine is installed.
    # This parameter works for ONLY for installed engines.
    $InstalledVersion,

    [Parameter(Mandatory=$false)]
    [string]
    # The path to the `.uproject` descriptor of the game project to install.
    # If the parameter is omitted, the plugin will be installed at the engine level.
    # If specified, works for both installed engines and source builds. If omitted, it REQUIRES a source build of the engine.
    $Project="",

    [Parameter(Mandatory=$false)]
    [string]
    # The plugin descriptor file to compile. Defaults to using a file with the `uplugin` extension in `$pwd.
    $PluginFile="*.uplugin",

    [Parameter(Mandatory=$false)]
    [string]
    [validateset('Yes', 'No', 'Force')]
    # Whether to copy the binaries to the target project or engine.
    # Defaults to 'Yes', which does not overwrites existing content. 'Force' will ovewrite an existing folder.
    $CopyToTarget='Yes',

    [Parameter(Mandatory=$false)]
    [string]
    # The temp folder to save the binaries of the plugin package.
    # Will be moved to the target project or engine, unless `-CopyToTarget No` is passed.
    $PackagePath=""
)

function Get-UnrealEngine
{
    param(
        [Parameter(Mandatory=$true)]
        [string]
        $Version
    )

    $InstalledDirectory = Get-ItemPropertyValue "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Version" -Name "InstalledDirectory"

    Join-Path -Path $InstalledDirectory -ChildPath 'Engine'
}

$EnginePath = switch ($PSCmdlet.ParameterSetName) {
    'EnginePath' { $Engine }
    'InstalledVersion' { Get-UnrealEngine $InstalledVersion }
}

if ($PackagePath -eq "")
{
    $PackagePath = [IO.Path]::GetFullPath("bin", $pwd)
}

$plugin = get-item $PluginFile
$buildPath = [IO.Path]::Combine($PackagePath, $plugin.BaseName)
$uat = Join-Path -Path $EnginePath -ChildPath 'Build\BatchFiles\RunUAT.bat'

# Execute the UAT script
& $uat BuildPlugin -Plugin="$plugin" -TargetPlatforms=Win64 -Package="$buildPath"

$pluginsPath = switch ($Project -eq "") {
    $True { Join-Path $EnginePath -ChildPath 'Plugins' $plugin.BaseName }
    $False { Join-Path -Path (Get-ChildItem -Path $Project -File).DirectoryName -ChildPath "Plugins" $plugin.BaseName}
}

switch ($CopyToTarget) {
    'Yes' {
        if (Test-Path $pluginsPath -PathType Container) {
            throw "Destination plugin folder already exisits. Use `-CopyToTarget Force` if you intend to overwrite it."
        }

        Move-Item -Path $buildPath -Destination $pluginsPath -Verbose
    }
    'Force' {
        Move-Item -Path $buildPath -Destination $pluginsPath -Force -Verbose
    }
    'No' {
        # noop
    } 
}

