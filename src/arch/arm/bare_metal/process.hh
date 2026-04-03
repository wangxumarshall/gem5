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

#ifndef __ARCH_ARM_BARE_METAL_PROCESS_HH__
#define __ARCH_ARM_BARE_METAL_PROCESS_HH__

#include "arch/arm/process.hh"

namespace gem5
{

namespace ArmISA
{

/**
 * ARM bare-metal process for SDC fuzzing.
 *
 * BOTH classes (32 and 64-bit) bypass the Linux-style argsInit() which
 * sets up virtual address ranges incompatible with physical-memory ELFs.
 * Instead they:
 *   1. Map ELF segments identity-mapped in the physical address space
 *   2. Map a minimal stack at 0x80000000 (stack_base = max physical mem)
 *   3. Set SP and PC to the bare-metal ELF entry point
 *   4. Enable NEON/FP
 */
class ArmBareMetalProcess32 : public ArmProcess32
{
  public:
    ArmBareMetalProcess32(const ProcessParams &params,
                          loader::ObjectFile *objFile, loader::Arch _arch);

  protected:
    void initState() override;

    uint32_t armHwcapImpl() const override;
    uint64_t armHwcapImpl2() const override;
};

/**
 * Extends ArmProcess directly (NOT ArmProcess64) to avoid Linux-style
 * argsInit() which sets up incompatible virtual addresses for bare-metal.
 * Reimplements the minimal bare-metal init: load ELF, map segments and
 * stack, set SP/PC, enable FP.
 */
class ArmBareMetalProcess64 : public ArmProcess
{
  public:
    ArmBareMetalProcess64(const ProcessParams &params,
                          loader::ObjectFile *objFile, loader::Arch _arch);

  protected:
    void initState() override;

    uint32_t armHwcapImpl() const override;
    uint64_t armHwcapImpl2() const override;
};

} // namespace ArmISA

} // namespace gem5

#endif // __ARCH_ARM_BARE_METAL_PROCESS_HH__