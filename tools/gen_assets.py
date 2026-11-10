#!/usr/bin/env python3
"""Generates the small, hand-authored assets used by tests/ and samples/.

The outputs are checked in, so this only needs to run when the assets change:

    python3 tools/gen_assets.py

Standard library only. Output is deterministic, so re-running without changes
produces identical files.
"""

import base64
import json
import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FLOAT = 5126
UNSIGNED_BYTE = 5121
UNSIGNED_SHORT = 5123
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963


class GltfBuilder:
    """Collects accessors into a single embedded buffer."""

    def __init__(self):
        self.blob = bytearray()
        self.views = []
        self.accessors = []

    def add(self, values, component_type, kind, target=None, with_bounds=False):
        """`values` is a list of tuples (or scalars for SCALAR). Returns the accessor index."""
        fmt = {FLOAT: "f", UNSIGNED_BYTE: "B", UNSIGNED_SHORT: "H"}[component_type]
        rows = [v if isinstance(v, (tuple, list)) else (v,) for v in values]

        while len(self.blob) % 4:
            self.blob.append(0)
        offset = len(self.blob)
        for row in rows:
            self.blob += struct.pack("<" + fmt * len(row), *row)

        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(self.blob) - offset}
        if target:
            view["target"] = target
        self.views.append(view)

        accessor = {
            "bufferView": len(self.views) - 1,
            "componentType": component_type,
            "count": len(rows),
            "type": kind,
        }
        if with_bounds:
            accessor["min"] = [min(r[i] for r in rows) for i in range(len(rows[0]))]
            accessor["max"] = [max(r[i] for r in rows) for i in range(len(rows[0]))]
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def finish(self, document):
        uri = "data:application/octet-stream;base64," + base64.b64encode(bytes(self.blob)).decode("ascii")
        document["asset"] = {"version": "2.0", "generator": "GeniEngine tools/gen_assets.py"}
        document["buffers"] = [{"byteLength": len(self.blob), "uri": uri}]
        document["bufferViews"] = self.views
        document["accessors"] = self.accessors
        return document


def write_json(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=1) + "\n")
    print("wrote", path.relative_to(ROOT))


def hierarchy_gltf():
    """A node tree that exercises every transform form the loader supports.

    Root (translation)
      Child (rotation + scale, red triangle mesh, animated translation)
      MatrixNode (matrix: translate (0,5,0) then scale (1,2,1))
      <unnamed node> (animated; the loader must give it the fallback name "node")
    """
    b = GltfBuilder()
    positions = b.add([(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)], FLOAT, "VEC3", ARRAY_BUFFER, True)
    normals = b.add([(0.0, 0.0, 1.0)] * 3, FLOAT, "VEC3", ARRAY_BUFFER)
    uvs = b.add([(0.0, 0.0), (1.0, 0.0), (0.0, 1.0)], FLOAT, "VEC2", ARRAY_BUFFER)
    indices = b.add([0, 1, 2], UNSIGNED_SHORT, "SCALAR", ELEMENT_ARRAY_BUFFER)

    times = b.add([0.0, 1.0], FLOAT, "SCALAR", None, True)
    slide = b.add([(0.0, 0.0, 0.0), (0.0, 2.0, 0.0)], FLOAT, "VEC3")
    grow = b.add([(1.0, 1.0, 1.0), (3.0, 3.0, 3.0)], FLOAT, "VEC3")

    half = math.sqrt(0.5)
    return b.finish(
        {
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": [
                {"name": "Root", "translation": [1.0, 2.0, 3.0], "children": [1, 2, 3]},
                # 90 degrees about +Y
                {"name": "Child", "rotation": [0.0, half, 0.0, half], "scale": [2.0, 2.0, 2.0], "mesh": 0},
                # column-major: scale (1,2,1), translation (0,5,0)
                {"name": "MatrixNode", "matrix": [1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, 0, 5, 0, 1]},
                {},
            ],
            "meshes": [
                {
                    "name": "Triangle",
                    "primitives": [
                        {
                            "attributes": {"POSITION": positions, "NORMAL": normals, "TEXCOORD_0": uvs},
                            "indices": indices,
                            "material": 0,
                        }
                    ],
                }
            ],
            "materials": [{"name": "Red", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.0, 0.0, 1.0]}}],
            "animations": [
                {
                    "name": "Slide",
                    "samplers": [
                        {"input": times, "output": slide, "interpolation": "LINEAR"},
                        {"input": times, "output": grow, "interpolation": "LINEAR"},
                    ],
                    "channels": [
                        {"sampler": 0, "target": {"node": 1, "path": "translation"}},
                        {"sampler": 1, "target": {"node": 3, "path": "scale"}},
                    ],
                }
            ],
        }
    )


