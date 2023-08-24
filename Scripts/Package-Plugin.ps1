param(
   [Parameter(Mandatory=$true)]
   [string]
   $EnginePath,
   [Parameter(Mandatory=$true)]
   [string]
   $EngineVersion
)

function New-TemporaryDirectory {
    $parent = [System.IO.Path]::GetTempPath()
    $name = [System.IO.Path]::GetRandomFileName()
    New-Item -ItemType Directory -Path (Join-Path $parent $name)
}

$PackagePath = New-TemporaryDirectory
& msbuild "-p:UnrealEngine=$EnginePath;OutputPath=$PackagePath;Versioned=true"

# Add EnabledByDefault property in the descriptor file
Write-Host "Patch plugin descriptor file"
$descriptor = "$PackagePath/VisualStudioTools.uplugin"
$a = Get-Content $descriptor | ConvertFrom-Json
$a | Add-Member -NotePropertyName EnabledByDefault -NotePropertyValue $true -ErrorAction Ignore
$a | ConvertTo-Json -depth 100 | Out-File $descriptor -Encoding utf8

Write-Host "Copy Config folder"
Copy-Item -Path Config -Destination $PackagePath/Config -Recurse

$PublishPath = "publish"
If(!(test-path -PathType Container $PublishPath))
{
      New-Item -ItemType Directory -Path $PublishPath | Out-Null
}

Write-Host "Create ZIP package"
$tag = $EngineVersion.Replace(".", "")
$files = Get-ChildItem $PackagePath -Exclude @("Binaries", "Intermediate")
$zip = "$PublishPath/VisualStudioTools_v$($a.VersionName)_ue$tag.zip"
Compress-Archive -Path $files -DestinationPath "$PublishPath/VisualStudioTools_v$($a.VersionName)_ue$tag.zip" -CompressionLevel Fastest

Remove-Item $PackagePath -Force -Recurse

Write-Host "Done: $($zip | Resolve-Path)"