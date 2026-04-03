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

#include "arch/arm/bare_metal/workload.hh"

#include "arch/arm/bare_metal/process.hh"
#include "arch/arm/faults.hh"
#include "cpu/thread_context.hh"
#include "sim/system.hh"

namespace gem5
{

namespace ArmISA
{

BareMetalWorkload::BareMetalWorkload(const Params &p) : SEWorkload(p)
{}

void
BareMetalWorkload::initState()
{
    SEWorkload::initState();

    // At this point, Process::initState() has already been called
    // (via the SimObject startup sequence) which invoked our
    // ArmBareMetalProcess64::initState() which already set the PC.
    // Only activate threads here; do NOT set PC again.
    for (auto *t : system->threads) {
        if (t->contextId() == 0) {
            t->activate();
        } else {
            t->suspend();
        }
    }
}

} // namespace ArmISA

namespace
{

/**
 * BareMetalLoader — matches ARM64/ARM bare-metal ELFs (no OSABI = SYSV or ELF
 * with OSABI set to something other than Linux/FreeBSD) and instantiates
 * ArmBareMetalProcess instead of ArmProcess64/ArmLinuxProcess64.
 *
 * The loader is registered via static initialization so it participates in
 * Process::tryLoaders().  It only matches when:
 *   - Architecture is Arm64, Arm, or Thumb
 *   - Operating system is Unknown (not Linux / FreeBSD / etc.)
 *   - No ELF interpreter is present (static binaries only)
 */
class BareMetalLoader : public Process::Loader
{
  public:
    Process *
    load(const ProcessParams &params, loader::ObjectFile *obj) override
    {
        auto arch = obj->getArch();
        auto opsys = obj->getOpSys();

        // Only handle ARM architectures
        if (arch != loader::Arm && arch != loader::Thumb &&
            arch != loader::Arm64) {
            return nullptr;
        }

        // Accept bare-metal binaries (UnknownOpSys) AND static UNIX System V
        // ELFs (aarch64-elf-gcc sets OSABI=SYSV even for bare-metal targets).
        // These are static ELFs with no interpreter, so they run without a kernel.
        // Reject only Linux/FreeBSD/etc. where a kernel is expected.
        if (opsys == loader::Linux || opsys == loader::FreeBSD)
            return nullptr;

        // Bare-metal ELF should have no interpreter (static binary)
        if (obj->getInterpreter())
            return nullptr;

        // Create the appropriate ArmBareMetalProcess based on word width
        if (arch == loader::Arm64)
            return new ArmISA::ArmBareMetalProcess64(params, obj, arch);
        else
            return new ArmISA::ArmBareMetalProcess32(params, obj, arch);
    }
};

BareMetalLoader bareMetalLoader;

} // anonymous namespace

} // namespace gem5
