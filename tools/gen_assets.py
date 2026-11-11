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


def write_json(path, document, compact=False):
    """Writes indented JSON, or one line per top-level array entry when `compact`.

    The city block is 10,000 nodes; indented it is 150,000 lines of diff noise, so it is
    written with one node, accessor or buffer per line instead.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    if compact:
        lines = ["{"]
        items = list(document.items())
        for i, (key, value) in enumerate(items):
            end = "," if i + 1 < len(items) else ""
            if isinstance(value, list) and value:
                lines.append(" " + json.dumps(key) + ": [")
                for j, entry in enumerate(value):
                    sep = "," if j + 1 < len(value) else ""
                    lines.append("  " + json.dumps(entry, separators=(",", ":")) + sep)
                lines.append(" ]" + end)
            else:
                lines.append(" " + json.dumps(key) + ": " + json.dumps(value, separators=(",", ":")) + end)
        lines.append("}")
        path.write_text("\n".join(lines) + "\n")
    else:
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


def textured_quad_gltf():
    """A quad whose material uses ../textures/checker.png twice: as base colour (cooked as
    sRGB colour) and as a normal map (cooked as BC5/linear). Exercises material texture
    dependencies and per-usage texture cooking.
    """
    b = GltfBuilder()
    positions = b.add(
        [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (1.0, 1.0, 0.0), (0.0, 1.0, 0.0)], FLOAT, "VEC3", ARRAY_BUFFER, True
    )
    normals = b.add([(0.0, 0.0, 1.0)] * 4, FLOAT, "VEC3", ARRAY_BUFFER)
    uvs = b.add([(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)], FLOAT, "VEC2", ARRAY_BUFFER)
    indices = b.add([0, 1, 2, 0, 2, 3], UNSIGNED_SHORT, "SCALAR", ELEMENT_ARRAY_BUFFER)
    return b.finish(
        {
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": [{"name": "Quad", "mesh": 0}],
            "meshes": [
                {
                    "name": "Quad",
                    "primitives": [
                        {
                            "attributes": {"POSITION": positions, "NORMAL": normals, "TEXCOORD_0": uvs},
                            "indices": indices,
                            "material": 0,
                        }
                    ],
                }
            ],
            "materials": [
                {
                    "name": "Checker",
                    "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}},
                    "normalTexture": {"index": 0, "scale": 1.0},
                }
            ],
            "textures": [{"source": 0}],
            "images": [{"uri": "../textures/checker.png"}],
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



def cylinder(segments, rings):
    """A closed unit cylinder (radius 0.5, y from -0.5 to 0.5) with smooth sides.

    Rows are (x, y, z, u, v, nx, ny, nz); indices are counter-clockwise from outside.
    """
    verts, idx = [], []
    for r in range(rings + 1):
        y = -0.5 + r / rings
        for s in range(segments + 1):
            a = 2.0 * math.pi * s / segments
            x, z = math.cos(a), math.sin(a)
            verts.append((0.5 * x, y, 0.5 * z, s / segments, r / rings, x, 0.0, z))
    row = segments + 1
    for r in range(rings):
        for s in range(segments):
            a, b_, c, d = r * row + s, r * row + s + 1, (r + 1) * row + s, (r + 1) * row + s + 1
            idx += [a, c, b_, b_, c, d]
    for y, ny in ((0.5, 1.0), (-0.5, -1.0)):
        centre = len(verts)
        verts.append((0.0, y, 0.0, 0.5, 0.5, 0.0, ny, 0.0))
        for s in range(segments + 1):
            a = 2.0 * math.pi * s / segments
            verts.append((0.5 * math.cos(a), y, 0.5 * math.sin(a),
                          0.5 + 0.5 * math.cos(a), 0.5 + 0.5 * math.sin(a), 0.0, ny, 0.0))
        for s in range(segments):
            p, q = centre + 1 + s, centre + 2 + s
            idx += [centre, q, p] if ny > 0 else [centre, p, q]
    return verts, idx


def uv_sphere(segments, rings):
    """A unit-diameter UV sphere with smooth normals, counter-clockwise from outside."""
    verts, idx = [], []
    for r in range(rings + 1):
        phi = math.pi * r / rings
        for s in range(segments + 1):
            theta = 2.0 * math.pi * s / segments
            n = (math.sin(phi) * math.cos(theta), math.cos(phi), math.sin(phi) * math.sin(theta))
            verts.append((0.5 * n[0], 0.5 * n[1], 0.5 * n[2], s / segments, r / rings, n[0], n[1], n[2]))
    row = segments + 1
    for r in range(rings):
        for s in range(segments):
            a, b_, c, d = r * row + s, r * row + s + 1, (r + 1) * row + s, (r + 1) * row + s + 1
            if r > 0:
                idx += [a, b_, c]
            if r < rings - 1:
                idx += [b_, d, c]
    return verts, idx


def city_kit_gltf():
    """The reference game's building kit: nine unit cubes, one per material.

    Only meshes and materials. The city block's layout (10,000 buildings, 100 lights, the
    camera) used to be generated here too; since M4 it is a scene edited in the editor,
    samples/city/assets/scenes/city.gscene, which refers to these meshes and materials by
    asset ID. The IDs come from this file's path and the mesh and material names, so
    renaming either breaks that scene. Each palette entry gets its own mesh so a glTF
    node can choose a material (glTF materials live on primitives, not nodes).
    """
    b = GltfBuilder()

    # Unit cube, identical to Mesh::CreateBox(1.0): position, color, uv, normal, CCW.
    h = 0.5
    vertices = [
        # Front face
        (h, h, h, 1, 0, 0, 1, 1, 0, 0, 1),
        (-h, h, h, 0, 1, 0, 0, 1, 0, 0, 1),
        (-h, -h, h, 0, 0, 1, 0, 0, 0, 0, 1),
        (h, -h, h, 1, 1, 0, 1, 0, 0, 0, 1),
        # Top face
        (h, h, -h, 1, 0, 0, 1, 0, 0, 1, 0),
        (-h, h, -h, 0, 1, 0, 0, 0, 0, 1, 0),
        (-h, h, h, 0, 0, 1, 0, 1, 0, 1, 0),
        (h, h, h, 1, 1, 0, 1, 1, 0, 1, 0),
        # Right face
        (h, h, -h, 1, 0, 0, 1, 1, 1, 0, 0),
        (h, h, h, 0, 1, 0, 0, 1, 1, 0, 0),
        (h, -h, h, 0, 0, 1, 0, 0, 1, 0, 0),
        (h, -h, -h, 1, 1, 0, 1, 0, 1, 0, 0),
        # Left face
        (-h, h, h, 1, 0, 0, 1, 1, -1, 0, 0),
        (-h, h, -h, 0, 1, 0, 0, 1, -1, 0, 0),
        (-h, -h, -h, 0, 0, 1, 0, 0, -1, 0, 0),
        (-h, -h, h, 1, 1, 0, 1, 0, -1, 0, 0),
        # Bottom face
        (h, -h, h, 1, 0, 0, 1, 1, 0, -1, 0),
        (-h, -h, h, 0, 1, 0, 0, 1, 0, -1, 0),
        (-h, -h, -h, 0, 0, 1, 0, 0, 0, -1, 0),
        (h, -h, -h, 1, 1, 0, 1, 0, 0, -1, 0),
        # Back face
        (-h, h, -h, 1, 0, 0, 1, 1, 0, 0, -1),
        (h, h, -h, 0, 1, 0, 0, 1, 0, 0, -1),
        (h, -h, -h, 0, 0, 1, 0, 0, 0, 0, -1),
        (-h, -h, -h, 1, 1, 0, 1, 0, 0, 0, -1),
    ]
    indices = [
        0,1,2, 0,2,3, 4,5,6, 4,6,7, 8,9,10, 8,10,11,
        12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23,
    ]
    positions = b.add([v[0:3] for v in vertices], FLOAT, "VEC3", ARRAY_BUFFER, True)
    colors = b.add([v[3:6] for v in vertices], FLOAT, "VEC3", ARRAY_BUFFER)
    uvs = b.add([v[6:8] for v in vertices], FLOAT, "VEC2", ARRAY_BUFFER)
    normals = b.add([v[8:11] for v in vertices], FLOAT, "VEC3", ARRAY_BUFFER)
    index_accessor = b.add(indices, UNSIGNED_SHORT, "SCALAR", ELEMENT_ARRAY_BUFFER)

    palette = [
        (0.56, 0.56, 0.58), (0.42, 0.40, 0.38), (0.65, 0.61, 0.55), (0.34, 0.32, 0.31),
        (0.52, 0.36, 0.30), (0.30, 0.37, 0.44), (0.68, 0.66, 0.62), (0.45, 0.29, 0.25),
    ]
    materials = []
    for i, color in enumerate(palette):
        glass = i == 5
        materials.append({
            "name": f"Palette{i}",
            "pbrMetallicRoughness": {
                "baseColorFactor": [color[0], color[1], color[2], 1.0],
                "metallicFactor": 0.55 if glass else 0.02,
                "roughnessFactor": 0.18 if glass else 0.62 + 0.1 * (i % 3),
            },
        })
    materials.append({
        "name": "Ground",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.20, 0.22, 0.24, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.92,
        },
    })

    # M5: a green material for trees (index 9). Appended, so the existing materials keep
    # their indices and names, and with them their asset IDs.
    materials.append({
        "name": "Foliage",
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.18, 0.34, 0.14, 1.0],
            "metallicFactor": 0.0,
            "roughnessFactor": 0.85,
        },
    })

    attributes = {"POSITION": positions, "COLOR_0": colors, "TEXCOORD_0": uvs, "NORMAL": normals}
    meshes = []
    for i in range(len(palette) + 1):
        meshes.append({"name": f"Cube{i}", "primitives": [{
            "attributes": attributes, "indices": index_accessor, "material": i,
        }]})

    # M5: two smooth, dense meshes, so the cooker's LODs (meshoptimizer) have something to
    # simplify: a round glass tower and a tree crown, both unit-sized like the cubes.
    def smooth_mesh(name, material, rows):
        verts, idx = rows
        acc_p = b.add([v[0:3] for v in verts], FLOAT, "VEC3", ARRAY_BUFFER, True)
        acc_c = b.add([(1.0, 1.0, 1.0) for _ in verts], FLOAT, "VEC3", ARRAY_BUFFER)
        acc_u = b.add([v[3:5] for v in verts], FLOAT, "VEC2", ARRAY_BUFFER)
        acc_n = b.add([v[5:8] for v in verts], FLOAT, "VEC3", ARRAY_BUFFER)
        acc_i = b.add(idx, UNSIGNED_SHORT, "SCALAR", ELEMENT_ARRAY_BUFFER)
        meshes.append({"name": name, "primitives": [{
            "attributes": {"POSITION": acc_p, "COLOR_0": acc_c, "TEXCOORD_0": acc_u, "NORMAL": acc_n},
            "indices": acc_i, "material": material,
        }]})

    meshes_before = len(meshes)
    smooth_mesh("Tower", 5, cylinder(48, 24))
    smooth_mesh("TreeCrown", len(palette) + 1, uv_sphere(32, 16))

    # One node per mesh, in a row, so importing the kit into a scene shows every piece.
    nodes = []
    for i in range(len(palette)):
        nodes.append({"name": f"Building{i}", "translation": [(i - 3.5) * 3.0, 0.5, 0.0], "mesh": i})
    nodes.append({"name": "Ground", "translation": [0.0, -0.5, 0.0], "scale": [30.0, 1.0, 30.0],
                  "mesh": len(palette)})
    nodes.append({"name": "Tower", "translation": [-6.0, 2.0, 6.0], "scale": [3.0, 4.0, 3.0],
                  "mesh": meshes_before})
    nodes.append({"name": "Tree", "translation": [6.0, 2.0, 6.0], "scale": [2.5, 2.5, 2.5],
                  "mesh": meshes_before + 1})

    return b.finish({
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
    })

def fnv1a64(text):
    """AssetID::FromName: 64-bit FNV-1a of the logical name, as 16 hex digits."""
    h = 14695981039346656037
    for c in text.encode("utf-8"):
        h ^= c
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % (h or 1)


def write_scene_text(path, document):
    """The layout of cooked::FormatSceneText: top-level fields one per line, one node per line."""
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["{"]
    fields = [(k, v) for k, v in document.items() if k != "nodes"]
    for key, value in fields:
        lines.append("  " + json.dumps(key) + ": " + json.dumps(value, separators=(",", ":")) + ",")
    nodes = document.get("nodes", [])
    lines.append('  "nodes": [')
    for i, node in enumerate(nodes):
        lines.append("    " + json.dumps(node, separators=(",", ":")) + ("," if i + 1 < len(nodes) else ""))
    lines.append("  ]")
    lines.append("}")
    path.write_text("\n".join(lines) + "\n")


class Lcg:
    """A small deterministic random generator (the output must not depend on Python's)."""

    def __init__(self, seed):
        self.state = (seed * 2654435761 + 1013904223) & 0xFFFFFFFF

    def next(self):
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.state / 4294967296.0

    def range(self, lo, hi):
        return lo + (hi - lo) * self.next()

    def pick(self, items):
        return items[int(self.next() * len(items)) % len(items)]


KIT = "models/city_kit.gltf"


def kit_mesh(name):
    return fnv1a64("mesh:" + KIT + ":@" + name + ":0")


def kit_material(name):
    return fnv1a64("material:" + KIT + ":@" + name)


def f32(value):
    """A float the way the engine writes one: the nearest float32, printed as a double.

    Scene files saved by the editor then match generated ones byte for byte, so saving a
    world region rewrites only the cells that changed.
    """
    return struct.unpack("<f", struct.pack("<f", value))[0]


def f32_tree(value):
    if isinstance(value, float):
        return f32(value)
    if isinstance(value, list):
        return [f32_tree(v) for v in value]
    if isinstance(value, dict):
        return {k: f32_tree(v) for k, v in value.items()}
    return value


def node(name, parent, position=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), rotation=(0.0, 0.0, 0.0, 1.0),
         components=None, prefab=None):
    # Key order and sorted component properties as SceneSerializer writes them.
    item = {"name": name, "parent": parent,
            "position": [f32(round(v, 3)) for v in position],
            "rotation": [f32(round(v, 6)) for v in rotation],
            "scale": [f32(round(v, 3)) for v in scale],
            "active": True}
    if prefab:
        item["prefab"] = prefab
    item["components"] = [{"type": c["type"], "properties": dict(sorted(f32_tree(c["properties"]).items()))}
                          for c in (components or [])]
    return item


