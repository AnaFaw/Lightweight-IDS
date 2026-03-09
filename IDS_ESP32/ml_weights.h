#pragma once
#include <math.h>

static const int ML_N = 4;
static const float ML_W[ML_N] = {0.46110623f, 1.13030811f, 0.11440093f, -0.00186953f};
static const float ML_B = -3.54567824f;

static inline float ml_sigmoid(float z){ return 1.0f/(1.0f+expf(-z)); }
