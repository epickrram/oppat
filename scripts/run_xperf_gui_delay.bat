
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
set SFX=gui_delay
set PFX=win
set ODIR=..\oppat_data\%PFX%\%SFX%
set SCR_DIR=%~dp0
set BIN_DIR=%SCR_DIR%\..\bin
echo SCR_DIR=%SCR_DIR%
echo BIN_DIR=%BIN_DIR%

mkdir %ODIR%

set _NT_SYMBOL_PATH=srv*%SystemDrive%\symbols*http://msdl.microsoft.com/download/symbols;C:\MySymbols

@rem start c:\data\pcm\pcm\bin\pcm.exe 0.1 -csv=%ODIR%\pcm.csv

@echo Administrative permissions required. Detecting permissions...
@net session >nul 2>&1
@if %errorLevel% == 0 (
	@echo Success: Administrative permissions confirmed.
) else (
	@echo Failure: Current permissions inadequate.
	goto :EOF
)

set WAIT_FILE=wait.pid.txt
del %WAIT_FILE%
start  %BIN_DIR%\wait.exe %ODIR%\etw_energy2.txt %ODIR%\wait.txt
:CheckForFile
IF EXIST %WAIT_FILE% GOTO FoundIt
TIMEOUT /T 1 >nul
GOTO CheckForFile
:FoundIt
ECHO Found: %WAIT_FILE%
for /f "delims=" %%x in (%WAIT_FILE%) do set pid=%%x


xperf -on PROC_THREAD+LOADER+PROFILE+CSWITCH+DISPATCHER+DISK_IO+NetworkTrace -stackWalk cswitch+profile -start usersession -on Microsoft-Windows-Win32k


start %BIN_DIR%\win_gui_delay.exe
timeout /T 12 > nul
taskkill /im win_gui_delay.exe
timeout /T 2 > nul
@rem %BIN_DIR%\spin.exe 4 > %ODIR%\spin.txt
xperf -stop usersession -stop -d %ODIR%\etw_trace.etl
%BIN_DIR%\win_send_signal.exe %pid%
@rem taskkill /im pcm.exe
xperf -i %ODIR%\etw_trace.etl -o %ODIR%\etw_trace.txt -symbols verbose -tle -tti -a dumper -stacktimeshifting 
del %WAIT_FILE%

@echo {"file_list":[ > %ODIR%/file_list.json
@echo    {"cur_dir":"%%root_dir%%/oppat_data/%PFX%/%SFX%"}, >> %ODIR%/file_list.json
@echo    {"cur_tag":"%PFX%_%SFX%"}, >> %ODIR%/file_list.json
@echo    {"txt_file":"etw_trace.txt", "tag":"%%cur_tag%%", "type":"ETW"}, >> %ODIR%/file_list.json
@echo    {"txt_file":"etw_energy2.txt", "wait_file":"wait.txt", "tag":"%%cur_tag%%", "type":"LUA"} >> %ODIR%/file_list.json
@echo   ]} >> %ODIR%/file_list.json

