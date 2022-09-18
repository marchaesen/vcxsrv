Get-Date
Write-Host "Downloading Freeglut"

$freeglut_zip = 'freeglut-MSVC.zip'
$freeglut_url = "https://www.transmissionzero.co.uk/files/software/development/GLUT/$freeglut_zip"

For ($i = 0; $i -lt 5; $i++) {
  Invoke-WebRequest -Uri $freeglut_url -OutFile $freeglut_zip
  $freeglut_downloaded = $?
  if ($freeglut_downloaded) {
    Break
  }
}

if (!$freeglut_downloaded) {
  Write-Host "Failed to download Freeglut"
  Exit 1
}

Get-Date
Write-Host "Installing Freeglut"
Expand-Archive $freeglut_zip -DestinationPath C:\
if (!$?) {
  Write-Host "Failed to install Freeglut"
  Exit 1
}

$MyPath = $MyInvocation.MyCommand.Path | Split-Path -Parent
. "$MyPath\mesa_vs_init.ps1"

Get-Date
Write-Host "Downloading glext.h"
New-Item -ItemType Directory -Path ".\glext" -Name "GL"
$ProgressPreference = "SilentlyContinue"
Invoke-WebRequest -Uri 'https://www.khronos.org/registry/OpenGL/api/GL/glext.h' -OutFile '.\glext\GL\glext.h' | Out-Null

Get-Date
Write-Host "Cloning Piglit"
git clone --no-progress --single-branch --no-checkout https://gitlab.freedesktop.org/mesa/piglit.git 'C:\src\piglit'
if (!$?) {
  Write-Host "Failed to clone Piglit repository"
  Exit 1
}
Push-Location -Path C:\src\piglit
git checkout f7f2a6c2275cae023a27b6cc81be3dda8c99492d
Pop-Location

Get-Date
$piglit_build = New-Item -ItemType Directory -Path "C:\src\piglit" -Name "build"
Push-Location -Path $piglit_build.FullName
Write-Host "Compiling Piglit"
cmake .. `
-GNinja `
-DCMAKE_BUILD_TYPE=Release `
-DCMAKE_INSTALL_PREFIX="C:\Piglit" `
-DGLUT_INCLUDE_DIR=C:\freeglut\include `
-DGLUT_glut_LIBRARY_RELEASE=C:\freeglut\lib\x64\freeglut.lib `
-DGLEXT_INCLUDE_DIR=.\glext && `
ninja -j32
$buildstatus = $?
ninja -j32 install | Out-Null
$installstatus = $?
Pop-Location
Remove-Item -Recurse -Path $piglit_build
if (!$buildstatus -Or !$installstatus) {
  Write-Host "Failed to compile or install Piglit"
  Exit 1
}

Copy-Item -Path C:\freeglut\bin\x64\freeglut.dll -Destination C:\Piglit\lib\piglit\bin\freeglut.dll

Get-Date
Write-Host "Cloning spirv-samples"
git clone --no-progress --single-branch --no-checkout https://github.com/dneto0/spirv-samples.git  C:\spirv-samples\
Push-Location -Path C:\spirv-samples\
git checkout 36372636df06a24c4e2de1551beee055db01b91d
Pop-Location

Get-Date
Write-Host "Cloning Vulkan and GL Conformance Tests"
$deqp_source = "C:\src\VK-GL-CTS\"
git clone --no-progress --single-branch https://github.com/lfrb/VK-GL-CTS.git -b windows-flush $deqp_source
if (!$?) {
  Write-Host "Failed to clone deqp repository"
  Exit 1
}

Push-Location -Path $deqp_source
# --insecure is due to SSL cert failures hitting sourceforge for zlib and
# libpng (sigh).  The archives get their checksums checked anyway, and git
# always goes through ssh or https.
py .\external\fetch_sources.py --insecure
Pop-Location

Get-Date
$deqp_build = New-Item -ItemType Directory -Path "C:\deqp"
Push-Location -Path $deqp_build.FullName
Write-Host "Compiling deqp"
cmake -S $($deqp_source) `
-B . `
-GNinja `
-DCMAKE_BUILD_TYPE=Release `
-DDEQP_TARGET=default && `
ninja -j32
$buildstatus = $?
Pop-Location
if (!$buildstatus -Or !$installstatus) {
  Write-Host "Failed to compile or install deqp"
  Exit 1
}

# Copy test result templates
Copy-Item -Path "$($deqp_source)\doc\testlog-stylesheet\testlog.css" -Destination $deqp_build
Copy-Item -Path "$($deqp_source)\doc\testlog-stylesheet\testlog.xsl" -Destination $deqp_build

# Copy Vulkan must-pass list
$deqp_mustpass = New-Item -ItemType Directory -Path $deqp_build -Name "mustpass"
$root_mustpass = Join-Path -Path $deqp_source -ChildPath "external\vulkancts\mustpass\master"
$files = Get-Content "$($root_mustpass)\vk-default.txt"
foreach($file in $files) {
  Get-Content "$($root_mustpass)\$($file)" | Add-Content -Path "$($deqp_mustpass)\vk-master.txt"
}
Remove-Item -Force -Recurse $deqp_source

Get-Date
$url = 'https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe';
Write-Host ('Downloading {0} ...' -f $url);
Invoke-WebRequest -Uri $url -OutFile 'rustup-init.exe';
Write-Host "Installing rust toolchain"
C:\rustup-init.exe -y;
Remove-Item C:\rustup-init.exe;

Get-Date
Write-Host "Installing deqp-runner"
$env:Path += ";$($env:USERPROFILE)\.cargo\bin"
cargo install --git https://gitlab.freedesktop.org/anholt/deqp-runner.git

Get-Date
Write-Host "Complete"
