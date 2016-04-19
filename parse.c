#include <stdio.h>
#include <stdlib.h>
#include "mpc.h"

// windows stuff
#ifdef _WIN32
#include <string.h>


static char buffer[2048];

// fake readline func for windows
char* readline(char* prompt) {
    fputs(prompt, stdout);
    fgets(buffer, 2048, stdin);
    char* cpy = malloc(strlen(buffer)+1);
    strcpy(cpy, buffer);
    cpu[strlen(cpy)-1] = '\0';
    return cpy;
}

// fake add_history function for windows
void add_history(char* unused) {}

// else include the libs
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

// possible lval types enum
enum { LVAL_NUM, LVAL_ERR };

// possible error types
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

// declare new lval struct (lisp value)
typedef struct {
    int type;
    long num;
    int err;
} lval;

// new num type lval
lval lval_num(long x) {
    lval v;
    v.type = LVAL_NUM;
    v.num = x;
    return v;
}

// create a new error type lval
lval lval_err(int x) {
    lval v;
    v.type = LVAL_ERR;
    v.err = x;
    return v;
}

// print an lval
void lval_print(lval v) {
    switch (v.type) {
        // in case the type is a number print it
        // then break out of the switch
        case LVAL_NUM:
            printf("%li", v.num);
            break;

        // in case it is an error
        case LVAL_ERR:
            // check what type of error it is and print it
            if (v.err == LERR_DIV_ZERO) {
                printf("Error: Division by Zero");
            }
            if (v.err == LERR_BAD_OP) {
                printf("Error: Invalid Operator");
            }
            if (v.err == LERR_BAD_NUM) {
                printf("Error: Invalid Number");
            }
        break;
    }
}

// lval print line
void lval_println(lval v) { lval_print(v); putchar('\n'); }

// eval operators
// GUESS WHAT YOU DO THE MATH
lval eval_op(lval x, char* op, lval y) {

    // if either value is an error return it
    if (x.type == LVAL_ERR) { return x; }
    if (y.type == LVAL_ERR) { return y; }


    // otherwise do the math
    if (strcmp(op, "+") == 0) { return lval_num(x.num + y.num); }
    if (strcmp(op, "-") == 0) { return lval_num(x.num - y.num); }
    if (strcmp(op, "*") == 0) { return lval_num(x.num * y.num); }
    if (strcmp(op, "/") == 0) {
        // if second operand is zero than return error
        return y.num == 0
            ? lval_err(LERR_DIV_ZERO)
            : lval_num(x.num / y.num);
    }
    return lval_err(LERR_BAD_OP);
}

// recursive eval
// traverses ast and evaluates
lval eval(mpc_ast_t* t) {
    // if tagged as number return it directly
    if (strstr(t->tag, "number")) {
        // check if there is some error in the conversion
        errno = 0;
        long x = strtol(t->contents, NULL, 10);
        return errno != ERANGE ? lval_num(x) : lval_err(LERR_BAD_NUM);
    }

    // the operator is always second child
    char* op = t->children[1]->contents;

    // store the third child in x
    lval x = eval(t->children[2]);

    // iterate the remaining children and combining
    int i = 3;
    while (strstr(t->children[i]->tag, "expr")) {
        x = eval_op(x, op, eval(t->children[i]));
        i++;
    }
    return x;
}

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
    puts("Slither version 0.0.4");
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
            // on success print the evaluation
            lval result = eval(r.output);
            lval_println(result);
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
