# Generates the texture files of this folder (needs Pillow). All textures are seamless: u and v repeat.
#
#   python textures/generate.py
import math
import random
from pathlib import Path

from PIL import Image

SIZE = 512
OUTPUT = Path(__file__).parent


class Perlin:
    """Perlin noise with period `cells` over [0, 1): the value at u = 0 equals the value at u = 1."""

    def __init__(self, cells, seed):
        rng = random.Random(seed)
        self.cells = cells
        self.gradients = []
        for _ in range(cells * cells):
            angle = rng.uniform(0, 2 * math.pi)
            self.gradients.append((math.cos(angle), math.sin(angle)))

    def gradient(self, x, y):
        return self.gradients[(y % self.cells) * self.cells + x % self.cells]

    def __call__(self, u, v):
        x, y = u * self.cells, v * self.cells
        x0, y0 = math.floor(x), math.floor(y)
        fx, fy = x - x0, y - y0

        def corner(cx, cy):
            gx, gy = self.gradient(x0 + cx, y0 + cy)
            return gx * (fx - cx) + gy * (fy - cy)

        sx = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
        sy = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
        top = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * sx
        bottom = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * sx
        return top + (bottom - top) * sy


def fractal(cells, octaves, seed):
    """Sum of octaves with doubling frequency, about [-1, 1]."""
    layers = [Perlin(cells * 2**i, seed + i) for i in range(octaves)]

    def noise(u, v):
        return sum(layer(u, v) / 2**i for i, layer in enumerate(layers))

    return noise


def clamp01(x):
    return min(max(x, 0.0), 1.0)


def mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def save(name, function):
    image = Image.new('RGB', (SIZE, SIZE))
    pixels = image.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = function(x / SIZE, y / SIZE)
            pixels[x, y] = (round(255 * clamp01(r)), round(255 * clamp01(g)), round(255 * clamp01(b)))
    image.save(OUTPUT / f'{name}.png')
    print(name)


def bricks():
    rows, columns, mortar = 8, 4, 0.1  # Mortar width as a part of the brick height
    rng = random.Random(1)
    shades = [[rng.uniform(0.75, 1.15) for _ in range(columns)] for _ in range(rows)]
    surface = fractal(16, 3, 10)
    wet = fractal(4, 3, 20)

    def brick(u, v):
        """Row and column of the brick, or None in the mortar."""
        row = int(v * rows)
        shifted = (u + (0.5 / columns if row % 2 else 0)) % 1
        column = int(shifted * columns)
        half_height = mortar / 2
        half_width = half_height * rows / columns
        if v * rows - row < half_height or row + 1 - v * rows < half_height:
            return None
        if shifted * columns - column < half_width or column + 1 - shifted * columns < half_width:
            return None
        return row, column

    def albedo(u, v):
        where = brick(u, v)
        grain = surface(u, v)
        if where is None:
            shade = 0.55 + 0.2 * grain
            return shade, shade * 0.95, shade * 0.85
        shade = shades[where[0]][where[1]] + 0.35 * grain
        return 0.62 * shade, 0.27 * shade, 0.2 * shade

    def specular(u, v):
        # Wet patches on the bricks shine, mortar does not
        mask = 0 if brick(u, v) is None else clamp01(0.3 + 2.5 * wet(u, v))
        return mask, mask, mask

    save('bricks_albedo', albedo)
    save('bricks_specular', specular)


def lava():
    rock = fractal(8, 4, 30)
    cracks = fractal(4, 4, 40)

    def glow(u, v):
        # Perlin noise crosses zero along thin curved lines
        return clamp01(1 - abs(cracks(u, v)) / 0.07)

    def albedo(u, v):
        shade = 0.2 + 0.2 * rock(u, v)
        return mix((shade, shade * 0.93, shade * 0.9), (0.05, 0.03, 0.02), glow(u, v))

    def specular(u, v):
        mask = clamp01(0.15 + 0.8 * rock(u, v)) * (1 - glow(u, v))
        return mask, mask, mask

    def emissive(u, v):
        g = glow(u, v)
        return g, 0.45 * g * g, 0.08 * g * g * g

    save('lava_albedo', albedo)
    save('lava_specular', specular)
    save('lava_emissive', emissive)


def metal():
    plates, seam = 4, 0.012
    surface = fractal(32, 2, 50)
    rust = fractal(4, 4, 60)

    def in_seam(u, v):
        fu, fv = (u * plates) % 1, (v * plates) % 1
        return min(fu, 1 - fu) < seam * plates or min(fv, 1 - fv) < seam * plates

    def is_rivet(u, v):
        fu, fv = (u * plates) % 1, (v * plates) % 1
        du, dv = min(fu, 1 - fu) - 0.08, min(fv, 1 - fv) - 0.08
        return du * du + dv * dv < 0.0006

    def rust_amount(u, v):
        return clamp01((rust(u, v) - 0.15) * 6)

    def albedo(u, v):
        if in_seam(u, v):
            return 0.12, 0.12, 0.13
        shade = (0.72 if is_rivet(u, v) else 0.52) + 0.08 * surface(u, v)
        return mix((shade, shade * 1.02, shade * 1.06), (0.42, 0.21, 0.1), rust_amount(u, v))

    def specular(u, v):
        # Clean metal shines, rust and seams do not
        mask = 0 if in_seam(u, v) else (0.95 + 0.2 * surface(u, v)) * (1 - rust_amount(u, v))
        return mask, mask, mask

    save('metal_albedo', albedo)
    save('metal_specular', specular)


def wood():
    # Vertical planks with grain along v. Only albedo: no highlight and no glow
    bend = fractal(2, 2, 70)
    fine = fractal(8, 2, 80)

    def albedo(u, v):
        rings = 0.5 + 0.5 * math.sin(2 * math.pi * (5 * u + 0.8 * bend(u, v)))
        shade = 0.9 + 0.15 * fine(u, v)
        return mix((0.36, 0.2, 0.1), (0.68, 0.46, 0.27), rings * shade)

    save('wood_albedo', albedo)


if __name__ == '__main__':
    bricks()
    lava()
    metal()
    wood()
