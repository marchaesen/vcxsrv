# Download new TLS certs from Windows Update
Get-Date
Write-Host "Updating TLS certificate store"
$certdir = (New-Item -ItemType Directory -Name "_tlscerts")
certutil -syncwithWU "$certdir"
Foreach ($file in (Get-ChildItem -Path "$certdir\*" -Include "*.crt")) {
  Import-Certificate -FilePath $file -CertStoreLocation Cert:\LocalMachine\Root
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
  choco install -y python3 --params="/InstallDir:C:\python3"
  $python_install = $?
  choco install --allow-empty-checksums -y cmake git git-lfs ninja pkgconfiglite winflexbison vulkan-sdk --installargs "ADD_CMAKE_TO_PATH=System"
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
pip3 install meson mako numpy
if (!$?) {
  Write-Host "Failed to install dependencies from pip"
  Exit 1
}

# we want more secure TLS 1.2 for most things, but it breaks SourceForge
# downloads so must be done after Chocolatey use
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor [Net.SecurityProtocolType]::Tls13;

Get-Date
Write-Host "Cloning LLVM release/12.x"
git clone -b release/12.x --depth=1 https://github.com/llvm/llvm-project llvm-project
if (!$?) {
  Write-Host "Failed to clone LLVM repository"
  Exit 1
}

# ideally we want to use a tag here insted of a sha,
# but as of today, SPIRV-LLVM-Translator doesn't have
# a tag matching LLVM 12.0.0
Get-Date
Write-Host "Cloning SPIRV-LLVM-Translator"
git clone https://github.com/KhronosGroup/SPIRV-LLVM-Translator llvm-project/llvm/projects/SPIRV-LLVM-Translator
if (!$?) {
  Write-Host "Failed to clone SPIRV-LLVM-Translator repository"
  Exit 1
}
Push-Location llvm-project/llvm/projects/SPIRV-LLVM-Translator
git checkout 5b641633b3bcc3251a52260eee11db13a79d7258
Pop-Location

Get-Date
# slightly convoluted syntax but avoids the CWD being under the PS filesystem meta-path
$llvm_build = New-Item -ItemType Directory -Path ".\llvm-project" -Name "build"
Push-Location -Path $llvm_build.FullName
Write-Host "Compiling LLVM and Clang"
cmd.exe /C 'C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64 && cmake ../llvm -GNinja -DCMAKE_BUILD_TYPE=Release -DLLVM_USE_CRT_RELEASE=MT -DCMAKE_INSTALL_PREFIX="C:\llvm-10" -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD=AMDGPU;X86 -DLLVM_OPTIMIZED_TABLEGEN=TRUE -DLLVM_ENABLE_ASSERTIONS=TRUE -DLLVM_INCLUDE_UTILS=OFF -DLLVM_INCLUDE_RUNTIMES=OFF -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_GO_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_BUILD_LLVM_C_DYLIB=OFF -DLLVM_ENABLE_DIA_SDK=OFF -DCLANG_BUILD_TOOLS=ON -DLLVM_SPIRV_INCLUDE_TESTS=OFF && ninja -j32 install'
$buildstatus = $?
Pop-Location
if (!$buildstatus) {
  Write-Host "Failed to compile LLVM"
  Exit 1
}

Get-Date
$libclc_build = New-Item -ItemType Directory -Path ".\llvm-project" -Name "build-libclc"
Push-Location -Path $libclc_build.FullName
Write-Host "Compiling libclc"
# libclc can only be built with Ninja, because CMake's VS backend doesn't know how to compile new language types
cmd.exe /C 'C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64 && cmake ../libclc -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-m64" -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_INSTALL_PREFIX="C:\llvm-10" -DLIBCLC_TARGETS_TO_BUILD="spirv-mesa3d-;spirv64-mesa3d-" && ninja -j32 install'
$buildstatus = $?
Pop-Location
Remove-Item -Recurse -Path $libclc_build
if (!$buildstatus) {
  Write-Host "Failed to compile libclc"
  Exit 1
}
Remove-Item -Recurse -Path $llvm_build

Get-Date
Write-Host "Cloning SPIRV-Tools"
git clone https://github.com/KhronosGroup/SPIRV-Tools
if (!$?) {
  Write-Host "Failed to clone SPIRV-Tools repository"
  Exit 1
}
git clone https://github.com/KhronosGroup/SPIRV-Headers SPIRV-Tools/external/SPIRV-Headers
if (!$?) {
  Write-Host "Failed to clone SPIRV-Headers repository"
  Exit 1
}
Write-Host "Building SPIRV-Tools"
$spv_build = New-Item -ItemType Directory -Path ".\SPIRV-Tools" -Name "build"
Push-Location -Path $spv_build.FullName
# SPIRV-Tools doesn't use multi-threaded MSVCRT, but we need it to
cmd.exe /C 'C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64 && cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_INSTALL_PREFIX="C:\spirv-tools" && ninja -j32 install'
$buildstatus = $?
Pop-Location
Remove-Item -Recurse -Path $spv_build
if (!$buildstatus) {
  Write-Host "Failed to compile SPIRV-Tools"
  Exit 1
}

# See https://gitlab.freedesktop.org/mesa/mesa/-/issues/3855
# Until that's resolved, we need the vulkan-runtime as a build dependency to be able to run any unit tests on GL
Get-Date
Write-Host "Downloading Vulkan-Runtime"
Invoke-WebRequest -Uri 'https://sdk.lunarg.com/sdk/download/latest/windows/vulkan-runtime.exe' -OutFile 'C:\vulkan-runtime.exe' | Out-Null
Write-Host "Installing Vulkan-Runtime"
Start-Process -NoNewWindow -Wait C:\vulkan-runtime.exe -ArgumentList '/S'
if (!$?) {
  Write-Host "Failed to install Vulkan-Runtime"
  Exit 1
}
Remove-Item C:\vulkan-runtime.exe -Force

Get-Date
Write-Host "Complete"
