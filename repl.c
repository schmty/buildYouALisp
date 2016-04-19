#include <stdio.h>
#include <stdlib.h>

// windows stuff
#ifdef _WIN32
#include <string.h>


static char buffer[2048];

// fake readline func
char* readline(char* prompt) {
    fputs(prompt, stdout);
    fgets(buffer, 2048, stdin);
    char* cpy = malloc(strlen(buffer)+1);
    strcpy(cpy, buffer);
    cpu[strlen(cpy)-1] = '\0';
    return cpy;
}

// fake add_history function
void add_history(char* unused) {}

// else include the libs
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif


int main(int argc, char** argv) {
    /* Print version and exit info */
    puts("Lispy version 0.0.2");
    puts("Press ctrl+c to exit\n");

    // in a never ending loop
    while (1) {

        // output our prompt and get input
        char* input = readline("lispy> ");

        // add input to our history
        add_history(input);

        // echo input back to user
        printf("No you're a %s\n", input);

        // free retrieved input
        free(input);
    }

    return 0;
}
