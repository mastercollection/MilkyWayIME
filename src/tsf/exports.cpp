#ifdef _WIN32
#define MWIME_EXPORT __declspec(dllexport)
#else
#define MWIME_EXPORT
#endif

extern "C" MWIME_EXPORT int MwimeTsfPlaceholder() {
  return 0;
}
