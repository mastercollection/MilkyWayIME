# Hanja Dictionary

MilkyWayIME uses libhangul's bundled Hanja data for candidate generation.

Runtime builds load these files from the installed TSF DLL directory first:

- `data\hanja\hanja.bin`
- `data\hanja\mssymbol.bin`

The source-tree files under `external\libhangul\data\hanja` remain the developer
generation and packaging source. `hanja.bin` contains forward and reverse
indexes; `mssymbol.bin` contains the forward index. Runtime lookup does not load
the text files as a fallback. Lookup remains exact dictionary lookup and does
not do morphology or selection-internal search.

Regenerate the binary caches from an elevated Visual Studio/MSBuild environment:

```cmd
tools\generate-hanja-cache.cmd
```
