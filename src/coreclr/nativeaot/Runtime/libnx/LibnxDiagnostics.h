#pragma once
// Optional native-only startup sink. It must not call managed code.
extern "C" void LibnxRuntimeDiagnostic(const char* stage) __attribute__((weak));
inline void LibnxTraceStartup(const char* stage)
{
    if (LibnxRuntimeDiagnostic) LibnxRuntimeDiagnostic(stage);
}