def mesh_component(mesh, material):
    return {"type": "MeshComponent", "properties": {"material": material, "mesh": mesh}}


def static_box():
    # The collider is a unit box in the object's space; the object's scale sizes it.
    return {"type": "PhysicsComponent", "properties": {
        "bodyType": "static", "colliderType": "box", "size": [1.0, 1.0, 1.0],
        "radius": 0.0, "height": 0.0, "mass": 0.0, "friction": 0.8}}


def point_light(color, intensity, light_range):
    return {"type": "LightComponent", "properties": {
        "color": list(color), "innerConeAngle": 0.0, "intensity": intensity,
        "outerConeAngle": 0.785398, "range": light_range, "type": "point"}}


# District prefabs: 128 m squares (origin at the centre) of 4 x 4 blocks on a 32 m street
# grid. Each kind is a different density, so the skyline rises towards the city centre.
DISTRICT_KINDS = {
    # name: (buildings per block side, footprint range, height range, tower chance, trees per block)
    "downtown": (2, (9.0, 11.0), (28.0, 96.0), 0.45, 0),
    "midrise": (2, (9.0, 11.0), (12.0, 34.0), 0.10, 1),
    "residential": (2, (7.0, 9.0), (4.0, 10.0), 0.0, 2),
    "park": (0, (0.0, 0.0), (0.0, 0.0), 0.0, 5),
}


