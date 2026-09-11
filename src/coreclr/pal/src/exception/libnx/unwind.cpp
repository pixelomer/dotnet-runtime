
#include "pal.h"
#include "pal/libnx/unwind.h"
#include <external/llvm-libunwind/src/AddressSpace.hpp>
#include <external/llvm-libunwind/src/DwarfInstructions.hpp>
#include <external/llvm-libunwind/src/EHHeaderParser.hpp>

namespace
{
// The .NET-vendored DWARF interpreter already carries an integer register's
// saved location. Treat the low 64 bits of D registers as raw register bits as
// well: AAPCS64 preserves only D8-D15, and this retains their homes without
// floating-point conversions or changing the shared LLVM float interface.
struct PalRegisters
{
    CONTEXT value;
    KNONVOLATILE_CONTEXT_POINTERS homes{};
    static int getArch() { return libunwind::REGISTERS_ARM64; }
    static constexpr int lastDwarfRegNum() { return _LIBUNWIND_HIGHEST_DWARF_REGISTER_ARM64; }
    bool validRegister(int reg) const
    {
        return (reg >= UNW_ARM64_X0 && reg <= UNW_ARM64_SP) ||
               (reg >= UNW_ARM64_D0 && reg <= UNW_ARM64_D31) || reg == UNW_REG_IP || reg == UNW_REG_SP;
    }
    bool validFloatRegister(int) const { return false; }
    bool validVectorRegister(int) const { return false; }
    uint64_t getRegister(int reg) const
    {
        if (reg >= UNW_ARM64_X0 && reg <= UNW_ARM64_X28) return value.X[reg];
        if (reg >= UNW_ARM64_D0 && reg <= UNW_ARM64_D31) return value.V[reg - UNW_ARM64_D0].Low;
        switch (reg)
        {
            case UNW_ARM64_FP: return value.Fp;
            case UNW_ARM64_LR: return value.Lr;
            case UNW_ARM64_SP: case UNW_REG_SP: return value.Sp;
            case UNW_REG_IP: return value.Pc;
            default: abort();
        }
    }
    void setRegister(int reg, uint64_t bits, uint64_t location)
    {
        if (reg >= UNW_ARM64_X0 && reg <= UNW_ARM64_X28) value.X[reg] = bits;
        else if (reg >= UNW_ARM64_D0 && reg <= UNW_ARM64_D31) value.V[reg - UNW_ARM64_D0].Low = bits;
        else switch (reg)
        {
            case UNW_ARM64_FP: value.Fp = bits; break;
            case UNW_ARM64_LR: value.Lr = bits; break;
            case UNW_ARM64_SP: case UNW_REG_SP: value.Sp = bits; break;
            case UNW_REG_IP: value.Pc = bits; break;
            default: abort();
        }
        PDWORD64 home = reinterpret_cast<PDWORD64>(location);
        // Name each member instead of indexing across distinct C++ members.
        switch (reg)
        {
#define HOME(regName, member) case regName: homes.member = home; break
            HOME(UNW_ARM64_X19, X19); HOME(UNW_ARM64_X20, X20);
            HOME(UNW_ARM64_X21, X21); HOME(UNW_ARM64_X22, X22);
            HOME(UNW_ARM64_X23, X23); HOME(UNW_ARM64_X24, X24);
            HOME(UNW_ARM64_X25, X25); HOME(UNW_ARM64_X26, X26);
            HOME(UNW_ARM64_X27, X27); HOME(UNW_ARM64_X28, X28);
            HOME(UNW_ARM64_FP, Fp); HOME(UNW_ARM64_LR, Lr);
            HOME(UNW_ARM64_D8, D8); HOME(UNW_ARM64_D9, D9);
            HOME(UNW_ARM64_D10, D10); HOME(UNW_ARM64_D11, D11);
            HOME(UNW_ARM64_D12, D12); HOME(UNW_ARM64_D13, D13);
            HOME(UNW_ARM64_D14, D14); HOME(UNW_ARM64_D15, D15);
#undef HOME
        }
    }
    uint64_t getSP() const { return value.Sp; }
    uint64_t getIP() const { return value.Pc; }
    void setSP(uint64_t bits, uint64_t location) { setRegister(UNW_REG_SP, bits, location); }
    void setIP(uint64_t bits, uint64_t location) { setRegister(UNW_REG_IP, bits, location); }
    double getFloatRegister(int) const { abort(); }
    void setFloatRegister(int, double) { abort(); }
    libunwind::v128 getVectorRegister(int) const { abort(); }
    void setVectorRegister(int, libunwind::v128) { abort(); }
};
}

