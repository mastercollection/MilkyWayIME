# Installer

Installer and registration packaging assets belong here.

The project does not have a user-facing installer yet. For now, use the
developer registration scripts in [tools/register-msvc-debug.cmd](/D:/Git/MilkyWayIME/tools/register-msvc-debug.cmd)
and [tools/unregister-msvc-debug.cmd](/D:/Git/MilkyWayIME/tools/unregister-msvc-debug.cmd)
after building `MilkyWayIME.sln` in `Debug|x64` from an elevated Command Prompt.

The current developer registration flow copies the debug TSF DLL into
`%ProgramW6432%\MilkyWayIME` from `build\MilkyWayIME.Tsf\x64\Debug\mwime_tsf.dll`
before calling `regsvr32`.
