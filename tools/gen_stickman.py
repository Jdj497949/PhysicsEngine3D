#!/usr/bin/env python3
# 生成 stickman.gltf：6 骨骼火柴人（hip→spine→双臂；hip→双腿），供动捕模拟器逐关节驱动
import json, struct, base64, math, os

OUT = os.path.dirname(os.path.abspath(__file__)) + "/../models"
os.makedirs(OUT, exist_ok=True)

def box_geo(cx, cy, cz, sx, sy, sz):
    x0, y0, z0 = cx - sx/2, cy - sy/2, cz - sz/2
    x1, y1, z1 = cx + sx/2, cy + sy/2, cz + sz/2
    v = [(x0,y0,z0),(x1,y0,z0),(x1,y0,z1),(x0,y0,z1),
         (x0,y1,z0),(x1,y1,z0),(x1,y1,z1),(x0,y1,z1)]
    idx = []
    for f in [(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)]:
        a, b, c, d = f
        idx += [a, b, c, a, c, d]
    return v, idx

# 骨骼定义：name, parent, 局部平移（关节枢轴），关节序号
BONES = [
    ("hip",      -1, (0, 0, 0)),        # 0 骨盆根
    ("spine",     0, (0, 1.0, 0)),      # 1 脊柱（枢轴在腰）
    ("leftArm",   1, (-0.2, 0.55, 0)),  # 2 左臂（枢轴在左肩）
    ("rightArm",  1, ( 0.2, 0.55, 0)),  # 3 右臂（枢轴在右肩）
    ("leftLeg",   0, (-0.11, 0.9, 0)),  # 4 左腿（枢轴在左髋）
    ("rightLeg",  0, ( 0.11, 0.9, 0)),  # 5 右腿（枢轴在右髋）
]
# 每个盒体：(几何参数, 关节序号)
BOXES = [
    ((0, 0.95, 0, 0.42, 0.22, 0.26), 0),     # 骨盆 → hip
    ((0, 1.28, 0, 0.5, 0.56, 0.3),   1),     # 躯干 → spine
    ((0, 1.72, 0, 0.24, 0.22, 0.24), 1),     # 头 → spine
    ((-0.34, 1.2, 0, 0.13, 0.7, 0.15), 2),   # 左臂 → leftArm
    (( 0.34, 1.2, 0, 0.13, 0.7, 0.15), 3),   # 右臂 → rightArm
    ((-0.11, 0.45, 0, 0.14, 0.9, 0.16), 4),  # 左腿 → leftLeg
    (( 0.11, 0.45, 0, 0.14, 0.9, 0.16), 5),  # 右腿 → rightLeg
]

pos, idx, jnt, wgt = [], [], [], []
wv = 0
for geo, j in BOXES:
    v, ii = box_geo(*geo)
    for p in v:
        pos.append(p)
        jnt += [j, 0, 0, 0]
        wgt += [1.0, 0.0, 0.0, 0.0]
    for i in ii:
        idx.append(i + wv)
    wv += len(v)

normals = []
for (x, y, z) in pos:
    l = math.sqrt(x*x + y*y + z*z) or 1.0
    normals.append([x/l, y/l, z/l])

def packf(flat):
    return base64.b64encode(struct.pack("<%df" % len(flat), *flat)).decode()

# 逆绑定矩阵：worldBind 的逆。QMatrix4x4 列主序：T(t) 列主序 = [1,0,0,0, 0,1,0,0, 0,0,1,0, tx,ty,tz,1]
def inv_bind_chain(bone_idx):
    t = [0.0, 0.0, 0.0]
    i = bone_idx
    while i >= 0:
        tx, ty, tz = BONES[i][2]
        t[0] += tx; t[1] += ty; t[2] += tz
        i = BONES[i][1]
    return [1,0,0,0, 0,1,0,0, 0,0,1,0, -t[0],-t[1],-t[2],1]

ibm_flat = []
for i in range(len(BONES)):
    ibm_flat += inv_bind_chain(i)

# 动画 idle：手臂前后摆、腿交替、躯干轻微起伏，1.2s 循环
DUR = 1.2
def quat_axis(axis, deg):
    r = math.radians(deg) / 2
    if axis == "x": q = [math.sin(r), 0, 0]
    elif axis == "y": q = [0, math.sin(r), 0]
    else: q = [0, 0, math.sin(r)]
    return [q[0], q[1], q[2], math.cos(r)]

times = [0.0, 0.3, 0.6, 0.9, 1.2]
def phase(deg, ph):
    return [deg * math.sin(2*math.pi*t/DUR + ph) for t in times]

tracks = [
    ("hip",      "x", phase(6, 0)),
    ("spine",    "x", phase(-4, 0)),
    ("leftArm",  "x", phase(35, 0)),
    ("rightArm", "x", phase(35, math.pi)),
    ("leftLeg",  "x", phase(25, math.pi)),
    ("rightLeg", "x", phase(25, 0)),
]
# 生成 sampler 数据
sampler_outputs = []
for name, axis, angs in tracks:
    flat = []
    for a in angs:
        flat += quat_axis(axis, a)
    sampler_outputs.append(packf(flat))
times_b64 = packf(times)

# 组装 binary blob
def f32(flat):
    return struct.pack("<%df" % len(flat), *flat)

pos_flat = [c for p in pos for c in p]
nrm_flat = [c for n in normals for c in n]
jnt_flat = jnt
wgt_flat = wgt
idx_flat = idx

