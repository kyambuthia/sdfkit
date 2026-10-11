// Demo scene: a morphing ground plus a few solids, enough to prove the shell.
// Edit this file while the demo is running - it reloads on save.

float scene(vec3 p) {
    // Ground that breathes. A heightfield like this is a poor distance bound,
    // so the marcher's conservative step factor matters here.
    float wave = 0.35 * sin(p.x * 0.45 + TIME * 0.7)
               + 0.28 * sin(p.z * 0.38 - TIME * 0.5)
               + 0.12 * sin((p.x + p.z) * 0.9 + TIME);
    float ground = (p.y - wave) * 0.6;

    float ball = sdf_sphere(p, vec3(0.0, 1.2 + 0.4 * sin(TIME * 1.3), 0.0), 0.9);
    float slab = sdf_round_box(p, vec3(3.2, 0.9, -1.0), vec3(0.8, 0.9, 0.8), 0.12);
    float limb = sdf_round_cone(p, vec3(-3.0, 0.2, 1.0), vec3(-2.2, 2.4, -0.4), 0.45, 0.14);

    float solids = min(min(ball, slab), limb);
    return sdf_smin(ground, solids, 0.45);
}

vec3 sky(vec3 dir) {
    return mix(vec3(0.46, 0.55, 0.66), vec3(0.09, 0.13, 0.21),
               clamp(dir.y * 1.3, 0.0, 1.0));
}

vec3 shade(vec3 p, vec3 n, float t) {
    vec3 light = normalize(vec3(0.55, 0.8, 0.3));
    float diffuse = max(dot(n, light), 0.0) * sdf_shadow(p, light, 14.0);
    float occlusion = sdf_occlusion(p, n);
    float ambient = (0.24 + 0.2 * n.y) * occlusion;
    vec3 albedo = mix(vec3(0.30, 0.42, 0.40), vec3(0.78, 0.76, 0.70),
                      smoothstep(0.55, 0.95, n.y));
    vec3 colour = albedo * (ambient + diffuse * vec3(1.25, 1.18, 1.05));
    return mix(colour, sky(normalize(p - EYE)), clamp(t / 90.0, 0.0, 1.0));
}
