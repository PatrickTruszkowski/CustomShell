#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void display_manual();

int main()
{
    char input[1024];

    while (1)
    {
        printf("shell> ");

        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = 0;

        char* tokens[64];
        int tokenCount = 0;

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
        else
        {
            printf("<Invalid Command>\n");
        }
    }
}

void display_manual()
{
    printf("\nManual\n   exit - quit shell\n\n");
}
