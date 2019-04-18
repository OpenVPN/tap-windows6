# Sign the (installer) file and verify it using non-kernel mode validation
Param (
    [parameter(Mandatory = $true, ValueFromPipeline = $true)] $SourceFile
)

Begin {
	Function Show-Usage {
		Write-Host "Usage: Sign-File.ps1 -SourceFile <file>"
		Write-Host
		Write-Host "Example 1: Sign tap-windows6 release package"
		Write-Host "    Sign-File.ps1 -SourceFile ..\tap-windows-9.23.2-I601.exe"
		Write-Host
		Write-Host "Example 2: sign all cabinet files under disk1"
		Write-Host "    Get-ChildItem disk1|Sign-File.ps1"
	}

	. "${PSScriptRoot}\Sign-Tap6.conf.ps1"
	. "${PSScriptRoot}\Verify-Path.ps1"

	Verify-Path $signtool "signtool.exe"
	Verify-Path $CrossCert "cross certificate"

	if (! ($crosscert)) {
		Show-Usage
		Exit 1
	}
}

Process {
    ForEach ($input in $SourceFile) {
		$fullpath = ($input).FullName
		Verify-Path $fullpath "source file"
		$not_signed = ((Get-AuthenticodeSignature $fullpath).Status -eq "NotSigned")
		if ($not_signed) {
			& $signtool sign /v /s My /n $subject /ac $crosscert /fd $digest $fullpath
			& $signtool timestamp /tr $timestamp /td $digest $fullpath
			& $signtool verify /pa /v $fullpath
		} else {
			Write-Host "${SourceFile} is signed already, not signing it"
		}
	}
}