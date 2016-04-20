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

#define LASSERT(args, cond, err) \
    if (!(cond)) { lval_del(args); return lval_err(err); }
// possible lval types enum
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

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

// construct a pointer to a new empty qexpr lval
lval* lval_qexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
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
        // if qexpr or sexpr then delete all elements inside
        case LVAL_QEXPR:
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

    // if root (>) or sexpr or qexpr then create empty list
    lval* x = NULL;
    if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
    if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }
    if (strstr(t->tag, "qexpr"))  { x = lval_qexpr(); }

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
        case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
    }
}

// lval print line
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

lval* lval_pop(lval* v, int i) {
    // find the item at i
    lval* x = v->cell[i];

    // shift memory after the item at "i" over the top
    memmove(&v->cell[i], &v->cell[i+1],
            sizeof(lval*) * (v->count-i-1));

    // decrease the count of items in list
    v->count--;

    // reallocate memory used
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    return x;
}

lval* lval_take(lval* v, int i) {
    lval* x = lval_pop(v, i);
    lval_del(v);
    return x;
}

// builtin op descriptions
lval* builtin_op(lval* a, char* op) {
    // ensure all arguments are numbers
    for (int i = 0; i < a->count; i++) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_del(a);
            return lval_err("Cannot operate on non-number!");
        }
    }

    // pop the first element
    lval* x = lval_pop(a, 0);

    // if no arguments and sub the perform unary negation
    if ((strcmp(op, "-") == 0) && a->count == 0) {
        x->num = -x->num;
    }

    // while there are still elements remaining
    while (a->count > 0) {

        // pop the next element
        lval* y = lval_pop(a, 0);

        // using += -= *= /= because of continual looping through list of
        // multiple arguments
        if (strcmp(op, "+") == 0) { x->num += y->num; }
        if (strcmp(op, "-") == 0) { x->num -= y->num; }
        if (strcmp(op, "*") == 0) { x->num *= y->num; }
        if (strcmp(op, "/") == 0) {
            if (y->num == 0) {
                lval_del(x);
                lval_del(y);
                x = lval_err("Division by Zero!"); break;
            }
            x->num /= y->num;
        }

        lval_del(y);
    }

    lval_del(a);
    return x;
}

lval* builtin_head(lval* a) {
    LASSERT(a, a->count == 1,
            "Function 'head' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'head' passed incorrect type!");
    LASSERT(a, a->cell[0]->count != 0,
            "Function 'head' passed {}!");

    lval* v = lval_take(a, 0);

    // delete all elements that are not head and return
    while (v->count > 1) { lval_del(lval_pop(v, 1)); }
    return v;
}

lval* builtin_tail(lval* a) {
    LASSERT(a, a->count == 1,
            "Function 'tail' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'tail' passed incorrect type!");
    LASSERT(a, a->cell[0]->count != 0,
            "Function 'tail' passed {}!");
    // take the first argument
    // note that this is taking the entire vector and modifying a new version (immutable?)
    // not sure
    lval* v = lval_take(a, 0);

    // delete first element and return
    lval_del(lval_pop(v, 0));
    return v;
}

lval* builtin_list(lval* a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval* lval_eval(lval* v);

lval* builtin_eval(lval* a) {
    LASSERT(a, a->count == 1,
            "Function 'eval' passed too many arguments!");
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'eval' passed incorrect type!");

    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(x);
}

lval* lval_join(lval* x, lval* y) {
    // for each cell in y add it to x
    while (y->count) {
        x = lval_add(x, lval_pop(y, 0));
    }

    // delete the empty y and return x
    // this does NOT look like immutability to me
    // will need to look into it further
    lval_del(y);
    return x;
}

lval* builtin_join(lval* a) {
    for (int i = 0; i < a->count; i++) {
        LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
                "Function 'join' passed incorrect type!");
    }

    // go over the details of lval_pop and lval_take more
    // need to gain better understanding
    lval* x = lval_pop(a, 0);
    while (a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }

    lval_del(a);
    return x;
}


lval* builtin_cons(lval* a) {
    LASSERT(a, a->count == 2,
            "Function 'cons' passed to many args!");
    lval* x = lval_qexpr();
    x = lval_add(x, lval_pop(a, 0));
    while (a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }
    return x;
}

lval* builtin(lval* a, char* func) {
    if (strcmp("list", func) == 0) { return builtin_list(a); }
    if (strcmp("head", func) == 0) { return builtin_head(a); }
    if (strcmp("tail", func) == 0) { return builtin_tail(a); }
    if (strcmp("join", func) == 0) { return builtin_join(a); }
    if (strcmp("eval", func) == 0) { return builtin_eval(a); }
    if (strcmp("cons", func) == 0) { return builtin_cons(a); }
    if (strstr("+-/*", func)) { return builtin_op(a, func); }
    lval_del(a);
    return lval_err("Unknown Function!");
}

lval* lval_eval_sexpr(lval* v) {

    // evaluate children
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(v->cell[i]);
    }

    // error checking
    for (int i = 0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
    }

    // empty expression
    if (v->count == 0) { return v; }

    // single expression
    if (v->count == 1) { return lval_take(v, 0); }

    // ensure first element is symbol
    lval* f = lval_pop(v, 0);
    if (f->type != LVAL_SYM) {
        lval_del(f);
        lval_del(v);
        return lval_err("S-expression Does not start with symbol");
    }

    // call builtin with operator
    lval* result = builtin(v, f->sym);
    lval_del(f);
    return result;
}

lval* lval_eval(lval* v) {
    // evaluate sexprs
    if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
    // all other lval types remain the same
    return v;
}

int main(int argc, char** argv) {
    // create some parsers
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Symbol = mpc_new("symbol");
    mpc_parser_t* Sexpr = mpc_new("sexpr");
    mpc_parser_t* Qexpr = mpc_new("qexpr");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Slither = mpc_new("slither");

    // define them with the following language
    mpca_lang(MPCA_LANG_DEFAULT,
            "                                                        \
            number   : /-?[0-9]+/ ;                                  \
            symbol   : \"list\" | \"head\" | \"tail\"                \
                     | \"join\" | \"eval\" | \"cons\"                \
                     | '+' | '-' | '*' | '/' ;                       \
            sexpr    : '(' <expr>* ')' ;                             \
            qexpr    : '{' <expr>* '}' ;                             \
            expr     : <number> | <symbol> | <sexpr> | <qexpr> ;     \
            slither  : /^/ <expr>* /$/ ;                             \
            ",
            Number, Symbol, Sexpr, Qexpr, Expr, Slither);

    /* Print version and exit info */
    puts("Slither version 0.0.8");
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
            lval* x = lval_eval(lval_read(r.output));
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
    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Slither);
    return 0;
}
