#!/usr/bin/env python3
"""Generates the 2D sample's assets (samples/platformer/assets, docs/design/0017).

The pixel art is drawn here from code so the repository needs no binary art source:
the tileset, the hero's sprite sheet, a coin, a crate and three background layers, each
with a `.meta` file that cooks it as a sprite (RGBA8, one mip level). It also writes the
level, scenes/level.gscene, in the layout SceneSerializer writes, so the editor can open
and save it with no spurious changes. Once the level has been edited in the editor, edit
it there rather than here.

Run from anywhere:  python3 tools/gen_2d_assets.py
"""

import json
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_assets import f32, fnv1a64, node, write_scene_text  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / "samples" / "platformer" / "assets"

T = 16  # pixels per tile and per hero frame


def write_png_rgba(path, width, height, pixels):
    """8-bit RGBA PNG from a flat list of (r, g, b, a), top row first."""

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw += bytes(pixels[y * width + x])
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print("wrote", path.relative_to(ROOT))


def write_meta(path, guid):
    """A sprite texture's .meta: a fixed GUID (so IDs never change) and the sprite usage."""
    meta = pathlib.Path(str(path) + ".meta")
    meta.write_text(json.dumps({"guid": guid, "usage": "sprite"}, indent=2) + "\n")


def texture_id(guid):
    # Cooker.cpp: "texture:" + RootName (the GUID when the .meta has one); the sprite
    # usage adds "#sprite".
    return fnv1a64("texture:guid:" + guid + "#sprite")


class Canvas:
    def __init__(self, width, height, fill=(0, 0, 0, 0)):
        self.width = width
        self.height = height
        self.pixels = [fill] * (width * height)

    def set(self, x, y, color):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y * self.width + x] = color

    def get(self, x, y):
        return self.pixels[y * self.width + x]

    def rect(self, x0, y0, w, h, color):
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                self.set(x, y, color)

    def save(self, relative, guid):
        path = ASSETS / relative
        write_png_rgba(path, self.width, self.height, self.pixels)
        write_meta(path, guid)


def noise(x, y, seed):
    """Deterministic per-pixel noise in 0..255."""
    h = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFF


def shade(color, amount):
    r, g, b, a = color
    return (max(0, min(255, r + amount)), max(0, min(255, g + amount)), max(0, min(255, b + amount)), a)


# Tileset: 8 x 4 tiles. Tiles 0-7 are solid (TILES_SOLID), 8-15 are decoration.
GRASS, DIRT, STONE, BRICK, PLANK, GRASS_LEFT, GRASS_RIGHT, DARK_DIRT = range(8)
FLOWER, TUFT, BUSH, PEBBLE, SIGN, MUSHROOM, FENCE, LAMP = range(8, 16)
TILES_SOLID = "0-7"

DIRT_COLOR = (139, 94, 60, 255)
GRASS_COLOR = (92, 168, 64, 255)
GRASS_DARK = (58, 122, 46, 255)
STONE_COLOR = (128, 132, 144, 255)
BRICK_COLOR = (170, 78, 58, 255)
WOOD_COLOR = (176, 124, 70, 255)


