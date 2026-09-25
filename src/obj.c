// Terminal OBJ viewer: textured, lit, multithreaded software rasterizer over notcurses.
//
// Controls:
//   q / ESC        quit
//   arrow keys     orbit camera (also W/A/S/D)
//   + / -          zoom in / out
//   mouse drag     orbit camera
//   mouse wheel    zoom
//   r              toggle auto-rotate
//   n / p          next / previous model (when multiple given on the command line)
//   x              toggle wireframe overlay
//   f              toggle solid fill on/off (wireframe-only mode)
//   b              cycle render/blit mode: AUTO -> HALFBLOCK -> BRAILLE -> ASCII-CELL
//
// Build:
//   gcc -O2 -pthread -o obj_viewer obj.c -lnotcurses -lnotcurses-core -lm
//
// Usage:
//   ./obj_viewer model1.obj [model2.obj ...]

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <notcurses/notcurses.h>

#define FAST_OBJ_IMPLEMENTATION
#include "fast_obj.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

typedef struct { float x, y, z; } Vec3;
typedef struct { float u, v; } Vec2;
typedef struct { unsigned char* data; int w, h, channels; } Texture;

static Vec3 vsub(Vec3 a, Vec3 b) { return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 vcross(Vec3 a, Vec3 b) { return (Vec3){a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x}; }
static float vdot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static Vec3 vnorm(Vec3 a) { float l = sqrtf(vdot(a, a)); if (l < 1e-8f) return (Vec3){0,0,1}; return (Vec3){a.x/l, a.y/l, a.z/l}; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

#define NEAR_PLANE 0.05f
static const Vec3 LIGHT_DIR_SRC = { 0.4f, 0.6f, -0.7f };

// ---------------------------------------------------------------------------
// Model loading (one fastObjMesh + its materials' textures + normalization)
// ---------------------------------------------------------------------------
typedef struct {
    fastObjMesh* mesh;
    Texture* textures;      // one per mesh->materials[] entry
    Vec3 center;
    float scale;
    Vec3* camv;             // scratch: camera-space position per mesh vertex
    char* name;
} Model;

static int model_load(Model* m, const char* path) {
    memset(m, 0, sizeof(*m));
    m->mesh = fast_obj_read(path);
    if (!m->mesh) {
        fprintf(stderr, "Failed to load '%s'\n", path);
        return 0;
    }
    m->name = strdup(path);

    m->textures = calloc(m->mesh->material_count, sizeof(Texture));
    for (unsigned int i = 0; i < m->mesh->material_count; i++) {
        unsigned int ti = m->mesh->materials[i].map_Kd;
        if (ti > 0 && ti < m->mesh->texture_count && m->mesh->textures[ti].path) {
            const char* tex_path = m->mesh->textures[ti].path;
            m->textures[i].data = stbi_load(tex_path, &m->textures[i].w, &m->textures[i].h, &m->textures[i].channels, 4);
            if (!m->textures[i].data) {
                fprintf(stderr, "Warning: failed to load texture '%s' for material '%s' in '%s': %s\n",
                        tex_path,
                        m->mesh->materials[i].name ? m->mesh->materials[i].name : "?",
                        path, stbi_failure_reason());
            }
        }
    }

    Vec3 min_b = {1e9f, 1e9f, 1e9f}, max_b = {-1e9f, -1e9f, -1e9f};
    for (unsigned int i = 1; i < m->mesh->position_count; i++) {
        float x = m->mesh->positions[3*i], y = m->mesh->positions[3*i+1], z = m->mesh->positions[3*i+2];
        if (x < min_b.x) min_b.x = x;
        if (y < min_b.y) min_b.y = y;
        if (z < min_b.z) min_b.z = z;
        if (x > max_b.x) max_b.x = x;
        if (y > max_b.y) max_b.y = y;
        if (z > max_b.z) max_b.z = z;
    }
    m->center = (Vec3){(min_b.x+max_b.x)/2.0f, (min_b.y+max_b.y)/2.0f, (min_b.z+max_b.z)/2.0f};
    float max_dim = fmaxf(max_b.x - min_b.x, fmaxf(max_b.y - min_b.y, max_b.z - min_b.z));
    m->scale = 2.0f / (max_dim == 0 ? 1.0f : max_dim);
    m->camv = malloc(m->mesh->position_count * sizeof(Vec3));
    return 1;
}

static void model_free(Model* m) {
    if (!m->mesh) return;
    for (unsigned int i = 0; i < m->mesh->material_count; i++) {
        if (m->textures[i].data) stbi_image_free(m->textures[i].data);
    }
    free(m->textures);
    free(m->camv);
    free(m->name);
    fast_obj_destroy(m->mesh);
}

// ---------------------------------------------------------------------------
// Near-plane clip (Sutherland-Hodgman, single plane cam.z >= NEAR_PLANE)
// ---------------------------------------------------------------------------
typedef struct { Vec3 cam; Vec2 uv; } ClipVert;

static int clip_near(const ClipVert* in, int n, ClipVert* out) {
    int oc = 0;
    for (int i = 0; i < n; i++) {
        ClipVert curr = in[i];
        ClipVert prev = in[(i - 1 + n) % n];
        int currIn = curr.cam.z >= NEAR_PLANE;
        int prevIn = prev.cam.z >= NEAR_PLANE;
        if (currIn != prevIn) {
            float t = (NEAR_PLANE - prev.cam.z) / (curr.cam.z - prev.cam.z);
            ClipVert nv;
            nv.cam.x = prev.cam.x + t * (curr.cam.x - prev.cam.x);
            nv.cam.y = prev.cam.y + t * (curr.cam.y - prev.cam.y);
            nv.cam.z = NEAR_PLANE;
            nv.uv.u = prev.uv.u + t * (curr.uv.u - prev.uv.u);
            nv.uv.v = prev.uv.v + t * (curr.uv.v - prev.uv.v);
            out[oc++] = nv;
        }
        if (currIn) out[oc++] = curr;
    }
    return oc;
}

// ---------------------------------------------------------------------------
// Render-triangle list (screen-space, perspective-correct attributes)
// ---------------------------------------------------------------------------
typedef struct {
    float sx[3], sy[3];
    float invz[3];
    float uoz[3], voz[3];
    Texture* tex;
    uint32_t fallback;
    float brightness;
} RTri;

typedef struct { RTri* data; int count, cap; } RTriList;

static void rtri_clear(RTriList* l) { l->count = 0; }
static void rtri_push(RTriList* l, RTri t) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 256;
        l->data = realloc(l->data, l->cap * sizeof(RTri));
    }
    l->data[l->count++] = t;
}

