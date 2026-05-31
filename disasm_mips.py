import struct

APP_PATH = r"C:/Users/user/src/_opencode_tests/7days/7days.app"
RAWD_OFFSET = 0x970
RAWD_VADDR = 0x80A00000

def read_rawd():
    with open(APP_PATH, "rb") as fh:
        fh.seek(RAWD_OFFSET)
        data = fh.read()
    return data

data = read_rawd()
print("Read %d bytes" % len(data))

def vaddr_to_offset(vaddr):
    return vaddr - RAWD_VADDR

def read_u32(vaddr):
    off = vaddr_to_offset(vaddr)
    if off < 0 or off + 4 > len(data):
        return None
    return struct.unpack_from("<I", data, off)[0]

regs = ["zero","at","v0","v1","a0","a1","a2","a3",
        "t0","t1","t2","t3","t4","t5","t6","t7",
        "s0","s1","s2","s3","s4","s5","s6","s7",
        "t8","t9","k0","k1","gp","sp","fp","ra"]

def disasm_instr(vaddr):
    word = read_u32(vaddr)
    if word is None:
        return None, None
    op = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    sa = (word >> 6) & 0x1F
    funct = word & 0x3F
    imm = word & 0xFFFF
    imm_s = imm if imm < 0x8000 else imm - 0x10000
    target = word & 0x3FFFFFF
    instr = None
    jal_target = None
    if op == 0x00:
        if funct == 0x00: instr = "nop"
        elif funct == 0x08: instr = "jr %s" % regs[rs]
        elif funct == 0x09: instr = "jalr %s, %s" % (regs[rd], regs[rs])
        elif funct == 0x0D: instr = "break"
        elif funct == 0x20: instr = "add %s, %s, %s" % (regs[rd], regs[rs], regs[rt])
        elif funct == 0x21: instr = "addu %s, %s, %s" % (regs[rd], regs[rs], regs[rt])
        elif funct == 0x22: instr = "sub %s, %s, %s" % (regs[rd], regs[rs], regs[rt])
        elif funct == 0x23: instr = "subu %s, %s, %s" % (regs[rd], regs[rs], regs[rt])
        elif funct == 0x24: instr = "and %s, %s, %s" % (regs[rd], regs[rs], regs[rt])
        elif funct == 0x25: instr = "or %s, %s, %s" % (regs[rd], regs[rs], regs[rt])
        elif funct == 0x26: instr = "xor %s, %s, %s" % (regs[rd], regs[rs], regs[rt])
        elif funct == 0x27: instr = "nor %s, %s, %s" % (regs[rd], regs[rs], regs[rt])
        elif funct == 0x2A: instr = "slt %s, %s, %s" % (regs[rd], regs[rs], regs[rt])
        elif funct == 0x2B: instr = "sltu %s, %s, %s" % (regs[rd], regs[rs], regs[rt])
        elif funct == 0x00: instr = "sll %s, %s, %d" % (regs[rd], regs[rt], sa)
        elif funct == 0x02: instr = "srl %s, %s, %d" % (regs[rd], regs[rt], sa)
        elif funct == 0x03: instr = "sra %s, %s, %d" % (regs[rd], regs[rt], sa)
        elif funct == 0x04: instr = "sllv %s, %s, %s" % (regs[rd], regs[rt], regs[rs])
        elif funct == 0x06: instr = "srlv %s, %s, %s" % (regs[rd], regs[rt], regs[rs])
        elif funct == 0x07: instr = "srav %s, %s, %s" % (regs[rd], regs[rt], regs[rs])
        elif funct == 0x0C: instr = "syscall"
        elif funct == 0x10: instr = "mfhi %s" % regs[rd]
        elif funct == 0x11: instr = "mthi %s" % regs[rs]
        elif funct == 0x12: instr = "mflo %s" % regs[rd]
        elif funct == 0x13: instr = "mtlo %s" % regs[rs]
        elif funct == 0x18: instr = "mult %s, %s" % (regs[rs], regs[rt])
        elif funct == 0x19: instr = "multu %s, %s" % (regs[rs], regs[rt])
        elif funct == 0x1A: instr = "div %s, %s" % (regs[rs], regs[rt])
        elif funct == 0x1B: instr = "divu %s, %s" % (regs[rs], regs[rt])
        else: instr = "SPECIAL funct=0x%02X" % funct
    elif op == 0x01:
        branch_addr = (vaddr + 4 + imm_s * 4) & 0xFFFFFFFF
        if rt == 0x00: instr = "bltz %s, 0x%08X" % (regs[rs], branch_addr)
        elif rt == 0x01: instr = "bgez %s, 0x%08X" % (regs[rs], branch_addr)
        elif rt == 0x10: instr = "bltzal %s, 0x%08X" % (regs[rs], branch_addr)
        elif rt == 0x11: instr = "bgezal %s, 0x%08X" % (regs[rs], branch_addr)
        else: instr = "REGIMM rt=0x%X" % rt
    elif op == 0x02:
        j_addr = ((vaddr + 4) & 0xF0000000) | (target << 2)
        instr = "j 0x%08X" % j_addr
    elif op == 0x03:
        j_addr = ((vaddr + 4) & 0xF0000000) | (target << 2)
        instr = "jal 0x%08X" % j_addr
        jal_target = j_addr
    elif op == 0x04:
        branch_addr = (vaddr + 4 + imm_s * 4) & 0xFFFFFFFF
        instr = "beq %s, %s, 0x%08X" % (regs[rs], regs[rt], branch_addr)
    elif op == 0x05:
        branch_addr = (vaddr + 4 + imm_s * 4) & 0xFFFFFFFF
        instr = "bne %s, %s, 0x%08X" % (regs[rs], regs[rt], branch_addr)
    elif op == 0x06:
        branch_addr = (vaddr + 4 + imm_s * 4) & 0xFFFFFFFF
        instr = "blez %s, 0x%08X" % (regs[rs], branch_addr)
    elif op == 0x07:
        branch_addr = (vaddr + 4 + imm_s * 4) & 0xFFFFFFFF
        instr = "bgtz %s, 0x%08X" % (regs[rs], branch_addr)
    elif op == 0x08: instr = "addi %s, %s, %d" % (regs[rt], regs[rs], imm_s)
    elif op == 0x09: instr = "addiu %s, %s, %d" % (regs[rt], regs[rs], imm_s)
    elif op == 0x0A: instr = "slti %s, %s, %d" % (regs[rt], regs[rs], imm_s)
    elif op == 0x0B: instr = "sltiu %s, %s, %d" % (regs[rt], regs[rs], imm_s)
    elif op == 0x0C: instr = "andi %s, %s, 0x%04X" % (regs[rt], regs[rs], imm)
    elif op == 0x0D: instr = "ori %s, %s, 0x%04X" % (regs[rt], regs[rs], imm)
    elif op == 0x0E: instr = "xori %s, %s, 0x%04X" % (regs[rt], regs[rs], imm)
    elif op == 0x0F: instr = "lui %s, 0x%04X" % (regs[rt], imm)
    elif op == 0x20: instr = "lb %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x21: instr = "lh %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x22: instr = "lwl %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x23: instr = "lw %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x24: instr = "lbu %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x25: instr = "lhu %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x26: instr = "lwr %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x28: instr = "sb %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x29: instr = "sh %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x2A: instr = "swl %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x2B: instr = "sw %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x2E: instr = "swr %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x32: instr = "lwc0 %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x33: instr = "lwc1 %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x3A: instr = "swc0 %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    elif op == 0x3B: instr = "swc1 %s, %d(%s)" % (regs[rt], imm_s, regs[rs])
    else: instr = "op=0x%02X" % op
    return instr, jal_target