def draw_tile(c, index):
    ox = (index % 8) * T
    oy = (index // 8) * T

    def px(x, y, color):
        c.set(ox + x, oy + y, color)

    def dirt(x, y, base=DIRT_COLOR):
        n = noise(ox + x, oy + y, 3)
        return shade(base, -18 if n < 40 else (14 if n > 225 else 0))

    if index in (GRASS, GRASS_LEFT, GRASS_RIGHT, DIRT, DARK_DIRT):
        base = shade(DIRT_COLOR, -30) if index == DARK_DIRT else DIRT_COLOR
        for y in range(T):
            for x in range(T):
                px(x, y, dirt(x, y, base))
        if index in (GRASS, GRASS_LEFT, GRASS_RIGHT):
            for x in range(T):
                depth = 4 + (1 if noise(ox + x, 0, 7) > 160 else 0) + (1 if noise(ox + x, 1, 9) > 220 else 0)
                for y in range(depth):
                    px(x, y, GRASS_COLOR if y < depth - 1 else GRASS_DARK)
                px(x, 0, shade(GRASS_COLOR, 30))
            if index == GRASS_LEFT:
                for y in range(T):
                    px(0, y, shade(GRASS_DARK, -10))
            if index == GRASS_RIGHT:
                for y in range(T):
                    px(T - 1, y, shade(GRASS_DARK, -10))
    elif index == STONE:
        for y in range(T):
            for x in range(T):
                edge = x == 0 or y == 0 or x == T - 1 or y == T - 1
                n = noise(ox + x, oy + y, 5)
                color = shade(STONE_COLOR, -40 if edge else (-12 if n < 50 else (10 if n > 220 else 0)))
                if x == 1 or y == 1:
                    color = shade(STONE_COLOR, 24)
                px(x, y, color)
    elif index == BRICK:
        mortar = (196, 180, 160, 255)
        for y in range(T):
            for x in range(T):
                row = y // 4
                offset = 4 if row % 2 else 0
                if y % 4 == 3 or (x + offset) % 8 == 7:
                    px(x, y, mortar)
                else:
                    px(x, y, shade(BRICK_COLOR, -10 if noise(ox + x, oy + y, 11) < 60 else 0))
    elif index == PLANK:
        # Three boards filling the tile: it collides as a whole tile.
        for y in range(T):
            for x in range(T):
                board = y % 16 // 5
                color = shade(WOOD_COLOR, (-12, 6, -4)[min(board, 2)])
                if y % 5 == 4 or y == T - 1 or x in (0, T - 1):
                    color = shade(WOOD_COLOR, -55)
                if x in (2, 13) and y % 5 == 2:
                    color = (80, 60, 40, 255)
                px(x, y, color)
    elif index == FLOWER:
        for y in range(10, 16):
            px(7, y, GRASS_DARK)
        px(6, 12, GRASS_COLOR)
        px(8, 13, GRASS_COLOR)
        for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1)):
            px(7 + dx, 9 + dy, (236, 84, 120, 255))
        px(7, 9, (250, 220, 80, 255))
    elif index == TUFT:
        for x, h in ((4, 4), (6, 6), (8, 5), (10, 7), (12, 4)):
            for y in range(T - h, T):
                px(x, y, GRASS_COLOR if y > T - h + 1 else shade(GRASS_COLOR, 30))
    elif index == BUSH:
        for y in range(T):
            for x in range(T):
                dx = x - 7.5
                dy = y - 11.0
                if dx * dx / 64.0 + dy * dy / 30.0 < 1.0:
                    n = noise(ox + x, oy + y, 13)
                    px(x, y, shade((70, 150, 60, 255), -20 if n < 70 else (16 if n > 220 else 0)))
    elif index == PEBBLE:
        for y in range(12, 16):
            for x in range(5, 11):
                if not ((y == 12) and (x in (5, 10))):
                    px(x, y, shade(STONE_COLOR, 10 if y == 12 else -10))
    elif index == SIGN:
        for y in range(9, 16):
            px(7, y, shade(WOOD_COLOR, -40))
            px(8, y, shade(WOOD_COLOR, -40))
        for y in range(3, 9):
            for x in range(2, 14):
                px(x, y, WOOD_COLOR if 2 < x < 13 and 3 < y < 8 else shade(WOOD_COLOR, -50))
        for x in (5, 6, 7, 9, 10):
            px(x, 5, (60, 40, 30, 255))
        px(11, 6, (60, 40, 30, 255))
    elif index == MUSHROOM:
        for y in range(11, 16):
            for x in range(6, 10):
                px(x, y, (236, 226, 200, 255))
        for y in range(6, 11):
            for x in range(3, 13):
                dx = x - 7.5
                if dx * dx / 25.0 + (y - 10.5) ** 2 / 20.0 < 1.0:
                    px(x, y, (210, 60, 50, 255))
        for x, y in ((5, 8), (9, 7), (10, 9)):
            px(x, y, (250, 240, 230, 255))
    elif index == FENCE:
        post = shade(WOOD_COLOR, 20)
        for y in range(6, 16):
            for x in (2, 3, 12, 13):
                px(x, y, post)
        for y in (8, 9, 12, 13):
            for x in range(T):
                px(x, y, WOOD_COLOR)
    elif index == LAMP:
        for y in range(4, 16):
            px(7, y, (60, 60, 70, 255))
            px(8, y, (60, 60, 70, 255))
        for y in range(1, 5):
            for x in range(5, 11):
                px(x, y, (255, 226, 130, 255) if 5 < x < 10 and 1 < y < 4 else (60, 60, 70, 255))


