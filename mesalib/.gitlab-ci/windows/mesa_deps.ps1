Get-Date
Write-Host "Installing Chocolatey"
Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))
Import-Module "$env:ProgramData\chocolatey\helpers\chocolateyProfile.psm1"
Update-SessionEnvironment
Write-Host "Installing Chocolatey packages"
choco install --allow-empty-checksums -y cmake --installargs "ADD_CMAKE_TO_PATH=System"
choco install --allow-empty-checksums -y python3 git git-lfs ninja pkgconfiglite winflexbison
Update-SessionEnvironment

Start-Process -NoNewWindow -Wait git -ArgumentList 'config --global core.autocrlf false'

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
Get-Item C:\BuildTools | Out-Host

Get-Date
Write-Host "Installing Meson"
Start-Process -NoNewWindow -Wait pip3 -ArgumentList 'install meson'

Write-Host "Installing Mako"
Start-Process -NoNewWindow -Wait pip3 -ArgumentList 'install mako'

Get-Date
Write-Host "Complete"
