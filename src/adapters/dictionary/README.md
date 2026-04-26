# Dictionary Adapter

Dictionary and Hanja data source adapters should be isolated here.

The current adapter wraps libhangul `HanjaTable` ownership and lookup. It keeps
the engine-facing candidate model free from libhangul headers, lazy-loads
`hanja.bin` and `mssymbol.bin`, and uses exact lookup only. Runtime lookup does
not load the text sources as a fallback. Hangul-to-Hanja, Hanja-to-Hangul
reverse lookup, and symbol lookup all go through the libhangul table API; the
adapter should not build a separate reverse index.
