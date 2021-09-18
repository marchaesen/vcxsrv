# force the CA cert cache to be rebuilt, in case Meson tries to access anything
Write-Host "Refreshing Windows TLS CA cache"
(New-Object System.Net.WebClient).DownloadString("https://github.com") >$null

$env:PYTHONUTF8=1

Get-Date
Write-Host "Compiling Mesa"
$builddir = New-Item -ItemType Directory -Name "_build"
$installdir = New-Item -ItemType Directory -Name "_install"
Push-Location $builddir.FullName
cmd.exe /C "C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64 && meson --default-library=shared -Dzlib:default_library=static --buildtype=release -Db_ndebug=false -Dc_std=c17 -Dcpp_std=vc++latest -Db_vscrt=mt --cmake-prefix-path=`"C:\llvm-10`" --pkg-config-path=`"C:\llvm-10\lib\pkgconfig;C:\llvm-10\share\pkgconfig;C:\spirv-tools\lib\pkgconfig`" --prefix=`"$installdir`" -Dllvm=enabled -Dshared-llvm=disabled -Dvulkan-drivers=swrast,amd -Dgallium-drivers=swrast,d3d12,zink -Dosmesa=true -Dshared-glapi=enabled -Dgles2=enabled -Dmicrosoft-clc=enabled -Dstatic-libclc=all -Dspirv-to-dxil=true -Dbuild-tests=true -Dwerror=true -Dwarning_level=2 -Dzlib:warning_level=1 -Dlibelf:warning_level=1 && ninja -j32 install && meson test --num-processes 32"
$buildstatus = $?
Pop-Location

Get-Date

if (!$buildstatus) {
  Write-Host "Mesa build or test failed"
  Exit 1
}

Copy-Item ".\.gitlab-ci\windows\piglit_run.ps1" -Destination $installdir
Copy-Item ".\.gitlab-ci\windows\quick_gl.txt" -Destination $installdir
