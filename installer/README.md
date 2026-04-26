# Installer

Installer and registration packaging assets belong here.

The project does not have a user-facing installer yet. For now, use the
developer registration scripts in [tools/register-msvc-debug.cmd](/D:/Git/MilkyWayIME/tools/register-msvc-debug.cmd)
and [tools/unregister-msvc-debug.cmd](/D:/Git/MilkyWayIME/tools/unregister-msvc-debug.cmd)
after building `MilkyWayIME.sln` in `Debug|x64` from an elevated Command Prompt.

The current developer registration flow copies the debug TSF DLL into
`%ProgramW6432%\MilkyWayIME` from `build\MilkyWayIME.Tsf\x64\Debug\mwime_tsf.dll`
and copies libhangul Hanja data into
`%ProgramW6432%\MilkyWayIME\data\hanja` before calling `regsvr32`. Binary cache
files are required; run `tools\generate-hanja-cache.cmd` if they are missing.

Future installer packaging must include:

- `external\libhangul\data\hanja\hanja.bin` as `data\hanja\hanja.bin`
- `external\libhangul\data\hanja\mssymbol.bin` as `data\hanja\mssymbol.bin`

The runtime lookup path is relative to the installed TSF DLL directory first.
The runtime loads binary cache files only; text sources are development inputs
for regenerating those binary caches.
