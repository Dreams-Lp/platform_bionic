/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include <sys/types.h>
#include <sys/atomics.h>
#include <sys/system_properties.h>
#include <sys/mman.h>

#if HAVE_DLADDR
#include <dlfcn.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <unwind.h>
#include <unistd.h>

#include "logd.h"
#include "bionic_tls.h"

/*
 * ===========================================================================
 *      Deadlock prediction
 * ===========================================================================
 */
/*
The idea is to predict the possibility of deadlock by recording the order
in which locks are acquired.  If we see an attempt to acquire a lock
out of order, we can identify the locks and offending code.

To make this work, we need to keep track of the locks held by each thread,
and create history trees for each lock.  When a thread tries to acquire
a new lock, we walk through the "history children" of the lock, looking
for a match with locks the thread already holds.  If we find a match,
it means the thread has made a request that could result in a deadlock.

To support recursive locks, we always allow re-locking a currently-held
lock, and maintain a recursion depth count.

An ASCII-art example, where letters represent locks:

        A
       /|\
      / | \
     B  |  D
      \ |
       \|
        C

The above is the tree we'd have after handling lock synchronization
sequences "ABC", "AC", "AD".  A has three children, {B, C, D}.  C is also
a child of B.  (The lines represent pointers between parent and child.
Every node can have multiple parents and multiple children.)

If we hold AC, and want to lock B, we recursively search through B's
children to see if A or C appears.  It does, so we reject the attempt.
(A straightforward way to implement it: add a link from C to B, then
determine whether the graph starting at B contains a cycle.)

If we hold AC and want to lock D, we would succeed, creating a new link
from C to D.

Updates to MutexInfo structs are only allowed for the thread that holds
the lock, so we actually do most of our deadlock prediction work after
the lock has been acquired.
*/

// =============================================================================
// log functions
// =============================================================================