static void sample_bilinear(Texture* tex, float u, float v, unsigned char out[4]) {
    float fx = u * tex->w - 0.5f;
    float fy = (1.0f - v) * tex->h - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    x0 = ((x0 % tex->w) + tex->w) % tex->w;
    x1 = ((x1 % tex->w) + tex->w) % tex->w;
    y0 = ((y0 % tex->h) + tex->h) % tex->h;
    y1 = ((y1 % tex->h) + tex->h) % tex->h;
    unsigned char* p00 = &tex->data[(y0 * tex->w + x0) * 4];
    unsigned char* p10 = &tex->data[(y0 * tex->w + x1) * 4];
    unsigned char* p01 = &tex->data[(y1 * tex->w + x0) * 4];
    unsigned char* p11 = &tex->data[(y1 * tex->w + x1) * 4];
    for (int c = 0; c < 4; c++) {
        float top = p00[c] * (1 - tx) + p10[c] * tx;
        float bot = p01[c] * (1 - tx) + p11[c] * tx;
        out[c] = (unsigned char)(top * (1 - ty) + bot * ty);
    }
}

// rasterize one triangle, restricted to scanlines [y0, y1) -- used by worker threads
// so that each thread only ever writes into its own disjoint horizontal band.
static void raster_tri_band(RTri* t, uint32_t* cbuf, float* zbuf, int w, int h, int y0, int y1) {
    float minXf = fminf(t->sx[0], fminf(t->sx[1], t->sx[2]));
    float maxXf = fmaxf(t->sx[0], fmaxf(t->sx[1], t->sx[2]));
    float minYf = fminf(t->sy[0], fminf(t->sy[1], t->sy[2]));
    float maxYf = fmaxf(t->sy[0], fmaxf(t->sy[1], t->sy[2]));
    int minX = (int)fmaxf(0, floorf(minXf)), maxX = (int)fminf(w - 1, ceilf(maxXf));
    int minY = (int)fmaxf((float)y0, floorf(minYf)), maxY = (int)fminf((float)(y1 - 1), ceilf(maxYf));
    if (minY > maxY || minX > maxX) return;

    float x0 = t->sx[0], y0f = t->sy[0], x1 = t->sx[1], y1f = t->sy[1], x2 = t->sx[2], y2f = t->sy[2];
    float denom = (y1f - y2f) * (x0 - x2) + (x2 - x1) * (y0f - y2f);
    if (fabsf(denom) < 1e-6f) return;

    for (int y = minY; y <= maxY; y++) {
        for (int x = minX; x <= maxX; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((y1f - y2f) * (px - x2) + (x2 - x1) * (py - y2f)) / denom;
            float w1 = ((y2f - y0f) * (px - x2) + (x0 - x2) * (py - y2f)) / denom;
            float w2 = 1.0f - w0 - w1;
            if (w0 >= -0.001f && w1 >= -0.001f && w2 >= -0.001f) {
                float invz = w0 * t->invz[0] + w1 * t->invz[1] + w2 * t->invz[2];
                if (invz <= 0.0f) continue;
                float z = 1.0f / invz;
                int idx = y * w + x;
                if (z < zbuf[idx]) {
                    zbuf[idx] = z;
                    unsigned char col[4] = {255, 255, 255, 255};
                    if (t->tex && t->tex->data) {
                        float uoz = w0 * t->uoz[0] + w1 * t->uoz[1] + w2 * t->uoz[2];
                        float voz = w0 * t->voz[0] + w1 * t->voz[1] + w2 * t->voz[2];
                        float u = uoz / invz, v = voz / invz;
                        sample_bilinear(t->tex, u, v, col);
                    } else {
                        col[0] = t->fallback & 0xFF;
                        col[1] = (t->fallback >> 8) & 0xFF;
                        col[2] = (t->fallback >> 16) & 0xFF;
                    }
                    float b = t->brightness;
                    cbuf[idx] = RGBA((int)(col[0]*b), (int)(col[1]*b), (int)(col[2]*b), 255);
                }
            }
        }
    }
}

