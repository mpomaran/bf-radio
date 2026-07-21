@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
  echo Usage: tx_text.cmd message.txt
  exit /b 1
)
bin\bf_text_tx.exe --config config\digirig.cfg --output-level 0.3 --verbose "%~1"
