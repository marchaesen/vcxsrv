# USAGE NOTES:
#
# Step 1) Switch to Windows Containers Mode:
#         Right click on docker system tray icon, and select "Switch to Windows Containers Mode"
#
# Step 2) Docker => Settings => Docker Engine
#        (Without this the docker build will fail during VS Community install)
#      Add :
#           "storage-opts": [
#             "size=120GB"
#           ]
#
# Step 3) dockerBuild.cmd
#        (This can take a couple hours and looks frozen soon after starting.  Let it have more memory if you can.)
#        This also results in a 32GB docker image

# Use .net 4.8 with Long-Term Support Channel Windows Server Core Image 2019
#
# This is used as microsoft state:
#
# "If you base your image directly on microsoft/windowsservercore, the .NET
# Framework might not install properly and no install error is indicated.
# Managed code might not run after the install is complete. Instead, base your
# image on microsoft/dotnet-framework:4.8 or later."
#
# Full tag list: https://mcr.microsoft.com/v2/dotnet/framework/runtime/tags/list
#
# https://learn.microsoft.com/en-us/visualstudio/install/build-tools-container?view=vs-2022#create-and-build-the-dockerfile
FROM mcr.microsoft.com/dotnet/framework/runtime:4.8-windowsservercore-ltsc2022
SHELL ["cmd", "/S", "/C"]

# Install VS Community 2022; based on https://docs.microsoft.com/en-us/visualstudio/install/build-tools-container?view=vs-2022
RUN \
    # Download the Build Tools bootstrapper.
    curl -SL --output vs_buildtools.exe https://aka.ms/vs/17/release/vs_buildtools.exe \
    \
    # From: https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=vs-2022#desktop-development-with-c
    # Install Build Tools with the Microsoft.VisualStudio.Workload.VCTools workload, excluding workloads and components with known issues.
    && (start /w vs_buildtools.exe --quiet --wait --norestart --nocache \
        --installPath "%ProgramFiles%\Microsoft Visual Studio\2022\Community" \
        --add Microsoft.VisualStudio.Workload.VCTools;includeRecommended \
        --remove Microsoft.VisualStudio.Component.Windows10SDK.10240 \
        --remove Microsoft.VisualStudio.Component.Windows10SDK.10586 \
        --remove Microsoft.VisualStudio.Component.Windows10SDK.14393 \
        --remove Microsoft.VisualStudio.Component.Windows81SDK \
        || IF "%ERRORLEVEL%"=="3010" EXIT 0) \
    \
    # Cleanup
    && del /q vs_buildtools.exe

# Install Chocolatey
RUN powershell -NoProfile -Command \
        Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1')); \
        Start-Process %ProgramData%\chocolatey\bin\choco.exe -ArgumentList 'feature disable –name showDownloadProgress' -Wait

# Install Strawberry Perl into C:\Perl
RUN choco install -y strawberryperl --install-arguments="INSTALLDIR=""C:\Perl""" \
        && refreshenv \
        && rd /s /q %TEMP% \
        && mkdir %TEMP%

# Install Nullsoft Scriptable Install System
RUN choco install -y nsis \
        && refreshenv \
        && rd /s /q %TEMP% \
        && mkdir %TEMP% \
        && del %ChocolateyInstall%\lib\nsis.install\*.nupkg

# Disable creating junctions
ENV CYGWIN=winsymlinks:lnk

# Set the timezone to match container for cygwin
RUN setx /M TZ "America/Los_Angeles"

# Install Cygwin
RUN choco install -y cygwin \
        && refreshenv \
        && c:\tools\cygwin\cygwinsetup.exe -q -v -P vim,bison,flex,gawk,gperf,gzip,nasm,sed,python27,python39-lxml -W -R c:\tools\cygwin -a x86_64 \
        && rd /s /q %TEMP% \
        && mkdir %TEMP% \
        && del %ChocolateyInstall%\lib\cygwin\*.nupkg

# Install Python3
RUN choco install -y python3 --version 3.9.13 \
        && refreshenv \
        && rd /s /q %TEMP% \
        && mkdir %TEMP% \
        && pip install lxml \
        && pip install mako \
        && del %ChocolateyInstall%\lib\python3\*.nupkg

# Install git
RUN choco install -y git \
        && refreshenv \
        && rd /s /q %TEMP% \
        && mkdir %TEMP% \
        && del %ChocolateyInstall%\lib\git.install\*.nupkg \
        && git config --global core.autocrlf false \
        && git config --global --add safe.directory C:/src/.git

# Launch VS2022 developer (64bit) command prompt when started
SHELL ["cmd", "/S", "/K"]
ENTRYPOINT ""%ProgramFiles%\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat""


# Ensure any local changes have been committed to git
#
# Launch with: runDocker.cmd
# Or docker run -m 2G -v:<VcXSrvGit>:c:\src -it vcsb
#
# Once inside:
#   git clone src vcx
#   cygwin
#   cd /cygdrive/c/vcx
#   export SHELLOPTS
#   set -o igncr
#   ./buildall.sh 1 9 R   # 1 = 64bit (0 = 32bit), 9 = 9 parallell builds, R = Release (D = Debug)
#   ./copyRelease.sh
