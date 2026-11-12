"""Check that fatal exception assembly and IDT wiring cover every CPU vector.

Static guardrail; this does not validate runtime CPU exception handling.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_ERROR_VECTORS = {8, 10, 11, 12, 13, 14, 17, 21, 29, 30}


class TestExceptionEntryContract(unittest.TestCase):
    def test_exactly_32_exceptions_and_correct_cpu_error_codes(self):
        assembly = (ROOT / "kernel/irq.S").read_text()
        matches = re.findall(r"^NOVA_EXC_(NOERR|ERR)\s+(\d+)$",
                             assembly, flags=re.MULTILINE)
        self.assertEqual(len(matches), 32)
        vectors = {}
        for kind, number in matches:
            index = int(number)
            self.assertNotIn(index, vectors, "duplicate exception vector")
            vectors[index] = kind
        self.assertEqual(set(vectors), set(range(32)))
        self.assertEqual({v for v, kind in vectors.items() if kind == "ERR"},
                         EXPECTED_ERROR_VECTORS)

    def test_error_code_stubs_preserve_cpu_frame_layout(self):
        assembly = (ROOT / "kernel/irq.S").read_text()
        expected_instructions = {
            "NOVA_EXC_NOERR": [
                "pushq $0",
                r"pushq $\\n",
                "jmp nova_exception_common",
            ],
            "NOVA_EXC_ERR": [
                r"pushq $\\n",
                "jmp nova_exception_common",
            ],
        }
        for macro, expected in expected_instructions.items():
            match = re.search(
                rf"(?ms)^\\.macro {macro} n\\s*\\n(.*?)^\\.endm\\s*$",
                assembly,
            )
            self.assertIsNotNone(match, f"missing {macro} macro")
            body = re.sub(r"/\\*.*?\\*/", "", match.group(1), flags=re.DOTALL)
            instructions = [line.strip() for line in body.splitlines() if line.strip()]
            self.assertEqual(instructions, expected, f"incorrect {macro} stack frame")

    def test_idt_has_one_handler_per_exception(self):
        code = (ROOT / "kernel/interrupts.h").read_text()
        for i in range(32):
            self.assertIn(f"extern void nova_exception_{i}(void);", code)
            self.assertRegex(code, rf"\bnova_exception_{i}\b")
        self.assertIn(
            "if(!nova_idt_set(i,nova_cpu_exception_handlers[i]))return false;",
            code,
        )
        self.assertIn("for(unsigned i=0;i<32;i++)", code)

    def test_fault_frame_and_serial_path_never_resume(self):
        code = (ROOT / "kernel/main.c").read_text()
        assembly = (ROOT / "kernel/irq.S").read_text()
        header = (ROOT / "kernel/fault.h").read_text()
        self.assertIn("nova_fault_format(frame,cr2,report,sizeof(report))", code)
        self.assertIn("mov %%cr2", code)
        self.assertIn("void nova_exception_panic(const struct nova_fault_frame *frame)", code)
        self.assertIn("pushq %r15", assembly)
        self.assertIn("pushq %rax", assembly)
        self.assertIn("movq %rsp,%rdi", assembly)
        self.assertIn("call nova_exception_panic", assembly)
        self.assertIn("jmp 1b", assembly)
        self.assertIn("offsetof(struct nova_fault_frame,vector)==120u", header)
        self.assertIn("offsetof(struct nova_fault_frame,rip)==136u", header)


if __name__ == "__main__":
    unittest.main()
