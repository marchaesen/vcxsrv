# Download new TLS certs from Windows Update
Get-Date
Write-Host "Updating TLS certificate store"
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue "_tlscerts" | Out-Null
$certdir = (New-Item -ItemType Directory -Name "_tlscerts")
certutil -syncwithWU "$certdir"
Foreach ($file in (Get-ChildItem -Path "$certdir\*" -Include "*.crt")) {
  Import-Certificate -FilePath $file -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
}
Remove-Item -Recurse -Path $certdir


Get-Date
Write-Host "Installing Chocolatey"
Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
Import-Module "$env:ProgramData\chocolatey\helpers\chocolateyProfile.psm1"
Update-SessionEnvironment
Write-Host "Installing Chocolatey packages"

# Chocolatey tries to download winflexbison from SourceForge, which is not super reliable, and has no retry
# loop of its own - so we give it a helping hand here
For ($i = 0; $i -lt 5; $i++) {
  choco install --no-progress -y python3 --params="/InstallDir:C:\python3"
  $python_install = $?
  choco install --allow-empty-checksums --no-progress -y cmake git git-lfs ninja pkgconfiglite winflexbison --installargs "ADD_CMAKE_TO_PATH=System"
  $other_install = $?
  $choco_installed = $other_install -and $python_install
  if ($choco_installed) {
    Break
  }
}

if (!$choco_installed) {
  Write-Host "Couldn't install dependencies from Chocolatey"
  Exit 1
}

# Add Chocolatey's native install path
Update-SessionEnvironment
# Python and CMake add themselves to the system environment path, which doesn't get refreshed
# until we start a new shell
$env:PATH = "C:\python3;C:\python3\scripts;C:\Program Files\CMake\bin;$env:PATH"

Start-Process -NoNewWindow -Wait git -ArgumentList 'config --global core.autocrlf false'

Get-Date
Write-Host "Installing Meson, Mako and numpy"
pip3 install meson mako numpy --progress-bar off
if (!$?) {
  Write-Host "Failed to install dependencies from pip"
  Exit 1
}

Get-Date
Write-Host "Downloading Vulkan-SDK"
Invoke-WebRequest -Uri "https://sdk.lunarg.com/sdk/download/$env:VULKAN_SDK_VERSION/windows/VulkanSDK-$env:VULKAN_SDK_VERSION-Installer.exe" -OutFile 'C:\vulkan_sdk.exe'
C:\vulkan_sdk.exe --am --al -c in
if (!$?) {
    Write-Host "Failed to install Vulkan SDK"
    Exit 1
}
Remove-Item C:\vulkan_sdk.exe -Force

Get-Date
Write-Host "Downloading Vulkan-Runtime"
Invoke-WebRequest -Uri "https://sdk.lunarg.com/sdk/download/$env:VULKAN_SDK_VERSION/windows/VulkanRT-$env:VULKAN_SDK_VERSION-Installer.exe" -OutFile 'C:\vulkan-runtime.exe' | Out-Null
Write-Host "Installing Vulkan-Runtime"
Start-Process -NoNewWindow -Wait C:\vulkan-runtime.exe -ArgumentList '/S'
if (!$?) {
  Write-Host "Failed to install Vulkan-Runtime"
  Exit 1
}
Remove-Item C:\vulkan-runtime.exe -Force

Get-Date
Write-Host "Installing graphics tools (DirectX debug layer)"
Set-Service -Name wuauserv -StartupType Manual
if (!$?) {
  Write-Host "Failed to enable Windows Update"
  Exit 1
}

For ($i = 0; $i -lt 5; $i++) {
  Dism /online /quiet /add-capability /capabilityname:Tools.Graphics.DirectX~~~~0.0.1.0
  $graphics_tools_installed = $?
  if ($graphics_tools_installed) {
    Break
  }
}

if (!$graphics_tools_installed) {
  Write-Host "Failed to install graphics tools"
  Get-Content C:\Windows\Logs\DISM\dism.log
  Exit 1
}
