/* Memory layout for the QEMU "mps2-an385" machine (Cortex-M3).
 *
 * Used only by the picolibc semihosting link+run CI check
 * (tests/embedded/semihost_smoke.cpp). Picolibc's picolibc.ld consumes these
 * __flash/__ram/__stack_size symbols and provides the full section layout,
 * vector table and crt0. Regions are generously sized so a small C++ program
 * that pulls in printf comfortably fits; the mps2-an385 has far more memory.
 */

__flash      = 0x00000000;
__flash_size = 0x00400000; /* 4 MiB */
__ram        = 0x20000000;
__ram_size   = 0x00400000; /* 4 MiB */
__stack_size = 0x00001000; /* 4 KiB */

INCLUDE picolibc.ld
