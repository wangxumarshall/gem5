/*
 * Copyright 2026 SDC Fuzzing Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "arch/arm/bare_metal/process.hh"

#include <algorithm>

#include "arch/arm/page_size.hh"
#include "arch/arm/process.hh"
#include "arch/arm/regs/int.hh"
#include "arch/arm/regs/misc.hh"
#include "base/loader/object_file.hh"
#include "base/types.hh"
#include "cpu/thread_context.hh"
#include "mem/se_translating_port_proxy.hh"
#include "sim/process.hh"
#include "sim/system.hh"
#include "sim/mem_state.hh"

namespace gem5
{

namespace ArmISA
{

// =============================================================================
// 32-bit bare-metal process
// =============================================================================

ArmBareMetalProcess32::ArmBareMetalProcess32(
        const ProcessParams &params,
        loader::ObjectFile *objFile, loader::Arch _arch)
    : ArmProcess32(params, objFile, _arch)
{
    // For bare-metal: stack at 0x80000000 (2GB), 64KB — within physical mem
    Addr brk_point = roundUp(image.maxAddr(), PageBytes);
    Addr stack_base = 0x80000000L;
    Addr max_stack_size = 0x10000; // 64KB
    Addr next_thread_stack_base = stack_base - max_stack_size;
    Addr mmap_end = 0x40000000L;

    memState = std::make_shared<MemState>(
            this, brk_point, stack_base, max_stack_size,
            next_thread_stack_base, mmap_end);
}

void
ArmBareMetalProcess32::initState()
{
    // Call Process::initState() to load ELF segments and init page table
    Process::initState();

    // Manually allocate and map stack at 0x80000000 (inside physical mem range)
    allocateMem(0x80000000L, 0x10000, false);

    ThreadContext *tc = system->threads[contextIds[0]];

    // Set SP to top of 64KB stack minus 16-byte alignment padding
    Addr stack_top = 0x80010000L;
    tc->setReg(int_reg::Sp, stack_top);

    // Enable floating point / NEON for AArch32
    CPACR cpacr = tc->readMiscReg(MISCREG_CPACR);
    cpacr.cp10 = 0x3;
    cpacr.cp11 = 0x3;
    tc->setMiscReg(MISCREG_CPACR, cpacr);
    FPEXC fpexc = tc->readMiscReg(MISCREG_FPEXC);
    fpexc.en = 1;
    tc->setMiscReg(MISCREG_FPEXC, fpexc);

    // Activate thread 0, suspend others
    tc->activate();
    for (size_t i = 1; i < contextIds.size(); ++i) {
        system->threads[contextIds[i]]->suspend();
    }
}

uint32_t
ArmBareMetalProcess32::armHwcapImpl() const
{
    // Matches ArmProcess32::armHwcapImpl() values:
    // Arm_Swp(1)|Arm_Half(2)|Arm_Thumb(4)|Arm_FastMult(16)|
    // Arm_Vfp(64)|Arm_Edsp(128)|Arm_Neon(4096)|Arm_Vfpv3(8192)|Arm_Vfpv3d16(16384)
    return 0x3a | (1 << 12) | (1 << 13) | (1 << 14);
}

uint64_t
ArmBareMetalProcess32::armHwcapImpl2() const
{
    return 0;
}

// =============================================================================
// 64-bit bare-metal process
// =============================================================================

ArmBareMetalProcess64::ArmBareMetalProcess64(
        const ProcessParams &params,
        loader::ObjectFile *objFile, loader::Arch _arch)
    : ArmProcess(params, objFile, _arch)
{
    // Bare-metal: stack at 0x80000000 (top 2GB), 64KB
    Addr brk_point = roundUp(image.maxAddr(), PageBytes);
    Addr stack_base = 0x80000000L;
    Addr max_stack_size = 0x10000; // 64KB
    Addr next_thread_stack_base = stack_base - max_stack_size;
    Addr mmap_end = 0x40000000L;

    memState = std::make_shared<MemState>(
            this, brk_point, stack_base, max_stack_size,
            next_thread_stack_base, mmap_end);
}

void
ArmBareMetalProcess64::initState()
{
    ThreadContext *tc = system->threads[contextIds[0]];
    const Addr page_size = pTable->pageSize();
    Addr entry = objFile->entryPoint();

    // ================================================================
    // Bare-metal ELF initialization (extends ArmProcess directly).
    //
    // We bypass ArmProcess64/ArmLinuxProcess64 because argsInit() sets
    // up Linux-style virtual addresses (stack_base = 0x7fffff0000) which are
    // incompatible with bare-metal ELFs that use physical addresses
    // (e.g., entry = 0x4004f0, data = 0x4105a0).
    //
    // Bare-metal memory layout:
    //   Code/data segments: physical load address from ELF (0x400000+)
    //   Stack:              0x80000000 (top 2GB of 4GB physical mem)
    //   CPU page table:     identity-maps both segments and stack
    //
    // SETranslatingPortProxy must be able to write via the page table
    // (for BSS zeroing), so we map BEFORE calling Process::initState().
    // ================================================================

    // Map all ELF segments identity-mapped (vaddr == paddr).
    // For bare-metal ELFs, segments may include NOBITS (uninitialized BSS)
    // padding between file-backed data and BSS.  elf_object.cc sets
    // seg.size = p_filesz (not p_memsz), so we must iterate to find the
    // segment with the highest end address (seg.base + seg.size) to cover
    // the full extent including BSS.  Otherwise pages in the NOBITS gap are
    // unmapped and cause page faults when accessed as BSS.
    Addr min_seg_start = 0;
    Addr max_seg_end = 0;
    for (const auto &seg : image.segments()) {
        if (seg.size == 0)
            continue;
        min_seg_start = min_seg_start ? std::min(min_seg_start, seg.base)
                                      : seg.base;
        max_seg_end = std::max(max_seg_end, seg.base + seg.size);
    }
    if (max_seg_end > min_seg_start) {
        Addr page_start = roundDown(min_seg_start, page_size);
        Addr page_end   = roundUp(max_seg_end, page_size);
        Addr map_size = page_end - page_start;
        pTable->map(page_start, page_start, map_size,
                    EmulationPageTable::Clobber);
    }

    // Map the stack region (64KB at top of 2GB bare-metal memory area).
    Addr stack_base = 0x80000000L;
    Addr max_stack_size = 0x10000;
    pTable->map(stack_base, stack_base, max_stack_size,
                EmulationPageTable::Clobber);

    // Call Process::initState() to load the ELF via SETranslatingPortProxy.
    Process::initState();

    // Set SP to top of 64KB stack minus 16-byte alignment.
    Addr stack_top = stack_base + max_stack_size - 16;
    tc->setReg(int_reg::Sp0, stack_top);

    // Set PC to the bare-metal ELF entry point.
    ArmISA::PCState pc;
    pc.pc(entry);
    pc.aarch64(true);
    pc.nextAArch64(true);
    tc->pcState(pc);

    // Set CPSR to AArch64 EL0 (matching what ArmProcess64::initState does).
    CPSR cpsr = tc->readMiscReg(MISCREG_CPSR);
    cpsr.mode = MODE_EL0T;
    tc->setMiscReg(MISCREG_CPSR, cpsr);
}

uint32_t
ArmBareMetalProcess64::armHwcapImpl() const
{
    // Bare-metal HWCAP: minimal feature set for NEON/FP
    // Arm_Fp(1)|Arm_Asimd(2)|Arm_Atomics(256)|Arm_Fphp(512)|Arm_Asimdhp(1024)|
    // Arm_Fcma(16384)|Arm_Aes(8)|Arm_Pmull(16)|Arm_Sha1(32)|Arm_Sha2(64)|Arm_Crc32(128)
    return 1 | 2 | 256 | 512 | 1024 | 16384 | 8 | 16 | 32 | 64 | 128;
}

uint64_t
ArmBareMetalProcess64::armHwcapImpl2() const
{
    return 0;
}

} // namespace ArmISA

} // namespace gem5
