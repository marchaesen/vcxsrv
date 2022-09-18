# we want more secure TLS 1.2 for most things
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;

# VS16.x is 2019
$msvc_2019_url = 'https://aka.ms/vs/16/release/vs_buildtools.exe'

Get-Date
Write-Host "Downloading Visual Studio 2019 build tools"
Invoke-WebRequest -Uri $msvc_2019_url -OutFile C:\vs_buildtools.exe

Get-Date
Write-Host "Installing Visual Studio 2019"
# Command line
# https://docs.microsoft.com/en-us/visualstudio/install/command-line-parameter-examples?view=vs-2019
# Component ids
# https://docs.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=vs-2019
# https://docs.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-community?view=vs-2019
Start-Process -NoNewWindow -Wait -FilePath C:\vs_buildtools.exe `
-ArgumentList `
"--wait", `
"--quiet", `
"--norestart", `
"--nocache", `
"--installPath", "C:\BuildTools", `
"--add", "Microsoft.VisualStudio.Component.VC.ASAN", `
"--add", "Microsoft.VisualStudio.Component.VC.Redist.14.Latest", `
"--add", "Microsoft.VisualStudio.Component.VC.ATL", `
"--add", "Microsoft.VisualStudio.Component.VC.ATLMFC", `
"--add", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", `
"--add", "Microsoft.VisualStudio.Component.Graphics.Tools", `
"--add", "Microsoft.VisualStudio.Component.Windows10SDK.20348"

if (!$?) {
  Write-Host "Failed to install Visual Studio tools"
  Exit 1
}
Remove-Item C:\vs_buildtools.exe -Force
Get-Date
