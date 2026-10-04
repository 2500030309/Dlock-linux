#include "dlock.h"
#include "filesystem.h"

#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>

int demo_co5_filesystem(void) {
    printf("\n%s%s=== Linux Filesystem Inode & Metadata Inspection ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    char filepath[256] = "Makefile";

    if (is_interactive_tty()) {
        char line[256];
        printf("Enter file path to inspect with stat() [default: Makefile]: ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) && strlen(line) > 1) {
            size_t len = strlen(line);
            if (line[len - 1] == '\n') line[len - 1] = '\0';
            snprintf(filepath, sizeof(filepath), "%s", line);
        }
    }

    struct stat st;
    if (stat(filepath, &st) < 0) {
        perror("stat failed on path");
        return 1;
    }

    char time_str[64];
    struct tm tm_buf;
    localtime_r(&st.st_mtime, &tm_buf);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

    printf("\n  Metadata for '%s':\n", filepath);
    printf("    Inode Number         (st_ino)     : %lu\n", (unsigned long)st.st_ino);
    printf("    Device ID            (st_dev)     : %lu\n", (unsigned long)st.st_dev);
    printf("    Permissions          (st_mode)    : %o (octal)\n", st.st_mode & 0777);
    printf("    Hard Links           (st_nlink)   : %lu\n", (unsigned long)st.st_nlink);
    printf("    Owner UID / GID                   : %u / %u\n", (unsigned int)st.st_uid, (unsigned int)st.st_gid);
    printf("    File Size                         : %ld bytes\n", (long)st.st_size);
    printf("    Block Size / Blocks               : %ld bytes / %ld blocks\n", (long)st.st_blksize, (long)st.st_blocks);
    printf("    Last Modification                 : %s\n\n", time_str);

    printf("  Directory Inspection (Current Working Directory):\n");
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        int count = 0;
        while ((ent = readdir(dir)) != NULL && count < 8) {
            printf("    Entry: %-18s | Inode: %-12lu | Type: 0x%x\n",
                   ent->d_name, (unsigned long)ent->d_ino, ent->d_type);
            count++;
        }
        closedir(dir);
    }
    printf("\n");
    return 0;
}

int demo_co5_buffered(void) {
    printf("\n%s%s=== Buffered (stdio) vs Unbuffered (Direct Syscall) I/O ===%s\n",
           COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    const char *buf_path = "/tmp/dlock_buffered_demo.txt";
    const char *unbuf_path = "/tmp/dlock_unbuffered_demo.txt";

    /* Buffered I/O via stdio */
    FILE *fp = fopen(buf_path, "w");
    if (fp) {
        fprintf(fp, "Buffered Line: stdio user-space stream with internal buffer.\n");
        fclose(fp);
        printf("  [Buffered]   Written via fopen/fprintf/fclose (stdio buffer: %d bytes)\n", BUFSIZ);
    }

    /* Unbuffered I/O via POSIX direct syscalls */
    int fd = open(unbuf_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        const char *msg = "Unbuffered Line: written directly via write() system call without caching.\n";
        ssize_t w = write(fd, msg, strlen(msg));
        (void)w;
        close(fd);
        printf("  [Unbuffered] Written directly via open/write/close system calls\n\n");
    }

    unlink(buf_path);
    unlink(unbuf_path);
    return 0;
}

int demo_co5_mmapfile(void) {
    printf("\n%s%s=== Memory-Mapped File I/O (mmap / msync) ===%s\n", COLOR_CYAN, COLOR_BOLD, COLOR_RESET);

    const char *filepath = "/tmp/dlock_mmap_demo.bin";
    size_t file_size = 4096;

    int fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open failed");
        return 1;
    }

    if (ftruncate(fd, file_size) < 0) {
        perror("ftruncate failed");
        close(fd);
        return 1;
    }

    char *map = (char *)mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (map == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    snprintf(map, file_size, "DLOCK MMAP RECORD: Page Cache Direct Write without read/write syscall overhead!");
    msync(map, file_size, MS_SYNC);

    printf("  Mapped %zu bytes at memory address %p\n", file_size, (void*)map);
    printf("  Read back directly from memory: \"%s\"\n", map);

    munmap(map, file_size);
    unlink(filepath);
    printf("  %s[RESULT] Memory-mapped file I/O synchronized and unmapped cleanly.%s\n\n",
           COLOR_GREEN, COLOR_RESET);
    return 0;
}
