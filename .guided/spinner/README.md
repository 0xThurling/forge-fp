# Project 9 — Spinner (the capstone): a rotating shape in the terminal

Build a rotating wireframe shape rendered in the terminal, using **everything
in the library**: collections for the geometry, `match` for multiple shapes,
SIMD and a thread pool for the transform, an arena for per-frame scratch,
`Channel`/`Stream` for non-blocking input, and `pipe`/`ops` for the pipeline.

**Modules:** `vec.hpp`, `ranges.hpp`, `adt.hpp`, `simd.hpp`, `arena.hpp`, `concurrent.hpp`, `input.hpp`, `compose.hpp`, `ops.hpp`.
**Compile:** `g++ -std=c++20 -O2 -march=native -pthread -I src -o app app.cpp && ./app`

It's a terminal program: run it, watch it spin, press keys to control it, `q`
to quit. Each stage adds a piece and ends with a **Verify** check.

---

## Stage 1 — Model the cube

Goal: a shape is a list of 3D points and a list of edges (pairs of indices).

```cpp
#include <fp/vec.hpp>
#include <vector>

struct Vec3 { double x, y, z; };
struct Edge { int a, b; };

std::vector<Vec3> cube = {
    {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},   // back face
    {-1,-1, 1}, {1,-1, 1}, {1,1, 1}, {-1,1, 1},   // front face
};
std::vector<Edge> cube_edges = {
    {0,1},{1,2},{2,3},{3,0},   // back
    {4,5},{5,6},{6,7},{7,4},   // front
    {0,4},{1,5},{2,6},{3,7},   // connecting
};
```

**Verify:** compiles. `cube.size() == 8`, `cube_edges.size() == 12`.

**Concept — data first.** The shape is plain data (points + edges); every
operation below is a pure function over it.

## Stage 2 — Rotate around Y

Goal: rotate a point around the Y axis by angle `t`. Then rotate *all* points
with `map`.

```cpp
#include <cmath>
Vec3 rot_y(Vec3 v, double t) {
    double c = std::cos(t), s = std::sin(t);
    return { v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
}

// rotate the whole cube by 45°
double t = 0.785398;
auto rotated = fp::map(cube, [t](Vec3 v) { return rot_y(v, t); });
```

**Verify:** `rot_y({1,0,0}, π/2)` ≈ `{0,0,-1}` (the x-axis maps to -z).

**Concept — a transform is a pure function.** `map` lifts `rot_y` over the
vertex list; no loops, no mutation.

## Stage 3 — Project to 2D and draw

Goal: orthographic projection, then draw each edge into a character grid with
Bresenham's line algorithm.

```cpp
struct Pixel { int x, y; };

Pixel project(Vec3 v, int w, int h) {
    double scale = std::min(w, h) / 4.0;
    return { int(w/2.0 + v.x * scale), int(h/2.0 - v.y * scale) };
}

void line(std::vector<std::string>& grid, Pixel a, Pixel b) {
    int dx = std::abs(b.x - a.x), sx = a.x < b.x ? 1 : -1;
    int dy = -std::abs(b.y - a.y), sy = a.y < b.y ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (a.x >= 0 && a.x < (int)grid[0].size() && a.y >= 0 && a.y < (int)grid.size())
            grid[a.y][a.x] = '#';
        if (a.x == b.x && a.y == b.y) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; a.x += sx; }
        if (e2 <= dx) { err += dx; a.y += sy; }
    }
}
```

**Verify:** compiles; `project({0,0,0}, 80, 24)` is the screen center.

**Concept — the geometry → image step.** Projection is a function; drawing is a
pure "write into a grid" (the grid is passed in, filled, returned).

## Stage 4 — Render one frame

Goal: project every vertex, draw every edge, print the grid.

```cpp
#include <iostream>
std::vector<std::string> render(std::vector<Vec3> const& verts,
                                std::vector<Edge> const& edges, int w, int h) {
    auto px = fp::map(verts, [&](Vec3 v) { return project(v, w, h); });
    std::vector<std::string> grid(h, std::string(w, ' '));
    for (auto const& e : edges)
        line(grid, px[e.a], px[e.b]);
    return grid;
}

int main() {
    for (auto const& row : render(cube, cube_edges, 60, 20))
        std::cout << row << "\n";
}
```

