/* app/src/main/cpp/termux-bootstrap.c */
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <linux/elf.h>
#include <termios.h>
#include <pty.h>
#include <dlfcn.h>

#define ALIGN_DOWN(base, size) ((base) & -((typeof(base)) (size)))
#define ALIGN_UP(base, size)   ALIGN_DOWN((base) + (size) - 1, (size))

static const int PAGE_SIZE_4K = 4096;
static const int PAGE_SIZE_16K = 16384;

extern const unsigned char blob[];
extern const int32_t blob_size;

static int validate_elf(Elf64_Ehdr *ehdr) {
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) return 0;
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) return 0;
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) return 0;
    if (ehdr->e_machine != EM_AARCH64) return 0;
    return 1;
}

static uintptr_t load_elf_segments(int fd, Elf64_Ehdr *ehdr, uintptr_t *base_addr) {
    Elf64_Phdr *phdr = malloc(ehdr->e_phnum * sizeof(Elf64_Phdr));
    if (!phdr) return 0;
    lseek(fd, ehdr->e_phoff, SEEK_SET);
    read(fd, phdr, ehdr->e_phnum * sizeof(Elf64_Phdr));

    uintptr_t min_vaddr = (uintptr_t)-1;
    uintptr_t max_vaddr = 0;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            if (phdr[i].p_vaddr < min_vaddr) min_vaddr = phdr[i].p_vaddr;
            if (phdr[i].p_vaddr + phdr[i].p_memsz > max_vaddr) max_vaddr = phdr[i].p_vaddr + phdr[i].p_memsz;
        }
    }

    size_t total_size = ALIGN_UP(max_vaddr - min_vaddr, PAGE_SIZE_16K);

    void *map = mmap(NULL, total_size, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED) {
        free(phdr);
        return 0;
    }

    *base_addr = (uintptr_t)map;

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uintptr_t seg_start = (uintptr_t)map + (phdr[i].p_vaddr - min_vaddr);

            lseek(fd, phdr[i].p_offset, SEEK_SET);
            read(fd, (void*)seg_start, phdr[i].p_filesz);

            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                memset((void*)(seg_start + phdr[i].p_filesz), 0, phdr[i].p_memsz - phdr[i].p_filesz);
            }

            int prot = 0;
            if (phdr[i].p_flags & PF_R) prot |= PROT_READ;
            if (phdr[i].p_flags & PF_W) prot |= PROT_WRITE;
            if (phdr[i].p_flags & PF_X) prot |= PROT_EXEC;

            uintptr_t prot_start = ALIGN_DOWN(seg_start, PAGE_SIZE_16K);
            uintptr_t prot_end = ALIGN_UP(seg_start + phdr[i].p_memsz, PAGE_SIZE_16K);

            mprotect((void*)prot_start, prot_end - prot_start, prot);
        }
    }

    uintptr_t entry = (uintptr_t)map + (ehdr->e_entry - min_vaddr);
    free(phdr);
    return entry;
}

static void *setup_stack(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    size_t stack_size = 1024 * 1024 * 8;
    void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) return NULL;
    uintptr_t sp = (uintptr_t)stack + stack_size;

    sp = (sp & ~15);

    int envc = 0;
    while (envp[envc]) envc++;

    sp -= sizeof(Elf64_auxv_t) * 32;
    Elf64_auxv_t *auxv_ptr = (Elf64_auxv_t *)sp;
    int aux_idx = 0;
    while (auxv[aux_idx].a_type != AT_NULL) {
        auxv_ptr[aux_idx] = auxv[aux_idx];
        aux_idx++;
    }
    auxv_ptr[aux_idx].a_type = AT_NULL;
    auxv_ptr[aux_idx].a_un.a_val = 0;

    sp -= (envc + 1) * sizeof(char*);
    char **env_ptr = (char**)sp;
    for (int i = 0; i < envc; i++) env_ptr[i] = envp[i];
    env_ptr[envc] = NULL;

    sp -= (argc + 1) * sizeof(char*);
    char **arg_ptr = (char**)sp;
    for (int i = 0; i < argc; i++) arg_ptr[i] = argv[i];
    arg_ptr[argc] = NULL;

    sp -= sizeof(long);
    *(long*)sp = argc;

    return (void*)sp;
}

