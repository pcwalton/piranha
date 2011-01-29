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
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "bstrlib.h"

#define THREAD_ENTRY_LENGTH     0x3c

struct map {
    uint32_t start;
    uint32_t end;
    uint32_t offset;
    bstring name;
};

struct basic_info {
    uint32_t thread_entry_offset;
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

struct map *get_map_for_addr(bstring maps, uint32_t addr)
{
    return (struct map *)bsearch(&addr, maps->data, maps->slen /
        sizeof(struct map), sizeof(struct map), compare_addr_and_map);
}

void print_addr(struct map *map, uint32_t addr)
{
    if (!map) {
        printf("\"%08x\"", addr);
    } else {
        printf("\"%s+%08x\"", map->name->data, addr - map->start +
            map->offset); 
    }
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
        if ((maybe_bl & 0x0f000000) == 0x0b000000 ||
                (maybe_bl & 0x0ffffff0) == 0x012fff30) {
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

bool in_thread_entry(struct basic_info *binfo, struct map *map, uint32_t pc)
{
    struct tagbstring libc_so = bsStatic("libc.so");
    if (!map || binstr(map->name, 0, &libc_so) == BSTR_ERR)
        return false;
    uint32_t rel_pc = pc - map->start + map->offset;
    return rel_pc >= binfo->thread_entry_offset &&
        rel_pc < binfo->thread_entry_offset + THREAD_ENTRY_LENGTH;
}

bool unwind(struct basic_info *binfo, pid_t pid, bstring maps)
{
    struct pt_regs regs;
    memset(&regs, '\0', sizeof(regs));

    int err = ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    if (err) {
        perror("Couldn't read registers");
        return false;
    }

    printf("[ ");

    struct map *map = get_map_for_addr(maps, regs.ARM_pc - 8);
    print_addr(map, regs.ARM_pc - 4);

    uint32_t lr = regs.ARM_lr & 0xfffffffe, sp = regs.ARM_sp;

#ifdef DEBUG_STACK_WALKING
    printf(" /* sp: %08x */", sp);
#endif

    while (lr && !in_thread_entry(binfo, map, lr)) {
        printf(", ");
        print_addr(map, lr);

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

        map = get_map_for_addr(maps, lr);
    }

    printf(" ]");
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
    if (!path)
        return false;

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
            "%x-%x %*s %x %*s %*u %255s", &map.start, &map.end, &map.offset,
            name);
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

void detach_from_thread(pid_t thread_pid)
{
    if (ptrace(PTRACE_DETACH, thread_pid, NULL, NULL))
        perror("Failed to detach from thread");
}

void wait_for_thread_attachment(pid_t thread_pid)
{
    int status;
    while (waitpid(thread_pid, &status, __WCLONE) <= 0) {
        // empty
    }
}

bool sample(struct basic_info *binfo, pid_t pid, bstring maps)
{
    bool ok = true;

    printf("\t\t{");

    bstring tasks_path = bformat("/proc/%d/task", (int)pid);
    if (!tasks_path)
        return false;

    DIR *tasks_dir = opendir((char *)tasks_path->data);
    bdestroy(tasks_path);
    if (!tasks_dir) {
        perror("Failed to open /proc/x/task");
        return false;
    }

    struct dirent *ent;
    bool comma = false;
    while ((ent = readdir(tasks_dir))) {
        int thread_pid;
        if (!sscanf(ent->d_name, "%d", &thread_pid))
            continue;

        if (ptrace(PTRACE_ATTACH, thread_pid, NULL, NULL))
            continue;

        if (pid == thread_pid)
            wait_for_process_to_stop(thread_pid);
        else
            wait_for_thread_attachment(thread_pid);

        printf("%s\n\t\t\t\"%d\": ", comma ? "," : "", thread_pid);

        if (!unwind(binfo, (pid_t)thread_pid, maps)) {
            ok = false;
            break;
        }

        detach_from_thread((pid_t)thread_pid);

        comma = true;
    }

    closedir(tasks_dir);
    return ok;
}

bool compute_thread_entry(struct basic_info *info)
{
    bool ok = true;

    void *lib = dlopen("libc.so", RTLD_LAZY);
    if (!lib) {
        fprintf(stderr, "failed to dlopen libc.so: %s\n", dlerror());
        return false;
    }

    void *thread_entry = dlsym(lib, "__thread_entry");
    if (!thread_entry) {
        ok = false;
        fprintf(stderr, "failed to dlsym __thread_entry: %s\n", dlerror());
        goto out;
    }

    Dl_info dl_info;
    if (!dladdr(thread_entry, &dl_info)) {
        ok = false;
        fprintf(stderr, "failed to dladdr __thread_entry: %s\n", dlerror());
        goto out;
    }

    info->thread_entry_offset = (uint32_t)dl_info.dli_saddr -
        (uint32_t)dl_info.dli_fbase;

out:
    dlclose(lib);
    return ok;
}

int main(int argc, char **argv)
{
    bool ok;

    if (argc < 2) {
        fprintf(stderr, "usage: piranha PID\n");
        return 1;
    }

    struct basic_info binfo;
    if (!compute_thread_entry(&binfo))
        return 1;

    pid_t pid = strtol(argv[1], NULL, 0);

    bstring maps;
    if (!read_maps(pid, &maps))
        return 1;

    printf("{\n\t\"maps\": ");
    print_maps(maps);

    printf(",\n\t\"samples\": [\n");
    ok = sample(&binfo, pid, maps);
    printf("\n\t]\n}\n");

    return !ok;
}

