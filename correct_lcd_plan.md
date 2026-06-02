Plan: Real-OS-Accurate LCD Functions in the Emulator

Step 1: Disassemble \_lcd\_set\_frame (0x8000E280) from ccpmp.bin

We have the binary and know the exact address from the RE export table. Before implementing anything, we need to answer:



Arguments: Is $a0 really just the end pointer? Are there more args in $a1-3?

Return value: What does $v0 contain? (Allocated buffer address? Status?)

Sub-callees: What do 0x80155064 (alloc fb), 0x80155140 (init/clear), 0x801564b0 (memcpy), 0x80157020 (fmt conv) each do?

Global state: What globals does the function read/write (e.g. frame queue head at 0x80242B2C)?

Use the existing analyze\_lcd\_fns2.py framework or a small C++ util to dump the 62 instructions and trace the call graph.



Step 2: Implement Frame Buffer Queue

Replace the current single m\_frame\_addr/m\_frame\_back pair with a proper FIFO queue matching the OS:



struct FrameBuffer {

&#x20;   u32 phys\_addr;    // physical address of pixel data

&#x20;   u32 size;         // typically 153600 (320\*240\*2)

&#x20;   bool in\_use;

};

// Queue with head pointer (matches 0x80242B2C pattern)

std::vector<FrameBuffer> m\_fb\_pool;  // allocation pool

std::deque<u32> m\_fb\_queue;          // ready frames (phys addrs)

allocate\_fb(size) → returns physical addr from pool (or malloc from heap if empty)

release\_fb(phys\_addr) → returns to pool

Step 3: Reimplement impl\_\_lcd\_set\_frame

Based on Step 1's disassembly. The likely pattern (from the RE analysis):



Allocate framebuffer object via allocate\_fb(153600)

Clear it to 0 (or fill with background)

If source pixel data was passed (register or global), blit it line-by-line with format conversion

Push the buffer address onto the frame queue

Update LCD\_DAH register in the emulated hardware state

Return the buffer phys addr in $v0

The current behavior that captures end\_ptr and subtracts FB\_SIZE should be preserved if the game passes end pointers — this is confirmed by the working title CG.



Step 4: Implement DMA Controller Stub for ap\_lcd\_set\_frame

The real function at 0x80007298 writes to 0xB004200C (DMA ch. CMD = 0x24) and 0xB0042004 (CTRL). On the JZ4730, the DMA controller lives at phys 0x10042000.



In memory.cpp, handle writes to the DMA register range:



// In write\_u32/write\_u16, add region:

else if (phys >= 0x10042000 \&\& phys < 0x10042100) {

&#x20;   log\_dma\_write(phys, val);

&#x20;   // Track DMA descriptor state

}

The DMA descriptor is in guest memory — when the game writes to CTRL to start a DMA, read the descriptor from guest RAM and perform the memory copy (source → LCD controller / destination). For ap\_lcd\_set\_frame, the DMA pushes a framebuffer to the LCD panel — in emulator terms, just ensure the pixel data is copied to m\_framebuffer or mark the address as dirty.



Step 5: Wire Hardware Registers in Memory

The RE research gives us the real JZ4740 LCD register map. The current memory.cpp has incorrect register names (lines 189-195) — e.g. +0x10 is labeled LCD\_DAH but the real LCD controller maps LCD\_DAH at +0x30. Update the register map:



Offset	Actual Name	Current (wrong) Label	Fix

+0x00	LCD\_CFG	LCD\_CFG	Keep

+0x04	LCD\_CTRL	LCD\_VSYNC	Fix

+0x0C	LCD\_CFG2	LCD\_VAT	Fix

+0x30	LCD\_DAH	LCD\_CTRL	Fix — this is the framebuffer address register

+0x40	LCD\_DBA	LCD\_DA0	Fix — buffer A

+0x50	LCD\_DBB	LCD\_DA1	Fix — buffer B

Then implement read handlers that return the emulator's tracked addresses.



Step 6: Remove the g\_detected\_fb\_addr Interception Hack

Once the framebuffer queue and \_lcd\_set\_frame are working properly, the LW interception at PC=0x80A21E78 (cpu.cpp:302-311) should become unnecessary — \_lcd\_get\_frame would return the real address from the queue. Remove the hack and verify nothing breaks.



Summary of Effort

Step	Difficulty	Impact	Depends On

1\. Disassemble	Easy (tool exists)	High (guides everything)	Nothing

2\. FB queue	Medium	High	Step 1

3\. Reimpl \_lcd\_set\_frame	Medium	Critical (likely fixes black CG)	Step 1, 2

4\. DMA stub	Medium	Medium (needed if game calls ap\_lcd\_set\_frame)	Nothing

5\. HW regs	Easy	Medium (correctness)	Nothing

6\. Remove hack	Easy	Low	Step 2, 3

Want me to start with Step 1 — dump and analyze the real 0x8000E280 disassembly from ccpmp.bin?


_lcd_set_frame doesn't read pixel format from the LCD controller hardware register. It uses a runtime mirror at 0x80508FC0 (populated during LcdGetDisMode/rgb_user_init) and a palette flag at 0x806A1DDC.

The decision flow (in format handler 0x80157020, called with a0=$s6 from _lcd_set_frame):

Read format width from 0x80508FC0 → compare with stack parameter
Check 0x806A1DDC:
Zero (default) → RGB565 path: calls 0x801591DC → 0x801532E0 (LCD_Color2Index is bypassed), direct pixel copy
Non-zero → Palette/CLUT path: enters a linked list of format objects via 0x8015147c, calls 0x801590A0 then 0x801591DC which does invoke LCD_Color2Index for 8-bit index→RGB conversion
The palette mode source data would be 8-bit indexed (1 BPP), with actual colors resolved via the LCD controller's CLUT at 0xB3050100 (256×4-byte entries). The flag at 0x806A1DDC is set during display init when the mode is configured for palette/indexed color.

The palette flag at 0x806A1DDC and the format mirror at 0x80508FC0 are how the system knows which mode to use — set once during LCD init, checked on every frame operation.