def skinned_bar_gltf():
    """A square bar along +Y, 2 units tall, skinned to two joints and bending at the middle.

    Armature
      Joint0 (at the base)
        Joint1 (1 unit up, animated rotation about Z)
    Bar (mesh + skin)
    """
    b = GltfBuilder()
    half = 0.15
    segments = 8
    height = 2.0

    positions, normals, uvs, joints, weights, indices = [], [], [], [], [], []
    # (normal, corner a, corner b) for each of the four sides, counter-clockwise from outside
    sides = [
        ((0.0, 0.0, 1.0), (-half, half), (half, half)),
        ((1.0, 0.0, 0.0), (half, half), (half, -half)),
        ((0.0, 0.0, -1.0), (half, -half), (-half, -half)),
        ((-1.0, 0.0, 0.0), (-half, -half), (-half, half)),
    ]
    for normal, a, c in sides:
        base = len(positions)
        for ring in range(segments + 1):
            t = ring / segments
            y = t * height
            # Blend from Joint0 to Joint1 across the middle half of the bar.
            w1 = min(max((t - 0.25) / 0.5, 0.0), 1.0)
            for u, (x, z) in ((0.0, a), (1.0, c)):
                positions.append((x, y, z))
                normals.append(normal)
                uvs.append((u, t))
                joints.append((0, 1, 0, 0))
                weights.append((1.0 - w1, w1, 0.0, 0.0))
        for ring in range(segments):
            i = base + ring * 2
            indices += [i, i + 1, i + 3, i, i + 3, i + 2]

    a_pos = b.add(positions, FLOAT, "VEC3", ARRAY_BUFFER, True)
    a_nrm = b.add(normals, FLOAT, "VEC3", ARRAY_BUFFER)
    a_uv = b.add(uvs, FLOAT, "VEC2", ARRAY_BUFFER)
    a_jnt = b.add(joints, UNSIGNED_BYTE, "VEC4", ARRAY_BUFFER)
    a_wgt = b.add(weights, FLOAT, "VEC4", ARRAY_BUFFER)
    a_idx = b.add(indices, UNSIGNED_SHORT, "SCALAR", ELEMENT_ARRAY_BUFFER)

    identity = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    joint1_inverse_bind = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1)
    a_ibm = b.add([tuple(float(v) for v in identity), tuple(float(v) for v in joint1_inverse_bind)], FLOAT, "MAT4")

    key_times = [0.0, 0.5, 1.0, 1.5, 2.0]
    key_degrees = [0.0, 50.0, 0.0, -50.0, 0.0]
    rotations = []
    for degrees in key_degrees:
        half_angle = math.radians(degrees) / 2.0
        rotations.append((0.0, 0.0, math.sin(half_angle), math.cos(half_angle)))
    a_time = b.add(key_times, FLOAT, "SCALAR", None, True)
    a_rot = b.add(rotations, FLOAT, "VEC4")

    return b.finish(
        {
            "scene": 0,
            "scenes": [{"nodes": [0, 3]}],
            "nodes": [
                {"name": "Armature", "children": [1]},
                {"name": "Joint0", "children": [2]},
                {"name": "Joint1", "translation": [0.0, 1.0, 0.0]},
                {"name": "Bar", "mesh": 0, "skin": 0},
            ],
            "meshes": [
                {
                    "name": "Bar",
                    "primitives": [
                        {
                            "attributes": {
                                "POSITION": a_pos,
                                "NORMAL": a_nrm,
                                "TEXCOORD_0": a_uv,
                                "JOINTS_0": a_jnt,
                                "WEIGHTS_0": a_wgt,
                            },
                            "indices": a_idx,
                            "material": 0,
                        }
                    ],
                }
            ],
            "materials": [{"name": "Teal", "pbrMetallicRoughness": {"baseColorFactor": [0.1, 0.65, 0.6, 1.0]}}],
            "skins": [{"name": "BarSkin", "joints": [1, 2], "skeleton": 1, "inverseBindMatrices": a_ibm}],
            "animations": [
                {
                    "name": "Bend",
                    "samplers": [{"input": a_time, "output": a_rot, "interpolation": "LINEAR"}],
                    "channels": [{"sampler": 0, "target": {"node": 2, "path": "rotation"}}],
                }
            ],
        }
    )


def write_png(path, width, height, pixel):
    """Writes an 8-bit RGB PNG. `pixel(x, y)` returns (r, g, b)."""

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: none
        for x in range(width):
            raw += bytes(pixel(x, y))

    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += chunk(b"IEND", b"")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print("wrote", path.relative_to(ROOT))


def checker(x, y):
    light = ((x // 8) + (y // 8)) % 2 == 0
    return (235, 235, 235) if light else (150, 155, 165)


def main():
    write_json(ROOT / "tests/assets/models/hierarchy.gltf", hierarchy_gltf())
    bar = skinned_bar_gltf()
    write_json(ROOT / "tests/assets/models/skinned_bar.gltf", bar)
    write_json(ROOT / "samples/sandbox/assets/models/skinned_bar.gltf", bar)
    write_png(ROOT / "tests/assets/textures/checker.png", 16, 16, checker)
    write_png(ROOT / "samples/sandbox/assets/textures/checker.png", 64, 64, checker)


if __name__ == "__main__":
    main()