JNIEXPORT jobject JNICALL Java_com_termux_app_service_ServiceExecutionManager_nativeStartSession(
    JNIEnv *env, jclass clazz, jstring cmd, jobjectArray args, jobjectArray envVars) {

    const char *cmd_cstr = (*env)->GetStringUTFChars(env, cmd, NULL);
    int argc = (*env)->GetArrayLength(env, args);
    char **argv = malloc(sizeof(char*) * (argc + 2));
    argv[0] = strdup(cmd_cstr);
    for (int i = 0; i < argc; i++) {
        jstring arg = (jstring)(*env)->GetObjectArrayElement(env, args, i);
        const char *arg_cstr = (*env)->GetStringUTFChars(env, arg, NULL);
        argv[i + 1] = strdup(arg_cstr);
        (*env)->ReleaseStringUTFChars(env, arg, arg_cstr);
    }
    argv[argc + 1] = NULL;
    argc++;

    int envc = (*env)->GetArrayLength(env, envVars);
    char **envp = malloc(sizeof(char*) * (envc + 1));
    for (int i = 0; i < envc; i++) {
        jstring envVar = (jstring)(*env)->GetObjectArrayElement(env, envVars, i);
        const char *env_cstr = (*env)->GetStringUTFChars(env, envVar, NULL);
        envp[i] = strdup(env_cstr);
        (*env)->ReleaseStringUTFChars(env, envVar, env_cstr);
    }
    envp[envc] = NULL;

    (*env)->ReleaseStringUTFChars(env, cmd, cmd_cstr);

    int master, slave;
    char pty_name[100];
    if (openpty(&master, &slave, pty_name, NULL, NULL) == -1) {
        return NULL;
    }

    pid_t pid = fork();

    if (pid < 0) {
        close(master);
        close(slave);
        return NULL;
    }

    if (pid == 0) {
        close(master);
        setsid();
        if (ioctl(slave, TIOCSCTTY, NULL) == -1) {
        }

        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2) close(slave);

        int fd = open(argv[0], O_RDONLY);
        if (fd < 0) exit(1);

        Elf64_Ehdr ehdr;
        read(fd, &ehdr, sizeof(ehdr));

        if (!validate_elf(&ehdr)) exit(2);

        uintptr_t interp_entry = 0;
        uintptr_t elf_base = 0;
        uintptr_t entry_point = 0;

        Elf64_Phdr *phdrs = malloc(ehdr.e_phnum * sizeof(Elf64_Phdr));
        lseek(fd, ehdr.e_phoff, SEEK_SET);
        read(fd, phdrs, ehdr.e_phnum * sizeof(Elf64_Phdr));

        char interp_path[256] = {0};
        for (int i = 0; i < ehdr.e_phnum; i++) {
            if (phdrs[i].p_type == PT_INTERP) {
                lseek(fd, phdrs[i].p_offset, SEEK_SET);
                read(fd, interp_path, phdrs[i].p_filesz);
                break;
            }
        }
        free(phdrs);

        entry_point = load_elf_segments(fd, &ehdr, &elf_base);
        close(fd);

        if (strlen(interp_path) > 0) {
            int lfd = open(interp_path, O_RDONLY);
            if (lfd >= 0) {
                Elf64_Ehdr l_ehdr;
                read(lfd, &l_ehdr, sizeof(l_ehdr));
                uintptr_t linker_base = 0;
                interp_entry = load_elf_segments(lfd, &l_ehdr, &linker_base);
                close(lfd);

                Elf64_auxv_t auxv[] = {
                    {AT_PHDR, { .a_val = elf_base + ehdr.e_phoff }},
                    {AT_PHENT, { .a_val = ehdr.e_phentsize }},
                    {AT_PHNUM, { .a_val = ehdr.e_phnum }},
                    {AT_ENTRY, { .a_val = entry_point }},
                    {AT_BASE, { .a_val = linker_base }},
                    {AT_UID, { .a_val = getuid() }},
                    {AT_EUID, { .a_val = geteuid() }},
                    {AT_GID, { .a_val = getgid() }},
                    {AT_EGID, { .a_val = getegid() }},
                    {AT_NULL, { .a_val = 0 }}
                };

                void *sp = setup_stack(argc, argv, envp, auxv);

                asm volatile (
                    "mov sp, %0\n"
                    "mov x0, 0\n"
                    "br %1\n"
                    : : "r"(sp), "r"(interp_entry) : "memory"
                );
            }
        } else {
            Elf64_auxv_t auxv[] = {
                {AT_PHDR, { .a_val = elf_base + ehdr.e_phoff }},
                {AT_PHENT, { .a_val = ehdr.e_phentsize }},
                {AT_PHNUM, { .a_val = ehdr.e_phnum }},
                {AT_ENTRY, { .a_val = entry_point }},
                {AT_NULL, { .a_val = 0 }}
            };
            void *sp = setup_stack(argc, argv, envp, auxv);

            asm volatile (
                "mov sp, %0\n"
                "mov x0, 0\n"
                "br %1\n"
                : : "r"(sp), "r"(entry_point) : "memory"
            );
        }

        exit(127);
    }

    close(slave);

    jintArray result = (*env)->NewIntArray(env, 2);
    jint temp[2];
    temp[0] = (jint)pid;
    temp[1] = (jint)master;
    (*env)->SetIntArrayRegion(env, result, 0, 2, temp);

    return result;
}

JNIEXPORT jbyteArray JNICALL Java_com_termux_app_TermuxInstaller_getZip(JNIEnv *env, jclass clazz) {
    (void)clazz;
    if (blob_size <= 0) return NULL;
    jbyteArray ret = (*env)->NewByteArray(env, (jsize)blob_size);
    if (ret == NULL) return NULL;
    (*env)->SetByteArrayRegion(env, ret, 0, (jsize)blob_size, (const jbyte *)blob);
    return ret;
}