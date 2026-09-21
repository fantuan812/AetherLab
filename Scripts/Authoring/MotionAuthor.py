"""固定 C ABI 的离线动作作者入口。仅在统一实现完成后执行；不在玩家机器下载模型。
extract 从模型 API 取骨架；bake 产生真实 agent 姿态；style 将 UE 导出的 G1 姿态转为 GGUF。
转换报告与实际视觉审批分离；loader 接受文件不代表动作质量通过。
"""
from __future__ import annotations
import argparse
import ctypes as c
import hashlib
import json
import math
import os
from pathlib import Path
import struct

REVISION = "ee0cf5d9035f639ed0787f390fb1ce05d6a4c463"
UPSTREAM = "a0732b642c0333077e127a2f56ab0014c196bca4"
H, U, Q, F = c.c_void_p, c.c_uint32, c.c_uint64, c.c_float

def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def atomic_json(path, obj):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(obj, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temp.replace(path)

class Native:
    def __init__(self, root):
        self.root = Path(root).resolve()
        record = json.loads((self.root / "stage.json").read_text(encoding="utf-8-sig"))
        if record["nativeRevision"] != REVISION or record["abi"] != 1:
            raise ValueError("原生制品版本不兼容")
        listed = set()
        for entry in record["files"]:
            relative = entry["path"]
            path = (self.root / relative).resolve()
            if not path.is_relative_to(self.root) or relative in listed:
                raise ValueError("制品路径重复或越界")
            if path.stat().st_size != entry["bytes"] or digest(path) != entry["sha256"]:
                raise ValueError("制品哈希失败：" + relative)
            listed.add(relative)
        if any(p.name not in listed for p in self.root.glob("*.dll")):
            raise ValueError("制品目录含未声明 DLL")
        self.search = os.add_dll_directory(str(self.root)) if os.name == "nt" else None
        self.dependencies = [c.CDLL(str(self.root / x)) for x in
                             (["ggml-base.dll", "ggml.dll"] if os.name == "nt" else ["libggml-base.so", "libggml.so"])]
        self.dll = c.CDLL(str(self.root / ("motionbricks.dll" if os.name == "nt" else "libmotionbricks.so")))
        self.fn("mb_abi_version", [], U)
        if self.dll.mb_abi_version() != 1:
            raise ValueError("ABI 不匹配")
        self.options, self.model = H(), H()
        self.call("mb_runtime_options_create", [c.POINTER(H)], c.byref(self.options))
        try:
            self.call("mb_runtime_options_set_device", [H, U], self.options, 1)
            self.call("mb_runtime_options_set_threads", [H, U], self.options, 2)
            self.call("mb_runtime_options_set_backend_directory", [H, c.c_char_p], self.options, os.fsencode(self.root))
            self.call("mb_model_load", [c.c_char_p, H, c.POINTER(H)], os.fsencode(self.root / "g1-f32"), self.options, c.byref(self.model))
        except BaseException:
            self.close()
            raise

    def fn(self, name, args, result=U):
        fn = getattr(self.dll, name)
        fn.argtypes, fn.restype = args, result
        return fn

    def call(self, name, types, *args):
        error = c.create_string_buffer(4096)
        status = self.fn(name, types + [c.c_char_p, Q])(*args, error, len(error))
        if status:
            raise RuntimeError(name + ": " + str(status) + " " + error.value.decode("utf-8", errors="replace"))

    def free(self, kind, handle):
        if handle:
            self.fn("mb_" + kind + "_free", [H], None)(handle)

    def close(self):
        self.free("model", self.model)
        self.free("runtime_options", self.options)
        self.model, self.options = H(), H()
        if self.search:
            self.search.close()
            self.search = None

    def skeleton(self):
        count = U()
        self.call("mb_model_get_joint_count", [H, c.POINTER(U)], self.model, c.byref(count))
        if count.value != 34:
            raise ValueError("只支持锁定的 G1Skeleton34")
        joints = []
        for index in range(34):
            name, parent = c.c_char_p(), c.c_int32()
            x, y, z = F(), F(), F()
            self.call("mb_model_get_joint_name", [H, U, c.POINTER(c.c_char_p)], self.model, index, c.byref(name))
            self.call("mb_model_get_joint_parent", [H, U, c.POINTER(c.c_int32)], self.model, index, c.byref(parent))
            self.call("mb_model_get_neutral_joint_position", [H, U, c.POINTER(F), c.POINTER(F), c.POINTER(F)],
                      self.model, index, c.byref(x), c.byref(y), c.byref(z))
            if parent.value >= index or parent.value < -1 or (index != 0 and parent.value == -1):
                raise ValueError("骨架层级无效")
            joints.append(dict(name=name.value.decode(), parent=parent.value, neutral=[x.value, y.value, z.value]))
        encoded = json.dumps(joints, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
        return dict(schema=1, nativeRevision=REVISION, skeleton="g1skel34", coordinates="X-right,Y-up,Z-forward",
                    units="meters", fps=30, skeletonSha256=hashlib.sha256(encoded).hexdigest(), joints=joints)

    def bake(self, style, seconds, direction, speed, seed):
        if not style.replace("_", "").isalnum() or not 0.2 <= seconds <= 60:
            raise ValueError("风格 ID 或烘焙时长无效")
        handles = {"style": H(), "agent": H(), "command": H()}
        try:
            self.call("mb_style_load", [H, c.c_char_p, c.POINTER(H)], self.model,
                      os.fsencode(self.root / "styles" / (style + ".mbstyle")), c.byref(handles["style"]))
            self.call("mb_agent_create", [H, c.POINTER(H)], self.model, c.byref(handles["agent"]))
            self.call("mb_agent_reset", [H, H], handles["agent"], handles["style"])
            self.call("mb_command_create", [c.POINTER(H)], c.byref(handles["command"]))
            command = handles["command"]
            self.call("mb_command_set_style", [H, H], command, handles["style"])
            for field in ["movement_direction", "facing_direction"]:
                self.call("mb_command_set_" + field, [H, F, F, F], command, *direction)
            self.call("mb_command_set_target_speed", [H, F], command, speed)
            roots, rotations = [], []
            target = int(round(seconds * 30))
            while len(roots) < target:
                self.call("mb_command_set_seed", [H, Q], command, seed + len(roots))
                motion = H()
                self.call("mb_agent_plan", [H, H, c.POINTER(H)], handles["agent"], command, c.byref(motion))
                try:
                    frames, joints = Q(), Q()
                    self.call("mb_motion_get_frame_count", [H, c.POINTER(Q)], motion, c.byref(frames))
                    self.call("mb_motion_get_joint_count", [H, c.POINTER(Q)], motion, c.byref(joints))
                    if not 8 <= frames.value <= 64 or joints.value != 34:
                        raise ValueError("模型输出维度无效")
                    arrays = []
                    for field, stride in [("root_translations", 3), ("local_rotations_xyzw", 136)]:
                        pointer, size = c.POINTER(F)(), Q()
                        self.call("mb_motion_get_" + field, [H, c.POINTER(c.POINTER(F)), c.POINTER(Q)], motion, c.byref(pointer), c.byref(size))
                        if size.value != frames.value * stride:
                            raise ValueError("姿态长度不匹配")
                        arrays.append([list(pointer[i:i+stride]) for i in range(0, size.value, stride)])
                    # plan 的前四帧是上一段实际上下文；每次只加入新帧。
                    take = min(frames.value - 4, target - len(roots))
                    roots.extend(arrays[0][4:4+take])
                    rotations.extend(arrays[1][4:4+take])
                    boundary_root = (F * 12)(*sum(arrays[0][take:take+4], []))
                    boundary_rot = (F * 544)(*sum(arrays[1][take:take+4], []))
                    self.call("mb_agent_set_context", [H, c.POINTER(F), c.POINTER(F), Q, Q],
                              handles["agent"], boundary_root, boundary_rot, 4, 34)
                finally:
                    self.free("motion", motion)
            result = self.skeleton()
            result.update(sourceAsset="motionbricks:" + style, sourceSha256=digest(self.root / "styles" / (style + ".mbstyle")),
                          roots=roots, rotations=rotations)
            return result
        finally:
            for kind in ["command", "agent", "style"]:
                self.free(kind, handles[kind])

def make_style(source, output, name, speed, duration_count, native):
    # 只接收 G1 source 的局部姿态，任意 Manny/Quinn 必须先通过正式反向 retarget 资产。
    import numpy as np
    data = json.loads(Path(source).read_text(encoding="utf-8-sig"))
    skeleton = native.skeleton()
    if any(data.get(k) != skeleton[k] for k in ["skeletonSha256", "coordinates", "units", "fps"]):
        raise ValueError("源骨架、坐标、单位或帧率不符")
    roots = np.asarray(data["roots"], dtype=np.float32)
    q = np.asarray(data["rotations"], dtype=np.float32).reshape(-1, 34, 4)
    frames = len(roots)
    if roots.shape != (frames, 3) or q.shape != (frames, 34, 4) or not 4 <= frames <= 1800:
        raise ValueError("风格必须为 4 至 1800 帧的 G1 动画")
    if not np.isfinite(roots).all() or not np.isfinite(q).all() or np.max(np.abs(roots)) > 10000:
        raise ValueError("非有限或过大坐标")
    norm = np.linalg.norm(q, axis=-1)
    if np.max(np.abs(norm - 1)) > .02:
        raise ValueError("四元数未归一化")
    q = q / norm[..., None]
    x,y,z,w = [q[..., i] for i in range(4)]
    local = np.stack([1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),
                      2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),
                      2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)],axis=-1).reshape(frames,34,3,3)
    positions, rotations = np.zeros((frames,34,3),np.float32), np.empty_like(local)
    neutral = np.array([j["neutral"] for j in skeleton["joints"]], dtype=np.float32)
    for j, joint in enumerate(skeleton["joints"]):
        p = joint["parent"]
        if p < 0:
            positions[:,j], rotations[:,j] = roots, local[:,j]
        else:
            rotations[:,j] = rotations[:,p] @ local[:,j]
            positions[:,j] = positions[:,p] + np.einsum("fij,j->fi",rotations[:,p],neutral[j]-neutral[p])
    heading = np.unwrap(np.arctan2(rotations[:,0,0,2], rotations[:,0,2,2])).astype(np.float32)
    allowed = np.zeros(11, np.int32)
    if not 1 <= duration_count <= 11 or not math.isfinite(speed) or speed < 0 or not name.replace("_","").isalnum():
        raise ValueError("风格元数据无效")
    allowed[:duration_count] = 1
    values = [("global_joint_positions", positions), ("global_joint_rotations", rotations.reshape(frames,34,9)),
              ("global_root_positions", roots), ("global_headings", heading), ("allowed_tokens", allowed)]
    source_hash = digest(source)
    # GGUF v3，固定 F32/I32 和 32 字节对齐；所有张量布局与锁定 loader 一致。
    def string(value):
        raw = value.encode("utf-8")
        return struct.pack("<Q", len(raw)) + raw
    def kv(key, kind, value):
        return string(key) + struct.pack("<I", kind) + (string(value) if kind == 8 else struct.pack("<f" if kind == 6 else "<I", value))
    metadata = [
        kv("general.architecture",8,"motionbricks"), kv("general.name",8,name),
        kv("motionbricks.component",8,"style"), kv("motionbricks.skeleton",8,"g1skel34"),
        kv("motionbricks.upstream_revision",8,UPSTREAM),kv("motionbricks.source_sha256",8,source_hash),
        kv("motionbricks.style_name",8,name),kv("motionbricks.style_speed",6,speed),
        kv("motionbricks.style_frames",4,frames)]
    descriptors, blobs, offset = [], [], 0
    for key, value in values:
        value = np.ascontiguousarray(value.astype("<i4" if key == "allowed_tokens" else "<f4"))
        blob = value.tobytes()
        descriptors.append(string(key)+struct.pack("<I",value.ndim)+struct.pack("<"+"Q"*value.ndim,*reversed(value.shape))
                           +struct.pack("<IQ",26 if key == "allowed_tokens" else 0,offset))
        padding = (-len(blob)) % 32
        blobs.append(blob + bytes(padding))
        offset += len(blob) + padding
    header = struct.pack("<4sIQQ",b"GGUF",3,len(values),len(metadata)) + b"".join(metadata+descriptors)
    path = Path(output);path.parent.mkdir(parents=True,exist_ok=True)
    temp = path.with_suffix(path.suffix+".tmp")
    temp.write_bytes(header+bytes((-len(header))%32)+b"".join(blobs))
    loaded = H()
    try:
        native.call("mb_style_load",[H,c.c_char_p,c.POINTER(H)],native.model,os.fsencode(temp),c.byref(loaded))
    except BaseException:
        temp.unlink(missing_ok=True)
        raise
    finally:
        native.free("style",loaded)
    temp.replace(path)
    atomic_json(path.with_suffix(".source.json"),dict(schema=1,sourceAsset=data.get("sourceAsset"),sourceSha256=source_hash,
                skeleton=skeleton,styleSha256=digest(path),frames=frames,fps=30,qualityApproved=False))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode",choices=["extract","bake","style"])
    p.add_argument("--stage",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--source",type=Path)
    p.add_argument("--style",default="walk")
    p.add_argument("--seconds",type=float,default=4)
    p.add_argument("--speed",type=float,default=1)
    p.add_argument("--direction",type=float,nargs=3,default=[0,0,1])
    p.add_argument("--seed",type=int,default=10)
    p.add_argument("--duration-count",type=int,default=6)
    a=p.parse_args()
    native=Native(a.stage)
    try:
        if a.mode=="extract": atomic_json(a.output,native.skeleton())
        elif a.mode=="bake": atomic_json(a.output,native.bake(a.style,a.seconds,a.direction,a.speed,a.seed))
        else:
            if not a.source: p.error("style 需要 --source")
            make_style(a.source,a.output,a.style,a.speed,a.duration_count,native)
    finally: native.close()
if __name__=="__main__": main()
