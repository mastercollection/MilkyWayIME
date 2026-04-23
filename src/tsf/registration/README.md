# Registration Layer

COM registration and profile registration code belongs here.

The current implementation exposes `DllRegisterServer` / `DllUnregisterServer`
from `mwime_tsf` and writes per-user COM registration under
`HKCU\Software\Classes\CLSID\...` so the development DLL can be registered
without requiring an installer.
