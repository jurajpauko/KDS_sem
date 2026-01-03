@echo off
REM Compile both sender.cpp and reciever.cpp with OpenSSL and Windows sockets

set OPENSSL_DIR="C:\Program Files\OpenSSL-Win64"

echo Compiling sender.cpp...
cl /EHsc ^
 /I %OPENSSL_DIR%\include ^
 sender.cpp ^
 /link ^
 /LIBPATH:%OPENSSL_DIR%\lib\VC\x64\MD ^
 libcrypto.lib Ws2_32.lib

echo.
echo Compiling reciever.cpp...
cl /EHsc ^
 /I %OPENSSL_DIR%\include ^
 reciever.cpp ^
 /link ^
 /LIBPATH:%OPENSSL_DIR%\lib\VC\x64\MD ^
 libcrypto.lib Ws2_32.lib

echo.
echo Build finished. Executables: sender.exe, reciever.exe
pause
