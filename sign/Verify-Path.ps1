Function Verify-Path ([string]$mypath, [string]$msg) {
    if ( ! ($mypath)) {
        Write-Host "ERROR: empty string defined for ${msg}"
        Exit 1
    }
    if (! (Test-Path $mypath)) {
        Write-Host "ERROR: ${msg} not found!"
        Exit 1
    }
}