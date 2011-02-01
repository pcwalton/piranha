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
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "bstrlib.h"

// The length of Bionic's __thread_entry routine. This is obviously a gross
// hack, of which I am ashamed.
#define THREAD_ENTRY_LENGTH     0x3c

#define EBML_HEADER_TAG         0x1a45dfa3
#define EBML_MEMORY_MAP_TAG     0x81
#define EBML_MEMORY_REGION_TAG  0x82
#define EBML_SAMPLES_TAG        0x83
#define EBML_SAMPLE_TAG         0x84
#define EBML_THREAD_SAMPLE_TAG  0x85
#define EBML_THREAD_STATUS_TAG  0x86
#define EBML_STACK_TAG          0x87

#define length_of(x)    (sizeof(x) / sizeof((x)[0]))

struct map {
    uint32_t start;
    uint32_t end;
    uint32_t offset;
    bstring name;
};

struct basic_info {
    pid_t pid;
    uint32_t thread_entry_offset;
    bstring maps;
};

struct ebml_writer {
    FILE *f;
    uint32_t tag_offsets[4];
    int tag_stack_size;
};

//
// EBML writing
//

bool ebml_start_tag(struct ebml_writer *writer, uint32_t tag_id)
{
    assert(tag_stack_size < length_of(writer->tag_offsets));

    uint32_t buf = htonl(tag_id);

    bool ok;
    if (tag_id & 0xff000000)
        ok = !!fwrite(&buf, 4, 1, writer->f);
    else if (tag_id & 0x00ff0000)
        ok = !!fwrite((char *)&buf + 1, 3, 1, writer->f);
    else if (tag_id & 0x0000ff00)
        ok = !!fwrite((char *)&buf + 2, 2, 1, writer->f);
    else
        ok = !!fwrite((char *)&buf + 3, 1, 1, writer->f);

    if (!ok)
        return false;

    writer->tag_offsets[writer->tag_stack_size++] = ftell(writer->f);

    // Write a placeholder size
    uint32_t zero = 0;
    if (!fwrite(&zero, 4, 1, writer->f))
        return false;

    return true;
}

void ebml_end_tag(struct ebml_writer *writer)
{
    assert(writer->tag_stack_size);

    uint32_t orig_offset = ftell(writer->f);
    uint32_t offset = writer->tag_offsets[writer->tag_stack_size - 1];
    uint32_t size = ftell(writer->f) - offset - 4;
    assert(size < 0x10000000);

    fseek(writer->f, offset, SEEK_SET);
    fputc(0x10 | ((size >> 24) & 0xf), writer->f);
    fputc((size >> 16) & 0xff, writer->f);
    fputc((size >> 8) & 0xff, writer->f);
    fputc(size & 0xff, writer->f);

    fseek(writer->f, orig_offset, SEEK_SET);

    writer->tag_stack_size--;
}

bool ebml_write_header(struct ebml_writer *writer, const_bstring format_name)
{
    if (!ebml_start_tag(writer, EBML_HEADER_TAG))
        return false;

    bstring name_buf = bfromcstralloc(128, "");
    if (!name_buf)
        return false;

    bool ok = true;
    if (binsertch(name_buf, 0, 32, '\0') != BSTR_OK) {
        fprintf(stderr, "bpattern()\n");
        ok = false;
        goto out;
    }
    if (bsetstr(name_buf, 0, format_name, '\0') != BSTR_OK) {
        fprintf(stderr, "bsetstr()\n");
        ok = false;
        goto out;
    }

    if (!fwrite(name_buf->data, name_buf->slen + 1, 1, writer->f)) {
        perror("fwrite");
        ok = false;
        goto out;
    }

    ebml_end_tag(writer);

out:
    bdestroy(name_buf);
    return ok;
}

void ebml_finish(struct ebml_writer *writer)
{
    while (writer->tag_stack_size)
        ebml_end_tag(writer);
    fclose(writer->f);
}

