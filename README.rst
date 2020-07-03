TAP-Windows driver (NDIS 6)
===========================

This is an NDIS 6.20/6.30 implementation of the TAP-Windows driver, used by
OpenVPN and other apps. NDIS 6.20 drivers can run on Windows 7 or higher except
on ARM64 desktop systems where, since the platform relies on next-gen power
management in its drivers, NDIS 6.30 is required.

Build
-----

The prerequisites for building are:

- Python 2.7
- Microsoft Windows 10 EWDK (Enterprise Windows Driver Kit)
    - Visual Studio+Windows Driver Kit works too. Make sure to work from a "Command Prompt for Visual Studio" and to call buildtap.py with "--sdk=wdk".
- The devcon source code directory (setup/devcon) from `Windows-driver-samples <https://github.com/OpenVPN/Windows-driver-samples>`_ (optional)
    - If you use the repo from `upstream <https://github.com/Microsoft/Windows-driver-samples>`_ remember to include our patch to devcon.vcxproj to ensure that devcon.exe is statically linked.
- Windows code signing certificate
- Git (not strictly required, but useful for running commands using bundled bash shell)
- MakeNSIS (optional)
- Prebuilt tapinstall.exe binaries (optional)
- Visual Studio 2019 and WiX Toolset for MSM packaging (optional)

Make sure you add Python's install directory (usually c:\\python27) to the PATH 
environment variable.

Tap-windows6 has been successfully build on Windows 10 and Windows Server 2016 using
CMD.exe, Powershell and Git Bash.

View build script options::

  $ python buildtap.py
  Usage: buildtap.py [options]
  
  Options:
    -h, --help           show this help message and exit
    -s SRC, --src=SRC    TAP-Windows top-level directory, default=<CWD>
    --ti=TAPINSTALL      tapinstall (i.e. devcon) directory (optional)
    -d, --debug          enable debug build
    --hlk                build for HLK tests (test sign, no debug)
    -c, --clean          do an nmake clean before build
    -b, --build          build TAP-Windows and possibly tapinstall (add -c to
                         clean before build)
    --sdk=SDK            SDK to use for building: ewdk or wdk, default=ewdk
    --sign               sign the driver files
    -p, --package        generate an NSIS installer from the compiled files
    -m, --package-msm    generate a MSM installer from the compiled files
    --cert=CERT          Common name of code signing certificate,
                         default=openvpn
    --certfile=CERTFILE  Path to the code signing certificate
    --certpw=CERTPW      Password for the code signing certificate/key
                         (optional)
    --crosscert=CERT     The cross-certificate file to use, default=MSCV-
                         VSClass3.cer
    --timestamp=URL      Timestamp URL to use, default=http://timestamp.verisign
                         .com/scripts/timstamp.dll
    --versionoverride=FILE
                         Path to the version override file

Edit **version.m4** and **paths.py** as necessary then build::

  $ python buildtap.py -b

On successful completion, all build products will be placed in the "dist" 
directory as well as tap6.tar.gz. The NSIS installer package will be placed to
the build root directory.

Building tapinstall (optional)
------------------------------

The easiest way to build tapinstall is to clone the Microsoft driver samples
and copy the source for devcon.exe into the tap-windows6 tree. Using PowerShell::

  $ git clone https://github.com/Microsoft/Windows-driver-samples
  $ Copy-Item -Recurse Windows-driver-samples/setup/devcon tap-windows6
  $ cd tap-windows6
  $ python.exe buildtap.py -b --ti=devcon

The build system also supports reuse of pre-built tapinstall.exe executables.
To make sure the buildsystem finds the executables, create the following
directory structure under tap-windows6 directory::

  devcon
  ├── Release
  │   └── devcon.exe
  ├── x64
  │   └── Release
  │       └── devcon.exe
  └── ARM64
      └── Release
          └── devcon.exe

This structure is equal to what building tapinstall would create. Then call
buildtap.py with "--ti=devcon". Replace "Release" with your build configuration;
for example, when using --Hlk you'd use "Hlk".

Please note that the NSIS packaging (-p) step will fail if you don't have
tapinstall.exe available. Also don't use the "-c" flag or the above directories
will get wiped before MakeNSIS is able to find them.

Install/Update/Remove
---------------------

The driver can be installed using a command-line tool, tapinstall.exe, which is
bundled with OpenVPN and tap-windows installers. Note that in some versions of
OpenVPN tapinstall.exe is called devcon.exe. To install, update or remove the
tap-windows NDIS 6 driver follow these steps:

- place tapinstall.exe/devcon.exe to your PATH
- open an Administrator shell
- cd to **dist**
- cd to **amd64**, **i386**, or **arm64** depending on your system's processor architecture.

Install::

  $ tapinstall install OemVista.inf TAP0901

Update::

  $ tapinstall update OemVista.inf TAP0901

Remove::

  $ tapinstall remove TAP0901

Build for HLK tests
-------------------

A test-signed version of tap-windows6 driver should be used for the HLK tests.
The recommended procedure is to use pre-built, cross-signed devcon.exe and use
the WDK-generated key for signing the driver.

First setup the directory with prebuilt devcon as described above.
Then run the build with the --hlk option::

  $ python.exe buildtap.py -c -b --ti=devcon-prebuilt --hlk

