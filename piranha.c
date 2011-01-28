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
#include "bstrlib.h"

struct map {
    uint32_t start;
    uint32_t end;
    bstring name;
};

int compare_addr_and_map(const void *addr_p, const void *map_p)
{
    const uint32_t *addr = addr_p;
    const struct map *map = map_p;
    if (*addr < map->start)
        return -1;
    if (*addr >= map->end)
        return 1;
    return 0;
}

void print_addr(bstring maps, uint32_t addr)
{
    struct map *map = (struct map *)bsearch(&addr, maps->data,
        maps->slen / sizeof(struct map), sizeof(struct map),
        compare_addr_and_map);

    if (!map)
        printf("\"%08x\"", addr);
    else
        printf("\"%s+%08x\"", map->name->data, addr - map->start); 
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

bool unwind(pid_t pid, bstring maps)
{
    struct pt_regs regs;
    memset(&regs, '\0', sizeof(regs));

    int err = ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    if (err) {
        perror("Couldn't read registers");
        return false;
    }

    printf("[ ");
    print_addr(maps, regs.ARM_pc - 8);

    uint32_t lr = regs.ARM_lr & 0xfffffffe, sp = regs.ARM_sp;

#ifdef DEBUG_STACK_WALKING
    printf(" /* sp: %08x */", sp);
#endif

    while (lr) {
        printf(", ");
        print_addr(maps, lr);

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

    printf(" ]\n");
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

bool read_maps(pid_t pid, bstring *maps)
{
    int ok = true;

    bstring path = bformat("/proc/%d/maps", pid);
    FILE *f = fopen((char *)path->data, "r");
    bdestroy(path);
    if (!f) {
        perror("Failed to open /proc/x/maps");
        return false;
    }

    *maps = bfromcstr("");
    while (!feof(f)) {
        bstring line = bgets((bNgetc)fgetc, f, '\n');
        if (!line)
            break;

        struct map map;
        char name[256];
        int field_count = sscanf((char *)line->data,
            "%x-%x %*s %*x %*s %*u %255s", &map.start, &map.end, name);
        bdestroy(line);

        if (!field_count)
            break;

        map.name = bfromcstr(name);
        if (bcatblk(*maps, &map, sizeof(map)) != BSTR_OK) {
            bdestroy(map.name);
            break;
        }
    }

out:
    fclose(f);
    return ok;
}

void print_maps(bstring maps)
{
    bool comma = false;

    printf("[");

    for (int i = 0; i < maps->slen / sizeof(struct map); i++) {
        struct map *map = &((struct map *)maps->data)[i];
        printf("%s\n\t\t{ \"start\": \"%08x\", \"end\": \"%08x\", "
               "\"name\": \"%s\" }",
               comma ? "" : ",", map->start, map->end, map->name->data);
        comma = true;
    }

    printf("\n\t]");
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
        perror("Failed to attach");
        return 1;
    }

    if (!wait_for_process_to_stop(pid)) {
        fprintf(stderr, "Failed to wait for inferior to stop\n");
        return 1;
    }

    bstring maps;
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
        perror("Failed to detach");
    }

    return !ok;
}

