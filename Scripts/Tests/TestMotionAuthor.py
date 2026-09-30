"""无模型的烘焙输入/输出合同测试；不代表真实推理、UE 加载或视觉质量通过。"""
import contextlib
import copy
import io
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Authoring"))
from MotionAuthor import (G1_JOINTS, MAX_SEED, PLAN_MIN_FRAMES, PLAN_MAX_FRAMES,
                          CONTEXT_FRAMES, arguments, bake_request, validate_pose_frames)


def poses(frames=CONTEXT_FRAMES):
    return ([[0., .8, 0.] for _ in range(frames)],
            [[0., 0., 0., 1.] * G1_JOINTS for _ in range(frames)])


class MotionAuthorContract(unittest.TestCase):
    def request(self, **changes):
        values = dict(style="walk", seconds=1, movement=[1, 0, 0], facing=[0, 0, 1], speed=1, seed=10)
        values.update(changes)
        return bake_request(**values)

    def test_independent_movement_and_facing(self):
        frames, movement, facing = self.request()
        self.assertEqual(frames, 30)
        self.assertEqual(movement, (1., 0., 0.))
        self.assertEqual(facing, (0., 0., 1.))
        self.assertEqual(self.request(movement=[0, 0, 0], speed=0)[1], (0., 0., 0.))

    def test_bad_commands_are_rejected(self):
        for changes in [dict(facing=[0, 0, 0]), dict(movement=[1, 0]),
                        dict(movement=[float("nan"), 0, 0]), dict(facing=[0, float("inf"), 1]),
                        dict(movement=[1e300, 0, 0]), dict(speed=-1), dict(speed=20.1),
                        dict(seconds=float("nan")), dict(seconds=60.1), dict(seconds=.1),
                        dict(style="../walk"), dict(style="x" * 65), dict(style="___"),
                        dict(seed=-1), dict(seed=True), dict(seed=MAX_SEED)]:
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.request(**changes)

    def test_seed_range_includes_segment_offsets(self):
        self.assertEqual(self.request(seed=MAX_SEED - 29)[0], 30)
        with self.assertRaises(ValueError):
            self.request(seed=MAX_SEED - 28)

    def test_cli_requires_both_directions(self):
        base = ["bake", "--stage", "unused", "--output", "unused.json"]
        args = arguments(base + ["--movement-direction", "1", "0", "0",
                                 "--facing-direction", "0", "0", "1"])
        self.assertEqual(args.movement_direction, [1., 0., 0.])
        self.assertEqual(args.facing_direction, [0., 0., 1.])
        for extra in [[], ["--movement-direction", "1", "0", "0"],
                      ["--facing-direction", "0", "0", "1"], ["--direction", "0", "0", "1"]]:
            with self.subTest(extra=extra), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                arguments(base + extra)

    def test_valid_pose_is_not_repaired_or_mutated(self):
        roots, rotations = poses()
        original = copy.deepcopy((roots, rotations))
        self.assertEqual(validate_pose_frames(roots, rotations), CONTEXT_FRAMES)
        self.assertEqual((roots, rotations), original)

    def test_plan_shape_matches_locked_abi(self):
        for frames in [PLAN_MIN_FRAMES, PLAN_MAX_FRAMES]:
            self.assertEqual(validate_pose_frames(*poses(frames), minimum=PLAN_MIN_FRAMES,
                             maximum=PLAN_MAX_FRAMES, multiple=CONTEXT_FRAMES), frames)
        for frames in [8, 23, 25, 65]:
            with self.subTest(frames=frames), self.assertRaises(ValueError):
                validate_pose_frames(*poses(frames), minimum=PLAN_MIN_FRAMES,
                                     maximum=PLAN_MAX_FRAMES, multiple=CONTEXT_FRAMES)

    def test_wrong_target_skeleton_cannot_be_used_as_g1(self):
        roots, rotations = poses()
        rotations[0] = [0., 0., 0., 1.] * 65
        with self.assertRaises(ValueError):
            validate_pose_frames(roots, rotations)

    def test_bad_roots_and_rotations_are_rejected(self):
        for value in [float("nan"), float("inf"), -float("inf"), 10000.1]:
            roots, rotations = poses()
            roots[0][0] = value
            with self.subTest(root=value), self.assertRaises(ValueError):
                validate_pose_frames(roots, rotations)
        for value in [float("nan"), float("inf"), -float("inf")]:
            roots, rotations = poses()
            rotations[0][0] = value
            with self.subTest(rotation=value), self.assertRaises(ValueError):
                validate_pose_frames(roots, rotations)

    def test_quaternion_uses_runtime_squared_norm_tolerance(self):
        roots, rotations = poses()
        rotations[0][3] = 1.009
        validate_pose_frames(roots, rotations)
        rotations[0][3] = 1.011
        # norm 偏差仍小于 .02，但 squared-norm 偏差已超过运行时 .02。
        with self.assertRaises(ValueError):
            validate_pose_frames(roots, rotations)

    def test_mismatched_frame_counts_are_rejected(self):
        roots, rotations = poses()
        for root_rows, rotation_rows in [(roots[:-1], rotations[:-1]), (roots, rotations[:-1])]:
            with self.assertRaises(ValueError):
                validate_pose_frames(root_rows, rotation_rows)


if __name__ == "__main__":
    unittest.main()