**Verify:** running it prints a 2D cube (a square with an X) in `#`.

**Concept — a frame is a pure value.** `render` is `verts → grid`; printing is
the only effect, done once at the edge.

## Stage 5 — Animate

Goal: a loop that redraws, incrementing the angle, and clears the screen with
an ANSI escape.

```cpp
#include <thread>
#include <chrono>
using namespace std::chrono_literals;

int main() {
    double t = 0;
    for (;;) {
        auto verts = fp::map(cube, [t](Vec3 v) { return rot_y(v, t); });
        std::cout << "\033[2J\033[H";                      // clear + home
        for (auto const& row : render(verts, cube_edges, 60, 20))
            std::cout << row << "\n";
        t += 0.1;
        std::this_thread::sleep_for(30ms);
    }
}
```

**Verify:** runs and spins a cube. (Ctrl-C to stop for now.)

**Concept — the frame loop.** Each iteration is a pure "state → frame";
sleep paces it. State (`t`) is a value advanced each tick.

## Stage 6 — Quit on `q` (blocking key read)

Goal: read a key and stop. `raw_mode` + `read_key` (POSIX).

```cpp
#include <fp/input.hpp>

int main() {
    #if defined(__unix__) || defined(__APPLE__)
    fp::raw_mode(true);
    bool running = true;
    double t = 0;
    while (running) {
        // ... render ...
        auto k = fp::read_key();          // blocks until a key — too slow for now
        if (k.is_ok() && k.value() == 'q') running = false;
        t += 0.1;
    }
    fp::raw_mode(false);
    #endif
}
```

**Verify:** compiles. (Blocking input pauses the spin — that's what the next
stage fixes.)

**Concept — raw-mode keys.** `read_key` reads a single keypress with no echo and
no Enter. But it *blocks*, which fights a real-time loop.

## Stage 7 — Non-blocking input via a `Channel`

Goal: a reader thread pushes keys into a channel; the loop *polls* it (no block).

```cpp
#include <fp/concurrent.hpp>
#include <thread>

int main() {
    fp::Channel<char> keys;
    std::thread reader([&] {
        for (;;) {
            auto k = fp::read_key();
            if (!k.is_ok()) break;
            keys.send(k.value());
        }
    });

    bool running = true;
    double t = 0;
    while (running) {
        // ... render ...
        while (auto k = keys.try_recv()) {          // non-blocking drain
            if (*k == 'q') running = false;
        }
        t += 0.1;
        std::this_thread::sleep_for(30ms);
    }
    reader.detach();
    fp::raw_mode(false);
}
```

**Verify:** the cube spins *and* responds to keys without stalling; `q` quits.

**Concept — separate concerns with a channel.** The reader blocks (fine, it's in
its own thread); the render loop polls `try_recv` (non-blocking). The channel is
the boundary between "input arrives" and "frames render".

## Stage 8 — Controls: speed and axis

Goal: `+`/`-` change speed, `x`/`y` change the rotation axis. First add an X-axis
rotation alongside the Y one:

```cpp
Vec3 rot_x(Vec3 v, double t) {
    double c = std::cos(t), s = std::sin(t);
    return { v.x, v.y * c - v.z * s, v.y * s + v.z * c };
}

double speed = 0.1;
int axis = 0;   // 0 = Y, 1 = X
// ... in the key-drain loop:
while (auto k = keys.try_recv()) {
    switch (*k) {
        case 'q': running = false; break;
        case '+': speed += 0.02;   break;
        case '-': speed -= 0.02;   break;
        case 'x': axis = 1;        break;
        case 'y': axis = 0;        break;
    }
}
// rotation picks axis:
auto rot = [&](Vec3 v) { return axis == 0 ? rot_y(v, t) : rot_x(v, t); };
auto verts = fp::map(cube, rot);
```

**Verify:** `+`/`-` visibly change the spin rate; `x`/`y` change the axis.

**Concept — the loop state grows.** State is still just values (`speed`, `axis`,
`t`); keys are transitions over that state.

## Stage 9 — Multiple shapes with `match`

Goal: a cube, a pyramid, and a tetrahedron — pick one, dispatch with `match`.

