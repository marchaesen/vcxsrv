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
cmd.exe /C 'C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64 && cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="C:\Piglit" -DGLUT_INCLUDE_DIR=C:\freeglut\include -DGLUT_glut_LIBRARY_RELEASE=C:\freeglut\lib\x64\freeglut.lib -DGLEXT_INCLUDE_DIR=.\glext && ninja -j32'
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
git checkout 7ac0ad5a7fe0ec884faba1dc2916028d0268eeef
Pop-Location

Get-Date
Write-Host "Complete"
