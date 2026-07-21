@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
  set PREFIX=rx\capture
) else (
  set PREFIX=%~1
)
bin\bf_chirp_rx.exe --config config\digirig.cfg --record-seconds 30 --segment-seconds 3 --verbose "%PREFIX%"
