#!/usr/bin/python

# This tool is used to generate the assembler system call stubs,
# the header files listing all available system calls, and the
# makefiles used to build all the stubs.

import atexit
import commands
import filecmp
import glob
import logging
import os.path
import re
import shutil
import stat
import string
import sys
import tempfile


all_arches = [ "arm", "arm64", "mips", "mips64", "x86", "x86_64" ]


# temp directory where we store all intermediate files
bionic_temp = tempfile.mkdtemp(prefix="bionic_gensyscalls");
# Make sure the directory is deleted when the script exits.
atexit.register(shutil.rmtree, bionic_temp)

bionic_libc_root = os.path.join(os.environ["ANDROID_BUILD_TOP"], "bionic/libc")

warning = "Generated by gensyscalls.py. Do not edit."

DRY_RUN = False

def make_dir(path):
    path = os.path.abspath(path)
    if not os.path.exists(path):
        parent = os.path.dirname(path)
        if parent:
            make_dir(parent)
        os.mkdir(path)


def create_file(relpath):
    full_path = os.path.join(bionic_temp, relpath)
    dir = os.path.dirname(full_path)
    make_dir(dir)
    return open(full_path, "w")


syscall_stub_header = "/* " + warning + " */\n" + \
"""
#include <private/bionic_asm.h>

ENTRY(%(func)s)
"""


#
# ARM assembler templates for each syscall stub
#

arm_eabi_call_default = syscall_stub_header + """\
    mov     ip, r7
    ldr     r7, =%(__NR_name)s
    swi     #0
    mov     r7, ip
    cmn     r0, #(MAX_ERRNO + 1)
    bxls    lr
    neg     r0, r0
    b       __set_errno_internal
END(%(func)s)
"""

arm_eabi_call_long = syscall_stub_header + """\
    mov     ip, sp
    stmfd   sp!, {r4, r5, r6, r7}
    .cfi_def_cfa_offset 16
    .cfi_rel_offset r4, 0
    .cfi_rel_offset r5, 4
    .cfi_rel_offset r6, 8
    .cfi_rel_offset r7, 12
    ldmfd   ip, {r4, r5, r6}
    ldr     r7, =%(__NR_name)s
    swi     #0
    ldmfd   sp!, {r4, r5, r6, r7}
    .cfi_def_cfa_offset 0
    cmn     r0, #(MAX_ERRNO + 1)
    bxls    lr
    neg     r0, r0
    b       __set_errno_internal
END(%(func)s)
"""


#
# Arm64 assembler templates for each syscall stub
#

arm64_call = syscall_stub_header + """\
    mov     x8, %(__NR_name)s
    svc     #0

    cmn     x0, #(MAX_ERRNO + 1)
    cneg    x0, x0, hi
    b.hi    __set_errno_internal

    ret
END(%(func)s)
"""


#
# MIPS assembler templates for each syscall stub
#

mips_call = syscall_stub_header + """\
    .set noreorder
    .cpload t9
    li v0, %(__NR_name)s
    syscall
    bnez a3, 1f
    move a0, v0
    j ra
    nop
1:
    la t9,__set_errno_internal
    j t9
    nop
    .set reorder
END(%(func)s)
"""


#
# MIPS64 assembler templates for each syscall stub
#

mips64_call = syscall_stub_header + """\
    .set push
    .set noreorder
    li v0, %(__NR_name)s
    syscall
    bnez a3, 1f
    move a0, v0
    j ra
    nop
1:
    move t0, ra
    bal     2f
    nop
2:
    .cpsetup ra, t1, 2b
    LA t9,__set_errno_internal
    .cpreturn
    j t9
    move ra, t0
    .set pop
END(%(func)s)
"""


#
# x86 assembler templates for each syscall stub
#

x86_registers = [ "ebx", "ecx", "edx", "esi", "edi", "ebp" ]

x86_call = """\
    movl    $%(__NR_name)s, %%eax
    call    __sysenter
    cmpl    $-MAX_ERRNO, %%eax
    jb      1f
    negl    %%eax
    pushl   %%eax
    call    __set_errno_internal
    addl    $4, %%esp
1:
"""

