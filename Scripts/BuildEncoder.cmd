@echo off
call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE "%~dp0EncodeCapture.cpp" /Fe:"%~dp0..\Saved\EncodeCapture.exe" /Fo:"%~dp0..\Saved\EncodeCapture.obj" /link mfplat.lib mfreadwrite.lib mfuuid.lib windowscodecs.lib ole32.lib propsys.lib
