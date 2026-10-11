// Distance primitives, GPU side.
//
// Twin of include/sdfkit/sdf.hpp. These two must stay numerically identical:
// the field is both the surface the renderer sphere-traces and the field
// collision queries, and that is the whole reason for building on distance
// fields. tests/parity is what stops them drifting.
//
// Included by concatenation at load time, not by #include - the GL backend
// has no preprocessor for files, and loading shaders as plain text at runtime
// is what makes hot-reload possible.

float sdf_smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float sdf_smax(float a, float b, float k) {
    return -sdf_smin(-a, -b, k);
}

float sdf_sphere(vec3 p, vec3 centre, float radius) {
    return length(p - centre) - radius;
}

float sdf_round_box(vec3 p, vec3 centre, vec3 half_extent, float radius) {
    vec3 q = abs(p - centre) - half_extent;
    return length(max(q, vec3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0) - radius;
}

float sdf_round_cone(vec3 p, vec3 a, vec3 b, float r1, float r2) {
    vec3 ba = b - a;
    float l2 = dot(ba, ba);
    float rr = r1 - r2;
    float a2 = l2 - rr * rr;
    float il2 = 1.0 / l2;

    vec3 pa = p - a;
    float y = dot(pa, ba);
    float z = y - l2;
    vec3 cross_term = pa * l2 - ba * y;
    float x2 = dot(cross_term, cross_term);
    float y2 = y * y * l2;
    float z2 = z * z * l2;

    float k = sign(rr) * rr * rr * x2;
    if (sign(z) * a2 * z2 > k) return sqrt(x2 + z2) * il2 - r2;
    if (sign(y) * a2 * y2 < k) return sqrt(x2 + y2) * il2 - r1;
    return (sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}
