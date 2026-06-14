# Setup dependencies for grass-field-002 project
# Downloads ImGui source to subprojects/grass-field-002/third_party/

param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptRoot

$imgui_dir = "$projectRoot\projects\grannys-house-trials\subprojects\grass-field-002\third_party\imgui"

if ((Test-Path $imgui_dir) -and -not $Force) {
    Write-Host "ImGui already exists at $imgui_dir"
    Write-Host "Use -Force to re-download"
    exit 0
}

if (Test-Path $imgui_dir) {
    Write-Host "Removing existing ImGui directory..."
    Remove-Item -Recurse -Force $imgui_dir
}

Write-Host "Downloading ImGui v1.90.1..."
$tmpDir = [System.IO.Path]::GetTempPath()
$zipPath = "$tmpDir\imgui.zip"
$extractPath = "$tmpDir\imgui-extract"

# Download ImGui release
$imgui_url = "https://github.com/ocornut/imgui/archive/refs/tags/v1.90.1.zip"
Invoke-WebRequest -Uri $imgui_url -OutFile $zipPath

Write-Host "Extracting..."
Expand-Archive -Path $zipPath -DestinationPath $extractPath -Force

# Move to correct location
New-Item -ItemType Directory -Path "$projectRoot\projects\grannys-house-trials\subprojects\grass-field-002\third_party" -Force | Out-Null
Move-Item -Path "$extractPath\imgui-1.90.1" -Destination $imgui_dir -Force

# Keep only runtime files needed by this project and drop large example/docs trees
$prunePaths = @(
    "$imgui_dir\.github",
    "$imgui_dir\docs",
    "$imgui_dir\examples",
    "$imgui_dir\misc",
    "$imgui_dir\.editorconfig",
    "$imgui_dir\.gitattributes",
    "$imgui_dir\.gitignore"
)

foreach ($path in $prunePaths) {
    if (Test-Path $path) {
        Remove-Item -Recurse -Force $path
    }
}

# Cleanup
Remove-Item -Path $zipPath -Force
Remove-Item -Path $extractPath -Recurse -Force

Write-Host "ImGui installed successfully to:"
Write-Host "  $imgui_dir"
