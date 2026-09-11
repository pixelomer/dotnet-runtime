/* Supplement libnx's switch.ld; use --eh-frame-hdr and -T for this fragment. */
PROVIDE_HIDDEN(__eh_frame_start = ADDR(.eh_frame));
PROVIDE_HIDDEN(__eh_frame_end = ADDR(.eh_frame) + SIZEOF(.eh_frame));
