#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>

#define MAX_LINE 80 /* 80 chars per line, per command */
#define HISTORY_COUNT 10

/* Function Prototypes */
void setup(char inputBuffer[], char *args[], int *background);
void add_to_history(char *args[]);
void print_history();
int handle_internal_commands(char *args[], char inputBuffer[]);
int handle_io_redirection(char *args[]);

/* Global variables for history management */
char history[HISTORY_COUNT][MAX_LINE];
int history_count = 0;
bool callFromHistory = false;
bool skip_last_argument_removal = false; // New boolean to control argument removal

/* Function to add a command to history */
void add_to_history(char *args[])
{
    if (args[0] == NULL || strcmp(args[0], "history") == 0)
    {
        return; // Ignore empty commands and "history"
    }

    char command[MAX_LINE] = "";
    for (int i = 0; args[i] != NULL; i++)
    {
        strncat(command, args[i], MAX_LINE - strlen(command) - 1);
        strncat(command, " ", MAX_LINE - strlen(command) - 1);
    }

    size_t len = strlen(command);
    if (len > 0 && command[len - 1] == ' ')
    {
        command[len - 1] = '\0';
    }

    if (history_count < HISTORY_COUNT)
    {
        for (int i = history_count; i > 0; i--)
        {
            strncpy(history[i], history[i - 1], MAX_LINE);
            history[i][MAX_LINE - 1] = '\0';
        }
        history_count++;
    }
    else
    {
        for (int i = HISTORY_COUNT - 1; i > 0; i--)
        {
            strncpy(history[i], history[i - 1], MAX_LINE);
            history[i][MAX_LINE - 1] = '\0';
        }
    }

    strncpy(history[0], command, MAX_LINE);
    history[0][MAX_LINE - 1] = '\0';
}

/* Function to print history */
void print_history()
{
    if (history_count == 0)
    {
        printf("No commands in history.\n");
        return;
    }
    for (int i = 0; i < history_count; i++)
    {
        printf("%d %s\n", i, history[i]);
    }
}

int handle_io_redirection(char *args[])
{
    int fd_in = -1, fd_out = -1, fd_err = -1;
    char *input_file = NULL, *output_file = NULL, *error_file = NULL;
    int input_redirect = 0, output_redirect = 0, error_redirect = 0;
    int output_append = 0, error_append = 0;

    // First pass: identify and remove redirection operators
    for (int i = 0; args[i] != NULL; i++)
    {
        if (strcmp(args[i], "<") == 0)
        {
            if (input_redirect)
            {
                fprintf(stderr, "Error: Multiple input redirections not allowed\n");
                return -1;
            }
            if (args[i + 1] == NULL)
            {
                fprintf(stderr, "Error: Missing input file after '<'\n");
                return -1;
            }
            input_file = args[i + 1];
            input_redirect = 1;

            // Remove redirection and filename from args
            for (int j = i; args[j] != NULL; j++)
            {
                args[j] = args[j + 2];
            }
            i--;
        }
        else if (strcmp(args[i], ">") == 0)
        {
            if (output_redirect)
            {
                fprintf(stderr, "Error: Multiple output redirections not allowed\n");
                return -1;
            }
            if (args[i + 1] == NULL)
            {
                fprintf(stderr, "Error: Missing output file after '>'\n");
                return -1;
            }
            output_file = args[i + 1];
            output_redirect = 1;
            output_append = 0;

            // Remove redirection and filename from args
            for (int j = i; args[j] != NULL; j++)
            {
                args[j] = args[j + 2];
            }
            i--;
        }
        else if (strcmp(args[i], ">>") == 0)
        {
            if (output_redirect)
            {
                fprintf(stderr, "Error: Multiple output redirections not allowed\n");
                return -1;
            }
            if (args[i + 1] == NULL)
            {
                fprintf(stderr, "Error: Missing output file after '>>'\n");
                return -1;
            }
            output_file = args[i + 1];
            output_redirect = 1;
            output_append = 1;

            // Remove redirection and filename from args
            for (int j = i; args[j] != NULL; j++)
            {
                args[j] = args[j + 2];
            }
            i--;
        }
        else if (strcmp(args[i], "2>") == 0)
        {
            if (error_redirect)
            {
                fprintf(stderr, "Error: Multiple error redirections not allowed\n");
                return -1;
            }
            if (args[i + 1] == NULL)
            {
                fprintf(stderr, "Error: Missing error file after '2>'\n");
                return -1;
            }
            error_file = args[i + 1];
            error_redirect = 1;
            error_append = 0;

            // Remove redirection and filename from args
            for (int j = i; args[j] != NULL; j++)
            {
                args[j] = args[j + 2];
            }
            i--;
        }
        
    }

    // Perform input redirection
    if (input_redirect)
    {
        fd_in = open(input_file, O_RDONLY);
        if (fd_in < 0)
        {
            perror("Failed to open input file");
            return -1;
        }
        if (dup2(fd_in, STDIN_FILENO) < 0)
        {
            perror("Input redirection failed");
            close(fd_in);
            return -1;
        }
        close(fd_in);
    }

    // Perform output redirection
    if (output_redirect)
    {
        int flags = O_WRONLY | O_CREAT | (output_append ? O_APPEND : O_TRUNC);
        fd_out = open(output_file, flags, 0644);
        if (fd_out < 0)
        {
            perror("Failed to open output file");
            return -1;
        }
        if (dup2(fd_out, STDOUT_FILENO) < 0)
        {
            perror("Output redirection failed");
            close(fd_out);
            return -1;
        }
        close(fd_out);
    }

    // Perform error redirection
    if (error_redirect)
    {
        int flags = O_WRONLY | O_CREAT | (error_append ? O_APPEND : O_TRUNC);
        fd_err = open(error_file, flags, 0644);
        if (fd_err < 0)
        {
            perror("Failed to open error file");
            return -1;
        }
        if (dup2(fd_err, STDERR_FILENO) < 0)
        {
            perror("Error redirection failed");
            close(fd_err);
            return -1;
        }
        close(fd_err);
    }

    return 0;
}

