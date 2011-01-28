/*
 * piranha/piranha.c
 *
 * Tiny Android profiler
 *
 * Copyright (c) 2011 Mozilla Corporation
 * Patrick Walton <pcwalton@mimiga.net>
 */

#include <linux/ptrace.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

struct map {
    uint32_t start;
    uint32_t end;
    char *name;
};

int unwind(pid_t pid)
{
    struct pt_regs regs;
    bool comma = false;

    memset(&regs, '\0', sizeof(regs));

    int err = ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    if (err) {
        perror("Couldn't read registers: ");
        return 0;
    }

    printf("[\"%08lx\"", regs.ARM_pc - 8);

    uint32_t lr = regs.ARM_lr & 0xfffffffe, sp = regs.ARM_sp;

#ifdef DEBUG_STACK_WALKING
    printf(" /* sp: %08x */", sp);
#endif

    while (lr) {
        printf(",\"%08x\"", lr);

        while (true) {
            errno = 0;
            uint32_t maybe_lr = ptrace(PTRACE_PEEKDATA, pid, (void *)sp, NULL);
            if (errno) {
                // Reached the end of the stack.
                lr = 0;
                break;
            }

            sp += 4;

            // A non-word-aligned pointer can't possibly be the value of the
            // saved link register in ARM mode.
            if ((maybe_lr & 0x3) == 0x2)
                continue;

            bool thumb = maybe_lr & 0x1;
            if (thumb)
                maybe_lr--;

            // Read the memory that that stack value is pointing at.
            uint32_t maybe_bl_ptr = maybe_lr - 4;
            uint32_t maybe_bl;
            if (!thumb) {
                errno = 0;
                uint32_t maybe_bl = ptrace(PTRACE_PEEKDATA, pid,
                                           (void *)maybe_bl_ptr, NULL);
                if (errno)
                    continue;

#ifdef DEBUG_STACK_WALKING
                printf(" /* maybe_bl %08x */", maybe_bl);
#endif

                // Does it immediately follow a "bl" or "blx" instruction?
                if ((maybe_bl & 0x0f000000) == 0x0b000000) {
                    // Found!
                    lr = maybe_lr;
                    break;
                }

                continue;
            }

            // We're in Thumb mode. Word alignment makes this annoying.
            uint16_t maybe_bl_upper, maybe_bl_lower;
            if ((maybe_bl_ptr & 0x3) == 0) {
                errno = 0;
                maybe_bl = ptrace(PTRACE_PEEKDATA, pid, (void *)maybe_bl_ptr,
                                  NULL);
                if (errno)
                    continue;

                maybe_bl_upper = maybe_bl & 0xffff;
                maybe_bl_lower = maybe_bl >> 16;
            } else {
                assert((maybe_bl_ptr & 0x3) == 0x2);

                errno = 0;
                maybe_bl = ptrace(PTRACE_PEEKDATA, pid,
                                  (void *)(maybe_bl_ptr - 2), NULL);
                if (errno)
                    continue;
                maybe_bl_upper = maybe_bl >> 16;

                errno = 0;
                maybe_bl = ptrace(PTRACE_PEEKDATA, pid,
                                  (void *)(maybe_bl_ptr + 2), NULL);
                if (errno)
                    continue;
                maybe_bl_lower = maybe_bl & 0xffff;
            }
            if (errno)
                continue;

            // Does it immediately follow a "bl" or "blx" instruction?
            if ((maybe_bl_lower & 0xf000) == 0xf000 ||          // bl label
                    (maybe_bl_lower & 0xff87) == 0x4700 ||      // bx Rm
                    (maybe_bl_lower & 0xf801) == 0xe800 ||      // blx label
                    (maybe_bl_lower & 0xff87) == 0x4780 ||      // blx Rm
                    ((maybe_bl_upper & 0xf800) == 0xf000 &&
                     (maybe_bl_lower & 0xd000) == 0xd000)) {    // bl
                // Found!
                lr = maybe_lr;
                break;
            }
        }
    }

    printf("]\n");
    return 1;
}

int wait_for_process_to_stop(pid_t pid)
{
    int status;
    do {
        if (waitpid(pid, &status, WUNTRACED) == -1)
            return 0;
    } while (!WIFSTOPPED(status));
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: piranha PID\n");
        return 1;
    }

    pid_t pid = strtol(argv[1], NULL, 0);

    int err = ptrace(PTRACE_ATTACH, pid, NULL, NULL);
    if (err) {
        perror("Failed to attach: ");
        return 1;
    }

    if (!wait_for_process_to_stop(pid)) {
        fprintf(stderr, "Failed to wait for inferior to stop\n");
        return 1;
    }

    int ok = unwind(pid);

    err = ptrace(PTRACE_DETACH, pid, NULL, NULL);
    if (err) {
        ok = 0;
        perror("Failed to detach: ");
    }

    return !ok;
}

