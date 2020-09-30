Param (
    [string]$Target = "dist",
    [string]$Win7dist = "dist.win7",
    [string]$Win10dist = "dist.win10"
)

Function Show-Usage {
    Write-Host "Usage: Prepare-Msm.ps1 [-Target <target-directory>] [-Win7dist <distdir>] [-Win10dist <distdir>]"
}

# Parameter validation
if (! ($Target -and $Win7dist -and $Win10dist)) {
    Show-Usage
    Exit 1
}

# Check for presence of the source dist directories
if ( ! (Test-Path "${Win7dist}\amd64\tap0901.cat") ) {
    Write-Host "ERROR: win7dist directory not found!"
    Exit 1
}

if ( ! (Test-Path "${Win10dist}\amd64\tap0901.cat") ) {
    Write-Host "ERROR: win10dist directory not found!"
    Exit 1
}

# Get UNIX epoch-style timestamp for backup filename
$timestamp = (New-TimeSpan -Start (Get-Date "01/01/1970") -End (Get-Date)).TotalSeconds

# Backup old dist directory
if (Test-Path $Target) {
    Copy-Item -Recurse $Target "${Target}.${timestamp}"
}

# Prepare the directory layout for MSM packaging
Remove-Item -Recurse $Target
New-Item -ItemType Directory $Target

foreach ($dir in "amd64", "amd64/win10", "arm64", "arm64/win10", "include", "i386", "i386/win10") {
    New-Item -ItemType Directory "${Target}/${dir}"
}

foreach ($arch in "amd64", "i386") {
    Copy-Item "${Win7dist}\${arch}\*" "${Target}\${arch}\"
}

foreach ($arch in "amd64", "arm64", "i386") {
    Copy-Item "${Win10dist}\${arch}\*" "${Target}\${arch}\win10\"
}

Copy-Item "${Win10dist}\include\tap-windows.h" "${Target}\include\"

Get-ChildItem -Recurse $Target -Filter "tapinstall.exe"|Remove-Item