def tileset():
    c = Canvas(8 * T, 4 * T)
    for index in range(16):
        draw_tile(c, index)
    c.save("textures/tiles.png", "6c0b2f6e4d1a9e01")


# Hero sheet: 8 frames in a row. 0-1 idle, 2-5 run, 6 jump, 7 fall.
SKIN = (240, 196, 160, 255)
SHIRT = (64, 120, 212, 255)
PANTS = (52, 56, 84, 255)
HAIR = (90, 56, 36, 255)
BOOT = (40, 34, 30, 255)
OUTLINE = (24, 22, 30, 255)


def draw_hero(c, frame):
    ox = frame * T
    bob = 1 if frame == 1 else 0
    # Legs: per frame, (left foot x offset, right foot x offset, left lift, right lift).
    legs = {
        0: (0, 0, 0, 0), 1: (0, 0, 0, 0),
        2: (-2, 2, 0, 1), 3: (-1, 1, 1, 0), 4: (2, -2, 0, 1), 5: (1, -1, 1, 0),
        6: (-1, 2, 2, 1), 7: (-2, 1, 0, 2),
    }[frame]
    top = 2 + bob

    def px(x, y, color):
        c.set(ox + x, y, color)

    # Head.
    for y in range(top, top + 5):
        for x in range(5, 11):
            px(x, y, SKIN)
    for x in range(5, 11):
        px(x, top, HAIR)
        px(x, top + 1, HAIR if x < 8 else SKIN)
    px(5, top + 2, HAIR)
    px(9, top + 2, OUTLINE)  # eye, looking right
    # Body.
    for y in range(top + 5, top + 10):
        for x in range(5, 11):
            px(x, y, SHIRT)
    arm = -1 if frame in (2, 4, 6) else (1 if frame in (3, 5, 7) else 0)
    for y in range(top + 5, top + 9):
        px(4, y + (arm if y > top + 6 else 0), SKIN if y == top + 8 else SHIRT)
        px(11, y - (arm if y > top + 6 else 0), SKIN if y == top + 8 else SHIRT)
    # Legs.
    hip = top + 10
    for side, (dx, lift) in enumerate(((legs[0], legs[2]), (legs[1], legs[3]))):
        x0 = (5 if side == 0 else 8) + dx
        for y in range(hip, 16 - lift):
            for x in range(x0, x0 + 3):
                px(max(0, min(T - 1, x)), y, BOOT if y >= 14 - lift else PANTS)


def hero():
    c = Canvas(8 * T, T)
    for frame in range(8):
        draw_hero(c, frame)
    c.save("textures/hero.png", "6c0b2f6e4d1a9e02")


def coin():
    # 6 frames of 8 x 8: a coin turning.
    size = 8
    c = Canvas(6 * size, size)
    widths = [3.5, 2.6, 1.2, 0.6, 1.2, 2.6]
    for frame, half in enumerate(widths):
        for y in range(size):
            for x in range(size):
                dx = (x + 0.5 - 4.0) / half
                dy = (y + 0.5 - 4.0) / 3.5
                d = dx * dx + dy * dy
                if d <= 1.0:
                    color = (250, 204, 60, 255) if d < 0.55 else (214, 150, 30, 255)
                    if dx < -0.3 and dy < -0.2 and d < 0.6:
                        color = (255, 244, 170, 255)
                    c.set(frame * size + x, y, color)
    c.save("textures/coin.png", "6c0b2f6e4d1a9e03")


