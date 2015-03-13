# Prerequisites #

  * [OpenSSL-Win32](http://www.slproweb.com/products/Win32OpenSSL.html) - download ~8MB Installer edition of latest version
  * [expat XML Parser](http://sourceforge.net/projects/expat/files/) - download 2.0.1 .tgz
  * Xerces-C is no longer recommended as it does not properly work (on Windows, anyway).

# Building #

  1. Make a dir, ex. d:\sdk
  1. Install OpenSSL somewhere, ex d:\sdk\OpenSSL. Add **OpenSSL\include** to VC include path, add **OpenSSL\lib\VC\static** to VC Lib path.
  1. Unzip expat and put in **d:\sdk\expat**
  1. Import 'expat.dsw' into current Visual Studio.
  1. Edit expat\_external.h, and around line 62, comment out 'declspec(dllimport)'. We're not building expat as DLL.
  1. Build 'Release' of 'expat\_static' project.
  1. Unzip asdcp build files into **d:\sdk\asdcp**
  1. Download latest [asdcp-lib](http://www.cinecert.com/asdcplib/) and copy contents of 'src' dir into **d:\sdk\asdcp\src**.
  1. Build 'Release' of asdcp.
  1. Find release bits in asdcp\Release

This results in static MT runtime, manifest-less build of asdcp-lib and asdcp-test.exe