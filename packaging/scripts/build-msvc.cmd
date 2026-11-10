@echo off
setlocal
set "KIT=C:\CPMImageMSVC"
set "VC=%KIT%\MSVC\VC\Tools\MSVC\14.44.35207"
set "SDK=%KIT%\SDK"
set "CRT=%KIT%\MSVC\VC\Redist\MSVC\14.44.35112\arm64\Microsoft.VC143.CRT"
set "INCLUDE=%VC%\include;%SDK%\Include\10.0.26100.0\ucrt;%SDK%\Include\10.0.26100.0\shared;%SDK%\Include\10.0.26100.0\um"
set "LIB=%VC%\lib\arm64;%SDK%\Lib\10.0.26100.0\ucrt\arm64;%SDK%\Lib\10.0.26100.0\um\arm64"
set "VCToolsInstallDir=%VC%\"
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "QT_PLUGIN_PATH=%KIT%\Qt\plugins"
if not defined CPMIMAGE_ROOT set "CPMIMAGE_ROOT=C:\CPMImageSource\cpmimage-feasibility"
if not defined CPMIMAGE_HELPER set "CPMIMAGE_HELPER=%CPMIMAGE_ROOT%\prototype\build-windows\helper-readonly.exe"
if not defined CPMIMAGE_PYTHON set "CPMIMAGE_PYTHON=%CPMIMAGE_ROOT%\windows-runtime\offline-media\CPMImageTests\python\python.exe"
set "PATH=%VC%\bin\Hostarm64\arm64;%CRT%;%KIT%\CMake\bin;%KIT%\Qt\bin;%SDK%\bin\10.0.26100.0\arm64;%SystemRoot%\System32;%SystemRoot%"
if not exist "%KIT%\results" mkdir "%KIT%\results"
cl /Bv > "%KIT%\results\compiler.txt" 2>&1
cmake --fresh -S "%CPMIMAGE_ROOT%\prototype" -B "%KIT%\app-build" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DCMAKE_PREFIX_PATH="%KIT%\Qt" -DCMAKE_CXX_COMPILER=cl -DCMAKE_RC_COMPILER=C:/CPMImageMSVC/SDK/bin/10.0.26100.0/arm64/rc.exe -DCMAKE_MT=C:/CPMImageMSVC/SDK/bin/10.0.26100.0/arm64/mt.exe > "%KIT%\results\configure.txt" 2>&1
if errorlevel 1 goto failed
cmake --build "%KIT%\app-build" > "%KIT%\results\build.txt" 2>&1
if errorlevel 1 goto failed
ctest --test-dir "%KIT%\app-build" --output-on-failure --timeout 90 > "%KIT%\results\qt.txt" 2>&1
if errorlevel 1 goto failed
"%CPMIMAGE_PYTHON%" "%CPMIMAGE_ROOT%\prototype\tests\broker_check.py" > "%KIT%\results\broker.txt" 2>&1
if errorlevel 1 goto failed
"%CPMIMAGE_PYTHON%" -I -B "%CPMIMAGE_ROOT%\prototype\tests\final_track_check.py" --root "%CPMIMAGE_ROOT%" --helper "%CPMIMAGE_HELPER%" --report "%KIT%\results\final-track.json"
if errorlevel 1 goto failed
echo 0 > "%KIT%\results\build.exit"
exit /b 0
:failed
echo 1 > "%KIT%\results\build.exit"
exit /b 1
