import struct

with open('C:/Users/user/src/_opencode_tests/7days/7days.app', 'rb') as f:
    data = f.read()

rawd_start = 0x970
rawd = data[rawd_start:]

# The render functions we found
render_funcs = [0x80A0D870, 0x80A0D914, 0x80A0DA10, 0x80A0DB0C, 0x80A0DBE4, 0x80A0DCB0]
render_func_set = set(render_funcs)

# Search for JALR instructions that target these addresses
# JALR is SPECIAL, func=0x09
# In MIPS, JALR rd, rs: rd = PC+4, jump to rs
# Encoding: 0000 00rs ssss 1111 1rd dddd 0000 0000 1001
# op=0x00, rs=?, rd=31 (usually), func=0x09

print("=== Searching for function pointer dispatch to render functions ===")
print()

# Method 1: Search for LW + JALR pattern where LW loads a render func addr
print("Method 1: LW + JALR pattern loading render function addresses...")
for i in range(0, len(rawd)-8, 4):
    insn1 = struct.unpack_from('<I', rawd, i)[0]
    insn2 = struct.unpack_from('<I', rawd, i+4)[0]
    op1 = insn1 >> 26
    op2 = insn2 >> 26
    
    # Pattern: LW rt, offset(rs) + JALR
    if op1 == 0x23 and op2 == 0x00:  # LW + SPECIAL
        func2 = insn2 & 0x3F
        if func2 == 0x09:  # JALR
            # Get the LW source register and the JALR target register
            lw_rt = (insn1 >> 16) & 0x1F
            lw_rs = (insn1 >> 21) & 0x1F
            jalr_rs = (insn2 >> 21) & 0x1F
            jalr_rd = (insn2 >> 11) & 0x1F
            
            # Check if LW loads into register that JALR calls
            # Also allow for one intervening instruction (e.g., ADDIU, NOP)
            if lw_rt == jalr_rs:
                vaddr = 0x80A00000 + i
                print(f"  LW ${lw_rt},0x{insn1&0xFFFF:X}(${lw_rs}) + JALR at 0x{vaddr:08X}")
                # Print context
                for j in range(-4, 12, 4):
                    if i + j >= 0 and i + j + 3 < len(rawd):
                        ctx_insn = struct.unpack_from('<I', rawd, i + j)[0]
                        ctx_vaddr = 0x80A00000 + i + j
                        marker = " <--" if j >= 0 and j < 8 else ""
                        print(f"    0x{ctx_vaddr:08X}: 0x{ctx_insn:08X}{marker}")
                print()
                break  # Just find first one

# Method 2: Search for JALR instructions near load of vtable entries
print()
print("Method 2: Searching for vtable access patterns...")
# Virtual call: LW t0, 0(this) ; LW t1, offset(t0) ; JALR t1
for i in range(0, len(rawd)-16, 4):
    insns = [struct.unpack_from('<I', rawd, i + j*4)[0] for j in range(5)]
    ops = [insn >> 26 for insn in insns]
    
    # Pattern: LW rd, 0(rs) [load vtable ptr] + LW rd2, offset(rd) [load method] + ... + JALR
    for j in range(3):
        if ops[j] == 0x23:  # LW
            lw_rt = (insns[j] >> 16) & 0x1F
            lw_rs = (insns[j] >> 21) & 0x1F
            lw_imm = insns[j] & 0xFFFF
            if lw_imm == 0:  # Loading from offset 0 = vtable pointer
                for k in range(j+1, min(j+4, 5)):
                    if ops[k] == 0x23:  # Second LW
                        lw2_rt = (insns[k] >> 16) & 0x1F
                        lw2_rs = (insns[k] >> 21) & 0x1F
                        lw2_imm = insns[k] & 0xFFFF
                        if lw2_rs == lw_rt:  # Loading from vtable pointer
                            # Check for JALR within next 2 insns
                            for m in range(k+1, min(k+3, 5)):
                                if ops[m] == 0x00:  # SPECIAL
                                    func = insns[m] & 0x3F
                                    if func == 0x09:  # JALR
                                        jalr_rs = (insns[m] >> 21) & 0x1F
                                        if jalr_rs == lw2_rt:
                                            vaddr = 0x80A00000 + i + j*4
                                            print(f"  Virtual call at 0x{vaddr:08X}")
                                            print(f"    LW vtable_ptr from obj+0 -> LW method+0x{lw2_imm:X} -> JALR")
                                            for n in range(5):
                                                ctx_vaddr = 0x80A00000 + i + n*4
                                                print(f"    0x{ctx_vaddr:08X}: 0x{insns[n]:08X}")
                                            print()
                                            break
                    if ops[k] == 0x00:  # Could be NOP or other SPECIAL
                        continue
                    break

# Method 3: Search for JALR with $ra write (JALR $rd, $rs)
print()
print("Method 3: All JALR instructions with function pointer loading...")
# Find JALR instructions and look back for what loads the function pointer
jalr_locations = []
for i in range(0, len(rawd)-4, 4):
    insn = struct.unpack_from('<I', rawd, i)[0]
    if (insn >> 26) == 0x00:  # SPECIAL
        func = insn & 0x3F
        rs = (insn >> 21) & 0x1F
        rd = (insn >> 11) & 0x1F
        if func == 0x09 and rd == 31:  # JALR $ra, $rs
            # Look back for LW that loads the function pointer into $rs
            for lookback in range(4, 24, 4):
                if i - lookback >= 0:
                    prev_insn = struct.unpack_from('<I', rawd, i - lookback)[0]
                    prev_op = prev_insn >> 26
                    if prev_op == 0x23:  # LW
                        lw_rt = (prev_insn >> 16) & 0x1F
                        if lw_rt == rs:
                            lw_rs = (prev_insn >> 21) & 0x1F
                            lw_imm = prev_insn & 0xFFFF
                            # Check if this looks like a virtual call (LW from this+offset or from global)
                            vaddr = 0x80A00000 + i
                            jalr_locations.append((vaddr, i, lw_rs, lw_imm, rs))
                            break

print(f"  Total JALR with LW lookback: {len(jalr_locations)}")

# Show virtual calls (where LW loads from register = vtable dispatch)
print()
print("  Virtual calls (LW from register-based address):")
for vaddr, i, lw_rs, lw_imm, rs in jalr_locations[:30]:
    # Check if the LW base is likely a vtable/struct pointer (not SP/GP)
    if lw_rs not in [29, 28]:  # Not SP or GP
        # Print context
        print(f"  0x{vaddr:08X}: LW ${rs},0x{lw_imm:X}(${lw_rs}) -> JALR ${rs}")
        for j in range(-8, 0, 4):
            if i + j >= 0:
                ctx_insn = struct.unpack_from('<I', rawd, i + j)[0]
                ctx_vaddr = 0x80A00000 + i + j
                print(f"    {ctx_vaddr:08X}: 0x{ctx_insn:08X}")
        print()
