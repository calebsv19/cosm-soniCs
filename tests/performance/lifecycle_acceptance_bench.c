// Reuses the integrated workflow to measure repeated complete ownership lifetimes in one process.
#define DAW_SUSTAINED_ENTRY sustained_workflow_entry
#include "sustained_acceptance_bench.c"
#undef DAW_SUSTAINED_ENTRY

// Replaces complete projects/engines repeatedly and reports settled process memory without purging allocators.
int main(int argc, char** argv) {
    assert(argc == 2);
    int cycles = atoi(argv[1]);
    assert(cycles >= 2 && cycles <= 60);
    SDL_setenv("DAW_BENCH_HEAP", "1", 1);
    SDL_setenv("DAW_BENCH_UI", "1", 1);
    for (int cycle = 0; cycle < cycles; ++cycle) {
        printf("{\"type\":\"lifecycle\",\"cycle\":%d,\"phase\":\"begin\"}\n", cycle);
        export_energy = 0;
        char* args[] = {"sustained", "mixed", "48000", "128", "8", "10", "1", "32"};
        assert(sustained_workflow_entry(8, args) == 0);
        printf("{\"type\":\"lifecycle\",\"cycle\":%d,\"phase\":\"complete\"}\n", cycle);
        fflush(stdout);
    }
    return 0;
}