x86_return = """\
    ret
END(%(func)s)
"""


#
# x86_64 assembler templates for each syscall stub
#

x86_64_call = """\
    movl    $%(__NR_name)s, %%eax
    syscall
    cmpq    $-MAX_ERRNO, %%rax
    jb      1f
    negl    %%eax
    movl    %%eax, %%edi
    call    __set_errno_internal
1:
    ret
END(%(func)s)
"""


def param_uses_64bits(param):
    """Returns True iff a syscall parameter description corresponds
       to a 64-bit type."""
    param = param.strip()
    # First, check that the param type begins with one of the known
    # 64-bit types.
    if not ( \
       param.startswith("int64_t") or param.startswith("uint64_t") or \
       param.startswith("loff_t") or param.startswith("off64_t") or \
       param.startswith("long long") or param.startswith("unsigned long long") or
       param.startswith("signed long long") ):
           return False

    # Second, check that there is no pointer type here
    if param.find("*") >= 0:
            return False

    # Ok
    return True


def count_arm_param_registers(params):
    """This function is used to count the number of register used
       to pass parameters when invoking an ARM system call.
       This is because the ARM EABI mandates that 64-bit quantities
       must be passed in an even+odd register pair. So, for example,
       something like:

             foo(int fd, off64_t pos)

       would actually need 4 registers:
             r0 -> int
             r1 -> unused
             r2-r3 -> pos
   """
    count = 0
    for param in params:
        if param_uses_64bits(param):
            if (count & 1) != 0:
                count += 1
            count += 2
        else:
            count += 1
    return count


def count_generic_param_registers(params):
    count = 0
    for param in params:
        if param_uses_64bits(param):
            count += 2
        else:
            count += 1
    return count


def count_generic_param_registers64(params):
    count = 0
    for param in params:
        count += 1
    return count


# This lets us support regular system calls like __NR_write and also weird
# ones like __ARM_NR_cacheflush, where the NR doesn't come at the start.
def make__NR_name(name):
    if name.startswith("__ARM_NR_"):
        return name
    else:
        return "__NR_%s" % (name)


def add_footer(pointer_length, stub, syscall):
    # Add any aliases for this syscall.
    aliases = syscall["aliases"]
    for alias in aliases:
        stub += "\nALIAS_SYMBOL(%s, %s)\n" % (alias, syscall["func"])

    # Use hidden visibility on LP64 for any functions beginning with underscores.
    # Force hidden visibility for any functions which begin with 3 underscores
    if (pointer_length == 64 and syscall["func"].startswith("__")) or syscall["func"].startswith("___"):
        stub += '.hidden ' + syscall["func"] + '\n'

    return stub


def arm_eabi_genstub(syscall):
    num_regs = count_arm_param_registers(syscall["params"])
    if num_regs > 4:
        return arm_eabi_call_long % syscall
    return arm_eabi_call_default % syscall


def arm64_genstub(syscall):
    return arm64_call % syscall


def mips_genstub(syscall):
    return mips_call % syscall


def mips64_genstub(syscall):
    return mips64_call % syscall


def x86_genstub(syscall):
    result     = syscall_stub_header % syscall

    numparams = count_generic_param_registers(syscall["params"])
    stack_bias = numparams*4 + 4
    offset = 0
    mov_result = ""
    first_push = True
    for register in x86_registers[:numparams]:
        result     += "    pushl   %%%s\n" % register
        if first_push:
          result   += "    .cfi_def_cfa_offset 8\n"
          result   += "    .cfi_rel_offset %s, 0\n" % register
          first_push = False
        else:
          result   += "    .cfi_adjust_cfa_offset 4\n"
          result   += "    .cfi_rel_offset %s, 0\n" % register
        mov_result += "    mov     %d(%%esp), %%%s\n" % (stack_bias+offset, register)
        offset += 4

    result += mov_result
    result += x86_call % syscall

    for register in reversed(x86_registers[:numparams]):
        result += "    popl    %%%s\n" % register

    result += x86_return % syscall
    return result


