@echo off

SETLOCAL

:�����œn���ꂽ�t�@�C���Q���擾
set OUTFILES=%*
:�p�X��؂��u��
set OUTFILES=%OUTFILES:/=\%


:del_file
:1�t�@�C������del�R�}���h�ɓn���č폜
for /F "tokens=1,*" %%f in ("%OUTFILES%") DO (
  if exist %%f del /F /Q %%f
  set OUTFILES=%%g
)

:����ԍ폜�ł�����I��
if "%OUTFILES%" == "" goto :EOF
goto :del_file


:END

ENDLOCAL
exit /b
