@echo off
REM Store the current directory
set "original_dir=%cd%"

REM Change to the shaders directory
cd /d "%~dp0\shaders"

REM Check if glslc is available
where glslc >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: glslc command not found. Please ensure it's in your PATH.
    cd /d "%original_dir%"
    exit /b 1
)

REM Loop through all .vert files and compile them
for %%f in (*.vert) do (
    echo Compiling %%f...
    glslc "%%f" -o "%%~nf.vert.spv"
    if errorlevel 1 (
        echo Error compiling %%f
    ) else (
        echo Successfully compiled %%f to %%~nf.vert.spv
    )
)

REM Loop through all .frag files and compile them
for %%f in (*.frag) do (
    echo Compiling %%f...
    glslc "%%f" -o "%%~nf.frag.spv"
    if errorlevel 1 (
        echo Error compiling %%f
    ) else (
        echo Successfully compiled %%f to %%~nf.frag.spv
    )
)

echo Compilation process completed.

REM Change back to the original directory
cd /d "%original_dir%"
