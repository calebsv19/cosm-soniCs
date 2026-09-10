"""Reject unsafe output roots before any package command executes."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

SOURCE = pathlib.Path(__file__).resolve().parents[1]

class DisposableRootTests(unittest.TestCase):
    def test_invalid_roots_leave_existing_bytes_untouched(self):
        with tempfile.TemporaryDirectory(prefix="sonics-package-root-") as temporary:
            root = pathlib.Path(temporary)
            shutil.copyfile(SOURCE / "make/release-disposable.mk", root / "Makefile")
            parent = root / "build/release-authenticated"
            parent.mkdir(parents=True)
            existing = parent / "existing-job"
            existing.mkdir()
            (existing / "sentinel").write_text("keep")
            (parent / "linked-job").symlink_to(existing, target_is_directory=True)
            cases = ["", "/tmp/forbidden", "../outside", "build/release",
                     "build/release-authenticated/ab", "build/release-authenticated/../escape",
                     "build/release-authenticated/existing-job", "build/release-authenticated/linked-job"]
            for value in cases:
                with self.subTest(root=value):
                    result = subprocess.run(["make", "RELEASE_ROOT=" + value,
                        "release-artifact-disposable"], cwd=root, capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("RELEASE_ROOT", result.stdout + result.stderr)
                    self.assertEqual((existing / "sentinel").read_text(), "keep")
                    self.assertFalse((root / "dist").exists())
            shutil.rmtree(parent)
            parent.symlink_to(root, target_is_directory=True)
            result = subprocess.run(["make", "RELEASE_ROOT=build/release-authenticated/ancestor-job",
                "release-artifact-disposable"], cwd=root, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("ancestor must not be a symlink", result.stdout + result.stderr)
            self.assertFalse((root / "ancestor-job").exists())

    def test_self_test_uses_and_removes_private_runtime_on_success_and_failure(self):
        for exit_code in (0, 7):
            with self.subTest(exit_code=exit_code), tempfile.TemporaryDirectory(prefix="sonics-runtime-") as temporary:
                root = pathlib.Path(temporary)
                (root / "build").mkdir()
                (root / "Makefile").write_text(
                    (SOURCE / "make/release-disposable.mk").read_text()
                    + "\npackage-desktop-smoke:\n\t@true\n")
                launcher = root / "daw-launcher"
                launcher.write_text(
                    '#!/bin/sh\nset -eu\n'
                    'printf "%s" "$DAW_RUNTIME_DIR" > observed-runtime\n'
                    'test -d "$DAW_RUNTIME_DIR"\n'
                    'echo seeded > "$DAW_RUNTIME_DIR/sentinel"\n'
                    f'exit {exit_code}\n')
                launcher.chmod(0o755)
                result = subprocess.run([
                    "make", "release-package-self-test", "PACKAGE_MACOS_DIR=" + str(root)
                ], cwd=root, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, exit_code == 0)
                runtime = pathlib.Path((root / "observed-runtime").read_text())
                self.assertEqual(runtime.parent, (root / "build").resolve())
                self.assertFalse(runtime.exists())

if __name__ == "__main__":
    unittest.main()
