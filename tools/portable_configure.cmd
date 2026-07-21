@echo off
setlocal
cd /d "%~dp0"
bin\digirig_config.exe --write-config config\digirig.cfg --yes
