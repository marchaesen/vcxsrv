Get-Date
Write-Host "Installing Chocolatey"
Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
Import-Module "$env:ProgramData\chocolatey\helpers\chocolateyProfile.psm1"
Update-SessionEnvironment
Write-Host "Installing Chocolatey packages"

# Chocolatey tries to download winflexbison from SourceForge, which is not super reliable, and has no retry
# loop of its own - so we give it a helping hand here
For ($i = 0; $i -lt 5; $i++) {
  choco install -y python3 --params="/InstallDir:C:\python3"
  $python_install = $?
  choco install --allow-empty-checksums -y cmake git git-lfs ninja pkgconfiglite winflexbison
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
# Python adds itself to the system environment path, which doesn't get refreshed until we start a new shell
$env:PATH = "C:\python3;C:\python3\scripts;$env:PATH"

Start-Process -NoNewWindow -Wait git -ArgumentList 'config --global core.autocrlf false'

Get-Date
Write-Host "Installing Meson and Mako"
pip3 install meson mako
if (!$?) {
  Write-Host "Failed to install dependencies from pip"
  Exit 1
}

# we want more secure TLS 1.2 for most things, but it breaks SourceForge
# downloads so must be done after Chocolatey use
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

# VS16.x is 2019
$msvc_2019_url = 'https://aka.ms/vs/16/release/vs_buildtools.exe'

Get-Date
Write-Host "Downloading Visual Studio 2019 build tools"
Invoke-WebRequest -Uri $msvc_2019_url -OutFile C:\vs_buildtools.exe

Get-Date
Write-Host "Installing Visual Studio 2019"
Start-Process -NoNewWindow -Wait C:\vs_buildtools.exe -ArgumentList '--wait --quiet --norestart --nocache --installPath C:\BuildTools --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Workload.NativeDesktop --add Microsoft.VisualStudio.Component.VC.ATL --add Microsoft.VisualStudio.Component.VC.ATLMFC --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Graphics.Tools --add Microsoft.VisualStudio.Component.Windows10SDK.18362 --includeRecommended'
Remove-Item C:\vs_buildtools.exe -Force

Get-Date
Write-Host "Complete"
