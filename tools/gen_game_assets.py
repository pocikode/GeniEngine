#!/usr/bin/env python3
"""Generates the reference game's characters, cars and sounds (ROADMAP M6).

    python3 tools/gen_game_assets.py      (gen_assets.py runs it too)

Writes into samples/city/assets/:
  models/player.gltf, models/pedestrian_{a,b,c}.gltf
      A skinned mannequin (14 joints, rigid box parts) with in-place clips Idle, Walk,
      Run and Jump. Faces -Z like every engine object; feet at the origin.
  models/car_{red,blue,white,yellow}.gltf
      A car: Body and Cabin meshes and four wheels named Wheel0..Wheel3 (front-left,
      front-right, rear-left, rear-right) with their axle along X, as VehicleComponent
      expects. The origin is the chassis centre, 0.73 m above the ground at rest.
  sounds/*.wav
      Engine loop, footstep, door, horn, menu blips, city ambience and a music loop:
      synthesised, mono 16-bit 22.05 kHz.

Standard library only and deterministic, like gen_assets.py.
"""

import math
import struct
from pathlib import Path

from gen_assets import ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER, FLOAT, ROOT, UNSIGNED_BYTE, UNSIGNED_SHORT, GltfBuilder, \
    Lcg, cylinder, write_json

CITY = ROOT / "samples/city/assets"

# ---------------------------------------------------------------------------------------
# The mannequin

# name, parent, translation relative to the parent (bind pose has no rotations)
JOINTS = [
    ("Hips", None, (0.0, 0.95, 0.0)),
    ("Spine", "Hips", (0.0, 0.10, 0.0)),
    ("Chest", "Spine", (0.0, 0.22, 0.0)),
    ("Head", "Chest", (0.0, 0.30, 0.0)),
    ("UpperArmL", "Chest", (-0.24, 0.24, 0.0)),
    ("LowerArmL", "UpperArmL", (0.0, -0.29, 0.0)),
    ("UpperArmR", "Chest", (0.24, 0.24, 0.0)),
    ("LowerArmR", "UpperArmR", (0.0, -0.29, 0.0)),
    ("UpperLegL", "Hips", (-0.10, -0.04, 0.0)),
    ("LowerLegL", "UpperLegL", (0.0, -0.43, 0.0)),
    ("FootL", "LowerLegL", (0.0, -0.42, 0.0)),
    ("UpperLegR", "Hips", (0.10, -0.04, 0.0)),
    ("LowerLegR", "UpperLegR", (0.0, -0.43, 0.0)),
    ("FootR", "LowerLegR", (0.0, -0.42, 0.0)),
]

# joint, centre (joint space), size, material slot (0 skin, 1 top, 2 bottom)
PARTS = [
    ("Hips", (0.0, -0.02, 0.0), (0.34, 0.18, 0.21), 2),
    ("Spine", (0.0, 0.11, 0.0), (0.31, 0.24, 0.19), 1),
    ("Chest", (0.0, 0.13, 0.0), (0.42, 0.30, 0.23), 1),
    ("Chest", (0.0, 0.31, 0.0), (0.10, 0.08, 0.10), 0),  # neck
    ("Head", (0.0, 0.12, -0.01), (0.21, 0.25, 0.23), 0),
    ("Head", (0.0, 0.21, 0.02), (0.22, 0.08, 0.22), 2),  # hair
    ("UpperArmL", (0.0, -0.14, 0.0), (0.10, 0.30, 0.11), 1),
    ("LowerArmL", (0.0, -0.13, 0.0), (0.085, 0.27, 0.09), 0),
    ("UpperArmR", (0.0, -0.14, 0.0), (0.10, 0.30, 0.11), 1),
    ("LowerArmR", (0.0, -0.13, 0.0), (0.085, 0.27, 0.09), 0),
    ("UpperLegL", (0.0, -0.21, 0.0), (0.15, 0.44, 0.15), 2),
    ("LowerLegL", (0.0, -0.21, 0.0), (0.12, 0.42, 0.13), 2),
    ("FootL", (0.0, 0.0, -0.05), (0.11, 0.08, 0.26), 2),
    ("UpperLegR", (0.0, -0.21, 0.0), (0.15, 0.44, 0.15), 2),
    ("LowerLegR", (0.0, -0.21, 0.0), (0.12, 0.42, 0.13), 2),
    ("FootR", (0.0, 0.0, -0.05), (0.11, 0.08, 0.26), 2),
]

