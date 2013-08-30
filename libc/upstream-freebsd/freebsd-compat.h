/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _BIONIC_FREEBSD_COMPAT_H_included
#define _BIONIC_FREEBSD_COMPAT_H_included

#define __USE_BSD
#define REPLACE_GETOPT

#define _close close
#define _fcntl fcntl
#define _fstat fstat
#define _open open

#define _sseek __sseek /* Needed as long as we have a mix of OpenBSD and FreeBSD stdio. */

/*
 * The following serve a reverse function of the upstream FreeBSD namespace.h definitions.
 * Needed as long as we have a mix of OpenBSD and FreeBSD stdlib.
 */
#define _pthread_mutex_destroy pthread_mutex_destroy
#define _pthread_mutex_lock pthread_mutex_lock
#define _pthread_mutex_unlock pthread_mutex_unlock

/* Needed to workaround lack of upstream dlfcn support. */
#pragma weak _rtld_addr_phdr
int _rtld_addr_phdr(const void *addr, struct dl_phdr_info *phdr_info)
{

        return (0);
}

/*
 * Needed to workaround lack of upstream elf utils and dependent support.
 * TODO: implement full upstream elf utils support
 */
#pragma weak __elf_phdr_match_addr
int __elf_phdr_match_addr(struct dl_phdr_info *phdr_info, void *addr)
{
        return (1);
}

#endif