blob = b""
off = 0
blob += f32(pos_flat); bv_pos = (off, len(pos_flat) * 4); off += len(pos_flat) * 4
blob += f32(nrm_flat); bv_nrm = (off, len(nrm_flat) * 4); off += len(nrm_flat) * 4
blob += struct.pack("<%dH" % len(idx_flat), *idx_flat); bv_idx = (off, len(idx_flat) * 2); off += len(idx_flat) * 2
blob += struct.pack("<%dB" % len(jnt_flat), *jnt_flat); bv_jnt = (off, len(jnt_flat)); off += len(jnt_flat)
blob += f32(wgt_flat); bv_wgt = (off, len(wgt_flat) * 4); off += len(wgt_flat) * 4
blob += struct.pack("<%df" % (len(BONES) * 16), *ibm_flat); bv_ibm = (off, len(BONES) * 16 * 4); off += len(BONES) * 16 * 4
blob += f32(times); bv_times = (off, len(times) * 4); off += len(times) * 4
sampler_ranges = []
for so in sampler_outputs:
    raw = base64.b64decode(so)
    sampler_ranges.append((len(blob), len(raw)))
    blob += raw
b64 = base64.b64encode(blob).decode()

nodes = [{"name": "StickmanRoot", "mesh": 0, "skin": 0}]
# 节点：hip=1, spine=2, leftArm=3, rightArm=4, leftLeg=5, rightLeg=6
node_for_bone = {}
for bi, (name, parent, tr) in enumerate(BONES):
    node_for_bone[name] = bi + 1
for bi, (name, parent, tr) in enumerate(BONES):
    nodes.append({"name": name, "translation": [float(tr[0]), float(tr[1]), float(tr[2])]})
node_children = {name: [] for name, _, _ in BONES}
root_bones = []
for bi, (name, parent, tr) in enumerate(BONES):
    if parent >= 0:
        node_children[BONES[parent][0]].append(node_for_bone[name])
for bi, (name, parent, tr) in enumerate(BONES):
    if node_children[name]:
        nodes[bi + 1]["children"] = node_children[name]

channels, samplers_json, acc_extra, bvs_extra = [], [], [], []
acc_id = 9 + len(BONES)   # 0..8 基础，9+ ibm 之后是动画
acc_id = 6 + 1 + len(BONES)  # 0 pos,1 nrm,2 idx,3 jnt,4 wgt,5 ibm? 见下
# 重新排：0 pos, 1 nrm, 2 idx, 3 jnt, 4 wgt, 5 times, 6..(6+n-1) ibm? 顺序需与 bufferViews 一致
acc_id = 6 + len(BONES) + 1  # 6 ibm(占 len(BONES) 个? 不，一个 accessor 装 n 个 MAT4)
# 修正：ibm 是一个 accessor（MAT4 数组）
accessors = [
    {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3",
     "max": [0.6, 1.85, 0.4], "min": [-0.6, 0.0, -0.4]},
    {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
    {"bufferView": 2, "componentType": 5123, "count": len(idx_flat), "type": "SCALAR"},
    {"bufferView": 3, "componentType": 5121, "count": len(pos), "type": "VEC4"},
    {"bufferView": 4, "componentType": 5126, "count": len(pos), "type": "VEC4"},
    {"bufferView": 5, "componentType": 5126, "count": len(BONES), "type": "MAT4"},
    {"bufferView": 6, "componentType": 5126, "count": len(times), "type": "SCALAR"},
]
# sampler 输出 accessors
for k, so in enumerate(sampler_outputs):
    off, blen = sampler_ranges[k]
    accessors.append({"bufferView": 7 + k, "componentType": 5126, "count": len(times), "type": "VEC4"})
bufferViews = [
    {"buffer": 0, "byteOffset": bv_pos[0], "byteLength": bv_pos[1]},
    {"buffer": 0, "byteOffset": bv_nrm[0], "byteLength": bv_nrm[1]},
    {"buffer": 0, "byteOffset": bv_idx[0], "byteLength": bv_idx[1]},
    {"buffer": 0, "byteOffset": bv_jnt[0], "byteLength": bv_jnt[1]},
    {"buffer": 0, "byteOffset": bv_wgt[0], "byteLength": bv_wgt[1]},
    {"buffer": 0, "byteOffset": bv_ibm[0], "byteLength": bv_ibm[1]},
    {"buffer": 0, "byteOffset": bv_times[0], "byteLength": bv_times[1]},
]
for k, (off, blen) in enumerate(sampler_ranges):
    bufferViews.append({"buffer": 0, "byteOffset": off, "byteLength": blen})

for k, (name, axis, angs) in enumerate(tracks):
    samplers_json.append({"input": 6, "output": 7 + k, "interpolation": "LINEAR"})
    channels.append({"target": {"node": node_for_bone[name], "path": "rotation"}, "sampler": k})

gltf = {
  "asset": {"version": "2.0", "generator": "PhysicsEngine3D mocap rig"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": nodes,
  "skins": [{
    "joints": [node_for_bone[b[0]] for b in BONES],
    "skeleton": node_for_bone["hip"],
    "inverseBindMatrices": 5
  }],
  "meshes": [{
    "name": "stickman",
    "primitives": [{
      "attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 3, "WEIGHTS_0": 4},
      "indices": 2, "material": 0
    }]
  }],
  "materials": [{"name": "stickmanMat", "pbrMetallicRoughness": {"baseColorFactor": [0.55, 0.85, 0.6, 1.0], "metallicFactor": 0.1, "roughnessFactor": 0.7}}],
  "animations": [{"name": "idle", "channels": channels, "samplers": samplers_json}],
  "accessors": accessors,
  "bufferViews": bufferViews,
  "buffers": [{"byteLength": len(blob), "uri": f"data:application/octet-stream;base64,{b64}"}]
}
with open(OUT + "/stickman.gltf", "w") as f:
    json.dump(gltf, f)
print("stickman.gltf written, verts:", len(pos), "bones:", len(BONES))
