#!/usr/bin/env python3
# 生成真实外部模型：hut.obj（静态小屋）+ runner.gltf（双骨骼蒙皮+跑步动画）
import json, struct, base64, math, os

OUT = os.path.dirname(os.path.abspath(__file__)) + "/../models"
os.makedirs(OUT, exist_ok=True)

# ---------------- hut.obj ----------------
def box(cx, cy, cz, sx, sy, sz, name):
    x0,y0,z0 = cx-sx/2, cy-sy/2, cz-sz/2
    x1,y1,z1 = cx+sx/2, cy+sy/2, cz+sz/2
    v = [(x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1),
         (x0,y1,z0),(x1,y1,z0),(x1,y1,z1),(x0,y1,z1)]
    faces = [(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)]
    lines = [f"o {name}"]
    for p in v: lines.append(f"v {p[0]:.3f} {p[1]:.3f} {p[2]:.3f}")
    base = len(all_v)
    for f in faces:
        all_v.extend(v)
        lines.append(f"f {base+f[0]+1} {base+f[1]+1} {base+f[2]+1} {base+f[3]+1}")
all_v = []
lines = []
box(0, 0.5, 0, 1.2, 1.0, 1.0, "walls"); lines += all_v[len(lines)] if False else []
# 简化：直接顺序输出
verts, faces, nv = [], [], 0
def emit_box(cx, cy, cz, sx, sy, sz):
    global nv
    x0,y0,z0 = cx-sx/2, cy-sy/2, cz-sz/2
    x1,y1,z1 = cx+sx/2, cy+sy/2, cz+sz/2
    v = [(x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1),
         (x0,y1,z0),(x1,y1,z0),(x1,y1,z1),(x0,y1,z1)]
    for p in v: verts.append(p)
    for f in [(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)]:
        faces.append((nv+f[0]+1, nv+f[1]+1, nv+f[2]+1, nv+f[3]+1))
    nv += 8
emit_box(0, 0.55, 0, 1.4, 1.1, 1.2)          # 屋身
emit_box(0, 1.35, 0, 1.7, 0.55, 1.5)         # 屋顶
emit_box(0, 0.28, 0.62, 0.35, 0.56, 0.08)    # 门
with open(OUT + "/hut.obj", "w") as f:
    f.write("# hut - generated test model for PhysicsEngine3D\n")
    for p in verts: f.write(f"v {p[0]:.3f} {p[1]:.3f} {p[2]:.3f}\n")
    for a,b,c,d in faces: f.write(f"f {a} {b} {c} {d}\n")

# ---------------- runner.gltf（骨骼蒙皮 + run 动画） ----------------
# 小人：躯干/头/双臂/双腿 合并 mesh，下半身权重给 joint0(hip)，上半身给 joint1(spine)
def box_geo(cx, cy, cz, sx, sy, sz):
    x0,y0,z0 = cx-sx/2, cy-sy/2, cz-sz/2
    x1,y1,z1 = cx+sx/2, cy+sy/2, cz+sz/2
    v = [(x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1),
         (x0,y1,z0),(x1,y1,z0),(x1,y1,z1),(x0,y1,z1)]
    idx = []
    for f in [(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)]:
        a,b,c,d = f
        idx += [a, b, c, a, c, d]
    return v, idx

pos, idx, wv = [], [], 0
def joint_for(y):
    return 1 if y > 1.0 else 0
def add_box(*a):
    global wv
    v, ii = box_geo(*a)
    for p in v: pos.append(p)
    for i in ii: idx.append(i + wv)
    wv += len(v)

add_box(0, 0.50, 0, 0.42, 1.0, 0.26)   # 骨盆+腿（下半）
add_box(0, 1.28, 0, 0.5, 0.56, 0.3)    # 躯干
add_box(0, 1.72, 0, 0.24, 0.22, 0.24)  # 头
add_box(-0.34, 1.2, 0, 0.13, 0.7, 0.15)# 左臂
add_box( 0.34, 1.2, 0, 0.13, 0.7, 0.15)# 右臂

joints = []
weights = []
for (x, y, z) in pos:
    # 平滑权重：腰部过渡
    t = max(0.0, min(1.0, (y - 0.85) / 0.3))
    joints += [0, 1, 0, 0]
    weights += [round(1.0 - t, 3), round(t, 3), 0.0, 0.0]

# 法线（简单中心差分近似：直接用归一化位置当颜色即可，这里补上向外的法线 = 位置归一）
normals = []
for (x, y, z) in pos:
    l = math.sqrt(x*x + y*y + z*z) or 1.0
    normals.append([x/l, y/l, z/l])

def pack(fmt, data):
    return base64.b64encode(struct.pack(fmt, *data)).decode()

f32 = "<" + " ".join(["f"] * 0)  # placeholder
def packf(flat):
    return base64.b64encode(struct.pack("<%df" % len(flat), *flat)).decode()

pos_flat = [c for p in pos for c in p]
nrm_flat = [c for n in normals for c in n]
idx_flat = idx
jnt_flat = joints
wgt_flat = weights
# inverse bind matrices: hip identity 平移 -y? 骨骼都在原点，取单位阵即可（绑定姿态为 T-pose 站立）
inv = [
    1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,
    1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,
]
ibm_flat = inv

