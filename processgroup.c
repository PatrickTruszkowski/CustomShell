#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>

volatile sig_atomic_t interrupted = 0;

void processgroup_handle_signal(int sig) {
    interrupted = sig;
}

void usage(void) {
    printf("Usage: processgroup [-n processes] [-d seconds] [-s signal]\n");
    printf("Example: processgroup -n 3 -d 5 -s 15\n");
}

void processgroup(int argc, char* argv[]) {
    optind = 1;

    int num_processes = 3;
    int delay = 5;
    int sig_to_send = SIGTERM;
    int opt;
    pid_t group_id = 0;
    pid_t* child_pids = NULL;

    while ((opt = getopt(argc, argv, "n:d:s:h")) != -1) {
        switch (opt) {
        case 'n':
            num_processes = atoi(optarg);
            break;
        case 'd':
            delay = atoi(optarg);
            break;
        case 's':
            sig_to_send = atoi(optarg);
            break;
        case 'h':
            usage();
            return;
        default:
            usage();
            return;
        }
    }

    if (num_processes <= 0 || delay < 0) {
        usage();
        return;
    }

    signal(SIGINT, processgroup_handle_signal);
    signal(SIGTERM, processgroup_handle_signal);

    child_pids = malloc(num_processes * sizeof(pid_t));
    if (child_pids == NULL) {
        perror("malloc");
        return;
    }

    printf("Creating %d child processes...\n", num_processes);

    for (int i = 0; i < num_processes; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            free(child_pids);
            return;
        }

        if (pid == 0) {
            if (i == 0) {
                if (setpgid(0, 0) == -1) {
                    perror("setpgid");
                    exit(EXIT_FAILURE);
                }
            }
            else {
                if (setpgid(0, group_id) == -1) {
                    perror("setpgid");
                    exit(EXIT_FAILURE);
                }
            }

            printf("Child %d started | PID = %d | PGID = %d\n", i + 1, getpid(), getpgrp());

            execlp("sleep", "sleep", "30", NULL);
            perror("execlp");
            exit(EXIT_FAILURE);
        }
        else {
            child_pids[i] = pid;

            if (i == 0) {
                group_id = pid;
                if (setpgid(pid, pid) == -1 && errno != EACCES) {
                    perror("setpgid");
                }
            }
            else {
                if (setpgid(pid, group_id) == -1 && errno != EACCES) {
                    perror("setpgid");
                }
            }
        }
    }

    printf("\nProcess Group ID: %d\n", group_id);
    printf("Waiting %d seconds before sending signal %d...\n", delay, sig_to_send);

    sleep(delay);

    if (kill(-group_id, sig_to_send) == -1) {
        perror("kill");
    }
    else {
        printf("Signal %d sent to process group %d\n", sig_to_send, group_id);
    }

    for (int i = 0; i < num_processes; i++) {
        int status;
        pid_t ended = waitpid(child_pids[i], &status, 0);

        if (ended > 0) {
            if (WIFSIGNALED(status)) {
                printf("Child PID %d terminated by signal %d\n", ended, WTERMSIG(status));
            }
            else if (WIFEXITED(status)) {
                printf("Child PID %d exited with code %d\n", ended, WEXITSTATUS(status));
            }
        }
    }

    printf("\n----- processgroup Results -----\n");
    printf("Processes Created: %d\n", num_processes);
    printf("Process Group ID: %d\n", group_id);
    printf("Signal Sent: %d\n", sig_to_send);

    if (interrupted) {
        printf("Interrupted by user signal %d\n", interrupted);
    }

    printf("\n");

    free(child_pids);
}
