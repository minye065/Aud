@echo off

IF NOT EXIST ..\bin mkdir ..\bin
pushd ..\bin
cl -DALPHA=1 -DHDEBUG=1 -FC -Zi c:\dev\Aud\src\win32.c c:\dev\Aud\src\getActiveNotes.c user32.lib gdi32.lib dsound.lib
win32.exe
popd
