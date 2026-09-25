<div align="center">

# TUI3D

**Terminal 3D. Software rasterization. No GPU API.**

A lightweight Wavefront OBJ viewer that renders textured, lit 3D models directly inside your terminal.

![TUI3D](https://raw.githubusercontent.com/jyotiraditya-barman/Console3D/main/screenshot.png)

[![Language](https://img.shields.io/badge/C-13.3.0-A8B9CC?style=for-the-badge&logo=c)](https://gcc.gnu.org/)
[![Platform](https://img.shields.io/badge/Linux-supported-111111?style=for-the-badge&logo=linux)](https://www.linux.org/)
[![Notcurses](https://img.shields.io/badge/Notcurses-terminal%20graphics-222222?style=for-the-badge)](https://github.com/dankamongmen/notcurses)
[![Make](https://img.shields.io/badge/build-Make-000000?style=for-the-badge&logo=gnu)](https://www.gnu.org/software/make/)

</div>

---

TUI3D uses a software rasterizer to render 3D models straight into the terminal via **Notcurses**. Rendering is multithreaded with POSIX threads, and several terminal display modes are supported — from Unicode half-blocks to Braille to plain ASCII.

## Features

- Software 3D triangle rasterizer
- Wavefront `.obj` model loading with `.mtl` material support
- PNG/JPEG texture loading via `stb_image`
- Bilinear texture filtering, perspective-correct UVs
- Backface culling and near-plane clipping
- Directional lighting and depth buffering
- Multithreaded rasterization (`pthread`)
- Wireframe overlay, solid-fill and wireframe-only modes
- Automatic model rotation, mouse orbit camera, keyboard controls, zoom
- Multiple simultaneously loaded models
- Multiple terminal rendering modes

### Terminal rendering modes

```
AUTO · HALFBLOCK · BRAILLE · ASCII-CELL
```

Press **B** while running to cycle through them.

## Controls

| Key | Action |
|---|---|
| `Q` / `ESC` | Quit |
| Arrow keys / `W A S D` | Orbit camera |
| Mouse drag | Orbit camera |
| `+` / `-` / Mouse wheel | Zoom |
| `R` | Toggle auto-rotation |
| `N` / `P` | Next / previous model |
| `X` | Toggle wireframe |
| `F` | Toggle solid fill |
| `B` | Change terminal rendering mode |

## Requirements

Linux with GCC, GNU Make, POSIX threads, `libm`, and Notcurses development libraries.

On Ubuntu/Debian:

```bash
sudo apt install build-essential libnotcurses-dev
```

## Building

```bash
git clone https://github.com/jyotiraditya-barman/Console3D.git
cd Console3D
make
```

The executable is created at `bin/tui3d`.

| Command | Result |
|---|---|
| `make` | Normal build |
| `make debug` | Debug symbols, no optimization |
| `make release` | Aggressive optimization |
| `make clean` | Remove build artifacts |
| `make rebuild` | Clean + build |
| `make info` | Show build configuration |

## Running

```bash
./bin/tui3d models/fish.obj
```

Or via Make:

```bash
make run MODEL=models/fish.obj
```

Multiple models can be loaded at once — switch between them with `N` / `P`:

```bash
./bin/tui3d models/fish.obj models/doubleface.obj
```

## Project structure

```
TUI3D/
├── bin/
│   └── tui3d
├── models/
│   ├── fish.obj
│   ├── fish.mtl
│   ├── fishtex.png
│   └── ...
├── src/
│   ├── obj.c
│   ├── fast_obj.h
│   └── stb_image.h
├── Makefile
└── README.md
```

Keep each model's `.obj`, `.mtl`, and texture files together — it keeps relative asset paths simple:

```
models/fish/
├── fish.obj
├── fish.mtl
└── fishtex.png
```

## Rendering pipeline

```
OBJ model → load → transform/rotate → camera space
   → backface cull → near-plane clip → project
   → triangles → multithreaded rasterization
      (depth test · texture sample · lighting)
   → RGBA framebuffer → Notcurses → terminal
```

Rasterization is split into horizontal screen bands processed by worker threads, scaling automatically with available CPU cores up to a maximum of 8.

## Dependencies

- **Notcurses** — terminal rendering and input
- **fast_obj** — lightweight OBJ loading
- **stb_image** — image/texture loading
- **POSIX pthreads** — multithreaded rendering
- **GNU libm** — math functions

## Why TUI3D?

An experiment in bringing real-time 3D graphics techniques into a text terminal — no OpenGL, Vulkan, or Direct3D. TUI3D rasterizes to a framebuffer entirely in software and converts the result into terminal output, as a way to dig into rasterization, 3D math, texture mapping, lighting, and multithreading.

## Roadmap

- [ ] Frustum clipping
- [ ] Vertex normals & smooth shading
- [ ] Specular lighting, multiple lights
- [ ] Better camera controls
- [ ] Configurable renderer settings
- [ ] FPS counter & model info panel
- [ ] Improved texture format support
- [ ] More terminal drawing modes
- [ ] Scene management, material controls
- [ ] OBJ animation support
- [ ] SIMD optimizations

## License

Released under the license included in this repository. Third-party libraries (`fast_obj`, `stb_image`) remain under their own licenses.

## Author

**Jyotiraditya Barman**
[github.com/jyotiraditya-barman](https://github.com/jyotiraditya-barman) · [Console3D](https://github.com/jyotiraditya-barman/Console3D)
