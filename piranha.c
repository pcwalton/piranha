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
#include "ustr/ustr.h"

struct map {
    uint32_t start;
    uint32_t end;
    Ustr *name;
};

int compare_addr_and_map(const void *addr_p, const void *map_p)
{
    const uint32_t *addr = addr_p;
    const struct map *map = map_p;
    return *addr >= map->start && *addr < map->end;
}

struct map *find_map_for_addr(Ustr *maps, uint32_t addr)
{
    return (struct map *)bsearch(&addr, maps,
        ustr_len(maps) / sizeof(struct map), sizeof(struct map),
        compare_addr_and_map);
}

bool guess_lr_legitimacy(pid_t pid, uint32_t maybe_lr, uint32_t *real_lr)
{
    // A non-word-aligned pointer can't possibly be the value of the saved link
    // register in ARM mode.
    if ((maybe_lr & 0x3) == 0x2)
        return false;

    bool thumb = maybe_lr & 0x1;
    if (thumb)
        maybe_lr--;

    // Read the memory that that stack value is pointing at.
    uint32_t maybe_bl_ptr = maybe_lr - 4;
    uint32_t maybe_bl;
    if (!thumb) {
        errno = 0;
        uint32_t maybe_bl = ptrace(PTRACE_PEEKDATA, pid, (void *)maybe_bl_ptr,
                                   NULL);
        if (errno)
            return false;

#ifdef DEBUG_STACK_WALKING
        printf(" /* maybe_bl %08x */", maybe_bl);
#endif

        // Does it immediately follow a "bl" or "blx" instruction?
        if ((maybe_bl & 0x0f000000) == 0x0b000000) {
            // Found!
            *real_lr = maybe_lr;
            return true;
        }

        return false;
    }

    // We're in Thumb mode. Word alignment makes this annoying.
    uint16_t maybe_bl_upper, maybe_bl_lower;
    if ((maybe_bl_ptr & 0x3) == 0) {
        errno = 0;
        maybe_bl = ptrace(PTRACE_PEEKDATA, pid, (void *)maybe_bl_ptr,
                          NULL);
        if (errno)
            return false;

        maybe_bl_upper = maybe_bl & 0xffff;
        maybe_bl_lower = maybe_bl >> 16;
    } else {
        assert((maybe_bl_ptr & 0x3) == 0x2);

        errno = 0;
        maybe_bl = ptrace(PTRACE_PEEKDATA, pid,
                          (void *)(maybe_bl_ptr - 2), NULL);
        if (errno)
            return false;
        maybe_bl_upper = maybe_bl >> 16;

        errno = 0;
        maybe_bl = ptrace(PTRACE_PEEKDATA, pid,
                          (void *)(maybe_bl_ptr + 2), NULL);
        if (errno)
            return false;
        maybe_bl_lower = maybe_bl & 0xffff;
    }
    if (errno)
        return false;

    // Does it immediately follow a "bl" or "blx" instruction?
    if ((maybe_bl_lower & 0xf000) == 0xf000 ||          // bl label
            (maybe_bl_lower & 0xff87) == 0x4700 ||      // bx Rm
            (maybe_bl_lower & 0xf801) == 0xe800 ||      // blx label
            (maybe_bl_lower & 0xff87) == 0x4780 ||      // blx Rm
            ((maybe_bl_upper & 0xf800) == 0xf000 &&
             (maybe_bl_lower & 0xd000) == 0xd000)) {    // bl
        // Found!
        *real_lr = maybe_lr;
        return true;
    }

    return false;
}

bool unwind(pid_t pid, Ustr *maps)
{
    struct pt_regs regs;
    bool comma = false;

    memset(&regs, '\0', sizeof(regs));

    int err = ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    if (err) {
        perror("Couldn't read registers: ");
        return false;
    }

    printf("[\"%08lx\"", regs.ARM_pc - 8);

    uint32_t lr = regs.ARM_lr & 0xfffffffe, sp = regs.ARM_sp;

#ifdef DEBUG_STACK_WALKING
    printf(" /* sp: %08x */", sp);
#endif

    while (lr) {
        printf(",\"%08x\"", lr);

        uint32_t maybe_lr;
        do {
            errno = 0;
            maybe_lr = ptrace(PTRACE_PEEKDATA, pid, (void *)sp, NULL);
            if (errno) {
                // Reached the end of the stack.
                lr = 0;
                break;
            }

            sp += 4;
        } while (!guess_lr_legitimacy(pid, maybe_lr, &lr));
    }

    printf("]\n");
    return true;
}

bool wait_for_process_to_stop(pid_t pid)
{
    int status;
    do {
        if (waitpid(pid, &status, WUNTRACED) == -1)
            return false;
    } while (!WIFSTOPPED(status));

    return true;
}

bool read_maps(pid_t pid, Ustr **maps)
{
    int ok = true;

    Ustr *path = ustr_dup_fmt("/proc/%d/maps", pid);
    FILE *f = fopen(ustr_cstr(path), "r");
    ustr_sc_free(&path);
    if (!f)
        return false;

    *maps = ustr_dup_empty();
    while (!feof(f)) {
        Ustr *line = ustr_dup_empty();
        if (!ustr_io_getline(&line, f)) {
            ustr_free(line);
            ok = false;
            goto out;
        }

        struct map map;
        map.name = ustr_dup_undef(256);
        int field_count = sscanf(ustr_cstr(line),
            "%x-%x %*s %*x %*s %*u %256s", &map.start, &map.end,
            ustr_wstr(map.name));
        ustr_free(line);

        if (!field_count) {
            ustr_free(map.name);
            break;
        }

        if (!ustr_add_buf(maps, &map, sizeof(map))) {
            ustr_free(map.name);
            break;
        }
    }

out:
    fclose(f);
    return ok;
}

void print_maps(Ustr *maps)
{
    bool comma = false;

    printf("[");

    for (int i = 0; i < ustr_len(maps) / sizeof(struct map); i++) {
        struct map *map = &((struct map *)ustr_cstr(maps))[i];
        printf("%s\n\t{ \"start\": \"%08x\", \"end\": \"%08x\", "
               "\"name\": \"%s\" }",
               comma ? "" : ",", map->start, map->end, ustr_cstr(map->name));
        comma = true;
    }

    printf("\n]");
}

int main(int argc, char **argv)
{
    bool ok;

    if (argc < 2) {
        fprintf(stderr, "usage: piranha PID\n");
        return 1;
    }

    pid_t pid = strtol(argv[1], NULL, 0);

    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL)) {
        perror("Failed to attach: ");
        return 1;
    }

    if (!wait_for_process_to_stop(pid)) {
        fprintf(stderr, "Failed to wait for inferior to stop\n");
        return 1;
    }

    Ustr *maps;
    if (!(ok = read_maps(pid, &maps)))
        goto out;

    printf("{\n\t\"maps\": ");
    print_maps(maps);

    printf(",\n\t\"samples\": ");
    ok = unwind(pid, maps);
    printf("\n}\n");

out:
    if (ptrace(PTRACE_DETACH, pid, NULL, NULL)) {
        ok = false;
        perror("Failed to detach: ");
    }

    return !ok;
}

