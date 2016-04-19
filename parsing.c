#include <stdio.h>
#include <stdlib.h>
#include "mpc.h"

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
    // create some parsers
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Operator = mpc_new("operator");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Slither = mpc_new("slither");

    // define them with the following language
    mpca_lang(MPCA_LANG_DEFAULT,
            "                                                  \
            number   : /-?[0-9]+/ ;                            \
            operator : '+' | '-' | '*' | '/' ;                 \
            expr     : <number> | '(' <operator> <expr>+ ')' ; \
            slither  : /^/ <operator> <expr>+ /$/ ;            \
            ",
            Number, Operator, Expr, Slither);

    /* Print version and exit info */
    puts("Slither version 0.0.3");
    puts("Press ctrl+c to exit\n");

    // in a never ending loop
    while (1) {

        // output our prompt and get input
        char* input = readline("slither> ");

        // add input to our history
        add_history(input);

        // attempt to parse user input
        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Slither, &r)) {
            // on success print the ast
            mpc_ast_print(r.output);
            mpc_ast_delete(r.output);
        } else {
            // otherwise print the error
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }

        // free retrieved input
        free(input);
    }

    // undefine and delete parsers
    mpc_cleanup(4, Number, Operator, Expr, Slither);
    return 0;
}
