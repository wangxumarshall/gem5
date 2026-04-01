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
 * Unlike ArmProcess64 which sets Linux-style virtual addresses
 * (stack_base = 0x7fffff0000L), this class maps the stack into the
 * physical memory range (0x80000000) to work with bare-metal ELFs
 * that have no MMU/memory mapping support.
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

class ArmBareMetalProcess64 : public ArmProcess64
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