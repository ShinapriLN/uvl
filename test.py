import subprocess
import unittest
import os
import sys

class TestUVL(unittest.TestCase):

    def run_uvl(self, args, env_vars=None):
        env = os.environ.copy()
        env["UVL_DEBUG"] = "1"
        if env_vars:
            env.update(env_vars)
            
        result = subprocess.run(
            [sys.executable, "uvl.py"] + args,
            capture_output=True,
            text=True,
            env=env
        )
        return result

    def test_uv_mapping(self):
        res = self.run_uvl(["uv", "version"])
        self.assertEqual(res.returncode, 0)
        self.assertIn("DEBUG: env UV_LINK_MODE=symlink", res.stdout)
        self.assertIn("DEBUG: env UV_CACHE_DIR=", res.stdout)
        self.assertIn("/.uvl/store/uv", res.stdout)

    def test_cargo_mapping(self):
        res = self.run_uvl(["cargo", "build"])
        self.assertEqual(res.returncode, 0)
        self.assertIn("DEBUG: env CARGO_HOME=", res.stdout)
        self.assertIn("/.uvl/store/cargo", res.stdout)

    def test_has_flag(self):
        # Check for a tool that is definitely supported and probably in PATH
        # We'll use 'uv' since we know it's there from previous steps
        res = self.run_uvl(["--has", "uv"])
        self.assertEqual(res.returncode, 0)
        self.assertIn("true", res.stdout)

        # Check for a non-existent tool
        res = self.run_uvl(["--has", "nonexistent-tool-xyz"])
        self.assertEqual(res.returncode, 1)
        self.assertIn("false", res.stdout)

if __name__ == '__main__':
    unittest.main()