// See comments in profile(). This lame thing is the result of Android's lack
// of any signal handling mechanisms invented in the last 20 years.
int signal_sockets[2];

void signal_handler(int which)
{
    static bool sigint_handled = false;
    if (which == SIGINT) {
        if (sigint_handled) {
            fprintf(stderr, "Caught two SIGINTs; aborting\n");
            abort();
        }
        sigint_handled = true;
    }

    int8_t sig = which;
    write(signal_sockets[1], &sig, sizeof(sig));
}

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

bool unwind(struct basic_info *binfo, struct ebml_writer *writer, pid_t pid)
{
    struct pt_regs regs;
    memset(&regs, '\0', sizeof(regs));

    int err = ptrace(PTRACE_GETREGS, pid, NULL, &regs);
    if (err) {
        perror("Couldn't read registers");
        return false;
    }

    ebml_start_tag(writer, EBML_STACK_TAG);

    struct map *map = get_map_for_addr(binfo->maps, regs.ARM_pc - 8);
    uint32_t val = htonl(regs.ARM_pc - 4);
    if (!fwrite(&val, 4, 1, writer->f))
        return false;

    uint32_t lr = regs.ARM_lr & 0xfffffffe, sp = regs.ARM_sp;

#ifdef DEBUG_STACK_WALKING
    printf(" /* sp: %08x */", sp);
#endif

    bool ok = true;
    while (lr && !in_thread_entry(binfo, map, lr)) {
        val = htonl(lr);
        if (!fwrite(&val, 4, 1, writer->f)) {
            ok = false;
            break;
        }

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

        map = get_map_for_addr(binfo->maps, lr);
    }

    ebml_end_tag(writer);
    return ok;
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
    bool ok = true;

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

bool print_maps(struct ebml_writer *writer, bstring maps)
{
    if (!ebml_start_tag(writer, EBML_MEMORY_MAP_TAG))
        return false;

    for (int i = 0; i < maps->slen / sizeof(struct map); i++) {
        struct map *map = &((struct map *)maps->data)[i];

        if (!ebml_start_tag(writer, EBML_MEMORY_REGION_TAG))
            return false;

        uint32_t val = htonl(map->start);
        if (!fwrite(&val, 4, 1, writer->f))
            return false;
        val = htonl(map->end);
        if (!fwrite(&val, 4, 1, writer->f))
            return false;
        if (!fwrite(map->name->data, map->name->slen + 1, 1, writer->f))
            return false;

        ebml_end_tag(writer);
    }

    ebml_end_tag(writer);

    return true;
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

bool get_thread_state(pid_t thread_pid, bstring *result)
{
    bstring path = bformat("/proc/%d/status", thread_pid);
    if (!path)
        return false;

    FILE *f = fopen((char *)path->data, "r");
    bdestroy(path);
    if (!f) {
        perror("Failed to open /proc/x/status");
        return false;
    }

    bool ok = false;

    while (!feof(f)) {
        bstring line = bgets((bNgetc)fgetc, f, '\n');
        if (!line)
            break;

        char name[8];
        if (!sscanf((char *)line->data, "State:\t%7s", name))
            continue;

        *result = bfromcstr(name);
        ok = true;
    }

    fclose(f);
    return ok;
}

bool sample(struct basic_info *binfo, struct ebml_writer *writer)
{
    if (!ebml_start_tag(writer, EBML_SAMPLE_TAG))
        return false;

    bstring tasks_path = bformat("/proc/%d/task", (int)binfo->pid);
    if (!tasks_path)
        return false;

    DIR *tasks_dir = opendir((char *)tasks_path->data);
    bdestroy(tasks_path);
    if (!tasks_dir) {
        perror("Failed to open /proc/x/task");
        return false;
    }

    struct dirent *ent;
    bool ok = true;
    while (ok && (ent = readdir(tasks_dir))) {
        int thread_pid;
        if (!sscanf(ent->d_name, "%d", &thread_pid))
            continue;

        // We do this before we trace. If we don't, the status unhelpfully
        // returns "T" for "traced".
        bstring state;
        if (!get_thread_state(thread_pid, &state)) {
            ok = false;
            break;
        }

        if (ptrace(PTRACE_ATTACH, thread_pid, NULL, NULL))
            continue;

        if (binfo->pid == thread_pid)
            wait_for_process_to_stop(thread_pid);
        else
            wait_for_thread_attachment(thread_pid);

        if (!ebml_start_tag(writer, EBML_THREAD_SAMPLE_TAG)) {
            ok = false;
            break;
        }

        if (!ebml_start_tag(writer, EBML_THREAD_STATUS_TAG)) {
            ok = false;
            break;
        }
        if (!fwrite(state->data, state->slen + 1, 1, writer->f)) {
            ok = false;
            break;
        }
        ebml_end_tag(writer);

        if (!unwind(binfo, writer, thread_pid))
            ok = false;

        detach_from_thread((pid_t)thread_pid);

        ebml_end_tag(writer);
    }

    closedir(tasks_dir);
    ebml_end_tag(writer);
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

bool profile(struct basic_info *binfo, struct ebml_writer *writer)
{
    if (!ebml_start_tag(writer, EBML_SAMPLES_TAG))
        return false;

    // We have neither signalfd() nor sigwaitinfo() on Android so we have to do
    // this dumb thing with socketpair() to get a performant event model.
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, signal_sockets)) {
        perror("socketpair() failed");
        return false;
    }
    if (pipe(signal_sockets)) {
        perror("pipe() failed");
        return false;
    }

    if (signal(SIGINT, signal_handler)) {
        perror("signal(SIGINT) failed");
        return false;
    }
    if (signal(SIGALRM, signal_handler)) {
        perror("signal(SIGALRM) failed");
        return false;
    }

    // Create the timer
    struct sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    timer_t timer;
    if (timer_create(CLOCK_REALTIME, &sev, &timer)) {
        perror("timer_create() failed");
        return false;
    }

    // Arm the timer
    bool ok;
    struct itimerspec itspec;
    itspec.it_interval.tv_sec = 0;
    itspec.it_interval.tv_nsec = 10000000;  // 10ms
    itspec.it_value = itspec.it_interval;
    if (timer_settime(timer, 0, &itspec, NULL)) {
        perror("timer_settime() failed");
        ok = false;
        goto out;
    }

    int8_t sig;
    while (read(signal_sockets[0], &sig, sizeof(sig))) {
        switch (sig) {
        case SIGALRM:
            if (!(ok = sample(binfo, writer)))
                goto out;
            break;
        case SIGINT:
            goto out;
        }
    }

out:
    timer_delete(timer);
    ebml_end_tag(writer);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: piranha PID\n");
        return 1;
    }

    struct ebml_writer ebml_writer;
    memset(&ebml_writer, '\0', sizeof(ebml_writer));
    if (!(ebml_writer.f = fopen("profile.ebml", "wb"))) {
        perror("Couldn't open \"profile.ebml\"");
        return 1;
    }

    bool ok = true;
    struct tagbstring format_name = bsStatic("piranha-samples");
    if (!ebml_write_header(&ebml_writer, &format_name)) {
        fprintf(stderr, "Couldn't write header\n");
        goto out;
    }

    struct basic_info binfo;
    binfo.pid = strtol(argv[1], NULL, 0);
    if (!compute_thread_entry(&binfo)) {
        ok = false;
        goto out;
    }
    if (!read_maps(binfo.pid, &binfo.maps)) {
        ok = false;
        goto out;
    }
    print_maps(&ebml_writer, binfo.maps);

    ok = profile(&binfo, &ebml_writer);

out:
    bdestroy(binfo.maps);
    ebml_finish(&ebml_writer);
    return !ok;
}

