$dxil_dll = cmd.exe /C "C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64 -no_logo && where dxil.dll" 2>&1
if ($dxil_dll -notmatch "dxil.dll$") {
    Write-Output "Couldn't get path to dxil.dll"
    exit 1
}
$env:Path = "$(Split-Path $dxil_dll);$env:Path"

# VK_ICD_FILENAMES environment variable is not used when running with
# elevated privileges. Add a key to the registry instead.
$hkey_path = "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers\"
$hkey_name = Join-Path -Path $pwd -ChildPath "_install\share\vulkan\icd.d\dzn_icd.x86_64.json"
New-Item -Path $hkey_path -force
New-ItemProperty -Path $hkey_path -Name $hkey_name -Value 0 -PropertyType DWORD

$results = New-Item -ItemType Directory results
$baseline = ".\_install\warp-fails.txt"
$suite = ".\_install\deqp-dozen.toml"

$env:DZN_DEBUG = "warp"
$env:MESA_VK_IGNORE_CONFORMANCE_WARNING = "true"
deqp-runner suite --suite $($suite) --output $($results) --baseline $($baseline) --testlog-to-xml C:\deqp\executor\testlog-to-xml.exe --jobs 4 --fraction 3
$deqpstatus = $?

$template = "See https://$($env:CI_PROJECT_ROOT_NAMESPACE).pages.freedesktop.org/-/$($env:CI_PROJECT_NAME)/-/jobs/$($env:CI_JOB_ID)/artifacts/results/{{testcase}}.xml"
deqp-runner junit --testsuite dEQP --results "$($results)/failures.csv" --output "$($results)/junit.xml" --limit 50 --template $template
Copy-Item -Path "C:\deqp\testlog.css" -Destination $($results)
Copy-Item -Path "C:\deqp\testlog.xsl" -Destination $($results)

if (!$deqpstatus) {
    Exit 1
}