typedef struct {
    RTri* tris; int count;
    uint32_t* cbuf; float* zbuf;
    int w, h, y0, y1;
} BandJob;

static void* band_worker(void* arg) {
    BandJob* j = (BandJob*)arg;
    for (int i = 0; i < j->count; i++) {
        raster_tri_band(&j->tris[i], j->cbuf, j->zbuf, j->w, j->h, j->y0, j->y1);
    }
    return NULL;
}

#define MAX_THREADS 8
static void raster_threaded(RTriList* rtris, uint32_t* cbuf, float* zbuf, int w, int h, int nthreads) {
    if (nthreads < 1) nthreads = 1;
    if (nthreads > MAX_THREADS) nthreads = MAX_THREADS;
    pthread_t threads[MAX_THREADS];
    BandJob jobs[MAX_THREADS];
    int band_h = (h + nthreads - 1) / nthreads;
    for (int t = 0; t < nthreads; t++) {
        int y0 = t * band_h;
        int y1 = (t + 1) * band_h;
        if (y1 > h) y1 = h;
        jobs[t] = (BandJob){ rtris->data, rtris->count, cbuf, zbuf, w, h, y0, y1 };
        pthread_create(&threads[t], NULL, band_worker, &jobs[t]);
    }
    for (int t = 0; t < nthreads; t++) pthread_join(threads[t], NULL);
}

