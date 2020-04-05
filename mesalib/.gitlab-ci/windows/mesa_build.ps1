# force the CA cert cache to be rebuilt, in case Meson tries to access anything
Write-Host "Refreshing Windows TLS CA cache"
(New-Object System.Net.WebClient).DownloadString("https://github.com") >$null

Get-Date
Write-Host "Compiling Mesa"
$builddir = New-Item -ItemType Directory -Name "build"
Push-Location $builddir.FullName
cmd.exe /C "C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64 && meson -Dgallium-drivers=swrast -Dbuild-tests=true .. && ninja test"
$buildstatus = $?
Pop-Location
Remove-Item -Recurse -Path $builddir

Get-Date

if (!$buildstatus) {
  Write-Host "Mesa build or test failed"
  Exit 1
}
