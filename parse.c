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

#define LASSERT(args, cond, fmt, ...) \
    if (!(cond)) { \
        lval* err = lval_err(fmt, ##__VA_ARGS__); \
        lval_del(args); \
        return err; \
    }

// TODO: update EMPTYASSERT to resemble LASSERT
#define EMPTYASSERT(qexpr)\
    if (qexpr->cell[0]->count == 0) { lval_del(qexpr); return lval_err("Function passed an empty {}."); }


// FORWARD DECLARATIONS
struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;
// possible lval types enum
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUN };

// possible error types
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

typedef lval*(*lbuiltin) (lenv*, lval*);

// declare new lval struct (lisp value)
struct lval {
    int type;
    long num;
    // error and symbol types have some string data
    char* err;
    char* sym;

    lbuiltin fun;
    // count and pointer to a list of "lval*"
    int count;
    struct lval** cell;
};

// lenv struct
struct lenv {
    int count;
    char** syms;
    lval** vals;
};

char* ltype_name(int t) {
    switch(t) {
        case LVAL_FUN: return "Function";
        case LVAL_NUM: return "Number";
        case LVAL_ERR: return "Error";
        case LVAL_SYM: return "Symbol";
        case LVAL_SEXPR: return "S-Expression";
        case LVAL_QEXPR: return "Q-Expression";
        default: return "Unknown";
    }
}

// add lvals
lval* lval_add(lval* v, lval* x) {
    v->count++;
    // realloc cell with new amount of lval*'s
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    v->cell[v->count-1] = x;
    return v;
}