BOX_FACES = [
    # normal, four corners (unit cube, counter-clockwise seen from outside)
    ((0, 0, 1), [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]),
    ((0, 0, -1), [(1, -1, -1), (-1, -1, -1), (-1, 1, -1), (1, 1, -1)]),
    ((1, 0, 0), [(1, -1, 1), (1, -1, -1), (1, 1, -1), (1, 1, 1)]),
    ((-1, 0, 0), [(-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1)]),
    ((0, 1, 0), [(-1, 1, 1), (1, 1, 1), (1, 1, -1), (-1, 1, -1)]),
    ((0, -1, 0), [(-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)]),
]


def joint_world_positions():
    world = {}
    for name, parent, t in JOINTS:
        base = world[parent] if parent else (0.0, 0.0, 0.0)
        world[name] = (base[0] + t[0], base[1] + t[1], base[2] + t[2])
    return world


def quat_axis(axis, degrees):
    half = math.radians(degrees) / 2.0
    s = math.sin(half)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def euler(x=0.0, y=0.0, z=0.0):
    """Rotation about X, then Y, then Z (degrees), as one quaternion."""
    q = quat_axis((1, 0, 0), x)
    q = quat_mul(quat_axis((0, 1, 0), y), q)
    return quat_mul(quat_axis((0, 0, 1), z), q)


def clip_keys(duration, samples, pose_at):
    """Samples pose_at(t) -> {joint: (rotation quat or None, translation or None)}."""
    times = [duration * i / samples for i in range(samples + 1)]
    return times, [pose_at(t / duration) for t in times]


def walk_pose(phase, stride, knee, arm, elbow, bob, lean, twist):
    s = math.sin(2.0 * math.pi * phase)
    c = math.cos(2.0 * math.pi * phase)
    # Positive rotation about X swings a limb forward (towards -Z).
    knee_l = -(6.0 + knee * max(0.0, c))
    knee_r = -(6.0 + knee * max(0.0, -c))
    hips_y = 0.95 - bob + bob * math.cos(4.0 * math.pi * phase)
    return {
        "Hips": (euler(y=twist * s), (0.0, hips_y, 0.0)),
        "Spine": (euler(x=-lean, y=-twist * s * 1.5), None),
        "UpperLegL": (euler(x=stride * s), None),
        "UpperLegR": (euler(x=-stride * s), None),
        "LowerLegL": (euler(x=knee_l), None),
        "LowerLegR": (euler(x=knee_r), None),
        "FootL": (euler(x=-0.5 * stride * s if s < 0 else 0.0), None),
        "FootR": (euler(x=0.5 * stride * s if s > 0 else 0.0), None),
        "UpperArmL": (euler(x=-arm * s, z=-5.0), None),
        "UpperArmR": (euler(x=arm * s, z=5.0), None),
        "LowerArmL": (euler(x=elbow), None),
        "LowerArmR": (euler(x=elbow), None),
    }


def idle_pose(phase):
    breath = math.sin(2.0 * math.pi * phase)
    return {
        "Hips": (euler(), (0.0, 0.95 - 0.004 * breath, 0.0)),
        "Spine": (euler(x=1.0 * breath), None),
        "Chest": (euler(x=-1.5 * breath), None),
        "Head": (euler(x=1.0 * breath, y=4.0 * math.sin(2.0 * math.pi * phase + 1.0)), None),
        "UpperArmL": (euler(x=2.0 * breath, z=-7.0), None),
        "UpperArmR": (euler(x=2.0 * breath, z=7.0), None),
        "LowerArmL": (euler(x=8.0), None),
        "LowerArmR": (euler(x=8.0), None),
        "UpperLegL": (euler(z=-2.0), None),
        "UpperLegR": (euler(z=2.0), None),
        "LowerLegL": (euler(x=-3.0), None),
        "LowerLegR": (euler(x=-3.0), None),
    }


