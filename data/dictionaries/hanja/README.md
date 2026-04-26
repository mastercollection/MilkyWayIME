# Hanja Dictionary

MilkyWayIME uses libhangul's bundled Hanja data for candidate generation.

Runtime builds load these files from the installed TSF DLL directory first:

- `data\hanja\hanja.txt`
- `data\hanja\mssymbol.txt`

The source-tree files under `external\libhangul\data\hanja` remain the developer
fallback and packaging source. Lookup is exact one-character lookup only.
