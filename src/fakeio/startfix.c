/*
 * startfix — LD_PRELOAD for 2013 cabinet programs on a modern glibc.
 *
 * The loader's debug_sig_install() finds the outermost stack frame by following saved frame
 * pointers until it reads 0. The 2013 glibc entered the program's constructors and main()
 * with %ebp = 0, which ends that chain; a modern glibc does not, so the walk runs into code
 * and crashes. Here __libc_start_main is wrapped: the program's own init function and main()
 * are called through a trampoline that clears %ebp first.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>

typedef int (*main_fn)(int, char **, char **);
typedef void (*init_fn)(int, char **, char **);
typedef int (*start_fn)(main_fn, int, char **, init_fn, void (*)(void), void (*)(void), void *);

static main_fn real_main;
static init_fn real_init;

/* int call_with_ebp0(void *fn, int argc, char **argv, char **envp): call fn(argc, argv, envp)
 * with %ebp = 0 and %esp 16-byte aligned at the call, as the cabinet's code assumes (its
 * constructors use fxsave on stack buffers). %esi (callee-saved) keeps the frame. */
int call_with_ebp0(void *fn, int argc, char **argv, char **envp);
__asm__(
    ".text\n"
    ".globl call_with_ebp0\n"
    ".hidden call_with_ebp0\n"
    ".type call_with_ebp0, @function\n"
    "call_with_ebp0:\n"
    "    push %ebp\n"
    "    push %esi\n"
    "    mov %esp, %esi\n"
    "    xor %ebp, %ebp\n"
    "    and $-16, %esp\n"
    "    sub $4, %esp\n"
    "    pushl 24(%esi)\n"
    "    pushl 20(%esi)\n"
    "    pushl 16(%esi)\n"
    "    call *12(%esi)\n"
    "    mov %esi, %esp\n"
    "    pop %esi\n"
    "    pop %ebp\n"
    "    ret\n"
    ".size call_with_ebp0, .-call_with_ebp0\n");

static void wrapped_init(int argc, char **argv, char **envp) {
    if (real_init) call_with_ebp0((void *)real_init, argc, argv, envp);
}

static int wrapped_main(int argc, char **argv, char **envp) {
    return call_with_ebp0((void *)real_main, argc, argv, envp);
}

int __libc_start_main(main_fn main, int argc, char **argv, init_fn init, void (*fini)(void),
                      void (*rtld_fini)(void), void *stack_end) {
    start_fn real = (start_fn)dlvsym(RTLD_NEXT, "__libc_start_main", "GLIBC_2.0");
    if (!real) real = (start_fn)dlsym(RTLD_NEXT, "__libc_start_main");
    real_main = main;
    real_init = init;
    /* a non-NULL init makes glibc call it instead of running the program's constructors itself */
    return real(wrapped_main, argc, argv, wrapped_init, fini, rtld_fini, stack_end);
}

/* Port I/O permission for the pre-ION ISA board code: granted; crashlog.c emulates the ports. */
int ioperm(unsigned long from, unsigned long num, int on) { (void)from; (void)num; (void)on; return 0; }
int iopl(int level) { (void)level; return 0; }