def jump_pose(phase):
    # Tuck quickly, then hold: the clip does not loop.
    k = min(1.0, phase / 0.3)
    k = k * k * (3.0 - 2.0 * k)
    return {
        "Hips": (euler(), (0.0, 0.95, 0.0)),
        "Spine": (euler(x=-8.0 * k), None),
        "UpperLegL": (euler(x=55.0 * k), None),
        "UpperLegR": (euler(x=20.0 * k), None),
        "LowerLegL": (euler(x=-80.0 * k), None),
        "LowerLegR": (euler(x=-45.0 * k), None),
        "UpperArmL": (euler(x=-30.0 * k, z=-35.0 * k), None),
        "UpperArmR": (euler(x=40.0 * k, z=35.0 * k), None),
        "LowerArmL": (euler(x=30.0 * k), None),
        "LowerArmR": (euler(x=50.0 * k), None),
    }


CLIPS = [
    ("Idle", 2.4, 12, idle_pose),
    ("Walk", 1.05, 16, lambda p: walk_pose(p, 27.0, 38.0, 20.0, 14.0, 0.02, 2.0, 4.0)),
    ("Run", 0.70, 16, lambda p: walk_pose(p, 46.0, 85.0, 42.0, 75.0, 0.045, 11.0, 7.0)),
    ("Jump", 0.8, 8, jump_pose),
]


