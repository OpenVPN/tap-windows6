Param (
    [string]$SourceDir = "${PSScriptRoot}\..\dist",
    [switch]$Force,
    [switch]$Append
)

Function Show-Usage {
    Write-Host "Usage: Cross-Sign.ps1 [-SourceDir <sourcedir>] [-Force] [-VerifyOnly] [-Append]"
    Write-Host
    Write-Host "Example 1: Sign for OpenVPN (OSS)"
    Write-Host "    Cross-Sign.ps1 -Force"
    Write-Host
    Write-Host "Example 2: Sign for OpenVPN Connect (Access Server)"
    Write-Host "    Cross-Sign.ps1 -SourceDir ..\tapoas6 -Force"
    Write-Host
    Write-Host "Example 3: Append a signature to a signed driver (e.g. for Windows Vista)"
    Write-Host "    Cross-Sign.ps1 -Append"
    Write-Host
}

. "${PSScriptRoot}\Sign-Tap6.conf.ps1"
. "${PSScriptRoot}\Verify-Path.ps1"

# Parameter validation
if (! ($SourceDir -and $crosscert)) {
    Show-Usage
    Exit 1
}

if ( !($Append) -and !($Force)) {
    Write-Host "ERROR: You must use -Force when not using -Append!"
    Write-Host
    Show-Usage
    Exit 1
}

if ( $Append -and $Force) {
    Write-Host "ERROR: Using -Append and -Force are mutually exclusive parameters!"
    Write-Host
    Show-Usage
    Exit 1
}

# Inf2Cat.exe requires a fully-qualified path
$x86_driver_dir = Resolve-Path "${SourceDir}\i386"
$x64_driver_dir = Resolve-Path "${SourceDir}\amd64"
$arm64_driver_dir = Resolve-Path "${SourceDir}\arm64"
$inf_x86 = "${x86_driver_dir}/OemVista.inf"
$inf_x64 = "${x64_driver_dir}/OemVista.inf"
$inf_arm64 = "${arm64_driver_dir}/OemVista.inf"
# The next two result in a string such as "tap0901"
$x86_driver_basename = (Get-ChildItem $x86_driver_dir -Filter "*.sys").BaseName
$x64_driver_basename = (Get-ChildItem $x64_driver_dir -Filter "*.sys").BaseName
$arm64_driver_basename = (Get-ChildItem $arm64_driver_dir -Filter "*.sys").BaseName
$cat_x86 = "${x86_driver_dir}\${x86_driver_basename}.cat"
$cat_x64 = "${x64_driver_dir}\${x64_driver_basename}.cat"
$cat_arm64 = "${arm64_driver_dir}\${arm64_driver_basename}.cat"
$devcon_x86 = (Get-ChildItem $x86_driver_dir -Filter "*.exe").FullName
$devcon_x64 = (Get-ChildItem $x64_driver_dir -Filter "*.exe").FullName
$devcon_arm64 = (Get-ChildItem $arm64_driver_dir -Filter "*.exe").FullName
$sourcedir_basename = (Get-Item $SourceDir).Basename
$sourcedir_parent = (Get-Item $SourceDir).Parent.FullName

Verify-Path $inf2cat "Inf2Cat.exe"
Verify-Path $signtool "signtool.exe"
Verify-Path $CrossCert "cross certificate"
Verify-Path $SourceDir "tap-windows6 source directory"
Verify-Path $inf_x86 $inf_x86
Verify-Path $inf_x64 $inf_x64
Verify-Path $devcon_x86 "32-bit devcon/tapinstall.exe"
Verify-Path $devcon_x64 "64-bit devcon/tapinstall.exe"

if ($VerifyOnly) {
    Write-Host "Verification complete"
    Exit 0
}

# Recreate catalogs and catalog signatures if -Force is given
if ($Force) {
    foreach ($file in $cat_x86,$cat_x64) {
        Remove-Item $file
    }
}

# Generate catalogs
if (Test-Path $cat_x86) {
    Write-Host "Catalog file ${cat_X86} is present, not creating it"
} else {
    & $Inf2Cat /driver:$x86_driver_dir /os:Vista_X86,Server2008_X86,7_X86
}

if (Test-Path $cat_x64) {
    Write-Host "Catalog file ${cat_X64} is present, not creating it"
} else {
    & $Inf2Cat /driver:$x64_driver_dir /os:Vista_X64,Server2008_X64,Server2008R2_X64,7_X64
}

# Sign the catalogs
foreach ($file in $cat_x86,$cat_x64,$cat_arm64,$devcon_x86,$devcon_x64,$devcon_arm64) {
    $not_signed = ((Get-AuthenticodeSignature $file).Status -eq "NotSigned")
 
    if ( ($not_signed) -or ($Append) ) {
        & $signtool sign /v /s My /n $subject /ac $crosscert /as /fd $digest $file
        # signtool.exe counterintuitively rejects the /tp 0, claiming that the index is invalid;
        # hence we only define /tp if we're adding a second signature.
        if ($Append) { 
            & $signtool timestamp /tr $timestamp /td $digest /tp 1 $file
        } else {
		    & $signtool timestamp /tr $timestamp /td $digest $file
        }
    } else {
        Write-Host "${file} is signed already, not signing it"
    }
}