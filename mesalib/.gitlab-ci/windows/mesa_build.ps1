# force the CA cert cache to be rebuilt, in case Meson tries to access anything
Write-Host "Refreshing Windows TLS CA cache"
(New-Object System.Net.WebClient).DownloadString("https://github.com") >$null

Get-Date
Write-Host "Compiling Mesa"
$builddir = New-Item -ItemType Directory -Name "_build"
$installdir = New-Item -ItemType Directory -Name "_install"
Push-Location $builddir.FullName
cmd.exe /C "C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64 && meson --default-library=shared -Dzlib:default_library=static --buildtype=release -Db_ndebug=false -Db_vscrt=mt --cmake-prefix-path=`"C:\llvm-10`" --pkg-config-path=`"C:\llvm-10\lib\pkgconfig;C:\llvm-10\share\pkgconfig;C:\spirv-tools\lib\pkgconfig`" --prefix=`"$installdir`" -Dllvm=enabled -Dshared-llvm=disabled -Dgallium-drivers=swrast,d3d12 -Dmicrosoft-clc=enabled -Dstatic-libclc=all -Dbuild-tests=true && ninja -j32 install test"
$buildstatus = $?
Pop-Location

Get-Date

if (!$buildstatus) {
  Write-Host "Mesa build or test failed"
  Exit 1
}

Copy-Item ".\.gitlab-ci\windows\piglit_run.ps1" -Destination $installdir
Copy-Item ".\.gitlab-ci\windows\quick_gl.txt" -Destination $installdir