def mannequin_gltf(colors):
    """colors: (skin, top, bottom) RGB."""
    b = GltfBuilder()
    world = joint_world_positions()
    joint_index = {name: i for i, (name, _, _) in enumerate(JOINTS)}

    meshes = []
    for slot in range(3):
        positions, normals, uvs, joints, weights, indices = [], [], [], [], [], []
        for joint, centre, size, part_slot in PARTS:
            if part_slot != slot:
                continue
            origin = world[joint]
            for normal, corners in BOX_FACES:
                base = len(positions)
                for i, (cx, cy, cz) in enumerate(corners):
                    positions.append((origin[0] + centre[0] + 0.5 * size[0] * cx,
                                      origin[1] + centre[1] + 0.5 * size[1] * cy,
                                      origin[2] + centre[2] + 0.5 * size[2] * cz))
                    normals.append(normal)
                    uvs.append(((0, 0), (1, 0), (1, 1), (0, 1))[i])
                    joints.append((joint_index[joint], 0, 0, 0))
                    weights.append((1.0, 0.0, 0.0, 0.0))
                indices += [base, base + 1, base + 2, base, base + 2, base + 3]
        attributes = {
            "POSITION": b.add(positions, FLOAT, "VEC3", ARRAY_BUFFER, True),
            "NORMAL": b.add(normals, FLOAT, "VEC3", ARRAY_BUFFER),
            "TEXCOORD_0": b.add(uvs, FLOAT, "VEC2", ARRAY_BUFFER),
            "JOINTS_0": b.add(joints, UNSIGNED_BYTE, "VEC4", ARRAY_BUFFER),
            "WEIGHTS_0": b.add(weights, FLOAT, "VEC4", ARRAY_BUFFER),
        }
        meshes.append({"name": ("Skin", "Top", "Bottom")[slot], "primitives": [{
            "attributes": attributes, "indices": b.add(indices, UNSIGNED_SHORT, "SCALAR", ELEMENT_ARRAY_BUFFER),
            "material": slot}]})

    # Nodes: 0 Armature, 1..14 joints, then the three skinned mesh nodes.
    nodes = [{"name": "Armature", "children": [1]}]
    for name, parent, t in JOINTS:
        nodes.append({"name": name, "translation": list(t)})
    for i, (name, parent, _) in enumerate(JOINTS):
        if parent:
            nodes[1 + joint_index[parent]].setdefault("children", []).append(1 + i)
    mesh_nodes = []
    for slot in range(3):
        mesh_nodes.append(len(nodes))
        nodes.append({"name": meshes[slot]["name"] + "Mesh", "mesh": slot, "skin": 0})

    inverse_bind = []
    for name, _, _ in JOINTS:
        p = world[name]
        inverse_bind.append((1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, -p[0], -p[1], -p[2], 1.0))
    skin = {"name": "Mannequin", "joints": [1 + i for i in range(len(JOINTS))], "skeleton": 1,
            "inverseBindMatrices": b.add(inverse_bind, FLOAT, "MAT4")}

    animations = []
    for clip_name, duration, samples, pose_at in CLIPS:
        times, poses = clip_keys(duration, samples, pose_at)
        time_accessor = b.add(times, FLOAT, "SCALAR", None, True)
        samplers, channels = [], []
        for name, _, rest in JOINTS:
            rotations = [pose.get(name, (None, None))[0] for pose in poses]
            translations = [pose.get(name, (None, None))[1] for pose in poses]
            if any(r is not None for r in rotations):
                values = [r if r is not None else (0.0, 0.0, 0.0, 1.0) for r in rotations]
                samplers.append({"input": time_accessor, "output": b.add(values, FLOAT, "VEC4"),
                                 "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1,
                                 "target": {"node": 1 + joint_index[name], "path": "rotation"}})
            if any(t is not None for t in translations):
                values = [t if t is not None else rest for t in translations]
                samplers.append({"input": time_accessor, "output": b.add(values, FLOAT, "VEC3"),
                                 "interpolation": "LINEAR"})
                channels.append({"sampler": len(samplers) - 1,
                                 "target": {"node": 1 + joint_index[name], "path": "translation"}})
        animations.append({"name": clip_name, "samplers": samplers, "channels": channels})

    materials = []
    for slot, name in enumerate(("Skin", "Top", "Bottom")):
        c = colors[slot]
        materials.append({"name": name, "pbrMetallicRoughness": {
            "baseColorFactor": [c[0], c[1], c[2], 1.0], "metallicFactor": 0.0,
            "roughnessFactor": 0.75 if slot else 0.55}})

    return b.finish({
        "scene": 0,
        "scenes": [{"nodes": [0] + mesh_nodes}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
        "skins": [skin],
        "animations": animations,
    })


# ---------------------------------------------------------------------------------------
# Cars

def box_rows(centre, size):
    rows, idx = [], []
    for normal, corners in BOX_FACES:
        base = len(rows)
        for i, (cx, cy, cz) in enumerate(corners):
            rows.append((centre[0] + 0.5 * size[0] * cx, centre[1] + 0.5 * size[1] * cy,
                         centre[2] + 0.5 * size[2] * cz) + ((0, 0), (1, 0), (1, 1), (0, 1))[i] + normal)
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return rows, idx


def merge(*parts):
    rows, idx = [], []
    for part_rows, part_idx in parts:
        base = len(rows)
        rows += part_rows
        idx += [base + i for i in part_idx]
    return rows, idx


def wheel_rows():
    """A unit wheel: radius 0.5, width 1, axle along X (the cylinder turned on its side)."""
    verts, idx = cylinder(20, 1)
    rows = []
    for x, y, z, u, v, nx, ny, nz in verts:
        rows.append((y, x, z, u, v, ny, nx, nz))  # swap X and Y: the axle becomes X
    # Swapping two axes mirrors the mesh; flip the winding back.
    flipped = []
    for i in range(0, len(idx), 3):
        flipped += [idx[i], idx[i + 2], idx[i + 1]]
    return rows, flipped


# Wheel centres at rest, relative to the chassis centre (see VehicleSettings defaults).
WHEELS = [(-0.85, -0.37, -1.35), (0.85, -0.37, -1.35), (-0.85, -0.37, 1.35), (0.85, -0.37, 1.35)]
WHEEL_RADIUS = 0.36
WHEEL_WIDTH = 0.25


def car_gltf(paint):
    b = GltfBuilder()
    meshes = []

    def add_mesh(name, rows_idx, material):
        rows, idx = rows_idx
        meshes.append({"name": name, "primitives": [{
            "attributes": {
                "POSITION": b.add([r[0:3] for r in rows], FLOAT, "VEC3", ARRAY_BUFFER, True),
                "TEXCOORD_0": b.add([r[3:5] for r in rows], FLOAT, "VEC2", ARRAY_BUFFER),
                "NORMAL": b.add([r[5:8] for r in rows], FLOAT, "VEC3", ARRAY_BUFFER),
            },
            "indices": b.add(idx, UNSIGNED_SHORT, "SCALAR", ELEMENT_ARRAY_BUFFER),
            "material": material}]})
        return len(meshes) - 1

    # Front is -Z. Lower body with bumpers, a cabin with windows, lights.
    body = add_mesh("Body", merge(
        box_rows((0.0, -0.06, 0.0), (1.80, 0.58, 4.20)),
        box_rows((0.0, 0.26, -1.45), (1.72, 0.10, 1.25)),   # bonnet
        box_rows((0.0, 0.26, 1.62), (1.72, 0.10, 0.85)),    # boot
    ), 0)
    cabin = add_mesh("Cabin", merge(
        box_rows((0.0, 0.52, 0.15), (1.56, 0.44, 2.05)),
    ), 1)
    roof = add_mesh("Roof", merge(box_rows((0.0, 0.76, 0.18), (1.48, 0.06, 1.85))), 0)
    trim = add_mesh("Trim", merge(
        box_rows((0.0, -0.30, -2.12), (1.84, 0.16, 0.10)),  # front bumper
        box_rows((0.0, -0.30, 2.12), (1.84, 0.16, 0.10)),   # rear bumper
        box_rows((0.0, -0.34, 0.0), (1.70, 0.08, 3.9)),     # sills
    ), 2)
    head = add_mesh("Headlights", merge(
        box_rows((-0.62, 0.05, -2.11), (0.34, 0.14, 0.04)),
        box_rows((0.62, 0.05, -2.11), (0.34, 0.14, 0.04)),
    ), 3)
    tail = add_mesh("Taillights", merge(
        box_rows((-0.66, 0.08, 2.11), (0.30, 0.12, 0.04)),
        box_rows((0.66, 0.08, 2.11), (0.30, 0.12, 0.04)),
    ), 4)
    wheel = add_mesh("Wheel", wheel_rows(), 2)
    hub = add_mesh("Hub", merge(box_rows((0.0, 0.0, 0.0), (1.04, 0.5, 0.5))), 5)

    nodes = [{"name": "Body", "mesh": body}, {"name": "Cabin", "mesh": cabin}, {"name": "Roof", "mesh": roof},
             {"name": "Trim", "mesh": trim}, {"name": "Headlights", "mesh": head},
             {"name": "Taillights", "mesh": tail}]
    for i, (x, y, z) in enumerate(WHEELS):
        nodes.append({"name": "Wheel%d" % i, "translation": [x, y, z],
                      "scale": [WHEEL_WIDTH, 2.0 * WHEEL_RADIUS, 2.0 * WHEEL_RADIUS],
                      "mesh": wheel, "children": [len(nodes) + 1]})
        nodes.append({"name": "Hub%d" % i, "mesh": hub})

    # The wheels' hubs are children of the wheels; the wheels sit at the top level next to
    # the body so the cooked scene's root has them as direct children.
    materials = [
        {"name": "Paint", "pbrMetallicRoughness": {"baseColorFactor": list(paint) + [1.0],
                                                    "metallicFactor": 0.45, "roughnessFactor": 0.32}},
        {"name": "Glass", "pbrMetallicRoughness": {"baseColorFactor": [0.08, 0.10, 0.13, 1.0],
                                                    "metallicFactor": 0.6, "roughnessFactor": 0.12}},
        {"name": "Rubber", "pbrMetallicRoughness": {"baseColorFactor": [0.05, 0.05, 0.055, 1.0],
                                                     "metallicFactor": 0.0, "roughnessFactor": 0.9}},
        {"name": "Headlight", "emissiveFactor": [1.0, 0.95, 0.8],
         "pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 0.95, 1.0], "metallicFactor": 0.0,
                                  "roughnessFactor": 0.3}},
        {"name": "Taillight", "emissiveFactor": [0.7, 0.04, 0.03],
         "pbrMetallicRoughness": {"baseColorFactor": [0.6, 0.05, 0.04, 1.0], "metallicFactor": 0.0,
                                  "roughnessFactor": 0.3}},
        {"name": "Hub", "pbrMetallicRoughness": {"baseColorFactor": [0.62, 0.63, 0.66, 1.0],
                                                  "metallicFactor": 0.8, "roughnessFactor": 0.35}},
    ]
    top = [i for i, n in enumerate(nodes) if not n["name"].startswith("Hub")]
    return b.finish({"scene": 0, "scenes": [{"nodes": top}], "nodes": nodes, "meshes": meshes,
                     "materials": materials})


