"""Development guardrails: fail CI if early NOVA enables unsafe behavior.

These are source-level invariants only, NOT a security audit, sandbox, or proof
of exploit resistance. A secure release requires real runtime and VM testing.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TestNativeSafetyDefaults(unittest.TestCase):
    def text(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_iso_requires_explicit_manual_confirmation(self):
        workflow = self.text(".github/workflows/build-native-iso.yml")
        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("final_release:", workflow)
        self.assertIn("default: false", workflow)
        self.assertRegex(workflow, r"if:\s*\$\{\{\s*inputs\.final_release == true\s*\}\}")
        self.assertNotRegex(workflow, r"(?m)^\s{2}(push|pull_request|schedule):")

    def test_legacy_disk_driver_is_read_only(self):
        ata = self.text("kernel/ata.h")
        # ATA status/command register (0x1F7) must issue only read or identify.
        commands = re.findall(r"nova_ata_out\s*\(\s*0x1F7\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*\)", ata)
        self.assertGreaterEqual(len(commands), 2)
        self.assertEqual(set(int(c, 0) for c in commands), {0xEC, 0x20})
        self.assertNotRegex(ata, r"(?i)\b(outw|outsw|rep\s+outsw|write_sector|format_disk)\s*\(")
        self.assertIn("nova_ata_read_sector", ata)

    def test_vfs_refuses_disk_mutations(self):
        vfs = self.text("kernel/vfs.h")
        for function in ("nova_vfs_write", "nova_vfs_create", "nova_vfs_remove"):
            self.assertIn(function, vfs)
        # In every mutation function, the volume MUST be RAM: only.
        for start_name, end_name in (
            ("static bool nova_vfs_write(", "static bool nova_vfs_create("),
            ("static bool nova_vfs_create(", "static bool nova_vfs_remove("),
        ):
            section = vfs.split(start_name, 1)[1].split(end_name, 1)[0]
            self.assertIn("vol!=NOVA_VFS_RAM", section)
        remove = vfs.split("static bool nova_vfs_remove(", 1)[1].split("#endif", 1)[0]
        self.assertIn("vol!=NOVA_VFS_RAM", remove)

    def test_programs_remain_non_executable_during_development(self):
        paging = self.text("kernel/paging.h")
        process = self.text("kernel/process_image.h")
        app = self.text("kernel/main.c")
        self.assertIn("OFFLINE", paging)
        self.assertIn("NOT runnable processes", process)
        self.assertIn("nova_pt_prepare", app)
        # No x86-64 page-table activation or ring-3 transitions are permitted yet.
        self.assertNotRegex(
            app + paging + process,
            r"(?i)(?:movq?\s+[^;\n]{0,60}(?:%|\\)cr3|"
            r"(?:sysretq|swapgs)\s*[;\n]|"
            r"(?:wrmsr|ltr)\s*[;\n])"
        )

    def test_game_controls_are_a_nonprivileged_adapter(self):
        controls = self.text("kernel/game_input.h")
        self.assertIn("NOVA_GAME_CONTROLLER", controls)
        self.assertIn("nova_game_scan(", controls)
        self.assertIn("nova_game_set(", controls)
        self.assertNotIn("__asm__", controls)
        self.assertIn("nova_game_scan(&nova_game_keys,key)", self.text("kernel/main.c"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
