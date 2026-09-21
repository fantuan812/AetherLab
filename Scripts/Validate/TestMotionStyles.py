"""锁定真实 CPU 模型的作者回归：风格往返、根高度和有限姿态。不是视觉质量审批。"""
import argparse
import json
from pathlib import Path
import sys
import tempfile

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Authoring"))
from MotionAuthor import Native, atomic_json, make_style

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--stage", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
native = Native(a.stage)
rows = []


def check(name, clip, seed):
    roots = np.asarray(clip["roots"])
    rotations = np.asarray(clip["rotations"]).reshape(-1, 34, 4)
    # 不对推理输出修整或限幅；失败保留原始问题，避免作者工具输出地底姿态仍被记作成功。
    if (not np.isfinite(roots).all() or not np.isfinite(rotations).all()
            or roots[:, 1].min() < .2 or roots[:, 1].max() > 1.5
            or np.max(np.abs(np.linalg.norm(rotations, axis=-1) - 1)) > .02):
        raise RuntimeError(f"{name} seed={seed}: invalid generated pose, height={roots[:, 1].min()}..{roots[:, 1].max()}")
    rows.append(dict(style=name, seed=seed, frames=len(roots),
                     minHeightMeters=float(roots[:, 1].min()), maxHeightMeters=float(roots[:, 1].max())))
    return clip


try:
    for style, speed in [("idle", 0), ("crouch", 1.5), ("crouch_idle", 0)]:
        for seed in [10, 37, 991]:
            check(style, native.bake(style, 6, [0, 0, 1], speed, seed), seed)
    # 已加载模型与 DLL，不改实际 Stage：临时目录只接收新写出的风格，让往返测试独立。
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="style-roundtrip-", dir=a.output.parent) as folder:
        idle = native.bake("idle", 2, [0, 0, 1], 0, 10)
        source = Path(folder) / "idle.json"
        atomic_json(source, idle)
        target = Path(folder) / "styles/roundtrip.mbstyle"
        make_style(source, target, "roundtrip", 0, 6, native)
        native.root = Path(folder)
        check("idle_roundtrip", native.bake("roundtrip", 6, [0, 0, 1], 0, 10), 10)
    atomic_json(a.output, dict(passed=True, backend="CPU", visualQualityApproved=False, samples=rows))
    print("Real model style/roundtrip regression PASS:", a.output)
finally:
    native.close()
