# Hanja Dictionary

MilkyWayIME uses libhangul's bundled Hanja data for candidate generation.

Runtime builds search committed static C++ tables in
`src\adapters\dictionary\generated_hanja_data.cpp`. They do not load
`hanja.bin`, `mssymbol.bin`, or the text sources at startup or lookup time.
Lookup remains exact dictionary lookup and does not do morphology or
selection-internal search.

The source-tree files under `external\libhangul\data\hanja` are developer inputs
for regenerating the committed C++ table:

```cmd
tools\generate-static-hanja.cmd
```

Commit the regenerated C++ file with any source data change. ProgramData or
DLL-adjacent runtime Hanja data replacement is no longer supported.