```cpp
#include <fp/adt.hpp>
#include <variant>

struct Cube     { std::vector<Vec3> v; std::vector<Edge> e; };
struct Pyramid  { std::vector<Vec3> v; std::vector<Edge> e; };
struct Tetra    { std::vector<Vec3> v; std::vector<Edge> e; };
using Shape = std::variant<Cube, Pyramid, Tetra>;

// a "shape" is verts+edges; match picks them:
auto verts_edges = fp::match(shape,
    fp::case_<Cube>   ([](auto const& c) { return std::pair{c.v, c.e}; }),
    fp::case_<Pyramid>([](auto const& p) { return std::pair{p.v, p.e}; }),
    fp::case_<Tetra>  ([](auto const& t) { return std::pair{t.v, t.e}; }));
```

**Verify:** press a key to switch shapes; the compiler enforces that `match`
covers all three.

**Concept — sum types again.** The shape is a `variant`; every consumer
`match`es exhaustively. Add a shape → the compiler walks you through the update.

## Stage 10 — Parallelize the rotation

Goal: rotate the (many) vertices in parallel with `par_map`.

```cpp
fp::ThreadPool pool(4);
auto verts = fp::par_map(pool, shape_verts, rot);   // same order, faster
```

**Verify:** still correct, and scales for a high-vertex shape.

**Concept — parallelism is a drop-in.** `par_map` has the same contract as `map`
(order, result), just chunked across workers. `rot` is pure, so no locks.

## Stage 11 — SIMD on the flat data

Goal: after projection, scale the coordinates with SIMD. Flatten to a buffer,
`map_inplace`, `reduce` for a sanity metric.

```cpp
#include <fp/simd.hpp>

// flatten x coords, scale with SIMD
std::vector<double> xs = fp::map(verts, [](Vec3 v) { return v.x; });
fp::map_inplace(xs, [](fp::vec<double> x) { return x * 1.5; });

// sanity: average distance from origin (vectorized per coordinate)
auto mags = fp::map(verts, [](Vec3 v) { return v.x*v.x + v.y*v.y + v.z*v.z; });
double energy = fp::reduce(mags);
```

**Verify:** `energy` stays constant across frames (rotation preserves distance).

**Concept — SIMD where it applies.** `map_inplace`/`reduce` on flat numeric
buffers; the lambda operates on whole vectors. (`energy` is a good invariant to
watch — if it drifts, the rotation is buggy.)

## Stage 12 — Per-frame scratch with `Arena`

Goal: the projected pixels are per-frame temporaries — allocate them from an
arena, `reset` each tick.

```cpp
#include <fp/arena.hpp>

fp::Arena scratch;
for (;;) {
    auto* px = scratch.alloc<Pixel>(verts.size());
    // ... write projected pixels into px ...
    // ... draw from px ...
    scratch.reset();            // reclaim for next frame
}
```

**Verify:** runs with no per-frame allocation churn.

**Concept — scoped memory.** The frame's temporaries live and die inside one
iteration; the arena makes that one bump + one reset.

## Stage 13 — The pipeline as a value

Goal: name the whole "vertices → grid" pipeline with `pipe` + `ops`.

```cpp
auto render_pipeline = fp::pipe(
    [](auto verts) { return fp::map(verts, rot); },         // rotate
    [](auto verts) { return project_all(verts, w, h); },    // project
    [](auto px)    { return rasterize(px, edges, w, h); }); // draw

auto grid = render_pipeline(shape_verts);
```

**Verify:** `grid` matches the staged version.

**Concept — composition.** The frame is now one named function; the main loop is
just "advance `t`, read keys, apply the pipeline, print".

---

## 🏆 Extensions (make it yours)

1. **Hidden-line removal** — sort faces back-to-front and only draw front edges.
2. **A sphere / torus** — generate the vertices procedurally with `range` +
   `cartesian_product` + `map`.
3. **Shading** — shade each edge by its depth (pick a char from `".,-~:;=!*#$@"`).
4. **`s`/`w` to zoom** — drive the projection scale from a key, via SIMD
   `map_inplace` on the flat coordinate buffer.
5. **A status bar** — render the FPS / current shape / speed as a line
   (`fp::str::join`/`pad_right`).

The goal: a small program where *every* part is a pure function (`rotate`,
`project`, `draw`) threaded through the library's combinators (`map`, `par_map`,
`map_inplace`, `pipe`, `match`), with effects (`print`, key reads) confined to
the edges — the whole library, in one program.
