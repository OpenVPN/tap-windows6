# Produce Windows 10 DDF cabined files for attestation signing
Param (
    [string]$DistDir = "${PSScriptRoot}\..\dist",
    [String]$DDFTemplate = "${PSScriptRoot}\tap-windows6.ddf",
    [String]$CabinetBaseName = "tap-windows6"
)

. "${PSScriptRoot}\Verify-Path.ps1"
. "${PSScriptRoot}\Sign-Tap6.conf.ps1"

Verify-Path $DDFTemplate "tap-windows6 DDF template"
Verify-Path $DistDir "tap-windows6 dist directory"

foreach ($arch in "i386", "amd64", "arm64") {
    $ddf_file = "${CabinetBaseName}-${arch}.ddf"
    # makecab.exe can only understand Ascii files
    (Get-Content $DDFTemplate) -replace ("%ARCH%", "$arch") -replace ("%CABINETBASENAME%", "$CabinetBaseName")|Out-File $ddf_file -Encoding Ascii
    (Get-ChildItem "${DistDir}\${arch}").FullName|Out-File $ddf_file -Append -Encoding Ascii
    & makecab.exe /F "${ddf_file}"
    "${PSScriptRoot}/Sign-File.ps1 ${ddf_file}"
}