def district_prefab(kind, variant):
    per_side, footprint, heights, tower_chance, trees = DISTRICT_KINDS[kind]
    rng = Lcg(hash_text(kind) * 7 + variant)
    nodes = [node("District", -1)]
    walls = ["Palette%d" % i for i in (0, 1, 2, 3, 4, 6, 7)]
    for bz in range(4):
        for bx in range(4):
            cx = -48.0 + 32.0 * bx
            cz = -48.0 + 32.0 * bz
            if per_side:
                pitch = 24.0 / per_side
                for iz in range(per_side):
                    for ix in range(per_side):
                        x = cx - 12.0 + pitch * (ix + 0.5)
                        z = cz - 12.0 + pitch * (iz + 0.5)
                        w = min(rng.range(*footprint), pitch - 1.0)
                        d = min(rng.range(*footprint), pitch - 1.0)
                        h = round(rng.range(*heights) * (1.0 if rng.next() > 0.2 else 0.6), 1)
                        if rng.next() < tower_chance:
                            nodes.append(node("Tower", 0, (x, h * 0.6, z), (w, h * 1.2, w),
                                              components=[mesh_component(kit_mesh("Tower"), kit_material("Palette5")),
                                                          static_box()]))
                        else:
                            material = rng.pick(walls)
                            index = int(material[-1])
                            nodes.append(node("Building", 0, (x, h * 0.5, z), (w, h, d),
                                              components=[mesh_component(kit_mesh("Cube%d" % index), kit_material(material)),
                                                          static_box()]))
            for t in range(trees):
                x = cx + rng.range(-10.0, 10.0)
                z = cz + rng.range(-10.0, 10.0)
                size = rng.range(4.0, 7.0)
                trunk = len(nodes)
                nodes.append(node("Tree", 0, (x, size * 0.45, z), (0.5, size * 0.9, 0.5),
                                  components=[mesh_component(kit_mesh("Cube7"), kit_material("Palette7"))]))
                # The crown is a child of the trunk: its scale multiplies the trunk's, so
                # this makes a sphere about 0.8 * size across, centred at `size` height.
                nodes.append(node("Crown", trunk, (0.0, 0.62, 0.0), (1.6 * size, 0.889, 1.6 * size),
                                  components=[mesh_component(kit_mesh("TreeCrown"), kit_material("Foliage"))]))
    # Street lights at four inner crossings.
    for lx, lz in ((-16.0, -16.0), (16.0, -16.0), (-16.0, 16.0), (16.0, 16.0)):
        warm = rng.range(0.0, 1.0)
        nodes.append(node("StreetLight", 0, (lx, 7.0, lz),
                          components=[point_light((1.0, 0.85 + 0.1 * warm, 0.6 + 0.2 * warm), 4.0, 30.0)]))
    return {"format": "geni.prefab", "version": 1, "mainCamera": -1, "nodes": nodes}


