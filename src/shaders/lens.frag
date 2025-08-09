#version 410 core
out vec4 FragColor;

// Remove binding points as they're not supported in macOS OpenGL 4.1
layout(std140) uniform CameraBlock {
    mat4 uView;
    mat4 uProj;
    mat4 uViewProj;
    vec4 uCamPos;
};

layout(std140) uniform DiskBlock {
    vec4 uDisk; // x=r1, y=r2, z=density
};

in vec2 vUV;

bool intersectSphere(vec3 ro, vec3 rd, vec3 center, float radius, out float tHit) {
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius*radius;
    float disc = b*b - c;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    float t0 = -b - s;
    float t1 = -b + s;
    tHit = (t0 > 0.0) ? t0 : ((t1 > 0.0) ? t1 : -1.0);
    return tHit > 0.0;
}

bool intersectPlaneY(vec3 ro, vec3 rd, float y, out float t) {
    if (abs(rd.y) < 1e-6) return false;
    t = (y - ro.y) / rd.y;
    return t > 0.0;
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;

    mat4 invProj = inverse(uProj);
    mat4 invView = inverse(uView);
    vec4 clip = vec4(ndc, 1.0, 1.0);
    vec4 viewPos = invProj * clip;
    viewPos /= viewPos.w;
    vec3 rdView = normalize(viewPos.xyz);
    vec3 rd = normalize((invView * vec4(rdView, 0.0)).xyz);
    vec3 ro = uCamPos.xyz;

    float r_s = uDisk.x / 2.2;
    float r1  = uDisk.x;
    float r2  = uDisk.y;

    float tHit;
    bool hitBH = intersectSphere(ro, rd, vec3(0.0), r_s, tHit);

    float tPlane;
    bool hitPlane = intersectPlaneY(ro, rd, 0.0, tPlane);
    bool inDisk = false;
    vec3 pDisk = vec3(0.0);
    if (hitPlane) {
        pDisk = ro + tPlane * rd;
        float R = length(pDisk.xz);
        inDisk = (R >= r1 && R <= r2);
    }

    vec3 col = vec3(0.0);
    if (hitBH && (!hitPlane || tHit < tPlane)) {
        col = vec3(0.0);
    } else if (hitPlane && inDisk) {
        float R = clamp(length(pDisk.xz), r1, r2);
        float a = atan(pDisk.z, pDisk.x);
        float v = 1.0 - smoothstep(r1, r2, R);
        vec3 tint = vec3(1.0, 0.9, 0.7);
        col = v * tint * (0.8 + 0.2 * sin(5.0*a));
    } else {
        float r = length(ndc);
        float vignette = smoothstep(1.2, 0.2, r);
        col = vec3(0.02) * vignette;
    }

    FragColor = vec4(col, 1.0);
}