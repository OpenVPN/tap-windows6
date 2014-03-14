TAP-Windows driver (NDIS 6)
===========================

This is an NDIS 6 implementation of the TAP-Windows driver,
used by OpenVPN and other apps.  NDIS 6 drivers can run on
Windows Vista or higher.

Build
-----

To build, the following prerequisites are required:

- Python 2.7
- Microsoft Windows 7 WDK (Windows Driver Kit)
- Windows code signing certificate
- git (not strictly required, but useful for running commands using bundled bash shell)
- Source code directory of **devcon** sample from WDK (optional)

These instructions were tested on Windows 7 using the bash shell that is
bundled with git.

View build script options::

  $ python buildtap.py
  Usage: buildtap.py [options]

  Options:
    -h, --help         show this help message and exit
    -s SRC, --src=SRC  TAP-Windows top-level directory, default=c:\src\tap-
                       windows6
    --ti=TAPINSTALL    tapinstall (i.e. devcon) source directory (optional)
    -d, --debug        enable debug build
    -c, --clean        do an nmake clean before build
    -b, --build        build TAP-Windows and possibly tapinstall (add -c to
                       clean before build)
    --cert=CERT        Common name of code signing certificate, default=openvpn
    -a, --oas          Build for OpenVPN Access Server clients

Edit **version.m4** and **paths.py** as necessary then build::

  $ python buildtap.py -b

On successful completion, all build products will be placed in
the "dist" directory as well as tap6.tar.gz.

Install/Update/Remove
---------------------

- open an Administrator shell
- cd to **dist**
- cd to **amd64** or **i386** depending on your system's processor architecture.

Install::

  $ ./tapinstall install OemVista.inf TAP0901

Update::

  $ ./tapinstall update OemVista.inf TAP0901

Remove::

  $ ./tapinstall remove TAP0901
