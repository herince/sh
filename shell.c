#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <wait.h>
#include <errno.h>

#include <stdio.h>

//const char *COMMAND_PROMPT = "> ";

static const int ARG_MAX = 10000;

static const char *ERROR_COMMAND_LINE_LIMIT_EXCEEDED = "command line limit exceeded\n";
static const char *ERROR_UNEXPECTED_TOKEN_PIPE = "unexpected token '|'\n";

static int pipefd[2];

static int in;
static int out;

static const int IN = 0;
static const int OUT = 1;

// log error to stderr
void lerror(const char *msg)
{
    write(STDERR_FILENO, msg, strlen(msg));
}

// returns 0 on success and -1 on error
int expressiveExecvp(char *const *execArr, int piped, int isFirst, int isLast)
{
    int err = 0;
    pid_t pid;

    if ((pid = fork()) < 0)
    {
        perror("[expressiveExecv] fork()");
        return -1;
    } else if (pid == 0)
    {
        // redirect i/o if the command is piped with another
        if (piped && !isLast)
        {
            // command somewhere in the middle of the pipeline
            err = dup2(pipefd[OUT], OUT);
            if (err == -1)
            {
                perror("[expressiveExecvp] unable to duplicate file descriptor");
                exit(1);
            }
            close(pipefd[IN]);
        }

        err = execvp(execArr[0], execArr);
        if (err == -1)
        {
            perror("[expressiveExecvp] execvp()");
            exit(1);
        }
    } else
    {
        //parent
        if (piped && !isLast)
        {
            err = dup2(pipefd[IN], IN);
            if (err == -1)
            {
                perror("[expressiveExecvp] unable to duplicate file descriptor");
                exit(1);
            }
            close(pipefd[OUT]);
        }
    }

    return 0;
}

// initial value of (*argc) should be 0 for this function to work
// returns 0 for success, -1 for error, -2 for end of file
ssize_t readLine(int fd, char args[ARG_MAX], unsigned int *argc)
{
    ssize_t readChr;

    char currChr;

    int args_i = 0;

    do
    {
        readChr = read(fd, &currChr, 1);
        if (readChr < 0)
        {
            perror("[readLine] read()");
            return -1;
        } else if (readChr == 0)
        {
            return -2;
        }

        if (currChr != '\n')
        {
            args[args_i] = currChr;

            if ((args_i == 0 && currChr != ' ')
                || (args_i > 0 && currChr != ' ' && args[args_i - 1] == ' '))
            {
                (*argc)++;
            }
        } else
        {
            args[args_i] = 0;
        }

        args_i++;
    } while (currChr != '\n' && args_i <= ARG_MAX);

    if (args_i == ARG_MAX)
    {
        lerror(ERROR_COMMAND_LINE_LIMIT_EXCEEDED);
        return -1;
    }

    return 0;
}

// returns 0 on success and -1 on error
int execCmdLine(int argc, char **argv)
{
    int err;

    char *cmdArr[argc + 1];
    int j = 0;

    int piped = 0;
    int isFirst = 1;
    int last = 0;

    int children = 0;

    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "|") == 0)
        {
            piped = 1;

            if (j < 1)
            {
                lerror(ERROR_UNEXPECTED_TOKEN_PIPE);
                return -1;
            }

            cmdArr[j] = NULL;
            char *const *execArr = &cmdArr[0];

            // to fix!!!

            err = pipe(pipefd);
            if (err == -1)
            {
                perror("[execCmdLine] could not pipe piped commands");
                exit(1);
            }

            err = expressiveExecvp(execArr, piped, isFirst, last);
            if (err == -1)
            {
                lerror("could not execute command line\n");
                return -1;
            } else
            {
                children++;
            }

            j = 0;
            isFirst = 0;
        } else
        {
            cmdArr[j] = argv[i];
            j++;
        }
    }

    last = 1;

    cmdArr[j] = NULL;
    char *const *execArr = &cmdArr[0];

    err = expressiveExecvp(execArr, piped, isFirst, last);
    if (err == -1)
    {
        perror("[execCmdLine] expressiveExecvp()");
        return -1;
    } else
    {
        children++;
    }

    err = dup2(in, 0);
    if (err == -1)
    {
        perror("[execCmdLine] unable to duplicate file descriptor");
        exit(1);
    }

    err = dup2(out, 1);
    if (err == -1)
    {
        perror("[execCmdLine] unable to duplicate file descriptor");
        exit(1);
    }

    int returnStatus;
    for (int i = 0; i < children; i++)
    {
        do
        {
            err = waitpid(-1, &returnStatus, 0);
            if (err == -1)
            {
                perror("[expressiveExecvp] wait()");
                exit(1);
            }

            if (WIFEXITED(returnStatus))
            {
                printf("exited, status = %d\n", returnStatus);
            } else if (WIFSIGNALED(returnStatus))
            {
                printf("killed by signal %d\n", returnStatus);
            } else if (WIFSTOPPED(returnStatus))
            {
                printf("stopped by signal %d\n", returnStatus);
            } else if (WIFCONTINUED(returnStatus))
            {
                printf("continued\n");
            }
        } while (!WIFEXITED(returnStatus) && !WIFSIGNALED(returnStatus));
    }

    return 0;
}

// returns 0 on success and -1 on error
int shell()
{
    // save stdin and stdout file descriptors so that you can fix redirected (from pipes) i/o
    in = dup(0);
    if (in == -1)
    {
        perror("unable to save stdin descriptor");
        exit(1);
    }

    out = dup(1);
    if (out == -1)
    {
        perror("unable to save stdout descriptor");
        exit(1);
    }

    int exitShell = 0;

    // "possible error"-related variables
    int err;
    ssize_t readChr;
    ssize_t writtenChr;

    // string with arguments that I will read for the shell to interpret
    char args[ARG_MAX];

    // standard argc and argv variables for each new command line
    unsigned int argc;
    char **argv;
    int argv_i;

    do
    {
        argc = 0;

        printf("> \n");

        readChr = readLine(0, args, &argc);
        switch (readChr)
        {
            case 0:
            {
                break;
            }
            case -1:
            {
                lerror("unable to read line\n");
                return -1;
            }
            case -2:
            {
                exitShell = 1;
                continue;
            }
            default:
            {
                lerror("unexpected error code\n");
                exit(1);
            }
        };

        if (argc > 0)
        {
            argv_i = 0;
            argv = malloc(argc * sizeof(char *));

            // extract arguments
            argv[argv_i] = strtok(args, " ");

            for (argv_i = 1; argv_i < argc; argv_i++)
            {
                argv[argv_i] = strtok(NULL, " ");
            }

            if (argc == 1 && strcmp(argv[0], "exit") == 0)
            {
                exitShell = 1;
            } else
            {
                // run command with arguments:
                err = execCmdLine(argc, argv);
                if (err == -1)
                {
                    lerror("unable to execute command line\n");
                    return -1;
                }
            }

            free(argv);
        }
    } while (exitShell != 1);

    close(in);
    close(out);

    return 0;
}

int main(int argc, char **argv) {
    int err = shell();
    if (err == -1)
        return 1;
}