# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

# Replace the field in the plugin descriptor that changed between UE4 and UE5.
$pluginDescriptor = "$pwd\src\VisualStudioTools.uplugin";
(Get-Content $pluginDescriptor).Replace("PlatformAllowList", "WhitelistPlatforms") | Set-Content $pluginDescriptor