BOOL CorUnix::LibnxUnwindFrame(CONTEXT* context, KNONVOLATILE_CONTEXT_POINTERS* contextPointers,
                             const LibnxUnwindSections& sections)
{
    if (context == nullptr || context->Pc == 0 || sections.codeStart >= sections.codeEnd ||
        sections.ehFrame == 0 || sections.ehFrameSize == 0 ||
        sections.ehFrameSize > UINTPTR_MAX - sections.ehFrame ||
        sections.ehFrameHeaderSize > UINT32_MAX ||
        sections.ehFrameHeaderSize > UINTPTR_MAX - sections.ehFrameHeader)
        return FALSE;

    // Ordinary contexts hold a return address. Fault contexts hold the exact
    // instruction, including possibly the first instruction in a function.
    uintptr_t pc = context->Pc - ((context->ContextFlags & CONTEXT_EXCEPTION_ACTIVE) ? 0 : 1);
    if (pc < sections.codeStart || pc >= sections.codeEnd) return FALSE;

    using AddressSpace = libunwind::LocalAddressSpace;
    using Parser = libunwind::CFI_Parser<AddressSpace>;
    AddressSpace addressSpace;
    Parser::FDE_Info fde;
    Parser::CIE_Info cie;
    bool found = false;
    if (sections.ehFrameHeader != 0 && sections.ehFrameHeaderSize != 0)
        found = libunwind::EHHeaderParser<AddressSpace>::findFDE(addressSpace, pc,
            sections.ehFrameHeader, static_cast<uint32_t>(sections.ehFrameHeaderSize), &fde, &cie);
    if (!found)
        found = Parser::findFDE(addressSpace, pc, sections.ehFrame, sections.ehFrameSize, 0, &fde, &cie);
    if (!found) return FALSE;

    PalRegisters registers;
    registers.value = *context;
    bool signalFrame = false;
    int result = libunwind::DwarfInstructions<AddressSpace, PalRegisters>::stepWithDwarf(
        addressSpace, pc, fde.fdeStart, registers, signalFrame, false);
    if (result != UNW_STEP_SUCCESS) return FALSE;

    if (registers.value.Pc == context->Pc) registers.value.Pc = 0;
    if (signalFrame)
    {
        registers.value.ContextFlags |= CONTEXT_EXCEPTION_ACTIVE;
        registers.value.ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
    }
    else
    {
        registers.value.ContextFlags &= ~CONTEXT_EXCEPTION_ACTIVE;
        registers.value.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    }
    *context = registers.value;
    if (contextPointers != nullptr) *contextPointers = registers.homes;
    return TRUE;
}

// The first embedding target is a statically linked NRO. A native module loader
// must supply the corresponding image's sections to LibnxUnwindFrame; unknown
// code addresses fail instead of being treated as stack-frame-pointer chains.
extern "C" char __code_start[], __rodata_start[], __eh_frame_start[], __eh_frame_end[];
extern "C" char __eh_frame_hdr_start[], __eh_frame_hdr_end[];
BOOL CorUnix::LibnxUnwindNativeFrame(CONTEXT* context, KNONVOLATILE_CONTEXT_POINTERS* pointers)
{
    const LibnxUnwindSections sections = {
        reinterpret_cast<uintptr_t>(__code_start), reinterpret_cast<uintptr_t>(__rodata_start),
        reinterpret_cast<uintptr_t>(__eh_frame_start),
        static_cast<size_t>(__eh_frame_end - __eh_frame_start),
        reinterpret_cast<uintptr_t>(__eh_frame_hdr_start),
        static_cast<size_t>(__eh_frame_hdr_end - __eh_frame_hdr_start)
    };
    return LibnxUnwindFrame(context, pointers, sections);
}
