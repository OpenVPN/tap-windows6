# Produce a tap-windows6 dist directory from attestation signed driver packages
Param (
    [parameter(Mandatory = $true, ValueFromPipeline = $true)] $DriverPackage,
    [string]$DistDir = "${PSScriptRoot}\..\dist"
)

Begin {
    # Load configs and modules
    . "${PSScriptRoot}\Verify-Path.ps1"
    . "${PSScriptRoot}\Sign-Tap6.conf.ps1"

    # Get UNIX epoch-style timestamp for backup filename
    $timestamp = (New-TimeSpan -Start (Get-Date "01/01/1970") -End (Get-Date)).TotalSeconds

    # Backup old dist directory
    if (Test-Path $DistDir) {
        Copy-Item -Recurse $DistDir "${DistDir}.${timestamp}"
    }
    # Create disk directory again after rename or if its doesnt already exists.
    if (!(Test-Path $DistDir)) {
        New-Item -ItemType Directory -Force -Path $DistDir
    }
}

Process {
    # Extract the attestation signed zip file into a temporary directory and
    # copy its contents into the "dist" directory, overwriting any existing files
    # with the same name.
    ForEach ($input in $DriverPackage) {
        $fullpath = ($input).FullName
        Verify-Path $fullpath "driver package"
        $basename = ($input).BaseName
        if (Test-Path $basename) {
            Remove-Item -Recurse $basename
        }
        Expand-Archive -Path $fullpath -DestinationPath $basename
        Get-ChildItem "${basename}\drivers"|Copy-Item -Force -Recurse -Destination $DistDir
        Remove-Item -Recurse $basename
    }
}

End {
    # Verify that a signed tapinstall.exe is found
    $dist_dirs = ((Get-ChildItem -Path $DistDir -Directory)|Where-Object { $_Name -match "^(amd64|arm64|i386)$" }).FullName
    ForEach ($dist_dir in $dist_dirs) {
        $tapinstall = "${dist_dir}\tapinstall.exe"
        if (! (Test-Path $tapinstall)) {
            Write-Host "ERROR: ${tapinstall} not found!"
            Exit 1
        } else {
            if ((Get-AuthenticodeSignature $tapinstall).Status -ne "Valid") {
                Write-Host "ERROR: No valid authenticode signature found for ${tapinstall}!"
                Exit 1
            }
        }
    }
}