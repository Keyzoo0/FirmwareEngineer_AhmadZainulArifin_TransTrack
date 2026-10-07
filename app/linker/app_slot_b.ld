/* Application linked for slot B. The first 512 bytes of the slot hold the
 * image header written by scripts/prepare-firmware.py, so code starts at +0x200. */
_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x800;

MEMORY
{
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  CCMRAM (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
  FLASH  (rx)  : ORIGIN = 0x08080200, LENGTH = 384K - 0x200
}

INCLUDE sections.ld