def crate():
    c = Canvas(T, T)
    edge = (110, 70, 40, 255)
    for y in range(T):
        for x in range(T):
            border = x < 2 or y < 2 or x > T - 3 or y > T - 3
            diagonal = abs(x - y) <= 1 or abs(x - (T - 1 - y)) <= 1
            color = edge if border or diagonal else shade(WOOD_COLOR, -8 if (y // 4) % 2 else 6)
            c.set(x, y, color)
    c.save("textures/crate.png", "6c0b2f6e4d1a9e04")


def backgrounds():
    # Sky: a vertical gradient, drawn stretched with linear filtering.
    c = Canvas(4, 64)
    top = (92, 148, 220)
    bottom = (196, 228, 246)
    for y in range(64):
        t = y / 63.0
        color = tuple(int(round(a + (b - a) * t)) for a, b in zip(top, bottom)) + (255,)
        for x in range(4):
            c.set(x, y, color)
    c.save("textures/sky.png", "6c0b2f6e4d1a9e05")

    # Hills: 128 x 48, repeats horizontally (both edges at the same height).
    import math
    w, h = 128, 48
    c = Canvas(w, h)
    for x in range(w):
        a = 2.0 * math.pi * x / w
        far = 22 + 8 * math.sin(a) + 4 * math.sin(3 * a + 1.0)
        near = 32 + 6 * math.sin(2 * a + 2.0) + 3 * math.sin(5 * a)
        for y in range(h):
            if y >= near:
                c.set(x, y, (104, 170, 96, 255) if noise(x, y, 17) > 30 else (96, 160, 90, 255))
            elif y >= far:
                c.set(x, y, (150, 196, 170, 255))
    c.save("textures/hills.png", "6c0b2f6e4d1a9e06")

    # Clouds: 64 x 24.
    w, h = 64, 24
    c = Canvas(w, h)
    for cx, cy, rx, ry in ((18, 14, 14, 7), (32, 10, 14, 9), (46, 14, 13, 7)):
        for y in range(h):
            for x in range(w):
                d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
                if d < 1.0:
                    c.set(x, y, (255, 255, 255, 255) if y < cy + ry * 0.4 else (226, 236, 248, 255))
    c.save("textures/clouds.png", "6c0b2f6e4d1a9e07")


# The level: 96 x 18 cells of 1 unit, the map's top-left corner at (0, 18).
WIDTH, HEIGHT = 96, 18


def level_tiles():
    tiles = [[-1] * WIDTH for _ in range(HEIGHT)]
    ground = 14  # the grass row

    def column_fill(x, top, tile_top=GRASS):
        for y in range(top, HEIGHT):
            tiles[y][x] = tile_top if y == top else (DIRT if y < top + 2 else DARK_DIRT)

    gaps = {range(30, 33), range(58, 61)}
    heights = {}
    for x in range(WIDTH):
        if any(x in g for g in gaps):
            continue
        top = ground
        if 40 <= x < 48:
            top = ground - 2  # a raised step
        if 70 <= x < 76:
            top = ground - 1
        heights[x] = top
        column_fill(x, top)
    # Grass edges next to gaps and steps.
    for x, top in heights.items():
        if heights.get(x - 1) != top:
            tiles[top][x] = GRASS_LEFT
        if heights.get(x + 1) != top:
            tiles[top][x] = GRASS_RIGHT
    # Walls at the ends of the level.
    for y in range(HEIGHT):
        tiles[y][0] = STONE
        tiles[y][WIDTH - 1] = STONE
    # Floating platforms and a brick tower.
    for x in range(14, 19):
        tiles[10][x] = PLANK
    for x in range(26, 30):
        tiles[9][x] = PLANK
    for x in range(33, 37):
        tiles[11][x] = STONE
    for x in range(52, 56):
        tiles[8][x] = PLANK
    for y in range(9, 14):
        for x in range(64, 67):
            tiles[y][x] = BRICK
    for x in range(80, 86):
        tiles[9][x] = PLANK
    # Decoration on top of the ground.
    decor = {5: FLOWER, 7: TUFT, 9: SIGN, 12: BUSH, 21: MUSHROOM, 23: TUFT, 37: FENCE, 38: FENCE,
             44: FLOWER, 50: PEBBLE, 62: BUSH, 68: LAMP, 78: TUFT, 88: FLOWER, 90: MUSHROOM}
    for x, tile in decor.items():
        if x in heights and tiles[heights[x] - 1][x] == -1:
            tiles[heights[x] - 1][x] = tile
    return tiles, heights


def sprite(texture, size, layer, pivot=(0.5, 0.5), columns=1, rows=1, frame=0, color=(1.0, 1.0, 1.0, 1.0),
           filter_name="nearest"):
    return {"type": "SpriteComponent", "properties": {
        "texture": texture, "size": list(size), "pivot": list(pivot), "color": list(color),
        "columns": columns, "rows": rows, "frame": frame, "flipX": False, "flipY": False,
        "visible": True, "layer": layer, "filter": filter_name}}


def animator(first, count, fps, loop=True):
    return {"type": "SpriteAnimatorComponent", "properties": {
        "firstFrame": first, "frameCount": count, "fps": fps, "loop": loop, "playing": True}}


def level():
    tiles_id = texture_id("6c0b2f6e4d1a9e01")
    hero_id = texture_id("6c0b2f6e4d1a9e02")
    coin_id = texture_id("6c0b2f6e4d1a9e03")
    crate_id = texture_id("6c0b2f6e4d1a9e04")
    sky_id = texture_id("6c0b2f6e4d1a9e05")
    hills_id = texture_id("6c0b2f6e4d1a9e06")
    clouds_id = texture_id("6c0b2f6e4d1a9e07")
    tiles, heights = level_tiles()

    def ground_y(x):
        # World y of the top of the ground at column x.
        return float(HEIGHT - heights[x])

    nodes = []

    def add(name, parent, position=(0.0, 0.0, 0.0), components=None, scale=(1.0, 1.0, 1.0)):
        nodes.append(node(name, parent, position=position, scale=scale, components=components))
        return len(nodes) - 1

    root = add("Level", -1)
    add("Camera", root, position=(8.0, 9.0, 20.0), components=[
        {"type": "CameraComponent", "properties": {
            "far": 100.0, "fov": 60.0, "near": 0.1, "orthoSize": 12.0, "projection": "orthographic"}}])

    # Backgrounds: children of "Parallax <factor>" groups that the game moves with the camera.
    sky = add("Sky", root, position=(0.0, 9.0, -10.0),
              components=[sprite(sky_id, (400.0, 40.0), -100, filter_name="linear")])
    del sky
    far = add("Parallax 0.8", root)
    for i in range(6):
        add("Cloud %d" % i, far, position=(6.0 + i * 19.0, 8.5 + (i % 3) * 1.0, -8.0),
            components=[sprite(clouds_id, (4.0, 1.5), -90)])
    near = add("Parallax 0.5", root)
    for i in range(-1, 9):
        add("Hills %d" % (i + 1), near, position=(i * 16.0, 1.0, -6.0),
            components=[sprite(hills_id, (16.0, 6.0), -80, pivot=(0.0, 0.0))])

    text = "\n".join(",".join(str(t) for t in row) for row in tiles)
    add("Tilemap", root, position=(0.0, float(HEIGHT), 0.0), components=[{"type": "TilemapComponent", "properties": {
        "tileset": tiles_id, "tilesetColumns": 8, "tilesetRows": 4, "width": WIDTH, "height": HEIGHT,
        "tileSize": [1.0, 1.0], "tiles": text, "color": [1.0, 1.0, 1.0, 1.0], "layer": 0, "filter": "nearest",
        "collision": True, "solidTiles": TILES_SOLID}}])

    player = add("Player", root, position=(4.0, ground_y(4) + 0.05, 0.0), components=[
        {"type": "CharacterControllerComponent", "properties": {
            "radius": 0.3, "height": 0.9, "stepHeight": 0.25, "maxSlope": 50.0, "walkSpeed": 6.0, "runSpeed": 9.0,
            "jumpSpeed": 9.5, "acceleration": 40.0, "turnSpeed": 0.0, "playerInput": False, "speedParameter": ""}}])
    add("Body", player, components=[
        sprite(hero_id, (1.0, 1.0), 10, pivot=(0.5, 0.0), columns=8), animator(0, 2, 3.0)])

    for i, x in enumerate((15, 16, 17, 27, 28, 34, 35, 44, 45, 46, 53, 54, 65, 81, 82, 83, 88, 89, 90, 91)):
        top = HEIGHT - next(r for r in range(HEIGHT) if tiles[r][x] in (GRASS, GRASS_LEFT, GRASS_RIGHT, PLANK,
                                                                       STONE, BRICK) and r > 0)
        add("Coin %d" % i, root, position=(x + 0.5, top + 0.75, 0.0),
            components=[sprite(coin_id, (0.5, 0.5), 5, columns=6), animator(0, 6, 10.0)])

    for i, (x, stack) in enumerate(((20, 1), (38, 2), (49, 1), (77, 3))):
        for level_index in range(stack):
            add("Crate %d.%d" % (i, level_index), root,
                position=(x + 0.5, ground_y(x) + 0.5 + level_index * 1.0, 0.0), components=[
                    sprite(crate_id, (1.0, 1.0), 4),
                    {"type": "PhysicsComponent", "properties": {
                        "bodyType": "dynamic", "colliderType": "box", "size": [0.98, 0.98, 0.98], "radius": 0.0,
                        "height": 0.0, "mass": 8.0, "plane2D": True, "friction": 0.6}}])

    document = {"format": "geni.scene", "version": 1, "mainCamera": 1, "nodes": nodes}
    write_scene_text(ASSETS / "scenes" / "level.gscene", document)
    print("wrote", (ASSETS / "scenes" / "level.gscene").relative_to(ROOT))


def actions():
    path = ASSETS / "input" / "actions.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    document = {"actions": {
        "move_x": {"type": "axis", "group": "gameplay", "bindings": [
            {"control": "key:D"}, {"control": "key:Right"}, {"control": "key:A", "scale": -1},
            {"control": "key:Left", "scale": -1}, "pad:LeftX"]},
        "jump": {"type": "button", "group": "gameplay", "bindings": ["key:Space", "key:W", "key:Up", "pad:A"]},
        "run": {"type": "button", "group": "gameplay", "bindings": ["key:LeftShift", "pad:X"]},
        "pause": {"type": "button", "bindings": ["key:Escape", "key:P", "pad:Start"]},
        "ui_up": {"type": "button", "group": "ui", "bindings": ["key:Up", "key:W", "pad:DpadUp"]},
        "ui_down": {"type": "button", "group": "ui", "bindings": ["key:Down", "key:S", "pad:DpadDown"]},
        "ui_left": {"type": "button", "group": "ui", "bindings": ["key:Left", "key:A", "pad:DpadLeft"]},
        "ui_right": {"type": "button", "group": "ui", "bindings": ["key:Right", "key:D", "pad:DpadRight"]},
        "ui_accept": {"type": "button", "group": "ui", "bindings": ["key:Enter", "key:Space", "pad:A"]},
        "ui_back": {"type": "button", "group": "ui", "bindings": ["key:Escape", "pad:B"]},
    }}
    path.write_text(json.dumps(document, indent=2) + "\n")
    print("wrote", path.relative_to(ROOT))


def main():
    tileset()
    hero()
    coin()
    crate()
    backgrounds()
    level()
    actions()


if __name__ == "__main__":
    main()
