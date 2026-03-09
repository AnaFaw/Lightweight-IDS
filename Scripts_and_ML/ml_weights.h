#pragma once
#include <math.h>

static const int ML_N = 4;
static const float ML_W[ML_N] = {0.40694213f, 0.00027088f, 0.00000000f, 0.01531746f};
static const float ML_B = -6.30076460f;

static inline float ml_sigmoid(float z){ return 1.0f/(1.0f+expf(-z)); }
