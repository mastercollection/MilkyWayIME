# Dictionary Adapter

Dictionary and Hanja data source adapters should be isolated here.

The current adapter wraps libhangul `HanjaTable` ownership and lookup. It keeps
the engine-facing candidate model free from libhangul headers, lazy-loads
`hanja.txt` and `mssymbol.txt`, and uses exact lookup only.
