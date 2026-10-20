@echo off

IF NOT EXIST ..\bin mkdir ..\bin
pushd ..\bin
cl -DALPHA=1 -DHDEBUG=1 -FC -Zi %~dp0win32.c %~dp0getActiveNotes.c %~dp0translator.c user32.lib gdi32.lib dsound.lib comdlg32.lib
win32.exe
popd