def x86_genstub_socketcall(syscall):
    #   %ebx <--- Argument 1 - The call id of the needed vectored
    #                          syscall (socket, bind, recv, etc)
    #   %ecx <--- Argument 2 - Pointer to the rest of the arguments
    #                          from the original function called (socket())

    result = syscall_stub_header % syscall

    # save the regs we need
    result += "    pushl   %ebx\n"
    result += "    .cfi_def_cfa_offset 8\n"
    result += "    .cfi_rel_offset ebx, 0\n"
    result += "    pushl   %ecx\n"
    result += "    .cfi_adjust_cfa_offset 4\n"
    result += "    .cfi_rel_offset ecx, 0\n"
    stack_bias = 12

    # set the call id (%ebx)
    result += "    mov     $%d, %%ebx\n" % syscall["socketcall_id"]

    # set the pointer to the rest of the args into %ecx
    result += "    mov     %esp, %ecx\n"
    result += "    addl    $%d, %%ecx\n" % (stack_bias)

    # now do the syscall code itself
    result += x86_call % syscall

    # now restore the saved regs
    result += "    popl    %ecx\n"
    result += "    popl    %ebx\n"

    # epilog
    result += x86_return % syscall
    return result


def x86_64_genstub(syscall):
    result = syscall_stub_header % syscall
    num_regs = count_generic_param_registers64(syscall["params"])
    if (num_regs > 3):
        # rcx is used as 4th argument. Kernel wants it at r10.
        result += "    movq    %rcx, %r10\n"

    result += x86_64_call % syscall
    return result


class SysCallsTxtParser:
    def __init__(self):
        self.syscalls = []
        self.lineno   = 0

    def E(self, msg):
        print "%d: %s" % (self.lineno, msg)

    def parse_line(self, line):
        """ parse a syscall spec line.

        line processing, format is
           return type    func_name[|alias_list][:syscall_name[:socketcall_id]] ( [paramlist] ) architecture_list
        """
        pos_lparen = line.find('(')
        E          = self.E
        if pos_lparen < 0:
            E("missing left parenthesis in '%s'" % line)
            return

        pos_rparen = line.rfind(')')
        if pos_rparen < 0 or pos_rparen <= pos_lparen:
            E("missing or misplaced right parenthesis in '%s'" % line)
            return

        return_type = line[:pos_lparen].strip().split()
        if len(return_type) < 2:
            E("missing return type in '%s'" % line)
            return

        syscall_func = return_type[-1]
        return_type  = string.join(return_type[:-1],' ')
        socketcall_id = -1

        pos_colon = syscall_func.find(':')
        if pos_colon < 0:
            syscall_name = syscall_func
        else:
            if pos_colon == 0 or pos_colon+1 >= len(syscall_func):
                E("misplaced colon in '%s'" % line)
                return

            # now find if there is a socketcall_id for a dispatch-type syscall
            # after the optional 2nd colon
            pos_colon2 = syscall_func.find(':', pos_colon + 1)
            if pos_colon2 < 0:
                syscall_name = syscall_func[pos_colon+1:]
                syscall_func = syscall_func[:pos_colon]
            else:
                if pos_colon2+1 >= len(syscall_func):
                    E("misplaced colon2 in '%s'" % line)
                    return
                syscall_name = syscall_func[(pos_colon+1):pos_colon2]
                socketcall_id = int(syscall_func[pos_colon2+1:])
                syscall_func = syscall_func[:pos_colon]

        alias_delim = syscall_func.find('|')
        if alias_delim > 0:
            alias_list = syscall_func[alias_delim+1:].strip()
            syscall_func = syscall_func[:alias_delim]
            alias_delim = syscall_name.find('|')
            if alias_delim > 0:
                syscall_name = syscall_name[:alias_delim]
            syscall_aliases = string.split(alias_list, ',')
        else:
            syscall_aliases = []

        if pos_rparen > pos_lparen+1:
            syscall_params = line[pos_lparen+1:pos_rparen].split(',')
            params         = string.join(syscall_params,',')
        else:
            syscall_params = []
            params         = "void"

        t = {
              "name"    : syscall_name,
              "func"    : syscall_func,
              "aliases" : syscall_aliases,
              "params"  : syscall_params,
              "decl"    : "%-15s  %s (%s);" % (return_type, syscall_func, params),
              "socketcall_id" : socketcall_id
        }

        # Parse the architecture list.
        arch_list = line[pos_rparen+1:].strip()
        if arch_list == "all":
            for arch in all_arches:
                t[arch] = True
        else:
            for arch in string.split(arch_list, ','):
                if arch in all_arches:
                    t[arch] = True
                else:
                    E("invalid syscall architecture '%s' in '%s'" % (arch, line))
                    return

        self.syscalls.append(t)

        logging.debug(t)


    def parse_file(self, file_path):
        logging.debug("parse_file: %s" % file_path)
        fp = open(file_path)
        for line in fp.xreadlines():
            self.lineno += 1
            line = line.strip()
            if not line: continue
            if line[0] == '#': continue
            self.parse_line(line)

        fp.close()


