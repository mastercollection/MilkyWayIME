# Dictionary Adapter

Dictionary and Hanja data source adapters should be isolated here.

The current Hanja adapter searches committed static C++ tables generated from
libhangul's bundled `hanja.txt` and `mssymbol.txt`. It keeps the engine-facing
candidate model free from libhangul Hanja headers and uses exact lookup only.
Runtime lookup does not load txt or binary cache files, and installed
ProgramData/DLL-adjacent Hanja data replacement is intentionally unsupported.

Regenerate the committed table after updating libhangul Hanja text data:

```cmd
tools\generate-static-hanja.cmd
```

Commit the generated `generated_hanja_data.cpp` together with the source data
change. Normal builds and runtime lookup do not depend on this generator.
