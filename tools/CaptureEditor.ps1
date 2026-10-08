param([string]$Executable = "$PSScriptRoot/../build/windows/Debug/SoulsEditor.exe",[string]$Output = "$PSScriptRoot/../build/editor-playground.png")
# Capture the engine's swapchain through the RHI, independent of desktop occlusion.
$captureBmp = [IO.Path]::GetFullPath("$PSScriptRoot/../build/editor-playground.bmp")
& $Executable --validation --smoke 120 --capture $captureBmp
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Add-Type -AssemblyName System.Drawing
$image = [System.Drawing.Image]::FromFile($captureBmp)
$image.Save([IO.Path]::GetFullPath($Output),[System.Drawing.Imaging.ImageFormat]::Png)
$image.Dispose()
