@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo [ERROR] vcvarsall.bat failed
    exit /b 1
)
cd /d D:\FileTransfer\Client
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [ERROR] cmake configure failed
    exit /b 1
)
cmake --build build --config Release
if errorlevel 1 (
    echo [ERROR] cmake build failed
    exit /b 1
)
echo [OK] Build succeeded
endlocal