# ---------------------------------------------------------------------------------------
# Sounds

RATE = 22050


def write_wav(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = b"".join(struct.pack("<h", max(-32767, min(32767, int(s * 32767.0)))) for s in samples)
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack(
        "<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16) + b"data" + struct.pack("<I", len(data))
    path.write_bytes(header + data)
    print("wrote", path.relative_to(ROOT))


def noise(seed):
    rng = Lcg(seed)
    while True:
        yield rng.next() * 2.0 - 1.0


def lowpass(samples, alpha):
    out, y = [], 0.0
    for s in samples:
        y += alpha * (s - y)
        out.append(y)
    return out


def engine_loop():
    # 55 Hz fundamental and harmonics; a whole number of cycles in one second loops cleanly.
    n = RATE
    out = []
    for i in range(n):
        t = i / RATE
        f = 55.0
        v = (0.5 * math.sin(2 * math.pi * f * t) + 0.3 * math.sin(2 * math.pi * 2 * f * t + 0.4) +
             0.22 * math.sin(2 * math.pi * 3 * f * t + 1.1) + 0.12 * math.sin(2 * math.pi * 4 * f * t + 2.0) +
             0.08 * math.sin(2 * math.pi * 6 * f * t))
        # Firing pulses: four per engine cycle.
        v *= 0.75 + 0.25 * math.sin(2 * math.pi * 4 * f * t) ** 2
        out.append(0.55 * v)
    return out


def footstep():
    n = int(0.11 * RATE)
    src = noise(11)
    raw = [next(src) * math.exp(-i / (0.018 * RATE)) for i in range(n)]
    thump = [0.6 * math.sin(2 * math.pi * 90 * i / RATE) * math.exp(-i / (0.02 * RATE)) for i in range(n)]
    return [0.55 * (a + b) for a, b in zip(lowpass(raw, 0.25), thump)]


def door():
    n = int(0.22 * RATE)
    src = noise(23)
    out = []
    for i in range(n):
        t = i / RATE
        out.append(0.8 * math.sin(2 * math.pi * 70 * t) * math.exp(-t / 0.05) + 0.25 * next(src) * math.exp(-t / 0.012))
    return lowpass(out, 0.35)


def horn():
    n = int(0.55 * RATE)
    out = []
    for i in range(n):
        t = i / RATE
        env = min(1.0, t / 0.02) * min(1.0, (0.55 - t) / 0.05)
        v = sum(math.sin(2 * math.pi * f * k * t) / k for f in (415.0, 494.0) for k in (1, 2, 3))
        out.append(0.18 * env * v)
    return out


def blip(f0, f1, seconds):
    n = int(seconds * RATE)
    out, phase = [], 0.0
    for i in range(n):
        f = f0 + (f1 - f0) * i / n
        phase += 2 * math.pi * f / RATE
        env = math.sin(math.pi * i / n)
        out.append(0.35 * env * math.sin(phase))
    return out


def ambience():
    # Four seconds of low rumble; the end is crossfaded into the start so it loops.
    n = 4 * RATE
    fade = RATE // 2
    src = noise(37)
    raw = lowpass(lowpass([next(src) for _ in range(n + fade)], 0.03), 0.05)
    out = raw[:n]
    for i in range(fade):
        w = i / fade
        out[i] = out[i] * w + raw[n + i] * (1.0 - w)
    peak = max(abs(s) for s in out) or 1.0
    return [0.5 * s / peak for s in out]


def music():
    """Sixteen seconds: Am F C G (two bars each at 120 bpm), pads, bass and soft hats."""
    bpm = 120.0
    beat = 60.0 / bpm
    chords = [(57, 60, 64), (53, 57, 60), (48, 52, 55), (55, 59, 62)]  # MIDI notes
    n = 16 * RATE
    out = [0.0] * n
    hz = lambda m: 440.0 * 2 ** ((m - 69) / 12.0)
    chord_len = 4.0  # seconds
    for ci, chord in enumerate(chords):
        start = int(ci * chord_len * RATE)
        length = int(chord_len * RATE)
        for i in range(length):
            t = i / RATE
            env = min(1.0, t / 0.6) * min(1.0, (chord_len - t) / 0.6)
            v = 0.0
            for note in chord:
                f = hz(note)
                v += math.sin(2 * math.pi * f * t) + 0.3 * math.sin(2 * math.pi * 2 * f * t + 0.5)
                v += 0.5 * math.sin(2 * math.pi * f * 1.003 * t)  # slight detune: chorus
            out[start + i] += 0.045 * env * v
        # Bass: root on every beat, an octave below.
        root = hz(chord[0] - 12)
        for b_ in range(8):
            s0 = start + int(b_ * beat * RATE)
            blen = int(beat * RATE * 0.9)
            for i in range(blen):
                t = i / RATE
                env = min(1.0, t / 0.01) * math.exp(-t / 0.35)
                tri = 2.0 / math.pi * math.asin(math.sin(2 * math.pi * root * t))
                out[s0 + i] += 0.22 * env * tri
    # Hats on the off-beats.
    src = noise(53)
    hat = [next(src) for _ in range(int(0.05 * RATE))]
    hat = [s - h for s, h in zip(hat, lowpass(hat, 0.3))]
    for k in range(int(16 / (beat / 2))):
        if k % 2 == 1:
            s0 = int(k * beat / 2 * RATE)
            for i, s in enumerate(hat):
                if s0 + i < n:
                    out[s0 + i] += 0.08 * s * math.exp(-i / (0.012 * RATE))
    peak = max(abs(s) for s in out)
    return [0.7 * s / peak for s in out]


def main():
    models = CITY / "models"
    write_json(models / "player.gltf", mannequin_gltf(((0.86, 0.66, 0.52), (0.95, 0.45, 0.10), (0.16, 0.20, 0.30))),
               compact=True)
    for name, colors in (("pedestrian_a", ((0.80, 0.60, 0.48), (0.20, 0.42, 0.65), (0.25, 0.25, 0.27))),
                         ("pedestrian_b", ((0.55, 0.38, 0.28), (0.75, 0.75, 0.72), (0.30, 0.22, 0.16))),
                         ("pedestrian_c", ((0.92, 0.75, 0.62), (0.55, 0.18, 0.22), (0.12, 0.14, 0.18)))):
        write_json(models / (name + ".gltf"), mannequin_gltf(colors), compact=True)
    for name, paint in (("car_red", (0.62, 0.05, 0.04)), ("car_blue", (0.06, 0.18, 0.48)),
                        ("car_white", (0.80, 0.80, 0.78)), ("car_yellow", (0.85, 0.62, 0.06))):
        write_json(models / (name + ".gltf"), car_gltf(paint), compact=True)

    sounds = CITY / "sounds"
    write_wav(sounds / "engine_loop.wav", engine_loop())
    write_wav(sounds / "footstep.wav", footstep())
    write_wav(sounds / "door.wav", door())
    write_wav(sounds / "horn.wav", horn())
    write_wav(sounds / "ui_move.wav", blip(700.0, 900.0, 0.05))
    write_wav(sounds / "ui_select.wav", blip(600.0, 1200.0, 0.12))
    write_wav(sounds / "ambience.wav", ambience())
    write_wav(sounds / "music.wav", music())


if __name__ == "__main__":
    main()
