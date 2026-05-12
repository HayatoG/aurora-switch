#include <aurora/main.h>
#undef main

#if !defined(__SWITCH__)
#include <SDL3/SDL_main.h>
#endif

int main(int argc, char** argv) { return aurora_main(argc, argv); }
