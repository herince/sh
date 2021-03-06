#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <fcntl.h>

//const char *COMMAND_PROMPT = "> ";

static const int ARG_MAX = 10000;

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
int sh_execvp(char *const *execArr,
              int piped,
              int redirectedInput,
              const char *inFilename,
              int redirectedOutput,
              const char *outFilename,
              int appendOut,
              int isLast)
{
    int err = 0;

    pid_t pid;

    if ((pid = fork()) < 0)
    {
        perror("[sh_execvp] fork()");
        return -1;
    } else if (pid == 0)
    {
        // redirect i/o if the command is piped with another
        if (piped && !isLast)
        {
            // command somewhere in the middle of the pipeline
            err = dup2(pipefd[OUT], STDOUT_FILENO);
            if (err == -1)
            {
                perror("[sh_execvp(child)] dup2()");
                exit(1);
            }

            err = close(pipefd[IN]);
            if (err == -1)
            {
                perror("[sh_execvp(child)] close(in)");
                exit(1);
            }
            err = close(pipefd[OUT]);
            if (err == -1)
            {
                perror("[sh_execvp(child)] close(out)");
                exit(1);
            }
        }

        // redirect output if said to by > or >>
        if (redirectedOutput)
        {
            int outfd;

            if (appendOut)
            {
                outfd = open(outFilename, O_WRONLY | O_CREAT | O_APPEND);
            } else
            {
                outfd = open(outFilename, O_WRONLY | O_CREAT | O_TRUNC);
            }
            if (outfd == -1)
            {
                perror("[sh_execvp(child)] open()");
                exit(1);
            }

            err = chmod(outFilename, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP | S_IROTH);
            if (err == -1)
            {
                perror("[sh_execvp(child)] chmod()");
                exit(1);
            }

            err = dup2(outfd, STDOUT_FILENO);
            if (err == -1)
            {
                perror("[sh_execvp(child)] dup2()");
                exit(1);
            }
            close(outfd);
        }
        // redirect input if said to by <
        if (redirectedInput)
        {
            int infd;

            infd = open(inFilename, O_RDONLY);
            if (infd == -1)
            {
                perror("[sh_execvp(child)] open()");
                exit(1);
            }

            err = dup2(infd, STDIN_FILENO);
            if (err == -1)
            {
                perror("[sh_execvp(child)] dup2()");
                exit(1);
            }
            err = close(infd);
            if (err == -1)
            {
                perror("[sh_execvp(child)] close()");
                exit(1);
            }
        }

        execvp(execArr[0], execArr);

        perror("[sh_execvp] execvp()");
        exit(1);
    } else
    {
        //parent
        if (piped && !isLast)
        {
            err = dup2(pipefd[IN], STDIN_FILENO);
            if (err == -1)
            {
                perror("[sh_execvp(parent)] dup2()");
                exit(1);
            }

            err = close(pipefd[IN]);
            if (err == -1)
            {
                perror("[sh_execvp(parent)] close(in)");
                exit(1);
            }
            err = close(pipefd[OUT]);
            if (err == -1)
            {
                perror("[sh_execvp(parent)] close(out)");
                exit(1);
            }
        }
    }

    return 0;
}

// initial value of (*argc) should be 0 for this function to work
// returns 0 for success, -1 for error, -2 for end of file
ssize_t sh_readLine(int fd, char args[ARG_MAX], unsigned int *argc)
{
    ssize_t readChr;

    char currChr;

    int args_i = 0;

    do
    {
        readChr = read(fd, &currChr, 1);
        if (readChr < 0)
        {
            perror("[sh_readLine] read()");
            return -1;
        } else if (readChr == 0)
        {
            return -2;
        }

        if (currChr == '\n')
        {
            args[args_i] = 0;
        } else
        {
            args[args_i] = currChr;

            if ((args_i == 0 && currChr != ' ')
                || (args_i > 0 && currChr != ' ' && args[args_i - 1] == ' '))
            {
                (*argc)++;
            }
        }

        args_i++;
    } while (currChr != '\n' && args_i < ARG_MAX);

    if (strlen(args) == ARG_MAX)
    {
        lerror("[sh_readLine] command line limit exceeded\n");
        exit(1);
    }

    //printf("%d\n", strlen(args));

    return 0;
}

