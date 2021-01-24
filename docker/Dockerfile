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
FROM mcr.microsoft.com/windows/servercore:ltsc2019
SHELL ["cmd", "/S", "/C"]

# Install VS 2019 community
RUN powershell -NoProfile -Command \
	Set-ExecutionPolicy Bypass -Scope Process -Force; \
	$ProgressPreference = 'SilentlyContinue'; \
	[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; \
	mkdir c:\temp; \
	Invoke-WebRequest -Uri "https://aka.ms/vs/16/release/vs_community.exe" -Outfile "C:\TEMP\vs_community.exe"; \
	C:\TEMP\vs_community.exe --includeRecommended --includeOptional --quiet --nocache --add Microsoft.VisualStudio.Workload.NativeDesktop --norestart --wait; \
    & IF "%ERRORLEVEL%"=="3010" dir \
	& rd /s /q c:\temp \
	&& rd /s /q C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& mkdir C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& del c:\temp\vs_community.exe

#Install Chocolatey
RUN powershell -NoProfile -Command \
	Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1')); \
	Start-Process \ProgramData\chocolatey\bin\choco.exe -ArgumentList 'feature disable –name showDownloadProgress' -Wait

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

#Install 7Zip
RUN choco install -y 7zip \
	&& rd /s /q C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& mkdir C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& del c:\programdata\chocolatey\lib\7zip.install\*.nupkg

#Install Cygwin and remove hardlinks
RUN choco install -y cygwin --version 3.1.6 \
	&& C:\ProgramData\chocolatey\lib\cygwin\tools\setup-x86.exe -q -v -P bison,flex,gawk,gperf,gzip,nasm,sed,python27,python38-lxml -W -R c:\tools\cygwin -a x86_64 \
	&& 7z a -mx1 c:\cyg.7z c:\tools \
	&& rd /s /q c:\tools \
	&& 7z x c:\cyg.7z \
	&& del c:\cyg.7z \
	&& rd /s /q C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& mkdir C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& if exist c:\tools\cygwin\bin\link.exe ren c:\tools\cygwin\bin\link.exe link_cygwin.exe \
	&& del c:\programdata\chocolatey\lib\cygwin\*.nupkg \
	&& del c:\programdata\chocolatey\lib\cygwin\tools\setup-x86.exe

#Install Python3
RUN choco install python3 -y \
	&& rd /s /q C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& mkdir C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& pushd c:\python39 \
	&& C:\Python39\Scripts\pip wheel https://download.lfd.uci.edu/pythonlibs/z4tqcw5k/lxml-4.6.2-cp39-cp39-win_amd64.whl \
	&& C:\Python39\Scripts\pip install lxml \
	&& C:\Python39\Scripts\pip install mako \
	&& popd \
	&& del c:\programdata\chocolatey\lib\python3\*.nupkg \
	&& setx /m path "%path%;C:\Python39

#Install git
RUN choco install -y git \
	&& rd /s /q C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& mkdir C:\Users\ContainerAdministrator\AppData\Local\Temp \
	&& refreshenv \
	&& git config --global core.autocrlf false \
	&& del c:\programdata\chocolatey\lib\git.install\*.nupkg

# Launch VS2017 developer command prompt when started
SHELL ["cmd", "/S", "/K"]
ENTRYPOINT ""C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Auxiliary\\Build\vcvars64.bat""

# Launch with docker run -m 4G -v:<VcXSrvGit>:c:\src -it <ContainerName>
# Once inside:
#   git clone src vcx
#   cygwin
#   cd /cygdrive/c/vcx
#   ./buildall.sh 1 9 R