// ---------------------------------------------------------------------------
// Wireframe overlay
// ---------------------------------------------------------------------------
static void draw_line(uint32_t* cbuf, int w, int h, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) cbuf[y0 * w + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_wireframe(RTriList* rtris, uint32_t* cbuf, int w, int h) {
    uint32_t edge_color = RGBA(80, 255, 120, 255);
    for (int i = 0; i < rtris->count; i++) {
        RTri* t = &rtris->data[i];
        for (int e = 0; e < 3; e++) {
            int a = e, b = (e + 1) % 3;
            draw_line(cbuf, w, h, (int)t->sx[a], (int)t->sy[a], (int)t->sx[b], (int)t->sy[b], edge_color);
        }
    }
}

// ---------------------------------------------------------------------------
// Build the render-triangle list for one model at a given orientation/zoom
// ---------------------------------------------------------------------------
static void build_frame(Model* m, float angleX, float angleY, float camDist,
                         float fov, int cw, int ch, RTriList* out) {
    rtri_clear(out);
    Vec3 light_dir = vnorm(LIGHT_DIR_SRC);

    for (unsigned int i = 1; i < m->mesh->position_count; i++) {
        float x = (m->mesh->positions[3*i]   - m->center.x) * m->scale;
        float y = (m->mesh->positions[3*i+1] - m->center.y) * m->scale;
        float z = (m->mesh->positions[3*i+2] - m->center.z) * m->scale;
        float y1 = y * cosf(angleX) - z * sinf(angleX);
        float z1 = y * sinf(angleX) + z * cosf(angleX);
        float x2 = x * cosf(angleY) + z1 * sinf(angleY);
        float z2 = -x * sinf(angleY) + z1 * cosf(angleY);
        z2 += camDist;
        m->camv[i] = (Vec3){x2, y1, z2};
    }

    unsigned int index_offset = 0;
    for (unsigned int f = 0; f < m->mesh->face_count; f++) {
        unsigned int fv = m->mesh->face_vertices[f];
        unsigned int mat_idx = m->mesh->face_materials[f];
        float* kd = m->mesh->materials[mat_idx].Kd;
        uint32_t fallback = RGBA((int)(kd[0]*255), (int)(kd[1]*255), (int)(kd[2]*255), 255);
        Texture* tex = &m->textures[mat_idx];

        for (unsigned int v = 1; v < fv - 1; v++) {
            fastObjIndex i0 = m->mesh->indices[index_offset + 0];
            fastObjIndex i1 = m->mesh->indices[index_offset + v];
            fastObjIndex i2 = m->mesh->indices[index_offset + v + 1];
            if (!(i0.p > 0 && i1.p > 0 && i2.p > 0)) continue;

            Vec3 A = m->camv[i0.p], B = m->camv[i1.p], C = m->camv[i2.p];
            Vec2 ta = {0,0}, tb = {0,0}, tc = {0,0};
            if (i0.t > 0) { ta.u = m->mesh->texcoords[2*i0.t]; ta.v = m->mesh->texcoords[2*i0.t+1]; }
            if (i1.t > 0) { tb.u = m->mesh->texcoords[2*i1.t]; tb.v = m->mesh->texcoords[2*i1.t+1]; }
            if (i2.t > 0) { tc.u = m->mesh->texcoords[2*i2.t]; tc.v = m->mesh->texcoords[2*i2.t+1]; }

            Vec3 normal = vcross(vsub(B, A), vsub(C, A));
            Vec3 centroid = {(A.x+B.x+C.x)/3.0f, (A.y+B.y+C.y)/3.0f, (A.z+B.z+C.z)/3.0f};
            if (vdot(normal, centroid) >= 0.0f) { index_offset += 0; continue; } // backface

            float brightness = clampf(vdot(vnorm(normal), light_dir), 0.15f, 1.0f);

            ClipVert in[3] = {{A, ta}, {B, tb}, {C, tc}};
            ClipVert clipped[8];
            int n = clip_near(in, 3, clipped);
            if (n < 3) continue;

            for (int k = 1; k < n - 1; k++) {
                ClipVert p0 = clipped[0], p1 = clipped[k], p2 = clipped[k+1];
                Vec3 pts[3] = {p0.cam, p1.cam, p2.cam};
                Vec2 uvs[3] = {p0.uv, p1.uv, p2.uv};
                RTri t;
                for (int j = 0; j < 3; j++) {
                    t.sx[j] = (pts[j].x / pts[j].z) * fov + cw / 2.0f;
                    t.sy[j] = (-pts[j].y / pts[j].z) * fov + ch / 2.0f;
                    t.invz[j] = 1.0f / pts[j].z;
                    t.uoz[j] = uvs[j].u * t.invz[j];
                    t.voz[j] = uvs[j].v * t.invz[j];
                }
                t.tex = tex;
                t.fallback = fallback;
                t.brightness = brightness;
                rtri_push(out, t);
            }
        }
        index_offset += fv;
    }
}

// ---------------------------------------------------------------------------
// Blit modes
// ---------------------------------------------------------------------------
typedef enum { BLIT_AUTO, BLIT_HALFBLOCK, BLIT_BRAILLE, BLIT_ASCII_CELL, BLIT_MODE_COUNT } BlitMode;
static const char* blit_mode_name(BlitMode m) {
    switch (m) {
        case BLIT_AUTO: return "auto (pixel graphics if available)";
        case BLIT_HALFBLOCK: return "halfblock";
        case BLIT_BRAILLE: return "braille";
        case BLIT_ASCII_CELL: return "ascii-cell";
        default: return "?";
    }
}

int main(int argc, char* argv[]) {
    int model_count = argc > 1 ? argc - 1 : 1;
    const char* default_path = "model.obj";
    Model* models = calloc(model_count, sizeof(Model));
    int loaded = 0;
    for (int i = 0; i < model_count; i++) {
        const char* path = argc > 1 ? argv[i + 1] : default_path;
        if (model_load(&models[loaded], path)) loaded++;
    }
    if (loaded == 0) {
        fprintf(stderr, "No models could be loaded.\n");
        return EXIT_FAILURE;
    }
    model_count = loaded;
    int cur_model = 0;

    struct notcurses_options nopts = { .flags = NCOPTION_SUPPRESS_BANNERS };
    struct notcurses* nc = notcurses_init(&nopts, NULL);
    if (!nc) { fprintf(stderr, "Failed to init notcurses (need a real terminal)\n"); return EXIT_FAILURE; }
    struct ncplane* stdn = notcurses_stdplane(nc);
    notcurses_mice_enable(nc, NCMICE_ALL_EVENTS);

    int pixel_capable = notcurses_check_pixel_support(nc) != NCPIXEL_NONE;
    BlitMode blit_mode = BLIT_AUTO;

    uint32_t* pixel_buffer = NULL;
    float* z_buffer = NULL;
    int cur_w = 0, cur_h = 0;

    float angleX = 0.3f, angleY = 0.4f, camDist = 3.2f;
    int auto_rotate = 1;
    int wireframe = 0, solid_fill = 1;
    RTriList rtris = {0};

    int dragging = 0;
    int drag_last_x = 0, drag_last_y = 0;

    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    int nthreads = (int)(nproc > 0 ? nproc : 4);
    if (nthreads > MAX_THREADS) nthreads = MAX_THREADS;

    while (1) {
        struct ncinput ni;
        while (notcurses_get_nblock(nc, &ni)) {
            uint32_t id = ni.id;
            if (ni.evtype == NCTYPE_RELEASE && id == NCKEY_BUTTON1) {
                dragging = 0;
                continue;
            }
            if (id == NCKEY_BUTTON1 && ni.evtype != NCTYPE_RELEASE) {
                if (!dragging) { dragging = 1; drag_last_x = ni.x; drag_last_y = ni.y; }
                else {
                    int dx = ni.x - drag_last_x, dy = ni.y - drag_last_y;
                    angleY += dx * 0.02f;
                    angleX += dy * 0.02f;
                    drag_last_x = ni.x; drag_last_y = ni.y;
                    auto_rotate = 0;
                }
                continue;
            }
            if (id == NCKEY_BUTTON4) { camDist = clampf(camDist - 0.2f, 0.5f, 20.0f); continue; } // wheel up
            if (id == NCKEY_BUTTON5) { camDist = clampf(camDist + 0.2f, 0.5f, 20.0f); continue; } // wheel down

            if (id == 'q' || id == 27) goto done;
            if (id == NCKEY_LEFT  || id == 'a' || id == 'A') { angleY -= 0.08f; auto_rotate = 0; }
            if (id == NCKEY_RIGHT || id == 'd' || id == 'D') { angleY += 0.08f; auto_rotate = 0; }
            if (id == NCKEY_UP    || id == 'w' || id == 'W') { angleX -= 0.08f; auto_rotate = 0; }
            if (id == NCKEY_DOWN  || id == 's' || id == 'S') { angleX += 0.08f; auto_rotate = 0; }
            if (id == '+' || id == '=') camDist = clampf(camDist - 0.2f, 0.5f, 20.0f);
            if (id == '-' || id == '_') camDist = clampf(camDist + 0.2f, 0.5f, 20.0f);
            if (id == 'r' || id == 'R') auto_rotate = !auto_rotate;
            if (id == 'n' || id == 'N') cur_model = (cur_model + 1) % model_count;
            if (id == 'p' || id == 'P') cur_model = (cur_model - 1 + model_count) % model_count;
            if (id == 'x' || id == 'X') wireframe = !wireframe;
            if (id == 'f' || id == 'F') solid_fill = !solid_fill;
            if (id == 'b' || id == 'B') blit_mode = (blit_mode + 1) % BLIT_MODE_COUNT;
        }

        unsigned int term_y, term_x;
        ncplane_dim_yx(stdn, &term_y, &term_x);

        ncblitter_e blitter;
        int logical_w, logical_h;
        switch (blit_mode) {
            case BLIT_AUTO:
                if (pixel_capable) {
                    unsigned pxy, pxx, cdy, cdx, mby, mbx;
                    ncplane_pixel_geom(stdn, &pxy, &pxx, &cdy, &cdx, &mby, &mbx);
                    logical_w = (int)pxx; logical_h = (int)pxy;
                    if (mbx > 0 && logical_w > (int)mbx) logical_w = (int)mbx;
                    if (mby > 0 && logical_h > (int)mby) logical_h = (int)mby;
                    blitter = NCBLIT_PIXEL;
                } else {
                    logical_w = (int)term_x; logical_h = (int)term_y * 2;
                    blitter = NCBLIT_2x1;
                }
                break;
            case BLIT_BRAILLE:
                logical_w = (int)term_x * 2; logical_h = (int)term_y * 4;
                blitter = NCBLIT_BRAILLE;
                break;
            case BLIT_ASCII_CELL:
                logical_w = (int)term_x; logical_h = (int)term_y;
                blitter = NCBLIT_1x1;
                break;
            case BLIT_HALFBLOCK:
            default:
                logical_w = (int)term_x; logical_h = (int)term_y * 2;
                blitter = NCBLIT_2x1;
                break;
        }
        if (logical_w < 2) logical_w = 2;
        if (logical_h < 2) logical_h = 2;

        if (logical_w != cur_w || logical_h != cur_h) {
            pixel_buffer = realloc(pixel_buffer, (size_t)logical_w * logical_h * sizeof(uint32_t));
            z_buffer = realloc(z_buffer, (size_t)logical_w * logical_h * sizeof(float));
            cur_w = logical_w; cur_h = logical_h;
        }

        for (int i = 0; i < cur_w * cur_h; i++) {
            pixel_buffer[i] = RGBA(30, 30, 40, 255);
            z_buffer[i] = FLT_MAX;
        }

        float fov = (float)cur_w / 2.5f;
        Model* m = &models[cur_model];
        build_frame(m, angleX, angleY, camDist, fov, cur_w, cur_h, &rtris);

        if (solid_fill) raster_threaded(&rtris, pixel_buffer, z_buffer, cur_w, cur_h, nthreads);
        if (wireframe) draw_wireframe(&rtris, pixel_buffer, cur_w, cur_h);

        struct ncvisual* ncv = ncvisual_from_rgba(pixel_buffer, cur_h, cur_w * 4, cur_w);
        struct ncvisual_options vopts = { .n = stdn, .blitter = blitter, .scaling = NCSCALE_NONE, .flags = 0 };
        ncvisual_blit(nc, ncv, &vopts);
        ncvisual_destroy(ncv);

        ncplane_printf_yx(stdn, 0, 0, " model %d/%d: %s | blit:%s | %s%s | q:quit x:wireframe f:fill b:blitmode n/p:model ",
                           cur_model + 1, model_count, m->name,
                           blit_mode_name(blit_mode),
                           auto_rotate ? "auto-rotate" : "manual",
                           wireframe ? " +wire" : "");

        notcurses_render(nc);

        if (auto_rotate) {
            angleY += 0.03f;
            angleX += 0.015f;
        }
        usleep(16000);
    }

done:
    free(rtris.data);
    free(pixel_buffer);
    free(z_buffer);
    for (int i = 0; i < model_count; i++) model_free(&models[i]);
    free(models);
    notcurses_stop(nc);
    return 0;
}