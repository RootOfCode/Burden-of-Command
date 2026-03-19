@echo off

set SCRIPT_DIR=%~dp0
set LISP=%SCRIPT_DIR%boc.lisp

echo Building Burden of Command...
echo.

sbcl --no-sysinit --no-userinit ^
 --eval "(push :building *features*)" ^
 --load "%LISP%" ^
 --eval "(sb-ext:save-lisp-and-die \"boc.exe\" :toplevel #'cl-user::main :executable t :purify t)"

if exist "boc.exe" (
 echo.
 echo Done! Binary: boc.exe
) else (
 echo.
 echo Build failed. Make sure sbcl is installed and boc.lisp is present.
 exit /b 1
)
