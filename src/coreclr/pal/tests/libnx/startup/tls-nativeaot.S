#include "../../../../nativeaot/Runtime/unix/unixasmmacros.inc"
LEAF_ENTRY ProbeNativeAotAssemblyTls, _TEXT
    PROLOG_SAVE_REG_PAIR_INDEXED fp, lr, -16
    INLINE_GET_TLS_VAR x9, ProbeTlsValue
    mov x0, x9
    EPILOG_RESTORE_REG_PAIR_INDEXED fp, lr, 16
    EPILOG_RETURN
LEAF_END ProbeNativeAotAssemblyTls, _TEXT