Release signing
---------------

Microsoft's driver signing requirements have tightened considerably over the
last several years. Because of this this buildsystem no longer attempts to sign
files by default. If you want to sign the files at build time use the --sign
option. The "sign" directory contains several Powershell scripts that help
produce release-signed tap-windows6 packages:

- *Cross-Sign*: cross-sign tap-windows6 driver files and tapinstall.exe
- *Create-DriverSubmission*: create architecture-specific attestation signing submission cabinet files
- *Extract-DriverSubmission*: extract attestation-signed zip files
- *Sign-File*: sign files (e.g. tap-windows6 installer or driver submission cabinet files)
- *Sign-tap6.conf.ps1*: configuration file for all the scripts above

With the exception of Sign-File these scripts operate on the "dist" directory
that tap-windows6 build system produces. Below it is assumed that building and
signing is done on the same computer. It is also assumed that Cross-Sign.ps1 is
run as Administrator; according to Microsoft documentation Inf2Cat, which
Cross-Sign.ps1 uses to create (unsigned) catalog files, needs to run with
administrator privileges.

First produce cross-signed drivers and installers (Windows 7/8/8.1/Server 2012r2)::

  $ python.exe buildtap.py -c -b --ti=devcon
  $ sign\Cross-Sign.ps1 -SourceDir dist -Force
  $ python.exe buildtap.py -p --ti=devcon
  $ Get-Item tap-windows*.exe|sign\Sign-File.ps1

Note that the "-Force" option for Cross-Sign.ps1 is *required* except in the
unlikely case you're appending a signature.

Next produce a driver submission cabinet files for attestation signing::

  $ sign\Create-DriverSubmission.ps1
  $ Get-ChildItem -Path disk1|sign\Sign-File.ps1

Three architecture-specific (i386, amd64, arm64) cabinet files are created.
Submit these to Windows Dev Center for attestation signing. Take care to only
request signatures applicable for each architecture.

After downloading the attestation-signed drivers as zip files put them into
a temporary directory under the tap-windows6 directory. Then extract the drivers
into the "dist" directory, produce an installer and sign it::

  $ cd tap-windows6
  $ Get-ChildItem -Path tempdir -Filter "*.zip"|sign\Extract-DriverSubmission.ps1
  $ python.exe buildtap.py -p --ti=devcon
  $ Get-Item tap-windows*.exe|sign\Sign-File.ps1

Note that these steps will fail unless cross-signed tapinstall.exe is present
in each architecture-specific directory (i386, amd64, arm64) under the "dist"
directory.

For more detailed instructions and background information please refer to
`this article <https://community.openvpn.net/openvpn/wiki/BuildingTapWindows6>`_ on OpenVPN community wiki.

Overriding setting defined in version.m4
----------------------------------------

It is possible to override one or more of the settings in version.m4 file with
the --versionoverride <file> option. Any settings given in the override file
have precedence over those in version.m4.

This is useful when building several tap-windows6 drivers with different
component ids for example.

Notes on proxies
----------------

It is possible to build tap-windows6 without connectivity to the Internet but 
any attempt to timestamp the driver will fail. For this reason configure your 
outbound proxy server before starting the build. Note that the command prompt 
also needs to be restarted to make use of new proxy settings.

MSM packaging
-------------

In order to build the MSM packages build and sign the driver first:

- Build the TAP driver with buildtap.py and "-b" flag.
- EV-sign the drivers
- WHQL/Attestation-sign the drivers

Place the signed drivers in a directory structure under tap-windows6
directory. Each platform directory should contain the EV-signed driver with a
"win10" subdirectory containing WHQL/Attestation signed driver for that
platform::

  dist
  ├── amd64
  │   ├── win10
  │   │   ├── OemVista.inf
  │   │   ├── tap0901.cat
  │   │   └── tap0901.sys
  │   ├── OemVista.inf
  │   ├── tap0901.cat
  │   └── tap0901.sys
  ├── arm64
  │   ├── win10
  │   │   ├── OemVista.inf
  │   │   ├── tap0901.cat
  │   │   └── tap0901.sys
  │   └── (Note: EV-signed driver for arm64 is not used.)
  ├── dist
  │   └── include
  │       └── tap-windows.h
  └── i386
      ├── win10
      │   ├── OemVista.inf
      │   ├── tap0901.cat
      │   └── tap0901.sys
      ├── OemVista.inf
      ├── tap0901.cat
      └── tap0901.sys

Building MSM packages requires Visual Studio 2019 (EWDK is not sufficient) and
the WiX Toolset installed. In a Developer Command Prompt for Visual Studio
2019, run::

  $ python buildtap.py -m --sdk=wdk

This will compile the installer.dll file with embedded drivers and package it
as a platform-dependent tap-windows-<version>-<platform>.msm files.

As the WiX Toolset does not support the arm64 platform yet, only amd64 and
i386 MSM files are built.

Optional: Consider EV-signing the MSM packages before deploying them. Thou,
MSM signature is ignored when merging MSM into MSI package, users get a choice
to validate the integrity of the downloaded MSM packages manually.

License
-------

See the file `COPYING <COPYING>`_.
