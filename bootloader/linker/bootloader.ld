/* Bootloader: sectors 0-1 (32 KB). Must match BOOTLOADER_SIZE in flash_layout.h */
_Min_Heap_Size  = 0x0;
_Min_Stack_Size = 0x1000;

MEMORY
{
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  CCMRAM (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
}

INCLUDE sections.ld