/* Handle internal commands */
int handle_internal_commands(char *args[], char inputBuffer[])
{
    if (args[0] == NULL)
    {
        return 1; // Empty command
    }

    if (strcmp(args[0], "history") == 0)
    {
        if (args[1] == NULL)
        {
            print_history();
            return 1; // Command handled
        }
        else if (args[1][0] == '-' && args[1][1] == 'i' && args[2] != NULL && isdigit(args[2][0]))
        {
            callFromHistory = true;
            skip_last_argument_removal = true; // Prevent last argument removal
            int index = atoi(args[2]);
            if (index >= 0 && index < history_count)
            {
                printf("Executing: %s\n", history[index]);

                memset(inputBuffer, 0, MAX_LINE);
                strncpy(inputBuffer, history[index], MAX_LINE - 1);
                inputBuffer[MAX_LINE - 1] = '\0';

                // Parse the history command into args
                char *new_args[MAX_LINE / 2 + 1];
                int background = 0;
                setup(inputBuffer, new_args, &background);

                // Execute the command
                pid_t pid = fork();
                if (pid < 0)
                {
                    perror("Fork failed");
                    exit(1);
                }

                if (pid == 0)
                {
                    if (handle_io_redirection(new_args) < 0)
                    {
                        exit(1);
                    }
                    execvp(new_args[0], new_args);
                    perror("execvp failed");
                    exit(1);
                }
                else
                {
                    if (!background)
                    {
                        waitpid(pid, NULL, 0);
                        // Only add to history if the command executes successfully
                        add_to_history(new_args);
                    }
                    else
                    {
                        printf("Process running in background with PID: %d\n", pid);
                    }
                }

                return 1; // Command executed
            }
            else
            {
                fprintf(stderr, "No such command in history.\n");
            }
        }
        else
        {
            fprintf(stderr, "Invalid usage: history -i requires a valid number.\n");
        }
        return 1;
    }

    if (strcmp(args[0], "exit") == 0)
    {
        exit(0);
    }

    return 0; // Not an internal command
}

/* General setup function */
void setup(char inputBuffer[], char *args[], int *background)
{
    int length, i, start, ct;

    ct = 0;
    *background = 0;

    if (!callFromHistory)
    {
        length = read(STDIN_FILENO, inputBuffer, MAX_LINE);
        if (length == 0)
        {
            exit(0);
        }

        if ((length < 0) && (errno != EINTR))
        {
            perror("error reading the command");
            exit(-1);
        }
    }
    else
    {
        length = strlen(inputBuffer);
        callFromHistory = false;
    }

    start = -1;
    for (i = 0; i < length; i++)
    {
        switch (inputBuffer[i])
        {
        case ' ':
        case '\t':
            if (start != -1)
            {
                args[ct++] = &inputBuffer[start];
                inputBuffer[i] = '\0';
                start = -1;
            }
            break;

        case '\n':
            if (start != -1)
            {
                args[ct++] = &inputBuffer[start];
            }
            inputBuffer[i] = '\0';
            break;

        case '&':
            *background = 1;
            inputBuffer[i] = '\0';
            if (start != -1)
            {
                args[ct++] = &inputBuffer[start];
            }
            start = -1;
            break;

        default:
            if (start == -1)
            {
                start = i;
            }
        }
    }

    if (start != -1)
    {
        args[ct++] = &inputBuffer[start];
    }

    args[ct] = NULL;

    if (ct > 0 && !skip_last_argument_removal)
    {
        args[ct - 1] = NULL;
        ct--;
    }

    skip_last_argument_removal = false;
}
    
int main(void)
{
    char inputBuffer[MAX_LINE];
    int background;
    char *args[MAX_LINE / 2 + 1];

    while (1)
    {
        background = 0;
        printf("myshell: ");
        fflush(stdout);

        setup(inputBuffer, args, &background);

        if (handle_internal_commands(args, inputBuffer))
        {
            continue;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("Fork failed");
            exit(1);
        }

        if (pid == 0)
        { // Child process
            if (handle_io_redirection(args) < 0)
            {
                exit(1);
            }
            execvp(args[0], args);
            perror("execvp failed");
            exit(1);
        }
        else
        { // Parent process
            if (!background)
            {
                waitpid(pid, NULL, 0);
                add_to_history(args);
            }
            else
            {
                printf("Process running in background with PID: %d\n", pid);
            }
        }
    }
}
