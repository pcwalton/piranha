/*
 * piranha/piranha.c
 *
 * Tiny Android profiler
 *
 * Copyright (c) 2011 Mozilla Foundation
 * Patrick Walton <pcwalton@mimiga.net>
 */

#include <linux/ptrace.h>
#include <sys/mman.h>
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
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "bstrlib.h"

// The length of Bionic's __thread_entry routine. This is obviously a gross
// hack, of which I am ashamed.
#define THREAD_ENTRY_LENGTH     0x3c

#define EBML_HEADER_TAG         0x1a45dfa3
#define EBML_MEMORY_MAP_TAG     0x81          // root level
#define EBML_MEMORY_REGION_TAG  0x82          // contained by MEMORY_MAP
#define EBML_SAMPLES_TAG        0x83          // root level
#define EBML_SAMPLE_TAG         0x84          // contained by SAMPLES
#define EBML_THREAD_SAMPLE_TAG  0x85          // contained by SAMPLE
#define EBML_THREAD_STATUS_TAG  0x86          // contained by THREAD_SAMPLE
#define EBML_STACK_TAG          0x87          // contained by THREAD_SAMPLE
#define EBML_SYMBOLS_TAG        0x88          // root level
#define EBML_MODULE_TAG         0x89          // contained by SYMBOLS
#define EBML_MODULE_NAME_TAG    0x8a          // contained by MODULE
#define EBML_SYMBOL_TAG         0x8b          // contained by MODULE
#define EBML_THREAD_PID_TAG     0x8c          // contained by THREAD_SAMPLE

#define PENDING_SIGNAL_NONE     0
#define PENDING_SIGNAL_TICK     1
#define PENDING_SIGNAL_STOP     2

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
    int mem;
    uint32_t mem_offset;
};

struct ebml_writer {
    FILE *f;
    uint32_t tag_offsets[4];
    int tag_stack_size;
};

volatile int pending_signal = PENDING_SIGNAL_NONE;

//
// EBML writing
//