# 动画：run —— hip 绕 X 摆动 ±28°，spine 反向 ±12°，1.2s 循环
DUR = 1.2
def quat_x(deg):
    r = math.radians(deg) / 2
    return [0.0, math.sin(r), 0.0, math.cos(r)]
times = [0.0, 0.3, 0.6, 0.9, 1.2]
hip_out, spine_out = [], []
for i, t in enumerate(times):
    ang = 28.0 * math.sin(2 * math.pi * t / DUR)
    hip_out += quat_x(ang)
    spine_out += quat_x(-ang * 0.45)
anim_hip_b64 = packf(hip_out)
anim_spine_b64 = packf(spine_out)
times_b64 = packf(times)

blob = struct.pack("<%df" % len(pos_flat), *pos_flat) \
     + struct.pack("<%df" % len(nrm_flat), *nrm_flat) \
     + struct.pack("<%dH" % len(idx_flat), *idx_flat) \
     + struct.pack("<%dB" % len(jnt_flat), *jnt_flat) \
     + struct.pack("<%df" % len(wgt_flat), *wgt_flat) \
     + struct.pack("<16f", *inv[:16]) + struct.pack("<16f", *inv[16:]) \
     + struct.pack("<%df" % len(times), *times) \
     + struct.pack("<%df" % len(hip_out), *hip_out) \
     + struct.pack("<%df" % len(spine_out), *spine_out)
b64 = base64.b64encode(blob).decode()

gltf = {
  "asset": {"version": "2.0", "generator": "PhysicsEngine3D test rig"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [
    {"name": "MannequinRoot", "mesh": 0, "skin": 0},
    {"name": "hip", "children": [2]},
    {"name": "spine"}
  ],
  "skins": [{
    "joints": [1, 2],
    "skeleton": 1,
    "inverseBindMatrices": 7
  }],
  "meshes": [{
    "name": "runner",
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 3, "WEIGHTS_0": 4},
      "indices": 2, "material": 0
    }]
  }],
  "materials": [{"name": "runnerMat", "pbrMetallicRoughness": {"baseColorFactor": [0.95, 0.62, 0.25, 1.0], "metallicFactor": 0.1, "roughnessFactor": 0.7}}],
  "animations": [{
    "name": "run",
    "channels": [
      {"target": {"node": 1, "path": "rotation"}, "sampler": 0},
      {"target": {"node": 2, "path": "rotation"}, "sampler": 1}
    ],
    "samplers": [
      {"input": 5, "output": 6, "interpolation": "LINEAR"},
      {"input": 5, "output": 8, "interpolation": "LINEAR"}
    ]
  }],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "max": [0.6, 1.85, 0.4], "min": [-0.6, 0.0, -0.4]},
    {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
    {"bufferView": 2, "componentType": 5123, "count": len(idx_flat), "type": "SCALAR"},
    {"bufferView": 3, "componentType": 5121, "count": len(pos), "type": "VEC4"},
    {"bufferView": 4, "componentType": 5126, "count": len(pos), "type": "VEC4"},
    {"bufferView": 5, "componentType": 5126, "count": len(times), "type": "SCALAR"},
    {"bufferView": 6, "componentType": 5126, "count": len(times), "type": "VEC4"},
    {"bufferView": 7, "componentType": 5126, "count": 2, "type": "MAT4"},
    {"bufferView": 8, "componentType": 5126, "count": len(times), "type": "VEC4"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": len(pos_flat) * 4},
    {"buffer": 0, "byteOffset": len(pos_flat) * 4, "byteLength": len(nrm_flat) * 4},
    {"buffer": 0, "byteOffset": len(pos_flat) * 4 + len(nrm_flat) * 4, "byteLength": len(idx_flat) * 2},
    {"buffer": 0, "byteOffset": len(pos_flat) * 4 + len(nrm_flat) * 4 + len(idx_flat) * 2, "byteLength": len(jnt_flat)},
    {"buffer": 0, "byteOffset": len(pos_flat) * 4 + len(nrm_flat) * 4 + len(idx_flat) * 2 + len(jnt_flat), "byteLength": len(wgt_flat) * 4},
    {"buffer": 0, "byteOffset": len(pos_flat) * 4 + len(nrm_flat) * 4 + len(idx_flat) * 2 + len(jnt_flat) + len(wgt_flat) * 4, "byteLength": len(times) * 4},
    {"buffer": 0, "byteOffset": len(pos_flat) * 4 + len(nrm_flat) * 4 + len(idx_flat) * 2 + len(jnt_flat) + len(wgt_flat) * 4 + len(times) * 4, "byteLength": len(hip_out) * 4},
    {"buffer": 0, "byteOffset": len(pos_flat) * 4 + len(nrm_flat) * 4 + len(idx_flat) * 2 + len(jnt_flat) + len(wgt_flat) * 4 + len(times) * 4 + len(hip_out) * 4, "byteLength": 128},
    {"buffer": 0, "byteOffset": len(pos_flat) * 4 + len(nrm_flat) * 4 + len(idx_flat) * 2 + len(jnt_flat) + len(wgt_flat) * 4 + len(times) * 4 + len(hip_out) * 4 + 128, "byteLength": len(spine_out) * 4}
  ],
  "buffers": [{"byteLength": len(blob), "uri": f"data:application/octet-stream;base64,{b64}"}]
}
with open(OUT + "/runner.gltf", "w") as f:
    json.dump(gltf, f)
print("models written:", os.listdir(OUT), "verts:", len(pos))
