#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

volatile sig_atomic_t filediffadvanced_stop = 0;

void filediffadvanced_handle_sigint(int sig)
{
    filediffadvanced_stop = 1;
}

void filediffadvanced_usage() {
    printf("Usage: filediffadvanced -f <file1> -s <file2> [-b] [-t]\n");
    printf("  -b   binary mode\n");
    printf("  -t   text mode\n");
}

/* thread argument for parallel binary comparison */
typedef struct {
    char   *data1;
    char   *data2;
    size_t  start;
    size_t  end;
    size_t  diffs;
} ThreadArg;

void *compare_chunk(void *arg) {
    ThreadArg *t = (ThreadArg *)arg;
    t->diffs = 0;
    for (size_t i = t->start; i < t->end && !filediffadvanced_stop; i++)
    {
        if (t->data1[i] != t->data2[i])
            t->diffs++;
    }
    return NULL;
}

void filediffadvanced(int argc, char *argv[]) {
    optind = 1;
    char *file1 = NULL, *file2 = NULL;
    int binary = 0, text = 0, opt;

    while ((opt = getopt(argc, argv, "f:s:bth")) != -1) {
        switch (opt) {
            case 'f': file1  = optarg; break;
            case 's': file2  = optarg; break;
            case 'b': binary = 1;      break;
            case 't': text   = 1;      break;
            case 'h': filediffadvanced_usage(); return;
            default:  filediffadvanced_usage(); return;
        }
    }

    if (!file1 || !file2) {
        filediffadvanced_usage();
        return;
    }

    signal(SIGINT, filediffadvanced_handle_sigint);

    /* open and mmap file 1 */
    int fd1 = open(file1, O_RDONLY);
    if (fd1 < 0) { perror("open file1"); return; }
    struct stat st1;
    fstat(fd1, &st1);
    size_t size1 = st1.st_size;
    char *data1 = mmap(NULL, size1, PROT_READ, MAP_PRIVATE, fd1, 0);
    if (data1 == MAP_FAILED) { perror("mmap file1"); close(fd1); return; }

    /* open and mmap file 2 */
    int fd2 = open(file2, O_RDONLY);
    if (fd2 < 0) { perror("open file2"); munmap(data1, size1); close(fd1); return; }
    struct stat st2;
    fstat(fd2, &st2);
    size_t size2 = st2.st_size;
    char *data2 = mmap(NULL, size2, PROT_READ, MAP_PRIVATE, fd2, 0);
    if (data2 == MAP_FAILED) { perror("mmap file2"); munmap(data1, size1); close(fd1); close(fd2); return; }

    /* auto-detect binary if neither flag given: check for NUL bytes */
    if (!binary && !text) {
        size_t probe = size1 < 512 ? size1 : 512;
        for (size_t i = 0; i < probe; i++) {
            if (data1[i] == '\0') { binary = 1; break; }
        }
        if (!binary) text = 1;
    }

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    printf("\n----- filediffadvanced Results -----\n");
    printf("File 1: %s (%zu bytes)\n", file1, size1);
    printf("File 2: %s (%zu bytes)\n", file2, size2);

    if (size1 != size2)
        printf("Size mismatch: files differ in size\n");

    /* BINARY MODE: 4 threads, each checks a chunk */
    if (binary) {
        printf("Mode: binary\n");

        size_t cmp_size = size1 < size2 ? size1 : size2;
        int nthreads = 4;
        pthread_t tids[4];
        ThreadArg args[4];
        size_t chunk = cmp_size / nthreads;

        for (int i = 0; i < nthreads; i++) {
            args[i].data1 = data1;
            args[i].data2 = data2;
            args[i].start = i * chunk;
            args[i].end   = (i == nthreads - 1) ? cmp_size : (size_t)(i + 1) * chunk;
            args[i].diffs = 0;
            pthread_create(&tids[i], NULL, compare_chunk, &args[i]);
        }

        size_t total_diffs = 0;
        for (int i = 0; i < nthreads; i++) {
            pthread_join(tids[i], NULL);
            total_diffs += args[i].diffs;
        }

        if (total_diffs == 0 && size1 == size2)
            printf("Result: files are IDENTICAL\n");
        else {
            printf("Byte differences: %zu\n", total_diffs);
            int shown = 0;
            for (size_t i = 0; i < cmp_size && shown < 8; i++) {
                if (data1[i] != data2[i]) {
                    printf("  offset 0x%zx: 0x%02x vs 0x%02x\n",
                           i, (unsigned char)data1[i], (unsigned char)data2[i]);
                    shown++;
                }
            }
        }
    }

    /* TEXT MODE: line-by-line comparison */
    if (text) {
        printf("Mode: text\n");

        size_t line = 1, diffs = 0;
        size_t i = 0, j = 0;

        while (i < size1 && j < size2 && !filediffadvanced_stop)
        {
            size_t ei = i, ej = j;
            while (ei < size1 && data1[ei] != '\n') ei++;
            while (ej < size2 && data2[ej] != '\n') ej++;

            size_t len1 = ei - i;
            size_t len2 = ej - j;

            if (len1 != len2 || memcmp(data1 + i, data2 + j, len1) != 0) {
                if (diffs < 8)
                    printf("  line %zu differs\n", line);
                diffs++;
            }

            i = ei + 1;
            j = ej + 1;
            line++;
        }

        while (i < size1) { if (data1[i++] == '\n') diffs++; }
        while (j < size2) { if (data2[j++] == '\n') diffs++; }

        if (diffs == 0)
            printf("Result: files are IDENTICAL\n");
        else
            printf("Different lines: %zu\n", diffs);
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed = (t_end.tv_sec - t_start.tv_sec)
                   + (t_end.tv_nsec - t_start.tv_nsec) / 1e9;
    printf("Time elapsed: %.4f sec\n", elapsed);

    if (filediffadvanced_stop)
        printf("[!] Interrupted by user\n");

    printf("\n");

    munmap(data1, size1);
    munmap(data2, size2);
    close(fd1);
    close(fd2);
}
