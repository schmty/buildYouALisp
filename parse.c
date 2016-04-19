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
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR };

// possible error types
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

// declare new lval struct (lisp value)
typedef struct lval {
    int type;
    long num;
    // error and symbol types have some string data
    char* err;
    char* sym;
    // count and pointer to a list of "lval*"
    int count;
    struct lval** cell;
} lval;

// add lvals
lval* lval_add(lval* v, lval* x) {
    v->count++;
    // realloc cell with new amount of lval*'s
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    v->cell[v->count-1] = x;
    return v;
}


// construct a pointer to a new num type lval
lval* lval_num(long x) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

// construct a pointer to a new error type lval
lval* lval_err(char* m) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;
    // strlen(m) + 1 includes the null terminator to the string
    v->err = malloc(strlen(m) + 1);
    strcpy(v->err, m);
    return v;
}

// construct a pointer to a new symbol lval
lval* lval_sym(char* s) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(strlen(s) + 1);
    strcpy(v->sym, s);
    return v;
}

// construct a new pointer to an empty sexpr lval
lval* lval_sexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

// delete and free up lval memory
void lval_del(lval* v) {
    switch (v->type) {
        // do nothing for a number type
        case LVAL_NUM: break;
        // free err or sym string data
        case LVAL_ERR: free(v->err); break;
        case LVAL_SYM: free(v->sym); break;
        // if sexpr then delete all elements inside
        case LVAL_SEXPR:
            for (int i = 0; i < v->count; i++) {
                lval_del(v->cell[i]);
            }
            // also free the cell
            free(v->cell);
        break;
    }
    // free entire lval struct itself
    free(v);
}

// lval read num
lval* lval_read_num(mpc_ast_t* t) {
    errno = 0;
    long x = strtol(t->contents, NULL, 10);
    return errno != ERANGE ?
        lval_num(x) : lval_err("invalid number");
}

// lval read
lval* lval_read(mpc_ast_t* t) {
    // if symbol or number return conversion to that type
    if (strstr(t->tag, "number")) { return lval_read_num(t); }
    if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

    // if root (>) or sexpr then create empty list
    lval* x = NULL;
    if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
    if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }

    // fill this list with any valid expression contained within
    for (int i = 0; i < t->children_num; i++) {
        if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
        if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
        if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }
        x = lval_add(x, lval_read(t->children[i]));
    }
    return x;
}

// forward declaration of lval_print
void lval_print(lval* v);

// print lval exprs
void lval_expr_print(lval* v, char open, char close) {
    putchar(open);
    for (int i = 0; i < v->count; i++) {
        // print value contained within
        lval_print(v->cell[i]);

        // don't print trailing space if last element
        if (i != (v->count-1)) {
            putchar(' ');
        }
    }
    putchar(close);
}


// print an lval
void lval_print(lval* v) {
    switch (v->type) {
        case LVAL_NUM: printf("%li", v->num); break;
        case LVAL_ERR: printf("Error: %s", v->err); break;
        case LVAL_SYM: printf("%s", v->sym); break;
        case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
    }
}

// lval print line
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

int main(int argc, char** argv) {
    // create some parsers
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Symbol = mpc_new("symbol");
    mpc_parser_t* Sexpr = mpc_new("sexpr");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Slither = mpc_new("slither");

    // define them with the following language
    mpca_lang(MPCA_LANG_DEFAULT,
            "                                          \
            number   : /-?[0-9]+/ ;                    \
            symbol : '+' | '-' | '*' | '/' ;           \
            sexpr    : '(' <expr>* ')' ;               \
            expr     : <number> | <symbol> | <sexpr> ; \
            slither  : /^/ <expr>* /$/ ;               \
            ",
            Number, Symbol, Sexpr, Expr, Slither);

    /* Print version and exit info */
    puts("Slither version 0.0.5");
    puts("Press ctrl+c to exit\n");

    // in a never ending loop
    while (1) {

        // output our prompt and get input
        char* input = readline("slither> ");

        mpc_result_t r;
        // add input to our history
        add_history(input);

        if (mpc_parse("<stdin>", input, Slither, &r)) {
            // on success print the evaluation
            lval* x = lval_read(r.output);
            lval_println(x);
            lval_del(x);
        } else {
            // otherwise print the error
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }

        // free retrieved input
        free(input);
    }

    // undefine and delete parsers
    mpc_cleanup(4, Number, Symbol, Sexpr, Expr, Slither);
    return 0;
}
