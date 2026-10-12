float slopeStep(float x, float a, float b, float slope_width) {
    float start_slope = a - slope_width;
    float end_slope = b + slope_width;

    return clamp((x - start_slope) / (a - start_slope), 0.0, 1.0) *
           clamp((end_slope - x) / (end_slope - b), 0.0, 1.0);
}