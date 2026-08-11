#include "run.h"

void run_reset(RunState *r, int title) {
    RunState zero = {0};
    *r = zero;
    r->title     = title;
    r->smoke_rng = SMOKE_RNG_SEED;
}
