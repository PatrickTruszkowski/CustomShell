#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void display_manual();
void loganalyzer(int argc, char* argv[]);

int main()
{
    char input[1024];

    while (1)
    {
        printf("shell> ");

        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;

        char* tokens[64];
        size_t tokenCount = 0;

        char* token = strtok(input, " ");

        while (token != NULL)
        {
            tokens[tokenCount++] = token;
            token = strtok(NULL, " ");
        }

        if (tokenCount == 0)
        {
            continue;
        }

        char* command = tokens[0];

        if (strcmp(command, "exit") == 0)
        {
            break;
        }
        else if (strcmp(command, "help") == 0)
        {
            display_manual();
        }
        else if (strcmp(command, "loganalyzer") == 0)
        {
            loganalyzer(tokenCount, tokens);
        }
        else
        {
            printf("\n<Invalid Command>\n\n");
        }
    }
}

void display_manual()
{
    printf("\nManual\n");
    printf("   exit - quit shell\n");
    printf("   loganalyzer -f <file> -p <pattern> - parse log file\n");

    printf("\n");
}
