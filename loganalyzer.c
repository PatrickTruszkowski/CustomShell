#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>

volatile sig_atomic_t loganalyzer_stop = 0;

void handle_sigint(int sig) {
    loganalyzer_stop = 1;
}

void loganalyzer_usage() {
    printf("Usage: loganalyzer -f <file> [-p pattern]\n");
}

void loganalyzer(int argc, char *argv[]) {
    optind = 1;
    char *filename = NULL;
    char *pattern = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "f:p:")) != -1) {
        switch (opt) {
            case 'f':
                filename = optarg;
                break;
            case 'p':
                pattern = optarg;
                break;
            default:
                loganalyzer_usage();
                exit(EXIT_FAILURE);
        }
    }

    if (!filename) {
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, handle_sigint);

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        exit(EXIT_FAILURE);
    }

    size_t filesize = st.st_size;

    char *data = mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(fd);
        exit(EXIT_FAILURE);
    }

    size_t lines = 0;
    size_t matches = 0;

    for (int i = 0; i < filesize && !loganalyzer_stop; i++)
    {
        if (data[i] == '\n') {
            lines++;
        }

        if (pattern) {
            if (i + strlen(pattern) < filesize &&
                strncmp(&data[i], pattern, strlen(pattern)) == 0) {
                matches++;
            }
        }
    }

    printf("\n----- Results -----\n");
    printf("Total Lines: %zu\n", lines);

    if (pattern) {
        printf("Occurrences of '%s': %zu\n", pattern, matches);
    }

    if (loganalyzer_stop)
    {
        printf("\n[!] Interrupted by user\n");
    }

    printf("\n");

    munmap(data, filesize);
    close(fd);
}
