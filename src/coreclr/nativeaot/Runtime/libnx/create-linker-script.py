#!/usr/bin/env python3
"""Add NativeAOT regions to the installed libnx script, preserving its NRO ABI."""
import argparse, os
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('output', type=Path)
p.add_argument('--source', type=Path, default=Path(os.environ.get('DEVKITPRO','/opt/devkitpro'))/'libnx/switch.ld')
a=p.parse_args()
s=a.source.read_text()
text_anchor='\t\t*(.text .stub .text.* .gnu.linkonce.t.*)'
data_anchor='\t\t*(.data .data.* .gnu.linkonce.d.*)'
eh_anchor='\t.eh_frame          : { KEEP (*(.eh_frame)) *(.eh_frame.*) } :rodata'
for anchor in (text_anchor,data_anchor,eh_anchor):
    if s.count(anchor)!=1:raise SystemExit('Unsupported libnx linker script layout: '+str(a.source))
s=s.replace(text_anchor, text_anchor+'''
        . = ALIGN(16);
        __start___managedcode = .;
        KEEP (*(__managedcode))
        __stop___managedcode = .;
        . = ALIGN(16);
        __start___unbox = .;
        KEEP (*(__unbox))
        __stop___unbox = .;''')
s=s.replace(data_anchor, '''        . = ALIGN(4096);
        KEEP (*(.nx_gscookie))
        . = ALIGN(4096);
        __start___modules = .;
        KEEP (*(__modules))
        __stop___modules = .;
'''+data_anchor)
s=s.replace(eh_anchor, eh_anchor+'''
    PROVIDE_HIDDEN(__eh_frame_start = ADDR(.eh_frame));
    PROVIDE_HIDDEN(__eh_frame_end = ADDR(.eh_frame) + SIZEOF(.eh_frame));''')
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(s)