#define LOGD(format, ...)  \
    __libc_android_log_print(ANDROID_LOG_DEBUG, \
            "pthread_debug", (format), ##__VA_ARGS__ )

#define LOGW(format, ...)  \
    __libc_android_log_print(ANDROID_LOG_WARN, \
            "pthread_debug", (format), ##__VA_ARGS__ )

#define LOGE(format, ...)  \
    __libc_android_log_print(ANDROID_LOG_ERROR, \
            "pthread_debug", (format), ##__VA_ARGS__ )

#define LOGI(format, ...)  \
    __libc_android_log_print(ANDROID_LOG_INFO, \
            "pthread_debug", (format), ##__VA_ARGS__ )

static const char* const kStartBanner =
        "===============================================================";

static const char* const kEndBanner =
        "===============================================================";

extern char* __progname;

// =============================================================================
// map info functions
// =============================================================================

typedef struct mapinfo {
    struct mapinfo *next;
    unsigned start;
    unsigned end;
    char name[];
} mapinfo;

static mapinfo* sMapInfo = NULL;

static mapinfo *parse_maps_line(char *line)
{
    mapinfo *mi;
    int len = strlen(line);

    if(len < 1) return 0;
    line[--len] = 0;

    if(len < 50) return 0;
    if(line[20] != 'x') return 0;

    mi = malloc(sizeof(mapinfo) + (len - 47));
    if(mi == 0) return 0;

    mi->start = strtoul(line, 0, 16);
    mi->end = strtoul(line + 9, 0, 16);
    /* To be filled in parse_elf_info if the mapped section starts with
     * elf_header
     */
    mi->next = 0;
    strcpy(mi->name, line + 49);

    return mi;
}

static mapinfo *init_mapinfo(int pid)
{
    struct mapinfo *milist = NULL;
    char data[1024];
    sprintf(data, "/proc/%d/maps", pid);
    FILE *fp = fopen(data, "r");
    if(fp) {
        while(fgets(data, sizeof(data), fp)) {
            mapinfo *mi = parse_maps_line(data);
            if(mi) {
                mi->next = milist;
                milist = mi;
            }
        }
        fclose(fp);
    }

    return milist;
}

static void deinit_mapinfo(mapinfo *mi)
{
   mapinfo *del;
   while(mi) {
       del = mi;
       mi = mi->next;
       free(del);
   }
}

/* Find the containing map info for the pc */
static const mapinfo *pc_to_mapinfo(mapinfo *mi, unsigned pc, unsigned *rel_pc)
{
    *rel_pc = pc;
    while(mi) {
        if((pc >= mi->start) && (pc < mi->end)){
            // Only calculate the relative offset for shared libraries
            if (strstr(mi->name, ".so")) {
                *rel_pc -= mi->start;
            }
            return mi;
        }
        mi = mi->next;
    }
    return NULL;
}

// =============================================================================
// stack trace functions
// =============================================================================

#define STACK_TRACE_DEPTH 16

typedef struct
{
    size_t count;
    intptr_t* addrs;
} stack_crawl_state_t;

/* depends how the system includes define this */
#ifdef HAVE_UNWIND_CONTEXT_STRUCT
typedef struct _Unwind_Context __unwind_context;
#else
typedef _Unwind_Context __unwind_context;
#endif

static _Unwind_Reason_Code trace_function(__unwind_context *context, void *arg)
{
    stack_crawl_state_t* state = (stack_crawl_state_t*)arg;
    if (state->count) {
        intptr_t ip = (intptr_t)_Unwind_GetIP(context);
        if (ip) {
            state->addrs[0] = ip;
            state->addrs++;
            state->count--;
            return _URC_NO_REASON;
        }
    }
    /*
     * If we run out of space to record the address or 0 has been seen, stop
     * unwinding the stack.
     */
    return _URC_END_OF_STACK;
}

static inline
int get_backtrace(intptr_t* addrs, size_t max_entries)
{
    stack_crawl_state_t state;
    state.count = max_entries;
    state.addrs = (intptr_t*)addrs;
    _Unwind_Backtrace(trace_function, (void*)&state);
    return max_entries - state.count;
}

static void log_backtrace(intptr_t* addrs, size_t c)
{
    int index = 0;
    size_t i;
    for (i=0 ; i<c; i++) {
        unsigned int relpc;
        void* offset = 0;
        const char* symbol = NULL;

#if HAVE_DLADDR
        Dl_info info;
        if (dladdr((void*)addrs[i], &info)) {
            offset = info.dli_saddr;
            symbol = info.dli_sname;
        }
#endif

        if (symbol || index>0 || !HAVE_DLADDR) {
            /*
             * this test is a bit sketchy, but it allows us to skip the
             * stack trace entries due to this debugging code. it works
             * because those don't have a symbol (they're not exported)
             */
            mapinfo const* mi = pc_to_mapinfo(sMapInfo, addrs[i], &relpc);
            char const* soname = mi ? mi->name : NULL;
#if HAVE_DLADDR
            if (!soname)
                soname = info.dli_fname;
#endif
            if (!soname)
                soname = "unknown";

            if (symbol) {
                LOGW("          "
                     "#%02d  pc %08lx  %s (%s+0x%x)",
                     index, relpc, soname, symbol,
                     addrs[i] - (intptr_t)offset);
            } else {
                LOGW("          "
                     "#%02d  pc %08lx  %s",
                     index, relpc, soname);
            }
            index++;
        }
    }
}

/****************************************************************************/

/*
 * level <= 0 : deadlock prediction disabled
 * level    1 : deadlock prediction enabled, w/o call stacks
 * level    2 : deadlock prediction enabled w/ call stacks
 */
#define CAPTURE_CALLSTACK 2
static int sPthreadDebugLevel = 0;
static pid_t sPthreadDebugDisabledThread = -1;
static pthread_mutex_t sDbgLock = PTHREAD_MUTEX_INITIALIZER;

/****************************************************************************/

/* some simple/lame malloc replacement
 * NOT thread-safe and leaks everything
 */

#define DBG_ALLOC_BLOCK_SIZE PAGESIZE
static size_t sDbgAllocOffset = DBG_ALLOC_BLOCK_SIZE;
static char* sDbgAllocPtr = NULL;

static void* DbgAllocLocked(size_t size) {
    if ((sDbgAllocOffset + size) > DBG_ALLOC_BLOCK_SIZE) {
        sDbgAllocOffset = 0;
        sDbgAllocPtr = mmap(NULL, DBG_ALLOC_BLOCK_SIZE, PROT_READ|PROT_WRITE,
                MAP_ANON | MAP_PRIVATE, 0, 0);
        if (sDbgAllocPtr == MAP_FAILED) {
            return NULL;
        }
    }
    void* addr = sDbgAllocPtr + sDbgAllocOffset;
    sDbgAllocOffset += size;
    return addr;
}

static void* debug_realloc(void *ptr, size_t size, size_t old_size) {
    void* addr = mmap(NULL, size, PROT_READ|PROT_WRITE,
            MAP_ANON | MAP_PRIVATE, 0, 0);
    if (addr != MAP_FAILED) {
        if (ptr) {
            memcpy(addr, ptr, old_size);
            munmap(ptr, old_size);
        }
    } else {
        addr = NULL;
    }
    return addr;
}

/*****************************************************************************/

struct MutexInfo;

typedef struct CallStack {
    intptr_t    depth;
    intptr_t*   addrs;
} CallStack;

typedef struct MutexInfo* MutexInfoListEntry;
typedef struct CallStack  CallStackListEntry;

typedef struct GrowingList {
    int alloc;
    int count;
    union {
        void*               data;
        MutexInfoListEntry* list;
        CallStackListEntry* stack;
    };
} GrowingList;

typedef GrowingList MutexInfoList;
typedef GrowingList CallStackList;

typedef struct MutexInfo {
    // thread currently holding the lock or 0
    pid_t               owner;

    // most-recently-locked doubly-linked list
    struct MutexInfo*   prev;
    struct MutexInfo*   next;

    // for reentrant locks
    int                 lockCount;
    // when looking for loops in the graph, marks visited nodes
    int                 historyMark;
    // the actual mutex
    pthread_mutex_t*    mutex;
    // list of locks directly acquired AFTER this one in the same thread
    MutexInfoList       children;
    // list of locks directly acquired BEFORE this one in the same thread
    MutexInfoList       parents;
    // list of call stacks when a new link is established to this lock form its parent
    CallStackList       stacks;
    // call stack when this lock was acquired last
    int                 stackDepth;
    intptr_t            stackTrace[STACK_TRACE_DEPTH];
} MutexInfo;

static void growingListInit(GrowingList* list) {
    list->alloc = 0;
    list->count = 0;
    list->data = NULL;
}

static void growingListAdd(GrowingList* pList, size_t objSize) {
    if (pList->count == pList->alloc) {
        size_t oldsize = pList->alloc * objSize;
        pList->alloc += PAGESIZE / objSize;
        size_t size = pList->alloc * objSize;
        pList->data = debug_realloc(pList->data, size, oldsize);
    }
    pList->count++;
}

static void initMutexInfo(MutexInfo* object, pthread_mutex_t* mutex) {
    object->owner = 0;
    object->prev = 0;
    object->next = 0;
    object->lockCount = 0;
    object->historyMark = 0;
    object->mutex = mutex;
    growingListInit(&object->children);
    growingListInit(&object->parents);
    growingListInit(&object->stacks);
    object->stackDepth = 0;
}

typedef struct ThreadInfo {
    pid_t       pid;
    MutexInfo*  mrl;
} ThreadInfo;

static void initThreadInfo(ThreadInfo* object, pid_t pid) {
    object->pid = pid;
    object->mrl = NULL;
}

/****************************************************************************/

static MutexInfo* get_mutex_info(pthread_mutex_t *mutex);
static void mutex_lock_checked(MutexInfo* mrl, MutexInfo* object);
static void mutex_unlock_checked(MutexInfo* object);

/****************************************************************************/

extern int pthread_mutex_lock_impl(pthread_mutex_t *mutex);
extern int pthread_mutex_unlock_impl(pthread_mutex_t *mutex);

static int pthread_mutex_lock_unchecked(pthread_mutex_t *mutex) {
    return pthread_mutex_lock_impl(mutex);
}

static int pthread_mutex_unlock_unchecked(pthread_mutex_t *mutex) {
    return pthread_mutex_unlock_impl(mutex);
}

/****************************************************************************/

static void dup_backtrace(CallStack* stack, int count, intptr_t const* addrs) {
    stack->depth = count;
    stack->addrs = DbgAllocLocked(count * sizeof(intptr_t));
    memcpy(stack->addrs, addrs, count * sizeof(intptr_t));
}

/****************************************************************************/

static int historyListHas(
        const MutexInfoList* list, MutexInfo const * obj) {
    int i;
    for (i=0; i<list->count; i++) {
        if (list->list[i] == obj) {
            return i;
        }
    }
    return -1;
}

static void historyListAdd(MutexInfoList* pList, MutexInfo* obj) {
    growingListAdd(pList, sizeof(MutexInfoListEntry));
    pList->list[pList->count - 1] = obj;
}

static int historyListRemove(MutexInfoList* pList, MutexInfo* obj) {
    int i;
    for (i = pList->count-1; i >= 0; i--) {
        if (pList->list[i] == obj) {
            break;
        }
    }
    if (i < 0) {
        // not found!
        return 0;
    }

    if (i != pList->count-1) {
        // copy the last entry to the new free slot
        pList->list[i] = pList->list[pList->count-1];
    }
    pList->count--;
    memset(&pList->list[pList->count], 0, sizeof(MutexInfoListEntry));
    return 1;
}

static void linkParentToChild(MutexInfo* parent, MutexInfo* child) {
    historyListAdd(&parent->children, child);
    historyListAdd(&child->parents, parent);
}

static void unlinkParentFromChild(MutexInfo* parent, MutexInfo* child) {
    historyListRemove(&parent->children, child);
    historyListRemove(&child->parents, parent);
}

/****************************************************************************/

static void callstackListAdd(CallStackList* pList,
        int count, intptr_t const* addrs) {
    growingListAdd(pList, sizeof(CallStackListEntry));
    dup_backtrace(&pList->stack[pList->count - 1], count, addrs);
}

/****************************************************************************/

/*
 * Recursively traverse the object hierarchy starting at "obj".  We mark
 * ourselves on entry and clear the mark on exit.  If we ever encounter
 * a marked object, we have a cycle.
 *
 * Returns "true" if all is well, "false" if we found a cycle.
 */

static int traverseTree(MutexInfo* obj, MutexInfo const* objParent)
{
    /*
     * Have we been here before?
     */
    if (obj->historyMark) {
        int stackDepth;
        intptr_t addrs[STACK_TRACE_DEPTH];

        /* Turn off prediction temporarily in this thread while logging */
        sPthreadDebugDisabledThread = gettid();

        if (sMapInfo == NULL) {
            // note: we're protected by sDbgLock
            sMapInfo = init_mapinfo(getpid());
        }

        LOGW("%s\n", kStartBanner);
        LOGW("pid: %d, tid: %d >>> %s <<<", getpid(), gettid(), __progname);
        LOGW("Illegal lock attempt:\n");
        LOGW("--- pthread_mutex_t at %p\n", obj->mutex);
        stackDepth = get_backtrace(addrs, STACK_TRACE_DEPTH);
        log_backtrace(addrs, stackDepth);

        LOGW("+++ Currently held locks in this thread (in reverse order):");
        MutexInfo* cur = obj;
        pid_t ourtid = gettid();
        int i;
        for (i=0 ; i<cur->parents.count ; i++) {
            MutexInfo* parent = cur->parents.list[i];
            if (parent->owner == ourtid) {
                LOGW("--- pthread_mutex_t at %p\n", parent->mutex);
                if (sPthreadDebugLevel >= CAPTURE_CALLSTACK) {
                    log_backtrace(parent->stackTrace, parent->stackDepth);
                }
                cur = parent;
                break;
            }
        }

        LOGW("+++ Earlier, the following lock order (from last to first) was established\n");
        return 0;
    }

    obj->historyMark = 1;

    MutexInfoList* pList = &obj->children;
    int result = 1;
    int i;
    for (i = pList->count-1; i >= 0; i--) {
        MutexInfo* child = pList->list[i];
        if (!traverseTree(child,  obj)) {
            LOGW("--- pthread_mutex_t at %p\n", obj->mutex);
            if (sPthreadDebugLevel >= CAPTURE_CALLSTACK) {
                int index = historyListHas(&obj->parents, objParent);
                if ((size_t)index < (size_t)obj->stacks.count) {
                    log_backtrace(
                            obj->stacks.stack[index].addrs,
                            obj->stacks.stack[index].depth);
                } else {
                    log_backtrace(
                            obj->stackTrace,
                            obj->stackDepth);
                }
            }
            result = 0;
            break;
        }
    }

    obj->historyMark = 0;
    return result;
}

/****************************************************************************/

static void mutex_lock_checked(MutexInfo* mrl, MutexInfo* object)
{
    pid_t tid = gettid();
    if (object->owner == tid) {
        object->lockCount++;
        return;
    }

    object->owner = tid;
    object->lockCount = 0;

    if (sPthreadDebugLevel >= CAPTURE_CALLSTACK) {
        // always record the call stack when acquiring a lock.
        // it's not efficient, but is useful during diagnostics
        object->stackDepth = get_backtrace(object->stackTrace, STACK_TRACE_DEPTH);
    }

    // no other locks held in this thread -- no deadlock possible!
    if (mrl == NULL)
        return;

    // check if the lock we're trying to acquire is a direct descendant of
    // the most recently locked mutex in this thread, in which case we're
    // in a good situation -- no deadlock possible
    if (historyListHas(&mrl->children, object) >= 0)
        return;

    pthread_mutex_lock_unchecked(&sDbgLock);

    linkParentToChild(mrl, object);
    if (!traverseTree(object, mrl)) {
        deinit_mapinfo(sMapInfo);
        sMapInfo = NULL;
        LOGW("%s\n", kEndBanner);
        unlinkParentFromChild(mrl, object);
        // reenable pthread debugging for this thread
        sPthreadDebugDisabledThread = -1;
    } else {
        // record the call stack for this link
        // NOTE: the call stack is added at the same index
        // as mrl in object->parents[]
        // ie: object->parents.count == object->stacks.count, which is
        // also the index.
        if (sPthreadDebugLevel >= CAPTURE_CALLSTACK) {
            callstackListAdd(&object->stacks,
                    object->stackDepth, object->stackTrace);
        }
    }

    pthread_mutex_unlock_unchecked(&sDbgLock);
}

static void mutex_unlock_checked(MutexInfo* object)
{
    pid_t tid = gettid();
    if (object->owner == tid) {
        if (object->lockCount == 0) {
            object->owner = 0;
        } else {
            object->lockCount--;
        }
    }
}


// =============================================================================
// Hash Table functions
// =============================================================================

/****************************************************************************/

#define HASHTABLE_SIZE      256

typedef struct HashEntry HashEntry;
struct HashEntry {
    size_t slot;
    HashEntry* prev;
    HashEntry* next;
    void* data;
};

typedef struct HashTable HashTable;
struct HashTable {
    HashEntry* slots[HASHTABLE_SIZE];
};

static HashTable sMutexMap;
static HashTable sThreadMap;

/****************************************************************************/

static uint32_t get_hashcode(void const * key, size_t keySize)
{
    uint32_t h = keySize;
    char const* data = (char const*)key;
    size_t i;
    for (i = 0; i < keySize; i++) {
        h = h * 31 + *data;
        data++;
    }
    return (uint32_t)h;
}

static size_t get_index(uint32_t h)
{
    // We apply this secondary hashing discovered by Doug Lea to defend
    // against bad hashes.
    h += ~(h << 9);
    h ^= (((unsigned int) h) >> 14);
    h += (h << 4);
    h ^= (((unsigned int) h) >> 10);
    return (size_t)h & (HASHTABLE_SIZE - 1);
}

/****************************************************************************/

static void hashmap_init(HashTable* table) {
    memset(table, 0, sizeof(HashTable));
}

static HashEntry* hashmap_lookup(HashTable* table,
        void const* key, size_t ksize,
        int (*equals)(void const* data, void const* key))
{
    const uint32_t hash = get_hashcode(key, ksize);
    const size_t slot = get_index(hash);

    HashEntry* entry = table->slots[slot];
    while (entry) {
        if (equals(entry->data, key)) {
            break;
        }
        entry = entry->next;
    }

    if (entry == NULL) {
        // create a new entry
        entry = (HashEntry*)DbgAllocLocked(sizeof(HashEntry));
        entry->data = NULL;
        entry->slot = slot;
        entry->prev = NULL;
        entry->next = table->slots[slot];
        if (entry->next != NULL) {
            entry->next->prev = entry;
        }
        table->slots[slot] = entry;
    }
    return entry;
}

/****************************************************************************/

static int MutexInfo_equals(void const* data, void const* key) {
    return ((MutexInfo const *)data)->mutex == *(pthread_mutex_t **)key;
}

static MutexInfo* get_mutex_info(pthread_mutex_t *mutex)
{
    pthread_mutex_lock_unchecked(&sDbgLock);

    HashEntry* entry = hashmap_lookup(&sMutexMap,
            &mutex, sizeof(mutex),
            &MutexInfo_equals);
    if (entry->data == NULL) {
        entry->data = (MutexInfo*)DbgAllocLocked(sizeof(MutexInfo));
        initMutexInfo(entry->data, mutex);
    }

    pthread_mutex_unlock_unchecked(&sDbgLock);

    return (MutexInfo *)entry->data;
}

/****************************************************************************/

static int ThreadInfo_equals(void const* data, void const* key) {
    return ((ThreadInfo const *)data)->pid == *(pid_t *)key;
}

static ThreadInfo* get_thread_info(pid_t pid)
{
    pthread_mutex_lock_unchecked(&sDbgLock);

    HashEntry* entry = hashmap_lookup(&sThreadMap,
            &pid, sizeof(pid),
            &ThreadInfo_equals);
    if (entry->data == NULL) {
        entry->data = (ThreadInfo*)DbgAllocLocked(sizeof(ThreadInfo));
        initThreadInfo(entry->data, pid);
    }

    pthread_mutex_unlock_unchecked(&sDbgLock);

    return (ThreadInfo *)entry->data;
}

static void push_most_recently_locked(MutexInfo* mrl) {
    ThreadInfo* tinfo = get_thread_info(gettid());
    mrl->next = NULL;
    mrl->prev = tinfo->mrl;
    tinfo->mrl = mrl;
}

static void remove_most_recently_locked(MutexInfo* mrl) {
    ThreadInfo* tinfo = get_thread_info(gettid());
    if (mrl->next) {
        (mrl->next)->prev = mrl->prev;
    }
    if (mrl->prev) {
        (mrl->prev)->next = mrl->next;
    }
    if (tinfo->mrl == mrl) {
        tinfo->mrl = mrl->next;
    }
}

static MutexInfo* get_most_recently_locked() {
    ThreadInfo* tinfo = get_thread_info(gettid());
    return tinfo->mrl;
}

/****************************************************************************/

/* pthread_debug_init() is called from libc_init_dynamic() just
 * after system properties have been initialized
 */

__LIBC_HIDDEN__
void pthread_debug_init(void) {
    char env[PROP_VALUE_MAX];
    if (__system_property_get("debug.libc.pthread", env)) {
        int level = atoi(env);
        if (level) {
            LOGI("pthread deadlock detection level %d enabled for pid %d (%s)",
                    level, getpid(), __progname);
            hashmap_init(&sMutexMap);
            sPthreadDebugLevel = level;
        }
    }
}

/*
 * See if we were allowed to grab the lock at this time.  We do it
 * *after* acquiring the lock, rather than before, so that we can
 * freely update the MutexInfo struct.  This seems counter-intuitive,
 * but our goal is deadlock *prediction* not deadlock *prevention*.
 * (If we actually deadlock, the situation is easy to diagnose from
 * a thread dump, so there's no point making a special effort to do
 * the checks before the lock is held.)
 */

__LIBC_HIDDEN__
void pthread_debug_mutex_lock_check(pthread_mutex_t *mutex)
{
    if (sPthreadDebugLevel == 0) return;
    // prediction disabled for this thread
    if (sPthreadDebugDisabledThread == gettid())
        return;
    MutexInfo* object = get_mutex_info(mutex);
    MutexInfo* mrl = get_most_recently_locked();
    mutex_lock_checked(mrl, object);
    push_most_recently_locked(object);
}

/*
 * pthread_debug_mutex_unlock_check() must be called with the mutex
 * still held (ie: before calling the real unlock)
 */

__LIBC_HIDDEN__
void pthread_debug_mutex_unlock_check(pthread_mutex_t *mutex)
{
    if (sPthreadDebugLevel == 0) return;
    // prediction disabled for this thread
    if (sPthreadDebugDisabledThread == gettid())
        return;
    MutexInfo* object = get_mutex_info(mutex);
    remove_most_recently_locked(object);
    mutex_unlock_checked(object);
}
