#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>

// #define NDEBUG


static const char* SEP = "|";

int prevalidate_args(int argc, char*** argv, int* cps_cnt);
int parse_args(int argc, char*** argv, char**** cps_argv);
int launch_pipeline(int cps_cnt, char**** cps_argv, int*** pipe_fds, pid_t** cps_pid);
void cleanup(int cps_cnt, char**** cps_argv, int*** pipe_fds, pid_t** cps_pid);
void print_usage();


int main(int argc, char** argv) 
{
    // prevalidate args & get number of child processes to be launched
    int cps_cnt;
    if (prevalidate_args(argc, &argv, &cps_cnt) == 1) {
        print_usage();
        fprintf(stderr, "%s\n", "Invalid arguments");
        return 2;
    }
    
    // get list of child processes' arguments
    char*** cps_argv = (char***)malloc(sizeof(char**) * cps_cnt);
    if (cps_argv == NULL) {
        perror("malloc failure");
        return 1;
    }

    if (parse_args(argc, &argv, &cps_argv) == 1) {
        fprintf(stderr, "%s\n", "Failed to parse arguments");
        cleanup(cps_cnt, &cps_argv, NULL, NULL);
        return 1;
    }

    // launch pipeline
    int** pipe_fds = (int**)malloc(sizeof(int*) * (cps_cnt - 1));// file descriptors holder
    if (pipe_fds == NULL) {
        perror("malloc failure");
        cleanup(cps_cnt, &cps_argv, NULL, NULL);
        return 1;
    }
    for (int i = 0; i < cps_cnt - 1; ++i) {
        pipe_fds[i] = (int*)malloc(sizeof(int) * 2);
        if (pipe_fds[i] == NULL) {
            perror("malloc failure");
            cleanup(cps_cnt, &cps_argv, &pipe_fds, NULL);
            return 1;
        }
    }

    pid_t* cps_pid = (pid_t*)malloc(sizeof(pid_t) * cps_cnt);// child processes' pids holder
    if (cps_pid == NULL) {
        perror("malloc failure");
        cleanup(cps_cnt, &cps_argv, &pipe_fds, NULL);
        return 1;
    }

    if (launch_pipeline(cps_cnt, &cps_argv, &pipe_fds, &cps_pid) == 1) {
        fprintf(stderr, "%s\n", "Failed to launch all child processes");
        cleanup(cps_cnt, &cps_argv, &pipe_fds, &cps_pid);
        return 1;
    }

    // wait child processes to complete
    int exit_code = 0;
    int status;
    for (int i = 0; i < cps_cnt; ++i) {
        if (waitpid(cps_pid[i], &status, 0) == -1) {// waitpid(-1, &status, 0)
            perror("waitpid failure");
            exit_code = 1;
            break;
        }
        if (WIFEXITED(status)) {// normal exit
            if (WEXITSTATUS(status) != 0) {
                exit_code = 1;
            }
        }
    }
    cleanup(cps_cnt, &cps_argv, &pipe_fds, &cps_pid);
    return exit_code;
}

/*
Arguments:
    inputs:
        argc - number of command line arguments
        argv - list of command line arguments
    outputs:
        cps_cnt - number of child processes to be launched

Return:
    0 on success
    1 on failure
*/
int prevalidate_args(int argc, char*** argv, int* cps_cnt) 
{
    assert(argc > 0);
    assert(argv != NULL);
    assert(cps_cnt != NULL);

    *cps_cnt = 0;

    int cps_argc = 0;
    int pipe_cnt = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp((*argv)[i], SEP) == 0) {
            ++pipe_cnt;
            if (cps_argc > 0) {
                ++(*cps_cnt);
            }
            cps_argc = 0;
        } else {
            ++cps_argc;
        }
    }
    if (cps_argc > 0) {
        ++(*cps_cnt);
    }

    if (*cps_cnt != pipe_cnt + 1) {
        return 1;
    }

    return 0;
}

/*
Arguments:
    inputs:
        argc - number of command line arguments
        argv - list of command line arguments
    outputs:
        cps_argv - list of child processes' arguments

Return:
    0 on success
    1 on failure
*/
int parse_args(int argc, char*** argv, char**** cps_argv) 
{
    assert(argc > 1);
    assert(argv != NULL);
    assert(cps_argv != NULL);

    int cps_cur = 0;
    int cps_beg = 1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp((*argv)[i], SEP) == 0 || i == argc - 1) {
            int cps_argc = i - cps_beg;
            if (i == argc - 1) {
                ++cps_argc;
            }
            char** cps = (char**)malloc(sizeof(char*) * (cps_argc + 1));
            if (cps == NULL) {
                perror("malloc failure");
                return 1;
            }
            for (int j = 0; j < cps_argc; ++j) {
                cps[j] = (*argv)[cps_beg + j];
            }
            cps[cps_argc] = NULL;
            (*cps_argv)[cps_cur] = cps;

            ++cps_cur;
            cps_beg = i + 1;
        }
    }

    return 0;
}

/*
Arguments:
    inputs:
        cps_cnt - number of child processes
        argv - list of child processes' arguments
    outputs:
        pipe_fds - list of pipes' file descriptiors
        cps_pid - list of launched child processes' pids

Return:
    0 on success
    1 on failure
*/
int launch_pipeline(int cps_cnt, char**** cps_argv, int*** pipe_fds, pid_t** cps_pid) 
{
    assert(cps_cnt > 0);
    assert(cps_argv != NULL);
    assert(pipe_fds != NULL);
    assert(cps_pid != NULL);

    for (int i = 0; i < cps_cnt; ++i) {
        if (i < cps_cnt - 1) {
            if (pipe((*pipe_fds)[i]) == -1) {
                perror("pipe failure");
                return 1;
            }
        }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork error");
            return 1;
        }
        if (pid == 0) {
            char** args = (*cps_argv)[i];
            if (i < cps_cnt - 1) {
                if (close((*pipe_fds)[i][0]) == -1) {
                    perror("close failure");
                    return 1;
                }
                if (dup2((*pipe_fds)[i][1], 1) == -1) {
                    perror("dup2 failure");
                    return 1;
                }
            }
            if (execvp(args[0], args) == -1) {
                perror("execvp failure");
                return 1;
            }
        }
        if (i < cps_cnt - 1) {
            if (dup2((*pipe_fds)[i][0], 0) == -1) {
                perror("dup2 failure");
                return 1;
            }
            if (close((*pipe_fds)[i][1]) == -1) {
                perror("close failure");
                return 1;
            }
        }
        (*cps_pid)[i] = pid;
    }

    return 0;
}

void cleanup(int cps_cnt, char**** cps_argv, int*** pipe_fds, pid_t** cps_pid) 
{
    if (cps_argv != NULL) {
        for (int i = 0; i < cps_cnt; ++i) {
            free((*cps_argv)[i]);
        }
        free(*cps_argv);
    }
    if (pipe_fds != NULL) {
        for (int i = 0; i < cps_cnt - 1; ++i) {
            free((*pipe_fds)[i]);
        }
        free(*pipe_fds);
    }
    if (cps_pid != NULL) {
        free(*cps_pid);
    }
}

void print_usage() {
    fprintf(stderr, "%s\n", "usage: pipeline CMD1 | CMD2 | ... | CMDN");
}