// delete and free up lval memory
void lval_del(lval* v) {
    switch (v->type) {
        // do nothing for a number type or function type
        case LVAL_NUM: break;
        case LVAL_FUN: break;
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

// functions to create and delete lenvs
lenv* lenv_new(void) {
    // construct a new empty environment
    lenv* e = malloc(sizeof(lenv));
    e->count = 0;
    e->syms = NULL;
    e->vals = NULL;
    return e;
}

// delete lenv
void lenv_del(lenv* e) {
    for (int i = 0; i < e->count; i++) {
        free(e->syms[i]);
        lval_del(e->vals[i]);
    }
    free(e->syms);
    free(e->vals);
    free(e);
}

// lval function type
lval* lval_fun(lbuiltin func) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;
    v->fun = func;
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
lval* lval_err(char* fmt, ...) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;

    // create a va list and initialize it
    va_list va;
    va_start(va, fmt);

    // allocate 512 bytes of space
    v->err = malloc(512);

    // printf the error string with a maximum of 511 characters
    vsnprintf(v->err, 511, fmt, va);

    // reallocate to number of bytes actually used
    v->err = realloc(v->err, strlen(v->err)+1);

    // cleanup our va list
    va_end(va);
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
        case LVAL_FUN: printf("<function>"); break;
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

lval* lval_copy(lval* v) {
    lval* x = malloc(sizeof(lval));
    x->type = v->type;

    switch (v->type) {
        // copy functions and numbers directly
        case LVAL_FUN: x->fun = v->fun; break;
        case LVAL_NUM: x->num = v->num; break;

        // copy strings using malloc and strcpy
        case LVAL_ERR:
            x->err = malloc(strlen(v->err) + 1); // + 1 to include null terminator
            strcpy(x->err, v->err);
        break;

        case LVAL_SYM:
            x->sym = malloc(strlen(v->sym) + 1);
            strcpy(x->sym, v->sym);
        break;

        // copy lists by copying each sub expression
        case LVAL_SEXPR:
        case LVAL_QEXPR:
            x->count = v->count;
            x->cell = malloc(sizeof(lval*) * x->count);
            for (int i = 0; i < x->count; i++) {
                x->cell[i] = lval_copy(v->cell[i]);
            }
        break;
    }
    return x;
}

// get a value from the environment
lval* lenv_get(lenv* e, lval* k) {
    // iterate over all items in environment
    for (int i = 0; i < e->count; i++) {
        // check if the stored string matches the symbol string
        // if it does, return a copy of the value
        if (strcmp(e->syms[i], k->sym) == 0) {
            return lval_copy(e->vals[i]);
        }
    }
    // if no symbol found return error
    return lval_err("Unbound Symbol '%s'", k->sym);
}

// put a new variable into the environment
void lenv_put(lenv* e, lval* k, lval* v) {
    // iterate over all items in environment
    // this is to see if variable already exists
    for (int i = 0; i < e->count; i++) {
        // if variable is found delete item at that position
        // ad replace with variable supplied by user
        if (strcmp(e->syms[i], k->sym) == 0) {
            lval_del(e->vals[i]);
            e->vals[i] = lval_copy(v);
            return;
        }
    }

    // if no existing entry found allocate space for new entry
    e->count++;
    e->vals = realloc(e->vals, sizeof(lval*) * e->count);
    e->syms = realloc(e->syms, sizeof(char*) * e->count);

    // copy contents of lval and symbol string into new location
    e->vals[e->count-1] = lval_copy(v);
    e->syms[e->count-1] = malloc(strlen(k->sym)+1);
    strcpy(e->syms[e->count-1], k->sym);
}

// builtin op descriptions
lval* builtin_op(lenv* e, lval* a, char* op) {
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

lval* builtin_add(lenv* e, lval* a) {
    return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
    return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
    return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
    return builtin_op(e, a, "/");
}

lval* builtin_head(lenv* e, lval* a) {
    LASSERT(a, a->count == 1,
            "Function 'head' passed too many args. "
            "Got %i, Expected %i.",
            a->count, 1);
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'head' passed incorrect type for arg 0. "
            "Got %s, Expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
    EMPTYASSERT(a);

    lval* v = lval_take(a, 0);

    // delete all elements that are not head and return
    while (v->count > 1) { lval_del(lval_pop(v, 1)); }
    return v;
}

lval* builtin_tail(lenv* e, lval* a) {
    LASSERT(a, a->count == 1,
            "Function 'tail' too many args. "
            "Got %i, Expected %i.",
            a->count, 1);
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'tail' passed incorrect type for arg 0. "
            "Got %s, Expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
    EMPTYASSERT(a);
    // take the first argument
    // note that this is taking the entire vector and modifying a new version (immutable?)
    // not sure
    lval* v = lval_take(a, 0);

    // delete first element and return
    lval_del(lval_pop(v, 0));
    return v;
}

lval* builtin_list(lenv* e, lval* a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval* lval_eval(lenv* e, lval* v);

lval* builtin_eval(lenv* e, lval* a) {
    LASSERT(a, a->count == 1,
            "Function 'eval' passed too many args. "
            "Got %i, Expected %i.",
            a->count, 1);
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'eval' passed incorrect type for arg 0 "
            "Got %s, Expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));

    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;
    return lval_eval(e, x);
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

lval* builtin_join(lenv* e, lval* a) {
    for (int i = 0; i < a->count; i++) {
        LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
                "Function 'join' passed incorrect type for arg %i ",
                "Got %s, Expected %s.",
                i, ltype_name(a->cell[i]->type), ltype_name(LVAL_QEXPR));
    }

    // TODO: go over the details of lval_pop and lval_take more
    // need to gain better understanding
    lval* x = lval_pop(a, 0);
    while (a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }

    lval_del(a);
    return x;
}

// TODO: build a more efficient cons
lval* builtin_cons(lenv* e, lval* a) {
    LASSERT(a, a->count == 2,
            "Function 'cons' passed too many args. ",
            "Got %i, Expected %i.",
            a->count, 2);
    lval* x = lval_qexpr();
    x = lval_add(x, lval_pop(a, 0));
    while (a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }
    lval_del(a);
    return x;
}

// init will take a qexpr and return all elements except the final one
lval* builtin_init(lenv* e, lval* a) {
    // TODO: make all errors more informative like this one
    // TODO: make sure this is completely immutable
    LASSERT(a, a->count == 1,
            "Function 'init' passed too many args. "
            "Got %i, Expected %i.",
            a->count, 1);
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'init' given wrong type for arg 0 "
            "Got %s, Expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
    lval_pop(a->cell[0], a->cell[0]->count-1);

    lval* x = lval_take(a, 0);
    return x;
}

// should I return an int?
lval* builtin_len(lenv* e, lval* a) {
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'len' passed the wrong type for arg 0 "
            "Got %s, Expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
    LASSERT(a, a->count == 1,
            "Function 'len' passed too many args. "
            "Got %i, Expected %i.",
            a->count, 1);
    // @TODO: will i need to clean memory for this one? not sure
    lval* x = lval_num(a->cell[0]->count);
    // @TODO: should I delete a?
    // TODO: really see if deleting a is needed
    // I don't think so because we might still need it?
    lval_del(a);
    return x;
}

lval* builtin_def(lenv* e, lval* a) {
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'def' passed incorrect type for arg 0 ",
            "Got %s, Expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));

    // first argument is a symbol list
    lval* syms = a->cell[0];

    // ensure all elements of first list are symbols
    for (int i = 0; i < syms->count; i++) {
        LASSERT(a, syms->cell[i]->type == LVAL_SYM,
                "Function 'def' cannot define non-symbol");
    }

    // check correct number of symbols and values
    LASSERT(a, syms->count == a->count-1,
            "Function 'def' cannot define incorrect number of values to symbols");

    // assign copies of values to symbols
    // puts a copy of the variable defined into our environment
    for (int i = 0; i < syms->count; i++) {
        lenv_put(e, syms->cell[i], a->cell[i+1]);
    }

    lval_del(a);
    return lval_sexpr();
}

// add builtin functions to the environment
void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
    lval* k = lval_sym(name);
    lval* v = lval_fun(func);
    lenv_put(e, k, v);
    lval_del(k);
    lval_del(v);
}

void lenv_add_builtins(lenv* e) {
    // list functions
    lenv_add_builtin(e, "list", builtin_list);
    lenv_add_builtin(e, "head", builtin_head);
    lenv_add_builtin(e, "tail", builtin_tail);
    lenv_add_builtin(e, "eval", builtin_eval);
    lenv_add_builtin(e, "join", builtin_join);
    lenv_add_builtin(e, "cons", builtin_cons);
    lenv_add_builtin(e, "len", builtin_len);

    // mathematical functions
    lenv_add_builtin(e, "+", builtin_add);
    lenv_add_builtin(e, "-", builtin_sub);
    lenv_add_builtin(e, "*", builtin_mul);
    lenv_add_builtin(e, "/", builtin_div);

    // variable functions
    lenv_add_builtin(e, "def", builtin_def);
}

lval* builtin(lenv* e, lval* a, char* func) {
    if (strcmp("list", func) == 0) { return builtin_list(e, a); }
    if (strcmp("head", func) == 0) { return builtin_head(e, a); }
    if (strcmp("tail", func) == 0) { return builtin_tail(e, a); }
    if (strcmp("join", func) == 0) { return builtin_join(e, a); }
    if (strcmp("eval", func) == 0) { return builtin_eval(e, a); }
    if (strcmp("cons", func) == 0) { return builtin_cons(e, a); }
    if (strcmp("len", func) == 0)  { return builtin_len(e, a); }
    if (strcmp("init", func) == 0) { return builtin_init(e, a); }
    if (strstr("+-/*", func)) { return builtin_op(e, a, func); }
    lval_del(a);
    return lval_err("Unknown Function!");
}

lval* lval_eval_sexpr(lenv* e, lval* v) {

    // evaluate children
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(e, v->cell[i]);
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
    if (f->type != LVAL_FUN) {
        lval_del(v);
        lval_del(f);
        return lval_err("first element is not a function");
    }

    // call builtin with operator
    lval* result = f->fun(e, v);
    lval_del(f);
    return result;
}

lval* lval_eval(lenv* e, lval* v) {
    if (v->type == LVAL_SYM) {
        lval* x = lenv_get(e, v);
        lval_del(v);
        return x;
    }
    // evaluate sexprs
    if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(e, v); }
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
            "                                                      \
            number   : /-?[0-9]+/ ;                                \
            symbol   : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/;           \
            sexpr    : '(' <expr>* ')' ;                           \
            qexpr    : '{' <expr>* '}' ;                           \
            expr     : <number> | <symbol> | <sexpr> | <qexpr> ;   \
            slither  : /^/ <expr>* /$/ ;                           \
            ",
            Number, Symbol, Sexpr, Qexpr, Expr, Slither);

    /* Print version and exit info */
    puts("Slither version 0.0.9");
    puts("Press ctrl+c to exit\n");

    // create environment
    lenv* e = lenv_new();
    lenv_add_builtins(e);
    // in a never ending loop
    while (1) {

        // output our prompt and get input
        char* input = readline("slither> ");

        mpc_result_t r;
        // add input to our history
        add_history(input);

        if (mpc_parse("<stdin>", input, Slither, &r)) {
            // on success print the evaluation
            lval* x = lval_eval(e, lval_read(r.output));
            lval_println(x);
            lval_del(x);

            mpc_ast_delete(r.output);
        } else {
            // otherwise print the error
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }

        // free retrieved input
        free(input);
    }

    lenv_del(e);
    // undefine and delete parsers
    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Slither);
    return 0;
}