def hash_text(text):
    return int(fnv1a64(text)[:8], 16)


def metro_world():
    """samples/city/assets/worlds/metro: a 4 km x 4 km world of 16 x 16 cells of 256 m.

    Each cell is a ground tile and four district prefabs (2 x 2), chosen by distance from
    the centre and turned by multiples of 90 degrees. Generated once; from here on the
    world can be edited in the editor one region at a time (docs/design/0007).
    """
    folder = ROOT / "samples/city/assets/worlds/metro"
    prefab_ids = {}
    for kind in DISTRICT_KINDS:
        for variant in range(2):
            name = "%s_%d" % (kind, variant)
            relative = "worlds/metro/prefabs/%s.gprefab" % name
            write_scene_text(folder / "prefabs" / (name + ".gprefab"), district_prefab(kind, variant))
            prefab_ids[name] = fnv1a64("prefab:" + relative)

    cells, cell_size = 16, 256.0
    world = {"format": "geni.world", "version": 1, "cellSize": cell_size,
             "origin": [-cells * cell_size / 2, 0.0, -cells * cell_size / 2],
             "cellsX": cells, "cellsZ": cells, "persistent": "persistent.gscene", "cells": "cells"}
    (folder / "metro.gworld").write_text(json.dumps(world, indent=2) + "\n")

    sun_rotation = (-0.38803282380104065, 0.41029343008995056, 0.19876350462436676, 0.8009886741638184)
    # Roots, not children of one "World" object: origin rebasing moves roots, and a free
    # camera is simplest to place when its local position is its scene position.
    persistent = {"format": "geni.scene", "version": 1, "mainCamera": 1, "nodes": [
        node("Sun", -1, rotation=sun_rotation, components=[{"type": "LightComponent", "properties": {
            "color": [1.0, 0.95, 0.85], "innerConeAngle": 0.0, "intensity": 2.2,
            "outerConeAngle": 0.785398, "range": 10.0, "type": "directional"}}]),
        node("Camera", -1, (0.0, 60.0, 180.0), rotation=(-0.13052619, 0.0, 0.0, 0.99144486), components=[
            {"type": "CameraComponent", "properties": {"far": 2400.0, "fov": 60.0, "near": 0.5}},
            {"type": "FlyCameraComponent", "properties": {"speed": 30.0, "boost": 4.0, "sensitivity": 0.15}}]),
    ]}
    write_scene_text(folder / "persistent.gscene", persistent)

    turns = [(0.0, 0.0, 0.0, 1.0), (0.0, 0.70710678, 0.0, 0.70710678), (0.0, 1.0, 0.0, 0.0),
             (0.0, -0.70710678, 0.0, 0.70710678)]
    for cz in range(cells):
        for cx in range(cells):
            rng = Lcg(cx * 131 + cz * 977 + 5)
            nodes = [node("Ground", -1, (cell_size / 2, -0.5, cell_size / 2), (cell_size, 1.0, cell_size),
                          components=[mesh_component(kit_mesh("Cube8"), kit_material("Ground")), static_box()])]
            for dz in range(2):
                for dx in range(2):
                    wx = -cells * cell_size / 2 + cx * cell_size + 64.0 + 128.0 * dx
                    wz = -cells * cell_size / 2 + cz * cell_size + 64.0 + 128.0 * dz
                    r = math.hypot(wx, wz)
                    if rng.next() < 0.1:
                        kind = "park"
                    elif r < 640.0:
                        kind = "downtown"
                    elif r < 1300.0:
                        kind = "midrise"
                    else:
                        kind = "residential"
                    name = "%s_%d" % (kind, int(rng.next() * 2) % 2)
                    nodes.append(node(name, -1, (64.0 + 128.0 * dx, 0.0, 64.0 + 128.0 * dz),
                                      rotation=rng.pick(turns), prefab=prefab_ids[name]))
            write_scene_text(folder / "cells" / ("cell_%d_%d.gscene" % (cx, cz)),
                             {"format": "geni.scene", "version": 1, "mainCamera": -1, "nodes": nodes})
    print("wrote", folder.relative_to(ROOT), "(%d cells)" % (cells * cells))