bool ebml_start_tag(struct ebml_writer *writer, uint32_t tag_id)
{
    assert(writer->tag_stack_size < length_of(writer->tag_offsets));

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

    if (pending_signal == PENDING_SIGNAL_NONE) {
        uint8_t b = 0;
        write(signal_sockets[1], &b, sizeof(b));
    }

    if (which == SIGINT)
        pending_signal = PENDING_SIGNAL_STOP;
    else if (pending_signal < PENDING_SIGNAL_STOP)
        pending_signal = PENDING_SIGNAL_TICK;
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

// Grabs a word from the stack.
bool peek(struct basic_info *binfo, uint32_t addr, uint32_t *out)
{
    static int fast_paths = 0, slow_paths = 0;

    /*if (addr == binfo->mem_offset) {
        fast_paths++;
    } else {*/
        // Slow path: reposition
        slow_paths++;
        if (lseek64(binfo->mem, addr, SEEK_SET) == -1) {
            binfo->mem_offset = 0;
            perror("failed seek");
            return false;
        }
        assert(lseek64(binfo->mem, 0, SEEK_CUR) == addr);
    //}

    /*if ((fast_paths + slow_paths) % 10000 == 0)
        printf("fast: %d slow: %d\n", fast_paths, slow_paths);*/

    bool ok = read(binfo->mem, out, 4) == 4;

    /*static int zeroes = 0, nonzeroes = 0;
    if (!*out == 0)
        zeroes++;
    else
        nonzeroes++;
    if (!((zeroes + nonzeroes) % 10000))
        printf("zeroes: %d, nonzeroes=%d\n", zeroes, nonzeroes);*/

    binfo->mem_offset = addr + 4;
    return ok;
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

    assert(!(sp % 4));

#ifdef DEBUG_STACK_WALKING
    printf(" /* sp: %08x */", sp);
#endif

    static int loop_count = 0;

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
            if (!peek(binfo, sp, &maybe_lr)) {
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
    struct tagbstring dev_ashmem_lib = bsStatic("/dev/ashmem/lib");
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

    struct map ashmem_map;
    bool reading_ashmem_map = false;

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

        if (field_count < 4)
            continue;

        map.name = bfromcstr(name);

        // If we're reading an ashmem library, check for the end now.
        if (reading_ashmem_map && bstrcmp(ashmem_map.name, map.name)) {
            // We reached the end.
            ashmem_map.end = map.start;

            if (bcatblk(*maps, &ashmem_map, sizeof(ashmem_map))
                    != BSTR_OK) {
                bdestroy(ashmem_map.name);
                break;
            }

            reading_ashmem_map = false;
        }

        // Check whether this is an ashmem library (as used in Fennec's dynamic
        // linker).
        if (!reading_ashmem_map && !binstr(map.name, 0, &dev_ashmem_lib)) {
            ashmem_map = map;
            reading_ashmem_map = true;
            continue;
        }

        // If we got here and we're still reading the ashmem map, then discard
        // the current map; it's part of the ashmem map we're still reading.
        if (reading_ashmem_map) {
            bdestroy(map.name);
            continue;
        }

        if (bcatblk(*maps, &map, sizeof(map)) != BSTR_OK) {
            bdestroy(map.name);
            break;
        }
    }

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
        val = htonl(map->offset);
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

bool wait_for_thread_attachment(pid_t thread_pid)
{
    int status;
    return waitpid(thread_pid, &status, __WCLONE) >= 0;
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

    if (ptrace(PTRACE_ATTACH, binfo->pid, NULL, NULL))
        return false;
    if (!wait_for_process_to_stop(binfo->pid))
        goto out;

    DIR *tasks_dir = opendir((char *)tasks_path->data);
    bdestroy(tasks_path);
    if (!tasks_dir) {
        perror("Failed to open /proc/x/task");
        goto out;
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

        // Attach to the thread if we need to.
        if (binfo->pid != thread_pid) {
            if (ptrace(PTRACE_ATTACH, thread_pid, NULL, NULL))
                continue;
            if (!wait_for_thread_attachment(thread_pid))
                continue;
        }

        if (!ebml_start_tag(writer, EBML_THREAD_SAMPLE_TAG)) {
            ok = false;
            break;
        }

        if (!ebml_start_tag(writer, EBML_THREAD_PID_TAG)) {
            ok = false;
            break;
        }
        uint32_t pid_buf = htonl(thread_pid);
        if (!fwrite(&pid_buf, sizeof(pid_buf), 1, writer->f)) {
            ok = false;
            break;
        }
        ebml_end_tag(writer);

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

        if (binfo->pid != thread_pid)
            detach_from_thread((pid_t)thread_pid);

        ebml_end_tag(writer);
    }

    closedir(tasks_dir);

out:
    if (ptrace(PTRACE_DETACH, binfo->pid, NULL, NULL))
        perror("Failed to detach from process");
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

    // We have neither signalfd() nor sigwaitinfo() on Android, so we have to
    // do this dumb thing with socketpair() to get a performant event model.
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

    uint8_t b;
    while (read(signal_sockets[0], &b, sizeof(b))) {
        int sig = pending_signal;
        pending_signal = PENDING_SIGNAL_NONE;

        switch (sig) {
        case PENDING_SIGNAL_TICK:
            if (!(ok = sample(binfo, writer)))
                goto out;
            break;
        case PENDING_SIGNAL_STOP:
            goto out;
        }
    }

out:
    timer_delete(timer);
    ebml_end_tag(writer);
    return ok;
}

bool open_memory(struct basic_info *binfo)
{
    bstring mem_path = bformat("/proc/%d/mem", (int)binfo->pid);
    if (!mem_path)
        return false;

    bool ok = (binfo->mem = open((char *)mem_path->data, O_RDONLY)) >= 0;
    binfo->mem_offset = 0;

    bdestroy(mem_path);
    return ok;
}

void usage()
{
    fprintf(stderr, "usage: piranha [-o FILE] PID\n");
    exit(1);
}

int main(int argc, char **argv)
{
    char *out_path = "profile.ebml";
    int ch;
    while ((ch = getopt(argc, argv, "o:")) != -1) {
        switch (ch) {
        case 'o':
            out_path = optarg;
            break;
        default:
            usage();
            break;
        }
    }

    if (argc - optind != 1) {
        fprintf(stderr, "usage: piranha PID\n");
        return 1;
    }

    struct ebml_writer ebml_writer;
    memset(&ebml_writer, '\0', sizeof(ebml_writer));
    if (!(ebml_writer.f = fopen(out_path, "wb"))) {
        perror("Couldn't open the output file");
        return 1;
    }

    bool ok = true;
    struct tagbstring format_name = bsStatic("piranha-samples");
    if (!ebml_write_header(&ebml_writer, &format_name)) {
        fprintf(stderr, "Couldn't write header\n");
        goto out;
    }

    // Initialize the basic info structure
    struct basic_info binfo;
    memset(&binfo, '\0', sizeof(binfo));
    binfo.pid = strtol(argv[optind], NULL, 0);
    if (!compute_thread_entry(&binfo)) {
        ok = false;
        goto out;
    }
    if (!open_memory(&binfo)) {
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
    close(binfo.mem);
    ebml_finish(&ebml_writer);
    return !ok;
}

