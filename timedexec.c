#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

volatile sig_atomic_t child_pid = -1;
volatile sig_atomic_t last_signal = 0;
int sig_pipe[2] = {-1, -1};

void timedexec_handle_signal(int sig) {
    unsigned char ch = (unsigned char)sig;
    last_signal = sig;
    if (sig_pipe[1] != -1) {
        write(sig_pipe[1], &ch, 1);
    }
}

void timedexec_usage(void) {
    printf("Usage: timedexec [-c sec] [-m mb] [-t sec] [-g sec] [-q] -- command [args...]\n");
}

void timedexec(int argc, char *argv[]) {
    optind = 1;
    int cpu_limit = 0, mem_limit = 0, wall_limit = 0, grace = 2, quiet = 0;
    int opt, status = 0, exec_pipe[2], child_error = 0, timeout = 0;
    struct timespec start, now;
    struct rusage usage_info;

    memset(&usage_info, 0, sizeof(usage_info));

    while ((opt = getopt(argc, argv, "c:m:t:g:qh")) != -1) {
        switch (opt) {
            case 'c': cpu_limit = atoi(optarg); break;
            case 'm': mem_limit = atoi(optarg); break;
            case 't': wall_limit = atoi(optarg); break;
            case 'g': grace = atoi(optarg); break;
            case 'q': quiet = 1; break;
            case 'h':
                timedexec_usage();
                return;
            default:
                timedexec_usage();
                return;
        }
    }

    if (optind >= argc || cpu_limit < 0 || mem_limit < 0 || wall_limit < 0 || grace <= 0) {
        timedexec_usage();
        return;
    }

    signal(SIGINT, timedexec_handle_signal);
    signal(SIGTERM, timedexec_handle_signal);

    if (pipe(sig_pipe) == -1 || pipe(exec_pipe) == -1) {
        perror("pipe");
        return;
    }

    fcntl(sig_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);

    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
        perror("clock_gettime");
        return;
    }

    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        return;
    }

    if (child_pid == 0) {
        struct rlimit limit;

        close(sig_pipe[0]);
        close(sig_pipe[1]);
        close(exec_pipe[0]);
        setpgid(0, 0);

        if (cpu_limit > 0) {
            limit.rlim_cur = cpu_limit;
            limit.rlim_max = cpu_limit + 1;
            if (setrlimit(RLIMIT_CPU, &limit) == -1) {
                child_error = errno;
                write(exec_pipe[1], &child_error, sizeof(child_error));
                _exit(127);
            }
        }

        if (mem_limit > 0) {
            limit.rlim_cur = (rlim_t)mem_limit * 1024 * 1024;
            limit.rlim_max = (rlim_t)mem_limit * 1024 * 1024;
            if (setrlimit(RLIMIT_AS, &limit) == -1) {
                child_error = errno;
                write(exec_pipe[1], &child_error, sizeof(child_error));
                _exit(127);
            }
        }

        execvp(argv[optind], &argv[optind]);
        child_error = errno;
        write(exec_pipe[1], &child_error, sizeof(child_error));
        _exit(127);
    }

    close(exec_pipe[1]);

    while (1) {
        struct pollfd fds[2];
        pid_t done;
        int wait_ms = 250;
        unsigned char sig_byte;
        ssize_t n;

        done = wait4(child_pid, &status, WNOHANG, &usage_info);
        if (done == -1) {
            perror("wait4");
            return;
        }
        if (done == child_pid) {
            break;
        }

        if (wall_limit > 0) {
            long elapsed;
            long left;

            clock_gettime(CLOCK_MONOTONIC, &now);
            elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
            left = wall_limit * 1000L - elapsed;

            if (left <= 0) {
                timeout = 1;
                kill(-child_pid, SIGTERM);
                sleep(grace);
                kill(-child_pid, SIGKILL);
                continue;
            }

            if (left < wait_ms) {
                wait_ms = (int)left;
            }
        }

        fds[0].fd = sig_pipe[0];
        fds[0].events = POLLIN;
        fds[1].fd = exec_pipe[0];
        fds[1].events = POLLIN;

        if (poll(fds, 2, wait_ms) == -1 && errno != EINTR) {
            perror("poll");
            return;
        }

        if (fds[1].revents & POLLIN) {
            n = read(exec_pipe[0], &child_error, sizeof(child_error));
            if (n > 0) {
                fprintf(stderr, "exec failed: %s\n", strerror(child_error));
            }
        }

        if (fds[0].revents & POLLIN) {
            n = read(sig_pipe[0], &sig_byte, 1);
            if (n > 0) {
                kill(-child_pid, sig_byte);
                sleep(grace);
                kill(-child_pid, SIGKILL);
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &now);

    if (!quiet) {
        double real_time = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1000000000.0;

        printf("\n----- timedexec Results -----\n");
        printf("PID: %d\n", child_pid);
        printf("Real Time: %.3f sec\n", real_time);
        printf("User CPU Time: %.3f sec\n",
               usage_info.ru_utime.tv_sec + usage_info.ru_utime.tv_usec / 1000000.0);
        printf("System CPU Time: %.3f sec\n",
               usage_info.ru_stime.tv_sec + usage_info.ru_stime.tv_usec / 1000000.0);
        printf("Max RSS: %ld KB\n", usage_info.ru_maxrss);

        if (timeout) {
            printf("Result: wall-clock timeout reached\n");
        } else if (WIFEXITED(status)) {
            printf("Result: exited with code %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Result: terminated by signal %d\n", WTERMSIG(status));
        }

        if (last_signal) {
            printf("Interrupted by user signal %d\n", last_signal);
        }

        printf("\n");
    }

    close(exec_pipe[0]);
    close(sig_pipe[0]);
    close(sig_pipe[1]);

    if (timeout) return;
    if (last_signal) return;
    if (WIFEXITED(status)) return;
    if (WIFSIGNALED(status)) return;
    return;
}