def lod_sphere_gltf():
    """A dense sphere (1,024 triangles) for the cooker's LOD tests and the test world."""
    b = GltfBuilder()
    verts, idx = uv_sphere(32, 17)
    positions = b.add([v[0:3] for v in verts], FLOAT, "VEC3", ARRAY_BUFFER, True)
    uvs = b.add([v[3:5] for v in verts], FLOAT, "VEC2", ARRAY_BUFFER)
    normals = b.add([v[5:8] for v in verts], FLOAT, "VEC3", ARRAY_BUFFER)
    indices = b.add(idx, UNSIGNED_SHORT, "SCALAR", ELEMENT_ARRAY_BUFFER)
    return b.finish({
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Sphere", "mesh": 0}],
        "meshes": [{"name": "Sphere", "primitives": [{
            "attributes": {"POSITION": positions, "TEXCOORD_0": uvs, "NORMAL": normals},
            "indices": indices, "material": 0}]}],
        "materials": [{"name": "Stone", "pbrMetallicRoughness": {
            "baseColorFactor": [0.6, 0.58, 0.55, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.8}}],
    })


def tiny_world():
    """tests/assets/worlds/tiny: 4 x 4 cells of 32 m for the streaming tests.

    Every cell has a ground tile, a pillar with a static collider and a light, and one
    instance of a two-sphere prefab, so a loaded cell is 6 objects
    (4 nodes in the file, the prefab adding 2).
    """
    folder = ROOT / "tests/assets/worlds/tiny"
    sphere = fnv1a64("mesh:models/lod_sphere.gltf:@Sphere:0")
    stone = fnv1a64("material:models/lod_sphere.gltf:@Stone")
    write_scene_text(folder / "prefabs" / "pair.gprefab", {"format": "geni.prefab", "version": 1, "mainCamera": -1, "nodes": [
        node("Pair", -1),
        node("Left", 0, (-3.0, 1.0, 0.0), (2.0, 2.0, 2.0), components=[mesh_component(sphere, stone)]),
        node("Right", 0, (3.0, 1.0, 0.0), (2.0, 2.0, 2.0), components=[mesh_component(sphere, stone)]),
    ]})
    pair = fnv1a64("prefab:worlds/tiny/prefabs/pair.gprefab")
    world = {"format": "geni.world", "version": 1, "cellSize": 32.0, "origin": [-64.0, 0.0, -64.0],
             "cellsX": 4, "cellsZ": 4, "persistent": "persistent.gscene", "cells": "cells"}
    (folder / "tiny.gworld").write_text(json.dumps(world, indent=2) + "\n")
    write_scene_text(folder / "persistent.gscene", {"format": "geni.scene", "version": 1, "mainCamera": 1, "nodes": [
        node("Sun", -1, rotation=(-0.38803282380104065, 0.41029343008995056, 0.19876350462436676, 0.8009886741638184),
             components=[{"type": "LightComponent", "properties": {
                 "color": [1.0, 1.0, 1.0], "innerConeAngle": 0.0, "intensity": 2.0,
                 "outerConeAngle": 0.785398, "range": 10.0, "type": "directional"}}]),
        node("Camera", -1, (0.0, 10.0, 0.0), components=[
            {"type": "CameraComponent", "properties": {"far": 500.0, "fov": 60.0, "near": 0.1}}]),
    ]})
    for z in range(4):
        for x in range(4):
            write_scene_text(folder / "cells" / ("cell_%d_%d.gscene" % (x, z)), {
                "format": "geni.scene", "version": 1, "mainCamera": -1, "nodes": [
                    node("Ground", -1, (16.0, -0.5, 16.0), (32.0, 1.0, 32.0),
                         components=[mesh_component(sphere, stone), static_box()]),
                    node("Pillar", -1, (16.0, 4.0, 16.0), (2.0, 8.0, 2.0),
                         components=[mesh_component(sphere, stone), static_box()]),
                    node("Lamp", -1, (10.0, 3.0, 10.0), components=[point_light((1.0, 0.9, 0.7), 2.0, 12.0)]),
                    node("Pair", -1, (16.0, 0.0, 26.0), prefab=pair),
                ]})


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
    write_json(ROOT / "tests/assets/models/textured_quad.gltf", textured_quad_gltf())
    bar = skinned_bar_gltf()
    write_json(ROOT / "tests/assets/models/skinned_bar.gltf", bar)
    write_json(ROOT / "samples/sandbox/assets/models/skinned_bar.gltf", bar)
    write_png(ROOT / "tests/assets/textures/checker.png", 16, 16, checker)
    write_png(ROOT / "samples/sandbox/assets/textures/checker.png", 64, 64, checker)
    write_json(ROOT / "samples/city/assets/models/city_kit.gltf", city_kit_gltf(), compact=True)
    write_json(ROOT / "tests/assets/models/lod_sphere.gltf", lod_sphere_gltf(), compact=True)
    tiny_world()
    metro_world()


if __name__ == "__main__":
    main()
