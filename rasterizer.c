#include <notcurses/notcurses.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Helper to pack RGBA into a 32-bit integer (Little-Endian standard)
#define RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

typedef struct { float x, y, z; } Vec3;
typedef struct { int v1, v2; } Edge;

// Bresenham's Line Algorithm writing DIRECTLY to an RGBA pixel buffer
void draw_line(int x0, int y0, int x1, int y1, uint32_t* buf, int w, int h, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        // Bounds checking to prevent segfaults
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) {
            buf[y0 * w + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

int main() {
    // 1. Initialize Notcurses
    struct notcurses_options nopts = {
        .flags = NCOPTION_SUPPRESS_BANNERS // Don't print version info on startup
    };
    struct notcurses* nc = notcurses_init(&nopts, NULL);
    if (!nc) return EXIT_FAILURE;
    
    struct ncplane* stdn = notcurses_stdplane(nc);

    // 2. Define Local 3D Model (Cube)
    Vec3 vertices[8] = {
        {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
        {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1}
    };
    Edge edges[12] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7}
    };

    float angleX = 0.0f, angleY = 0.0f, angleZ = 0.0f;
    
    uint32_t* pixel_buffer = NULL;
    int cur_w = 0, cur_h = 0;

    // Main Game/Render Loop
    while (1) {
        // Handle input (Press 'q' or ESC to quit)
        struct ncinput ni;
        if (notcurses_get_nblock(nc, &ni)) {
            if (ni.id == 'q' || ni.id == 27) break;
        }

        // 3. Dynamic Terminal Resizing
        int term_y, term_x;
        ncplane_dim_yx(stdn, &term_y, &term_x);
        
        // Using Half-blocks (2 vertical pixels per terminal cell)
        // This makes our logical pixels perfectly square (1:1 aspect ratio)!
        int logical_width = term_x;
        int logical_height = term_y * 2;

        // Reallocate memory if terminal was resized
        if (logical_width != cur_w || logical_height != cur_h) {
            pixel_buffer = realloc(pixel_buffer, logical_width * logical_height * sizeof(uint32_t));
            cur_w = logical_width;
            cur_h = logical_height;
        }

        // Clear framebuffer (Black with 100% Alpha)
        for(int i = 0; i < cur_w * cur_h; i++) {
            pixel_buffer[i] = RGBA(15, 15, 20, 255); // Dark gray background
        }

        // 4. Matrix Pipeline (Vertex -> Screen)
        Vec3 projected[8];
        for (int i = 0; i < 8; i++) {
            float x = vertices[i].x;
            float y = vertices[i].y;
            float z = vertices[i].z;

            // Rotations
            float y1 = y * cos(angleX) - z * sin(angleX);
            float z1 = y * sin(angleX) + z * cos(angleX);

            float x2 = x * cos(angleY) + z1 * sin(angleY);
            float z2 = -x * sin(angleY) + z1 * cos(angleY);

            float x3 = x2 * cos(angleZ) - y1 * sin(angleZ);
            float y3 = x2 * sin(angleZ) + y1 * cos(angleZ);

            // Translation (Move camera back)
            z2 += 3.0f;

            // Perspective Projection
            // Since Notcurses half-blocks are square, we don't need aspect ratio hacks!
            float fov = (float)cur_w / 3.0f;
            projected[i].x = (x3 / z2) * fov + (cur_w / 2.0f);
            projected[i].y = (y3 / z2) * fov + (cur_h / 2.0f);
        }

        // 5. Rasterize Lines (Neon Cyan color)
        uint32_t line_color = RGBA(0, 255, 255, 255);
        for (int i = 0; i < 12; i++) {
            draw_line(
                (int)projected[edges[i].v1].x, (int)projected[edges[i].v1].y,
                (int)projected[edges[i].v2].x, (int)projected[edges[i].v2].y, 
                pixel_buffer, cur_w, cur_h, line_color
            );
        }

        // 6. Blit the raw memory buffer to the terminal using Notcurses
       /* struct ncvisual* ncv = ncvisual_from_rgba(pixel_buffer, cur_h, cur_w * 4, cur_w);
        struct ncvisual_options vopts = {
            .n = stdn,
            .blitter = NCBLIT_2x1,           // Use Unicode half-blocks
            .flags = NCVISUAL_OPTION_NODATA, // We provide our own plane data
        };
        
        ncvisual_render(nc, ncv, &vopts);
        ncvisual_destroy(ncv);
        
        // Push frame to screen
        notcurses_render(nc);
*/
// 6. Blit the raw memory buffer to the terminal using Notcurses
        struct ncvisual* ncv = ncvisual_from_rgba(pixel_buffer, cur_h, cur_w * 4, cur_w);
        
        struct ncvisual_options vopts = {
            .n = stdn,
            .blitter = NCBLIT_2x1, // Use Unicode half-blocks
            .flags = 0,            // (Removed NCVISUAL_OPTION_NODATA for V3)
        };
        
        // ncvisual_render was renamed to ncvisual_blit in Notcurses V3
        ncvisual_blit(nc, ncv, &vopts);
        ncvisual_destroy(ncv);
        
        // Push frame to screen
        notcurses_render(nc);
        // Spin model & lock framerate to ~60 FPS
        angleX += 0.03f; 
        angleY += 0.02f; 
        angleZ += 0.01f;
        usleep(16000); 
    }

    // Cleanup
    free(pixel_buffer);
    notcurses_stop(nc);
    return 0;
}