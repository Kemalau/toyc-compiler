"""将汇编交给 Zig/LLVM 汇编链接，再用 Unicorn 执行真正的 RV64 机器码。

测试运行库用私有 ecall 编号转交 I/O；这不是 Linux 或课程平台的运行库。
"""
import collections
import os
import pathlib
import struct
import subprocess
import tempfile

import unicorn
from unicorn import riscv_const as registers
import ziglang

ZIG = str(pathlib.Path(ziglang.__file__).parent / "zig")
MASK64 = (1 << 64) - 1

TEST_RUNTIME = """
.text
.globl _start
_start:
    call main
    li a7, 93
    ecall
.globl putint
putint:
    li a7, 1001
    ecall
    ret
.globl getint
getint:
    li a7, 1002
    ecall
    ret
.globl putch
putch:
    li a7, 1003
    ecall
    ret
.globl getch
getch:
    li a7, 1004
    ecall
    ret
"""


def signed32(value):
    value &= 0xFFFFFFFF
    return value - (1 << 32) if value & (1 << 31) else value


def assemble(assembly, extra_c=""):
    with tempfile.TemporaryDirectory(prefix="toyc-rv64-") as directory:
        directory = pathlib.Path(directory)
        source = directory / "program.s"
        runtime = directory / "runtime.s"
        executable = directory / "program.elf"
        source.write_text(assembly)
        runtime.write_text(TEST_RUNTIME)
        command = [ZIG, "cc", "-target", "riscv64-linux-musl", "-mcpu=generic_rv64+m",
                   "-mabi=lp64", "-nostdlib", "-static", "-O0", "-fno-sanitize=all",
                   "-fno-stack-protector", "-Wl,-e,_start",
                   str(source), str(runtime), "-o", str(executable)]
        if extra_c:
            helper = directory / "helper.c"
            helper.write_text(extra_c)
            command.append(str(helper))
        env = dict(os.environ)
        env.setdefault("ZIG_GLOBAL_CACHE_DIR", str(pathlib.Path(tempfile.gettempdir()) / "toyc-zig-global-cache"))
        env["ZIG_LOCAL_CACHE_DIR"] = str(directory / "cache")
        result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=60)
        if result.returncode:
            raise AssertionError("RISC-V assembly/link failed:\n" + result.stderr)
        return executable.read_bytes()


def execute(image, input_values=(), input_chars=""):
    if image[:6] != b"\x7fELF\x02\x01":
        raise AssertionError("expected a little-endian ELF64 executable")
    if struct.unpack_from("<H", image, 18)[0] != 243:
        raise AssertionError("expected RISC-V ELF machine type")
    entry, phoff = struct.unpack_from("<QQ", image, 24)
    phentsize, phnum = struct.unpack_from("<HH", image, 54)
    machine = unicorn.Uc(unicorn.UC_ARCH_RISCV, unicorn.UC_MODE_RISCV64)
    mapped = set()
    for n in range(phnum):
        kind, _, offset, address, _, filesz, memsz, _ = struct.unpack_from(
            "<IIQQQQQQ", image, phoff + n * phentsize)
        if kind != 1 or memsz == 0:
            continue
        for page in range(address & ~4095, (address + memsz + 4095) & ~4095, 4096):
            if page not in mapped:
                machine.mem_map(page, 4096)
                mapped.add(page)
        if filesz:
            machine.mem_write(address, image[offset:offset + filesz])

    stack = 0x70000000
    stack_size = 8 * 1024 * 1024
    machine.mem_map(stack, stack_size)
    initial_sp = stack + stack_size - 16
    initial_s0 = 0x12345678
    machine.reg_write(registers.UC_RISCV_REG_SP, initial_sp)
    machine.reg_write(registers.UC_RISCV_REG_S0, initial_s0)
    values = collections.deque(input_values)
    chars = collections.deque(map(ord, input_chars))
    output = []
    returned = []

    def on_instruction(uc, address, size, _):
        if size != 4 or bytes(uc.mem_read(address, 4)) != b"\x73\x00\x00\x00":
            return
        if uc.reg_read(registers.UC_RISCV_REG_SP) % 16:
            raise AssertionError("stack is not 16-byte aligned at runtime call")
        number = uc.reg_read(registers.UC_RISCV_REG_A7)
        value = signed32(uc.reg_read(registers.UC_RISCV_REG_A0))
        if number == 93:
            returned.append(value)
            uc.emu_stop()
            return
        result = 0
        if number == 1001:
            output.append(str(value))
        elif number == 1002:
            result = values.popleft() if values else 0
        elif number == 1003:
            output.append(chr(value & 255))
        elif number == 1004:
            result = chars.popleft() if chars else -1
        else:
            raise AssertionError(f"unexpected test syscall {number}")
        # 模拟外部运行库对 caller-saved 寄存器的覆盖，检验跨调用保存。
        for name in [f"T{n}" for n in range(7)] + [f"A{n}" for n in range(1, 8)]:
            uc.reg_write(getattr(registers, "UC_RISCV_REG_" + name), 0xBAD)
        uc.reg_write(registers.UC_RISCV_REG_A0, result & MASK64)
        uc.reg_write(registers.UC_RISCV_REG_PC, address + 4)

    machine.hook_add(unicorn.UC_HOOK_CODE, on_instruction)
    machine.emu_start(entry, 0, timeout=5_000_000, count=2_000_000)
    if not returned:
        raise AssertionError("program did not terminate within execution limit")
    if machine.reg_read(registers.UC_RISCV_REG_SP) != initial_sp:
        raise AssertionError("main did not restore sp")
    if machine.reg_read(registers.UC_RISCV_REG_S0) != initial_s0:
        raise AssertionError("main did not restore callee-saved s0")
    return returned[0], "".join(output)