// returns 0 on success and -1 on error
int sh_execCMDLine(int argc, char **argv)
{
    // parse commands from command line
    int cmdCount = 1;

    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "|") == 0)
        {
            cmdCount++;
        }
    }

    char *cmdArr[argc + 1];

    char ***commands = malloc((cmdCount) * sizeof(char **));

    int cmdIndex = 0;
    int argIndex = 0;

    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "|") == 0)
        {
            if (argIndex < 1)
            {
                lerror("[sh_execCMDLine] unexpected token '|'\n");
                return -1;
            }

            cmdArr[argIndex] = NULL;
            commands[cmdIndex] = malloc((argIndex + 1) * sizeof(char *));
            memcpy(commands[cmdIndex], cmdArr, (argIndex + 1) * sizeof(char *));

            cmdIndex++;
            argIndex = 0;
        } else
        {
            cmdArr[argIndex] = argv[i];
            argIndex++;
        }
    }
    cmdArr[argIndex] = NULL;
    commands[cmdIndex] = malloc((argIndex + 1) * sizeof(char *));
    memcpy(commands[cmdIndex], cmdArr, (argIndex + 1) * sizeof(char *));

    /* for (int i = 0; i < cmdCount; i++)
    {
        printf("{");
        for (int j = 0; commands[i][j] != NULL; j++)
        {
            printf("%s, ", commands[i][j]);
        }
        printf("}\n");
    } */


    // execute commands
    int piped;
    if (cmdCount > 1)
    {
        piped = 1;
    } else
    {
        piped = 0;
    }

    int redirectedIN = 0;
    char *inFilename = "";

    int redirectedOUT = 0;
    char *outFilename = "";
    int appendOut = 1;

    int last = 0;

    int err;
    int children = 0;

    // loop through all of the commands in the command line and pipe them (wherever needed)
    for (int i = 0; i < cmdCount; i++)
    {
        for (int j = 0; commands[i][j] != NULL; j++)
        {
            // check for output redirection
            if (strcmp(commands[i][j], ">") == 0)
            {
                if (commands[i][j + 1] == NULL)
                {
                    lerror("[sh_execCMDLine] syntax error near unexpected token `newline`");
                    return -1;
                } else
                {
                    redirectedOUT = 1;
                    appendOut = 0;

                    outFilename = commands[i][++j];
                }

                continue;
            }

            // check for output redirection
            if (strcmp(commands[i][j], ">>") == 0)
            {
                if (commands[i][j + 1] == NULL)
                {
                    lerror("[sh_execCMDLine] syntax error near unexpected token `newline`");
                    return -1;
                } else
                {
                    redirectedOUT = 1;
                    appendOut = 1;

                    outFilename = commands[i][++j];
                }

                continue;
            }

            // check for input redirection
            if (strcmp(commands[i][j], "<") == 0)
            {
                if (commands[i][j + 1] == NULL)
                {
                    lerror("[sh_execCMDLine] syntax error near unexpected token `newline`");
                    return -1;
                } else
                {
                    redirectedIN = 1;

                    inFilename = commands[i][++j];
                }

                continue;
            }
        }

        if (i == cmdCount - 1)
        {
            last = 1;
        }

        if (piped && !last)
        {
            pipe(pipefd);
        }

        err = sh_execvp(commands[i], piped, redirectedIN, inFilename, redirectedOUT, outFilename, appendOut, last);
        if (err == -1)
        {
            lerror("[sh_execCMDLine] could not execute command line\n");
            return -1;
        } else
        {
            children++;
        }

        redirectedIN = 0;
        redirectedOUT = 0;
    }

    err = dup2(in, STDIN_FILENO);
    if (err == -1)
    {
        perror("[sh_execCMDLine] dup2()");
        exit(1);
    }

    err = dup2(out, STDOUT_FILENO);
    if (err == -1)
    {
        perror("[sh_execCMDLine] dup2()");
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

            // todo - remove:
            if (WIFEXITED(returnStatus))
            {
                //printf("exited, status = %d\n", returnStatus);
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

void shell()
{
    // save stdin and stdout file descriptors so that you can fix redirected (from pipes) i/o
    in = dup(STDIN_FILENO);
    if (in == -1)
    {
        perror("[shell] unable to save stdin descriptor");
        exit(1);
    }

    out = dup(STDOUT_FILENO);
    if (out == -1)
    {
        perror("[shell] unable to save stdout descriptor");
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

        readChr = sh_readLine(0, args, &argc);
        switch (readChr)
        {
            case 0:
            {
                break;
            }
            case -1:
            {
                lerror("[shell] unable to read line\n");
                continue;
            }
            case -2:
            {
                exitShell = 1;
                continue;
            }
            default:
            {
                lerror("[shell] unexpected error code\n");
                exit(1);
            }
        };

        if (argc > 0)
        {
            argv_i = 0;
            argv = malloc(argc * sizeof(char *));

            // extract arguments
            char *token;
            token = strtok(args, " ");

            while (token != NULL)
            {
                argv[argv_i] = *(&token);
                token = strtok(NULL, " ");

                argv_i++;
            }

            if (argc == 1 && strcmp(argv[0], "exit") == 0)
            {
                exitShell = 1;
            } else
            {
                // run command with arguments:
                err = sh_execCMDLine(argc, argv);
                if (err == -1)
                {
                    lerror("[shell] unable to execute command line\n");
                    continue;
                }
            }

            free(argv);
        }
    } while (exitShell != 1);

    err = close(in);
    if (err == -1)
    {
        perror("[shell] close()");
        exit(1);
    }
    err = close(out);
    if (err == -1)
    {
        perror("[shell] close()");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    shell();
}