#version 330 core

in vec2 v_uv;
out vec4 FragColor;

uniform vec2  u_resolution;
uniform float u_time;

// Camera
uniform vec3 u_camPos;
uniform vec3 u_camForward;
uniform vec3 u_camRight;
uniform vec3 u_camUp;
uniform float u_fov;

// Fractal parameters
uniform float u_power;
uniform int   u_maxIter;
uniform float u_bailout;

// Raymarch parameters
uniform int   u_maxSteps;
uniform float u_maxDist;
uniform float u_epsilon;

// Shading / color
uniform vec3 u_colorA;
uniform vec3 u_colorB;
uniform int  u_enableAO;
uniform int  u_enableShadows;

// Distance Estimator
float mandelbulbDE(in vec3 pos)
{
    vec3 z = pos;
    float dr = 1.0;
    float r  = 0.0;

    const int HARD_MAX = 64; // compile-time upper bound

    for (int i = 0; i < HARD_MAX; i++) {
        if (i >= u_maxIter)
            break;

        r = length(z);
        if (r > u_bailout)
            break;

        float theta = acos(clamp(z.z / max(r, 1e-6), -1.0, 1.0));
        float phi   = atan(z.y, z.x);

        float zr = pow(r, u_power);
        dr = pow(r, u_power - 1.0) * u_power * dr + 1.0;

        theta *= u_power;
        phi   *= u_power;

        z = zr * vec3(
            sin(theta) * cos(phi),
            sin(theta) * sin(phi),
            cos(theta)
        );

        z += pos;
    }

    return 0.5 * log(r) * r / dr;
}

float raymarch(in vec3 ro, in vec3 rd, out int steps)
{
    float dist = 0.0;
    const int HARD_MAX_STEPS = 512;

    for (int i = 0; i < HARD_MAX_STEPS; i++) {
        if (i >= u_maxSteps)
            break;

        vec3 p = ro + rd * dist;
        float dS = mandelbulbDE(p);

        if (dS < u_epsilon) {
            steps = i;
            return dist;
        }

        if (dist > u_maxDist)
            break;

        dist += dS;
    }

    steps = u_maxSteps;
    return -1.0;
}

vec3 estimateNormal(in vec3 p)
{
    float eps = u_epsilon * 2.0;
    float d   = mandelbulbDE(p);
    vec2 e = vec2(1.0, -1.0) * eps;

    vec3 n = normalize(vec3(
        mandelbulbDE(p + vec3(e.x, e.y, e.y)) - d,
        mandelbulbDE(p + vec3(e.y, e.x, e.y)) - d,
        mandelbulbDE(p + vec3(e.y, e.y, e.x)) - d
    ));
    return n;
}

float softShadow(in vec3 ro, in vec3 rd)
{
    float res = 1.0;
    float t = 0.02;

    for (int i = 0; i < 50; i++) {
        vec3 p = ro + rd * t;
        float h = mandelbulbDE(p);
        if (h < 0.0005)
            return 0.0;

        res = min(res, 10.0 * h / t);
        t += clamp(h, 0.02, 0.2);
        if (t > 20.0)
            break;
    }

    return clamp(res, 0.0, 1.0);
}

float ambientOcclusion(in vec3 p, in vec3 n)
{
    float ao = 0.0;
    float sca = 1.0;

    for (int i = 0; i < 5; i++) {
        float h = 0.01 + 0.12 * float(i) / 4.0;
        float d = mandelbulbDE(p + n * h);
        ao += (h - d) * sca;
        sca *= 0.95;
    }

    return clamp(1.0 - 3.0 * ao, 0.0, 1.0);
}

void main()
{
    // Screen-space coordinates
    vec2 uv = (gl_FragCoord.xy / u_resolution.xy) * 2.0 - 1.0;
    uv.x *= u_resolution.x / u_resolution.y;

    // Camera ray
    vec3 rd = normalize(u_camForward +
                        uv.x * u_camRight * u_fov +
                        uv.y * u_camUp * u_fov);

    int steps;
    float t = raymarch(u_camPos, rd, steps);

    vec3 col;

    if (t > 0.0) {
        vec3 p = u_camPos + rd * t;
        vec3 n = estimateNormal(p);

        vec3 lightDir = normalize(vec3(0.4, 0.7, 0.2));

        float diff = max(dot(n, lightDir), 0.0);
        if (u_enableShadows != 0) {
            float sh = softShadow(p + n * 0.01, lightDir);
            diff *= sh;
        }

        float spec = 0.0;
        if (diff > 0.0) {
            vec3 h = normalize(lightDir - rd);
            spec = pow(max(dot(n, h), 0.0), 32.0);
        }

        float ao = 1.0;
        if (u_enableAO != 0) {
            ao = ambientOcclusion(p, n);
        }

        float stepRatio = clamp(float(steps) / float(u_maxSteps), 0.0, 1.0);
        vec3 baseColor  = mix(u_colorA, u_colorB, stepRatio);

        vec3 ambient = 0.2 * ao * baseColor;
        vec3 diffuse = diff * baseColor;
        vec3 specCol = spec * vec3(1.0);

        col = ambient + diffuse + specCol;
    } else {
        // Background gradient
        float h = 0.5 * (rd.y + 1.0);
        col = mix(vec3(0.03, 0.03, 0.08),
                  vec3(0.2, 0.3, 0.45), h);
    }

    // Gamma correction
    col = pow(col, vec3(0.4545)); // ~1/2.2

    FragColor = vec4(col, 1.0);
}
