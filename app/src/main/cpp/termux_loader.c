#include <jni.h>
#include <android/log.h>
#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <linux/auxvec.h>
#include <asm/unistd.h>

#define ALIGN_DOWN(base, size) ((base) & -((__typeof__(base))(size)))
#define ALIGN_UP(base, size)   ALIGN_DOWN((base) + (size) - 1, (size))

static size_t PAGE_SIZE_VAR = 4096;

static void fatal(const char* msg) {
    __android_log_print(ANDROID_LOG_FATAL, "TermuxLoader", "%s: %s", msg, strerror(errno));
    exit(1);
}

static uintptr_t loader_mmap(uintptr_t addr, size_t size, int prot, int flags, int fd, off_t offset) {
    void* ret = mmap((void*)addr, size, prot, flags, fd, offset);
    if (ret == MAP_FAILED) {
        fatal("mmap failed");
    }
    return (uintptr_t)ret;
}

static void read_full(int fd, void* buf, size_t count, off_t offset) {
    char* ptr = buf;
    while (count > 0) {
        ssize_t r = pread(fd, ptr, count, offset);
        if (r < 0) {
            if (errno == EINTR) continue;
            fatal("pread failed");
        }
        if (r == 0) fatal("pread unexpected EOF");
        count -= r;
        ptr += r;
        offset += r;
    }
}

static uintptr_t load_elf_segments(int fd, const Elf64_Ehdr* ehdr, const Elf64_Phdr* phdrs, uintptr_t* out_load_bias) {
    uintptr_t min_vaddr = (uintptr_t)-1;
    uintptr_t max_vaddr = 0;
    
    for (int i = 0; i < ehdr->e_phnum; ++i) {
        const Elf64_Phdr* phdr = &phdrs[i];
        if (phdr->p_type == PT_LOAD) {
            if (phdr->p_vaddr < min_vaddr) min_vaddr = phdr->p_vaddr;
            if (phdr->p_vaddr + phdr->p_memsz > max_vaddr) max_vaddr = phdr->p_vaddr + phdr->p_memsz;
        }
    }
    
    min_vaddr = ALIGN_DOWN(min_vaddr, PAGE_SIZE_VAR);
    max_vaddr = ALIGN_UP(max_vaddr, PAGE_SIZE_VAR);
    
    size_t total_size = max_vaddr - min_vaddr;
    uintptr_t load_bias = 0;
    
    void* base = mmap(NULL, total_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) fatal("mmap total reservation failed");
    
    if (ehdr->e_type == ET_DYN) {
        load_bias = (uintptr_t)base - min_vaddr;
    } else {
        load_bias = 0;
        munmap(base, total_size); 
    }
    
    if (out_load_bias) *out_load_bias = load_bias;

    for (int i = 0; i < ehdr->e_phnum; ++i) {
        const Elf64_Phdr* phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) continue;
        
        uintptr_t seg_start = load_bias + phdr->p_vaddr;
        uintptr_t seg_end   = seg_start + phdr->p_memsz;
        
        uintptr_t seg_page_start = ALIGN_DOWN(seg_start, PAGE_SIZE_VAR);
        uintptr_t seg_page_end   = ALIGN_UP(seg_end, PAGE_SIZE_VAR);
        
        uintptr_t file_start = load_bias + phdr->p_vaddr;
        uintptr_t file_end   = file_start + phdr->p_filesz;
        
        int prot = 0;
        if (phdr->p_flags & PF_R) prot |= PROT_READ;
        if (phdr->p_flags & PF_W) prot |= PROT_WRITE;
        if (phdr->p_flags & PF_X) prot |= PROT_EXEC;
        
        int map_prot = PROT_WRITE; 
        
        void* seg_addr = mmap((void*)seg_page_start, seg_page_end - seg_page_start, 
                              map_prot, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
        if (seg_addr == MAP_FAILED) fatal("mmap segment failed");
        
        uintptr_t offset_in_page = seg_start - seg_page_start;
        read_full(fd, (void*)(seg_page_start + offset_in_page), phdr->p_filesz, phdr->p_offset);

        mprotect((void*)seg_page_start, seg_page_end - seg_page_start, prot);
    }
    
    return load_bias;
}

static void setup_stack(uintptr_t stack_top, int argc, char** argv, char** envp, Elf64_auxv_t* auxv) {
    uintptr_t sp = stack_top;

    int envc = 0;
    while (envp[envc]) envc++;

    size_t str_size = 0;
    for (int i = 0; i < argc; i++) str_size += strlen(argv[i]) + 1;
    for (int i = 0; i < envc; i++) str_size += strlen(envp[i]) + 1;
    
    sp -= (str_size + 15) & ~15;
    uintptr_t str_base = sp;
    
    char** new_argv = malloc(sizeof(char*) * (argc + 1));
    char** new_envp = malloc(sizeof(char*) * (envc + 1));
    
    char* str_cursor = (char*)str_base;
    for (int i = 0; i < argc; i++) {
        strcpy(str_cursor, argv[i]);
        new_argv[i] = str_cursor;
        str_cursor += strlen(argv[i]) + 1;
    }
    new_argv[argc] = NULL;
    
    for (int i = 0; i < envc; i++) {
        strcpy(str_cursor, envp[i]);
        new_envp[i] = str_cursor;
        str_cursor += strlen(envp[i]) + 1;
    }
    new_envp[envc] = NULL;

    uint8_t random_bytes[16];
    int fd = open("/dev/urandom", O_RDONLY);
    read(fd, random_bytes, 16);
    close(fd);
    sp -= 16;
    memcpy((void*)sp, random_bytes, 16);
    uintptr_t random_ptr = sp;

    int auxv_count = 0;
    while (auxv[auxv_count].a_type != AT_NULL) auxv_count++;
    
    sp -= sizeof(Elf64_auxv_t) * (auxv_count + 1);
    Elf64_auxv_t* new_auxv = (Elf64_auxv_t*)sp;
    for (int i = 0; i < auxv_count; i++) {
        new_auxv[i] = auxv[i];
        if (new_auxv[i].a_type == AT_RANDOM) {
            new_auxv[i].a_un.a_val = random_ptr;
        }
    }
    new_auxv[auxv_count].a_type = AT_NULL;
    new_auxv[auxv_count].a_un.a_val = 0;

    sp -= sizeof(char*) * (envc + 1);
    memcpy((void*)sp, new_envp, sizeof(char*) * (envc + 1));

    sp -= sizeof(char*) * (argc + 1);
    memcpy((void*)sp, new_argv, sizeof(char*) * (argc + 1));

    sp -= sizeof(long);
    *(long*)sp = argc;
    
    free(new_argv);
    free(new_envp);
    
    asm volatile(
        "mov sp, %0\n"
        "mov x0, 0\n"
        "mov x1, 0\n"
        "mov x2, 0\n"
        "mov x3, 0\n"
        "br %1\n"
        : : "r"(sp), "r"(auxv[0].a_un.a_val) : "memory"
    );
}

