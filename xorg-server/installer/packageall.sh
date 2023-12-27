echo powershell.exe Start-Process -WorkingDirectory w:$(pwd) -FilePath cmd.exe -Wait -NoNewWindow -ArgumentList "/C", "packageall.bat", "$1"
