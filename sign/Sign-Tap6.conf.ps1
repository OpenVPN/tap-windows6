# Adapt this file to match your system
$ddk_bin = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0x86"
$inf2cat = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x86\Inf2Cat.exe"
$signtool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x86\signtool.exe"

# Signtool.exe selects the certificate based on the Subject[name] field.
# This script only searches from under CurrentUser\My certificate store.
# If you have several certificates with matching Subject fields, your only
# choice is to put them in different stores (LocalMachine and CurrentUser)
# and instruct Signtool.exe to use the correct store. If you have three or
# more certificates, then you need to use pfx files and the /f and /p switches
# instead.
$subject = "OpenVPN"

# The correct cross-certificate is needed for kernel-mode drivers. You can get
# them from here:
#
# <https://msdn.microsoft.com/en-us/library/windows/hardware/dn170454%28v=vs.85%29.aspx>
#
# The two settings below are for a Digicert EV code-signing certificate
#
$crosscert = ".\digicert-high-assurance-ev.crt"
$timestamp = "http://timestamp.digicert.com"

# The digest algorithm to use for the signature as well as the timestamp
$digest = "sha256"

# Creating tarballs has not been implemented yet, so you can ignore this
$tar = "C:\Program Files\Git\usr\bin\tar.exe"