JNIEXPORT jint JNICALL
Java_com_termux_app_TermuxService_nativeStartSession(
        JNIEnv* env,
        jobject thiz,
        jstring jPath,
        jobjectArray jArgs,
        jobjectArray jEnv
) {
    PAGE_SIZE_VAR = sysconf(_SC_PAGESIZE);

    const char* path = (*env)->GetStringUTFChars(env, jPath, 0);
    
    int argc = (*env)->GetArrayLength(env, jArgs);
    char** argv = malloc(sizeof(char*) * (argc + 1));
    for(int i=0; i<argc; i++) {
        jstring s = (jstring)(*env)->GetObjectArrayElement(env, jArgs, i);
        argv[i] = (char*)(*env)->GetStringUTFChars(env, s, 0);
    }
    argv[argc] = NULL;
    
    int envc = (*env)->GetArrayLength(env, jEnv);
    char** envp = malloc(sizeof(char*) * (envc + 1));
    for(int i=0; i<envc; i++) {
        jstring s = (jstring)(*env)->GetObjectArrayElement(env, jEnv, i);
        envp[i] = (char*)(*env)->GetStringUTFChars(env, s, 0);
    }
    envp[envc] = NULL;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid > 0) {
        return pid; 
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) fatal("Cannot open executable");

    Elf64_Ehdr ehdr;
    read_full(fd, &ehdr, sizeof(ehdr), 0);

    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 || ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        fatal("Invalid ELF header");
    }

    size_t phdr_size = ehdr.e_phnum * ehdr.e_phentsize;
    Elf64_Phdr* phdrs = malloc(phdr_size);
    read_full(fd, phdrs, phdr_size, ehdr.e_phoff);

    char interp_buf[256] = {0};
    int has_interp = 0;
    
    for(int i=0; i<ehdr.e_phnum; i++) {
        if(phdrs[i].p_type == PT_INTERP) {
            read_full(fd, interp_buf, phdrs[i].p_filesz, phdrs[i].p_offset);
            interp_buf[phdrs[i].p_filesz] = 0;
            has_interp = 1;
            break;
        }
    }

    uintptr_t entry_point = 0;
    uintptr_t interp_bias = 0;
    uintptr_t exec_bias = 0;
    
    if (has_interp) {
        int ifd = open(interp_buf, O_RDONLY);
        if (ifd < 0) fatal("Cannot open interpreter");
        
        Elf64_Ehdr iehdr;
        read_full(ifd, &iehdr, sizeof(iehdr), 0);
        Elf64_Phdr* iphdrs = malloc(iehdr.e_phnum * iehdr.e_phentsize);
        read_full(ifd, iphdrs, iehdr.e_phnum * iehdr.e_phentsize, iehdr.e_phoff);
        
        load_elf_segments(ifd, &iehdr, iphdrs, &interp_bias);
        entry_point = interp_bias + iehdr.e_entry;
        close(ifd);
        free(iphdrs);
        
        exec_bias = 0; 
    } else {
        load_elf_segments(fd, &ehdr, phdrs, &exec_bias);
        entry_point = exec_bias + ehdr.e_entry;
    }

    void* stack = mmap(NULL, 1024 * 1024 * 8, PROT_READ | PROT_WRITE, 
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) fatal("Stack allocation failed");
    uintptr_t stack_top = (uintptr_t)stack + 1024 * 1024 * 8;

    Elf64_auxv_t auxv[] = {
        {AT_PHDR,  exec_bias + ehdr.e_phoff},
        {AT_PHNUM, ehdr.e_phnum},
        {AT_PHENT, ehdr.e_phentsize},
        {AT_ENTRY, exec_bias + ehdr.e_entry}, 
        {AT_UID,   getuid()},
        {AT_EUID,  geteuid()},
        {AT_GID,   getgid()},
        {AT_EGID,  getegid()},
        {AT_SECURE, 0},
        {AT_PAGESZ, PAGE_SIZE_VAR},
        {AT_BASE,  interp_bias}, 
        {AT_FLAGS, 0},
        {AT_RANDOM, 0}, 
        {AT_NULL,  0}
    };
    
    if (!has_interp) {
        auxv[0].a_type = AT_NULL; 
        auxv[0].a_un.a_val = 0;
    }

    setup_stack(stack_top, argc, argv, envp, auxv);
    
    exit(0);
}