def disassemble(vaddr, count=40):
    jals = []
    for i in range(count):
        addr = vaddr + i * 4
        result = read_u32(addr)
        if result is None:
            print("  [%08X]  OUT OF BOUNDS" % addr)
            continue
        word = result
        instr_text, jt = disasm_instr(addr)
        if instr_text is None:
            print("  [%08X]  %08X  ???" % (addr, word))
        else:
            print("  [%08X]  %08X  %s" % (addr, word, instr_text))
        if jt is not None:
            jals.append((addr, jt))
    return jals

print("=" * 70)
print("FUNCTION 0x80A65B00 - First 40 instructions")
print("=" * 70)
jals_1 = disassemble(0x80A65B00, 40)
print()
print("=" * 70)
print("FUNCTION 0x80A798B4 - First 40 instructions")
print("=" * 70)
jals_2 = disassemble(0x80A798B4, 40)
print()
print("=" * 70)
print("FUNCTION 0x80A06BD8 - First 40 instructions")
print("=" * 70)
jals_3 = disassemble(0x80A06BD8, 40)
print()
print("=" * 70)
print("FUNCTION at 0x80AD8950 (and beyond) - First 60 instructions")
print("=" * 70)
jals_4 = disassemble(0x80AD8950, 60)
print()
print("=" * 70)
print("SUMMARY OF ALL JAL TARGETS")
print("=" * 70)
print()
all_jals = [("0x80A65B00", jals_1), ("0x80A798B4", jals_2),
            ("0x80A06BD8", jals_3), ("0x80AD8950+", jals_4)]
for func_name, jals in all_jals:
    if jals:
        print()
        print("%s:" % func_name)
        for call_addr, target in jals:
            print("  at %08X: jal 0x%08X" % (call_addr, target))
    else:
        print()
        print("%s: (no JAL calls in first 40/60 instructions)" % func_name)
