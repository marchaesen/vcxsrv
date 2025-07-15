# USAGE NOTES:
#
# Step 1) Switch to Windows Containers Mode
#
# Step 2) Docker => Settings => Daemon => Switch from Basic to Advanced
#	(Without this the docker build will fail during VS Community install)
#      Add :
#           "storage-opts": [
#             "size=120GB"
#           ]
#
# Step 3) docker image build -t vcxsrv/build-env -m 2GB .
#	(This can take a couple hours and looks frozen soon after starting.  Let it have more memory if you can.)
#	This also results in a 32GB docker image

# Use the Long-Term Support Channel Windows Server Core image
FROM mcr.microsoft.com/windows/servercore:ltsc2022
SHELL ["cmd", "/S", "/C"]

#Install VS Community 2022; based on https://docs.microsoft.com/en-us/visualstudio/install/build-tools-container?view=vs-2022
RUN \
    # Download the Build Tools bootstrapper.
    curl -SL --output vs_buildtools.exe https://aka.ms/vs/17/release/vs_buildtools.exe \
    \
    # Install Build Tools with the Microsoft.VisualStudio.Workload.VCTools workload.
    && (start /w vs_buildtools.exe --quiet --wait --norestart --nocache \
        --installPath "%ProgramFiles%\Microsoft Visual Studio\2022\Community" \
        --add Microsoft.VisualStudio.Workload.VCTools;includeRecommended \
        --add Microsoft.Net.Component.4.8.TargetingPack \
        || IF "%ERRORLEVEL%"=="3010" EXIT 0) \
    \
    # Cleanup
    && del /q vs_buildtools.exe

#Install Chocolatey
RUN powershell -NoProfile -Command \
	Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1')); \
	Start-Process %ProgramData%\chocolatey\bin\choco.exe -ArgumentList 'feature disable –name showDownloadProgress' -Wait

#Install Strawberry Perl into C:\Perl
RUN choco install -y strawberryperl \
	&& ren Strawberry Perl \
	&& rd /s /q C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& mkdir C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& setx /m path "%path%;c:\perl\perl\bin"

#Install Nullsoft Scriptable Install System
RUN choco install -y nsis \
	&& rd /s /q C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& mkdir C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& del c:\programdata\chocolatey\lib\nsis.install\*.nupkg

#Disable creating junctions
ENV CYGWIN=winsymlinks:lnk

#Install Cygwin
RUN choco install -y cygwin \
	&& c:\tools\cygwin\cygwinsetup.exe -q -v -P bison,flex,gawk,gperf,gzip,nasm,sed,python27,python38-lxml -W -R c:\tools\cygwin -a x86_64 \
	&& rd /s /q C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& mkdir C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& if exist c:\tools\cygwin\bin\link.exe ren c:\tools\cygwin\bin\link.exe link_cygwin.exe \
	&& del c:\programdata\chocolatey\lib\cygwin\*.nupkg \
	&& del c:\tools\cygwin\cygwinsetup.exe

#Install Python3
RUN choco install -y python3 --version 3.9.13 \
	&& rd /s /q C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& mkdir C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& pushd c:\python39 \
	&& C:\Python39\Scripts\pip install lxml \
	&& C:\Python39\Scripts\pip install mako \
	&& popd \
	&& del c:\programdata\chocolatey\lib\python3\*.nupkg \
	&& setx /m path "%path%;C:\Python39"

#Install git
RUN choco install -y git \
	&& rd /s /q C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& mkdir C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& refreshenv \
	&& git config --global core.autocrlf false \
	&& del c:\programdata\chocolatey\lib\git.install\*.nupkg

# Launch VS2022 developer command prompt when started
SHELL ["cmd", "/S", "/K"]
ENTRYPOINT ""%ProgramFiles%\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat""

# Launch with docker run -m 4G -v:<VcXSrvGit>:c:\src -it <ContainerName>
# Once inside:
#   git clone src vcx
#   cygwin
#   cd /cygdrive/c/vcx
#   ./buildall.sh 1 9 R
#   ./copyRelease.sh
