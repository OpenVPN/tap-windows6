TAP-Windows driver (NDIS 6)
===========================

This is an NDIS 6 implementation of the TAP-Windows driver, used by OpenVPN and 
other apps. NDIS 6 drivers can run on Windows Vista or higher.

Build
-----

To build, the following prerequisites are required:

- Python 2.7
- Microsoft Windows 7 WDK (Windows Driver Kit)
- Windows code signing certificate
- Git (not strictly required, but useful for running commands using bundled bash shell)
- Source code directory of **devcon** sample from WDK (optional)

Make sure you add Python's install directory (usually c:\python27) to the PATH 
environment variable.

These instructions have been tested on Windows 7 using Git Bash, as well as on 
Windows 2012 Server using Git Bash and Windows Powershell.

View build script options::

  $ python buildtap.py
  Usage: buildtap.py [options]

  Options:
    -h, --help         show this help message and exit
    -s SRC, --src=SRC  TAP-Windows top-level directory, default=<CWD>
    --ti=TAPINSTALL    tapinstall (i.e. devcon) source directory (optional)
    -d, --debug        enable debug build
    -c, --clean        do an nmake clean before build
    -b, --build        build TAP-Windows and possibly tapinstall (add -c to
                       clean before build)
    --cert=CERT        Common name of code signing certificate, default=openvpn
    --crosscert=CERT   The cross-certificate file to use, default=MSCV-
                       VSClass3.cer
    --timestamp=URL    Timestamp URL to use, default=http://timestamp.verisign.c
                       om/scripts/timstamp.dll
    -a, --oas          Build for OpenVPN Access Server clients

Edit **version.m4** and **paths.py** as necessary then build::

  $ python buildtap.py -b

On successful completion, all build products will be placed in the "dist" 
directory as well as tap6.tar.gz.

Install/Update/Remove
---------------------

The driver can be installed using a command-line tool, devcon.exe, which is 
bundled with OpenVPN and tap-windows installers. Note that in some versions of 
OpenVPN devcon.exe is called tapinstall.exe. To install, update or remove the 
tap-windows NDIS 6 driver follow these steps:

- place devcon.exe/tapinstall.exe to your PATH
- open an Administrator shell
- cd to **dist**
- cd to **amd64** or **i386** depending on your system's processor architecture.

Install::

  $ tapinstall install OemVista.inf TAP0901

Update::

  $ tapinstall update OemVista.inf TAP0901

Remove::

  $ tapinstall remove TAP0901

Notes on proxies
----------------

It is possible to build tap-windows6 without connectivity to the Internet but 
any attempt to timestamp the driver will fail. For this reason configure your 
outbound proxy server before starting the build. Note that the command prompt 
also needs to be restarted to make use of new proxy settings.
