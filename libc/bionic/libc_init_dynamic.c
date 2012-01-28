/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * libc_init_dynamic.c
 *
 * This source files provides two important functions for dynamic
 * executables:
 *
 * - a C runtime initializer (__libc_preinit), which is called by
 *   the dynamic linker when libc.so is loaded. This happens before
 *   any other initializer (e.g. static C++ constructors in other
 *   shared libraries the program depends on).
 *
 * - a program launch function (__libc_init), which is called after
 *   all dynamic linking has been performed. Technically, it is called
 *   from arch-$ARCH/bionic/crtbegin_dynamic.S which is itself called
 *   by the dynamic linker after all libraries have been loaded and
 *   initialized.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <elf.h>
#include "atexit.h"
#include "libc_init_common.h"
#include <bionic_tls.h>

/* We flag the __libc_preinit function as a constructor to ensure
 * that its address is listed in libc.so's .init_array section.
 * This ensures that the function is called by the dynamic linker
 * as soon as the shared library is loaded.
 */
void __attribute__((constructor)) __libc_preinit(void);

#ifdef __mips__
static void call_array(void(**list)())
{
    // First element is -1, list is null-terminated
    while (*++list) {
        (*list)();
    }
}
#endif

void __libc_preinit(void)
{
    /* Read the ELF data pointer from a special slot of the
     * TLS area, then call __libc_init_common with it.
     *
     * Note that:
     * - we clear the slot so no other initializer sees its value.
     * - __libc_init_common() will change the TLS area so the old one
     *   won't be accessible anyway.
     */
    void**      tls_area = (void**)__get_tls();
    unsigned*   elfdata   = tls_area[TLS_SLOT_BIONIC_PREINIT];

    tls_area[TLS_SLOT_BIONIC_PREINIT] = NULL;

    __libc_init_common(elfdata);

    /* Setup malloc routines accordingly to the environment.
     * Requires system properties
     */
    extern void malloc_debug_init(void);
    malloc_debug_init();
}

/* This function is called from the executable's _start entry point
 * (see arch-$ARCH/bionic/crtbegin_dynamic.S), which is itself
 * called by the dynamic linker after it has loaded all shared
 * libraries the executable depends on.
 *
 * Note that the dynamic linker has also run all constructors in the
 * executable at this point.
 */
__noreturn void __libc_init(uintptr_t *elfdata,
                       void (*onexit)(void),
                       int (*slingshot)(int, char**, char**),
                       structors_array_t const * const structors)
{
    int     argc = (int)*elfdata;
    char**  argv = (char**)(elfdata + 1);
    char**  envp = argv + argc + 1;

#ifdef __mips__
    /* .ctors section initializers, for non-arm-eabi ABIs */
    call_array(structors->ctors_array);
#endif

    /* Several Linux ABIs don't pass the onexit pointer, and the ones that
     * do never use it.  Therefore, we ignore it.
     */

    /* The executable may have its own destructors listed in its .fini_array
     * so we need to ensure that these are called when the program exits
     * normally.
     */
    if (structors->fini_array)
        __cxa_atexit(__libc_fini,structors->fini_array,NULL);
#ifdef __mips__
    /* run .dtors section destructors */
    __cxa_atexit(__libc_fini,structors->dtors_array,NULL);
#endif

    exit(slingshot(argc, argv, envp));
}
