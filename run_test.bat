@echo off
C:\Users\user\src\_opencode_tests\7days\emulator\emulator.exe --frames 120 ..\brick.app > C:\Users\user\src\_opencode_tests\7days\brick_output.txt 2>&1
type C:\Users\user\src\_opencode_tests\7days\brick_output.txt
echo EXIT=%ERRORLEVEL%
