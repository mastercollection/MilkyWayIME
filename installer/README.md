# Installer

Installer and registration packaging assets belong here.

The project does not have a user-facing installer yet. For now, use the
developer registration scripts in [tools/register-msvc-debug.cmd](/D:/Git/MilkyWayIME/tools/register-msvc-debug.cmd)
and [tools/unregister-msvc-debug.cmd](/D:/Git/MilkyWayIME/tools/unregister-msvc-debug.cmd)
after building `MilkyWayIME.sln` in `Debug|x64` from an elevated Command Prompt.

The current developer registration flow copies the debug TSF DLL into
`%ProgramW6432%\MilkyWayIME` from `build\MilkyWayIME.Tsf\x64\Debug\mwime_tsf.dll`
and copies libhangul Hanja data into
`%ProgramW6432%\MilkyWayIME\data\hanja` before calling `regsvr32`.

Future installer packaging must include:

- `external\libhangul\data\hanja\hanja.txt` as `data\hanja\hanja.txt`
- `external\libhangul\data\hanja\mssymbol.txt` as `data\hanja\mssymbol.txt`

The runtime lookup path is relative to the installed TSF DLL directory first,
with source-tree data used only as a developer fallback.
