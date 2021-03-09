cls
REM @echo off
call "F:\Vs2017\IDE\VC\Auxiliary\Build\vcvarsall.bat" x64


set inputFile=src\hexraysIDAplus.cpp
set includePath=/I .\include
set target=.\bin\hexraysIDAplus64.dll
set oLib=.\bin\hexRaysIDAplus64.lib
set exLibPath=/LIBPATH:.\lib\x64_win_vc_64\ ^
/LIBPATH:.\lib\x64_win_qt\

set marco=/D __NT__ /D __IDP__  /D DEBUG /D __EA64__
set cflags=/Od /Zi /EHsc /std:c++latest %includePath%  /LD %marco%
set rctLib=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib ^
ws2_32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib ^
odbccp32.lib compress.lib 
set exLib=ida.lib network.lib pro.lib Qt5Core.lib Qt5Gui.lib Qt5PrintSupport.lib Qt5Widgets.lib

cl.exe %cflags% %inputFile%  ^
/link  %rctLib% %exLib% %exLibPath% ^
/OUT:%target% /IMPLIB:%oLib%

copy .\bin\hexraysIDAplus64.dll F:\ctfTools\debugTools\IDA\IDA7.3\plugins\
del .\*.obj .\*.pdb .\bin\*.exp .\bin\*.lib .\bin\*.ilk .\bin\*.pdb