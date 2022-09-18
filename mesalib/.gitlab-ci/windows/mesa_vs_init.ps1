$vsInstallPath=& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -version 16.0  -property installationpath
Write-Output "vswhere.exe installPath: $vsInstallPath"
$vsInstallPath =  if ("$vsInstallPath" -eq "" ) { "C:\BuildTools" } else { "$vsInstallPath" }
Write-Output "Final installPath: $vsInstallPath"
Import-Module (Join-Path $vsInstallPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
# https://en.wikipedia.org/wiki/Microsoft_Visual_C%2B%2B
# VS2015 14.0
# VS2017 14.16
# VS2019 14.29
# VS2022 14.32
Enter-VsDevShell -VsInstallPath $vsInstallPath -SkipAutomaticLocation -DevCmdArguments '-vcvars_ver=14.29 -arch=x64 -no_logo -host_arch=amd64'