class State:
    def __init__(self):
        self.old_stubs = []
        self.new_stubs = []
        self.other_files = []
        self.syscalls = []


    def process_file(self, input):
        parser = SysCallsTxtParser()
        parser.parse_file(input)
        self.syscalls = parser.syscalls
        parser = None

        for syscall in self.syscalls:
            syscall["__NR_name"] = make__NR_name(syscall["name"])

            if syscall.has_key("arm"):
                syscall["asm-arm"] = add_footer(32, arm_eabi_genstub(syscall), syscall)

            if syscall.has_key("arm64"):
                syscall["asm-arm64"] = add_footer(64, arm64_genstub(syscall), syscall)

            if syscall.has_key("x86"):
                if syscall["socketcall_id"] >= 0:
                    syscall["asm-x86"] = add_footer(32, x86_genstub_socketcall(syscall), syscall)
                else:
                    syscall["asm-x86"] = add_footer(32, x86_genstub(syscall), syscall)
            elif syscall["socketcall_id"] >= 0:
                E("socketcall_id for dispatch syscalls is only supported for x86 in '%s'" % t)
                return

            if syscall.has_key("mips"):
                syscall["asm-mips"] = add_footer(32, mips_genstub(syscall), syscall)

            if syscall.has_key("mips64"):
                syscall["asm-mips64"] = add_footer(64, mips64_genstub(syscall), syscall)

            if syscall.has_key("x86_64"):
                syscall["asm-x86_64"] = add_footer(64, x86_64_genstub(syscall), syscall)

    # Scan a Linux kernel asm/unistd.h file containing __NR_* constants
    # and write out equivalent SYS_* constants for glibc source compatibility.
    def scan_linux_unistd_h(self, fp, path):
        pattern = re.compile(r'^#define __NR_([a-z]\S+) .*')
        syscalls = set() # MIPS defines everything three times; work around that.
        for line in open(path):
            m = re.search(pattern, line)
            if m:
                syscalls.add(m.group(1))
        for syscall in sorted(syscalls):
            fp.write("#define SYS_%s %s\n" % (syscall, make__NR_name(syscall)))


    def gen_glibc_syscalls_h(self):
        # TODO: generate a separate file for each architecture, like glibc's bits/syscall.h.
        glibc_syscalls_h_path = "include/sys/glibc-syscalls.h"
        logging.info("generating " + glibc_syscalls_h_path)
        glibc_fp = create_file(glibc_syscalls_h_path)
        glibc_fp.write("/* %s */\n" % warning)
        glibc_fp.write("#ifndef _BIONIC_GLIBC_SYSCALLS_H_\n")
        glibc_fp.write("#define _BIONIC_GLIBC_SYSCALLS_H_\n")

        glibc_fp.write("#if defined(__aarch64__)\n")
        self.scan_linux_unistd_h(glibc_fp, os.path.join(bionic_libc_root, "kernel/uapi/asm-generic/unistd.h"))
        glibc_fp.write("#elif defined(__arm__)\n")
        self.scan_linux_unistd_h(glibc_fp, os.path.join(bionic_libc_root, "kernel/uapi/asm-arm/asm/unistd.h"))
        glibc_fp.write("#elif defined(__mips__)\n")
        self.scan_linux_unistd_h(glibc_fp, os.path.join(bionic_libc_root, "kernel/uapi/asm-mips/asm/unistd.h"))
        glibc_fp.write("#elif defined(__i386__)\n")
        self.scan_linux_unistd_h(glibc_fp, os.path.join(bionic_libc_root, "kernel/uapi/asm-x86/asm/unistd_32.h"))
        glibc_fp.write("#elif defined(__x86_64__)\n")
        self.scan_linux_unistd_h(glibc_fp, os.path.join(bionic_libc_root, "kernel/uapi/asm-x86/asm/unistd_64.h"))
        glibc_fp.write("#endif\n")

        glibc_fp.write("#endif /* _BIONIC_GLIBC_SYSCALLS_H_ */\n")
        glibc_fp.close()
        self.other_files.append(glibc_syscalls_h_path)


    # Write each syscall stub.
    def gen_syscall_stubs(self):
        for syscall in self.syscalls:
            for arch in all_arches:
                if syscall.has_key("asm-%s" % arch):
                    filename = "arch-%s/syscalls/%s.S" % (arch, syscall["func"])
                    logging.info(">>> generating " + filename)
                    fp = create_file(filename)
                    fp.write(syscall["asm-%s" % arch])
                    fp.close()
                    self.new_stubs.append(filename)


    def regenerate(self):
        logging.info("scanning for existing architecture-specific stub files...")

        for arch in all_arches:
            arch_dir = "arch-" + arch
            logging.info("scanning " + os.path.join(bionic_libc_root, arch_dir))
            rel_path = os.path.join(arch_dir, "syscalls")
            for file in os.listdir(os.path.join(bionic_libc_root, rel_path)):
                if file.endswith(".S"):
                  self.old_stubs.append(os.path.join(rel_path, file))

        logging.info("found %d stub files" % len(self.old_stubs))

        if not os.path.exists(bionic_temp):
            logging.info("creating %s..." % bionic_temp)
            make_dir(bionic_temp)

        logging.info("re-generating stubs and support files...")

        self.gen_glibc_syscalls_h()
        self.gen_syscall_stubs()

        logging.info("comparing files...")
        adds    = []
        edits   = []

        for stub in self.new_stubs + self.other_files:
            tmp_file = os.path.join(bionic_temp, stub)
            libc_file = os.path.join(bionic_libc_root, stub)
            if not os.path.exists(libc_file):
                # new file, git add it
                logging.info("new file:     " + stub)
                adds.append(libc_file)
                shutil.copyfile(tmp_file, libc_file)

            elif not filecmp.cmp(tmp_file, libc_file):
                logging.info("changed file: " + stub)
                edits.append(stub)

        deletes = []
        for stub in self.old_stubs:
            if not stub in self.new_stubs:
                logging.info("deleted file: " + stub)
                deletes.append(os.path.join(bionic_libc_root, stub))

        if not DRY_RUN:
            if adds:
                commands.getoutput("git add " + " ".join(adds))
            if deletes:
                commands.getoutput("git rm " + " ".join(deletes))
            if edits:
                for file in edits:
                    shutil.copyfile(os.path.join(bionic_temp, file),
                                    os.path.join(bionic_libc_root, file))
                commands.getoutput("git add " + " ".join((os.path.join(bionic_libc_root, file)) for file in edits))

            commands.getoutput("git add %s" % (os.path.join(bionic_libc_root, "SYSCALLS.TXT")))

        if (not adds) and (not deletes) and (not edits):
            logging.info("no changes detected!")
        else:
            logging.info("ready to go!!")

logging.basicConfig(level=logging.INFO)

state = State()
state.process_file(os.path.join(bionic_libc_root, "SYSCALLS.TXT"))
state.regenerate()
