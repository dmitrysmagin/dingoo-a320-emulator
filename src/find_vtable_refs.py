import struct

with open('C:/Users/user/src/_opencode_tests/7days/7days.app', 'rb') as f:
    data = f.read()

rawd_start = 0x970
rawd = data[rawd_start:]

vtable_hi = 0x80AE
vtable_lo = 0xB230

print("Searching for code that references the rendering vtable...")
count = 0
for i in range(0, len(rawd)-12, 4):
    insn1 = struct.unpack_from('<I', rawd, i)[0]
    insn2 = struct.unpack_from('<I', rawd, i+4)[0]
    insn3 = struct.unpack_from('<I', rawd, i+8)[0]
    op1 = insn1 >> 26
    op2 = insn2 >> 26
    op3 = insn3 >> 26

    if op1 == 0x0F:  # LUI
        rt1 = (insn1 >> 16) & 0x1F
        imm1 = insn1 & 0xFFFF
        if imm1 == 0x80AE:
            # Check for LW with offset close to 0xB230
            for j, (insn, op) in enumerate([(insn2, op2), (insn3, op3)], 1):
                if op == 0x23:  # LW
                    rs = (insn >> 21) & 0x1F
                    rt = (insn >> 16) & 0x1F
                    imm = insn & 0xFFFF
                    if rs == rt1 and 0xB200 <= imm <= 0xB300:
                        vaddr = 0x80A00000 + i
                        if count < 15:
                            print(f"  0x{vaddr:08X}: LUI ${rt1},0x{imm1:04X}  LW ${rt},0x{imm:X}(${rs})  [+{j*4}]")
                        count += 1

print(f"  Total: {count} references")
