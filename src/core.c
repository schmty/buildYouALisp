#include "lib/mpc.h"

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

#define LASSERT_TYPE(func, args, index, expect) \
    LASSERT(args, args->cell[index]->type == expect, \
            "Function '%s' passed incorrect type for argument %i. " \
            "Got %s, Expected %s.", \
            func, index, ltype_name(args->cell[index]->type), ltype_name(expect))

#define LASSERT_NUM(func, args, num) \
    LASSERT(args, args->count == num, \
            "Function '%s' passed incorrect number of arguments. " \
            "Got %i, Expected %i.", func, args->count, num)

#define LASSERT_NOT_EMPTY(func, args, index) \
    LASSERT(args, args->cell[index]->count != 0, \
            "Function '%s' passed {} for argument %i.", func, index);

// TODO: update EMPTYASSERT to resemble LASSERT
#define EMPTYASSERT(qexpr, func_name) \
    if (qexpr->cell[0]->count == 0) { lval_del(qexpr); return lval_err("Function '%s' passed an empty {}.", func_name); }

#define LASSERT2TYPE(func, args, index, type1, type2)    \
    LASSERT(args, (args->cell[index]->type == type1 || args->cell[index]->type == type2), \
            "Function '%s' passed incorrect type for argument %i. " \
            "Got %s, Expected %s or %s.", \
            func, index, ltype_name(args->cell[index]->type), ltype_name(type1), ltype_name(type2));


// FORWARD DECLARATIONS
// TODO: make an ok value to return instead of ()
struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;
void lenv_del(lenv* e);
lenv* lenv_copy(lenv* e);
lval* lval_eval(lenv* e, lval* v);

// FORWARD PARSER DECLARATIONS
mpc_parser_t* Float;
mpc_parser_t* Long;
mpc_parser_t* Symbol;
mpc_parser_t* String;
mpc_parser_t* Comment;
mpc_parser_t* Sexpr;
mpc_parser_t* Qexpr;
mpc_parser_t* Expr;
mpc_parser_t* Slither;

// possible lval types enum
// TODO: Make LVAL_BOOL type
enum { LVAL_LONG, LVAL_FLOAT, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUN, LVAL_STR };

// possible error types
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

typedef lval*(*lbuiltin) (lenv*, lval*);

// declare new lval struct (lisp value)
struct lval {
    int type;

    // Basic
    long lnum;
    float fnum;
    char* err;
    char* sym;
    char* str;

    // Function
    lbuiltin builtin;
    lenv* env;
    lval* formals;
    lval* body;

    // Expression
    int count;
    lval** cell;
};

// lenv struct
struct lenv {
    // parent environment
    lenv* par;
    int count;
    char** syms;
    lval** vals;
};

char* ltype_name(int t) {
    switch(t) {
        case LVAL_FUN: return "Function";
        case LVAL_LONG: return "Long";
        case LVAL_FLOAT: return "Float";
        case LVAL_ERR: return "Error";
        case LVAL_SYM: return "Symbol";
        case LVAL_SEXPR: return "S-Expression";
        case LVAL_QEXPR: return "Q-Expression";
        case LVAL_STR: return "String";
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
        case LVAL_FLOAT:
        case LVAL_LONG: break;
        case LVAL_FUN:
            if (!v->builtin) {
                lenv_del(v->env);
                lval_del(v->formals);
                lval_del(v->body);
            }
        break;
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
        case LVAL_STR:
            free(v->str);
        break;
    }
    // free entire lval struct itself
    free(v);
}


// functions to create and delete lenvs
lenv* lenv_new(void) {
    // construct a new empty environment
    lenv* e = malloc(sizeof(lenv));
    e->par = NULL;
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

lval* lval_lambda(lval* formals, lval* body) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;

    // set builtin to null
    v->builtin = NULL;

    // build new environment
    v->env = lenv_new();

    // set formals and body
    v->formals = formals;
    v->body = body;
    return v;
}

// lval function type
lval* lval_fun(lbuiltin func) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;
    v->builtin = func;
    return v;
}

lval* lval_str(char* s) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_STR;
    v->str = malloc(strlen(s) + 1);
    strcpy(v->str, s);
    return v;
}

// construct a pointer to a new long type lval
lval* lval_long(long x) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_LONG;
    v->lnum = x;
    return v;
}

lval* lval_float(float x) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FLOAT;
    v->fnum = x;
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
lval* lval_read_long(mpc_ast_t* t) {
    errno = 0;
    long x = strtol(t->contents, NULL, 10);
    return errno != ERANGE ?
        lval_long(x) : lval_err("invalid long");
}

// lval read float
lval* lval_read_float(mpc_ast_t* t) {
    errno = 0;
    float x = strtof(t->contents, NULL);
    return errno != ERANGE ?
        lval_float(x) : lval_err("invalid float");
}

lval* lval_read_str(mpc_ast_t* t) {
    // cut off the final quote char
    t->contents[strlen(t->contents)-1] = '\0';
    // copy the string missing out the first quote character
    char* unescaped = malloc(strlen(t->contents+1)+1);
    strcpy(unescaped, t->contents+1);
    // pass through the unescape function
    unescaped = mpcf_unescape(unescaped);
    // construct a new lval using the string
    lval* str = lval_str(unescaped);
    // free the string and return
    free(unescaped);
    return str;
}

// lval read
lval* lval_read(mpc_ast_t* t) {
    // if symbol or number return conversion to that type
    if (strstr(t->tag, "long")) { return lval_read_long(t); }
    if (strstr(t->tag, "float")) { return lval_read_float(t); }
    if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

    // string reading
    if (strstr(t->tag, "string")) { return lval_read_str(t); }


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
        // ignore comments
        if (strstr(t->children[i]->tag, "comment")) { continue; }
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

void lval_print_str(lval* v) {
    // make copy of the string
    char* escaped = malloc(strlen(v->str)+1);
    strcpy(escaped, v->str);
    // pass it through the escape function
    escaped = mpcf_escape(escaped);
    // print it between " characters
    printf("\"%s\"", escaped);
    // free the copied string
    free(escaped);
}

// print an lval
void lval_print(lval* v) {
    switch (v->type) {
        case LVAL_LONG: printf("%li", v->lnum); break;
        case LVAL_FLOAT: printf("%f", v->fnum); break;
        case LVAL_FUN:
            if (v->builtin) {
                printf("<builtin>");
            } else {
                printf("(\\ ");
                lval_print(v->formals);
                putchar(' ');
                lval_print(v->body);
                putchar(')');
            }
        break;
        case LVAL_ERR: printf("Error: %s", v->err); break;
        case LVAL_SYM: printf("%s", v->sym); break;
        case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
        case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
        case LVAL_STR: lval_print_str(v); break;
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
        case LVAL_FUN:
            if (v->builtin) {
                x->builtin = v->builtin;
            } else {
                x->builtin = NULL;
                x->env = lenv_copy(v->env);
                x->formals = lval_copy(v->formals);
                x->body = lval_copy(v->body);
            }
        break;
        case LVAL_LONG: x->lnum = v->lnum; break;
        case LVAL_FLOAT: x->fnum = v->fnum; break;

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
        case LVAL_STR:
            x->str = malloc(strlen(v->str) + 1);
            strcpy(x->str, v->str);
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
    // if no symbol found check for in parent otherwise return error
    if (e->par) {
        return lenv_get(e->par, k);
    } else {
        return lval_err("Unbound Symbol '%s'", k->sym);
    }
}

lenv* lenv_copy(lenv* e) {
    lenv* n = malloc(sizeof(lenv));
    n->par = e->par;
    n->count = e->count;
    n->syms = malloc(sizeof(char*) * n->count);
    n->vals = malloc(sizeof(lval*) * n->count);
    for (int i = 0; i < e->count; i++) {
        n->syms[i] = malloc(strlen(e->syms[i]) + 1);
        strcpy(n->syms[i], e->syms[i]);
        n->vals[i] = lval_copy(e->vals[i]);
    }
    return n;
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

void lenv_def(lenv* e, lval* k, lval* v) {
    // iterate till e has no parent
    while (e->par) {
        e = e->par;
    }
    // put value in e
    lenv_put(e, k, v);
}

// long to float conversion
lval* lval_ltof(lval* a) {
    lval* val = lval_float((float) a->lnum);
    lval_del(a);
    return val;
}

// TODO: builtin op will go here
lval* builtin_op(lenv* e, lval* a, char* op) {
    for (int i = 0; i < a->count; i++) {
        LASSERT2TYPE(op, a, i, LVAL_FLOAT, LVAL_LONG);
    }
    // pop the first element
    lval* x = lval_pop(a, 0);

    // if no arguments and sub the perform unary negation
    if ((strcmp(op, "-") == 0) && a->count == 0 && x->type == LVAL_LONG) {
        x->lnum = -x->lnum;
    } else if ((strcmp(op, "-") == 0) && a->count == 0 && x->type == LVAL_FLOAT) {
        x->fnum = -x->fnum;
    }

    // while there are still elements remaining
    while (a->count > 0) {

        // pop the next element
        lval* y = lval_pop(a, 0);

        if (x->type == LVAL_LONG && y->type == LVAL_LONG) {
            if (strcmp(op, "+") == 0) { x->lnum += y->lnum; }
            if (strcmp(op, "-") == 0) { x->lnum -= y->lnum; }
            if (strcmp(op, "*") == 0) { x->lnum *= y->lnum; }
            if (strcmp(op, "/") == 0) {
                if (y->lnum == 0) {
                    lval_del(x);
                    lval_del(y);
                    x = lval_err("Division by Zero!"); break;
                }
                x->lnum /= y->lnum;
            }

            lval_del(y);
        } else if (x->type == LVAL_FLOAT && y->type == LVAL_LONG) {
            if (strcmp(op, "+") == 0) { x->fnum += y->lnum; }
            if (strcmp(op, "-") == 0) { x->fnum -= y->lnum; }
            if (strcmp(op, "*") == 0) { x->fnum *= y->lnum; }
            if (strcmp(op, "/") == 0) {
                if (y->lnum == 0) {
                    lval_del(x);
                    lval_del(y);
                    x = lval_err("Division by Zero!"); break;
                }
                x->fnum /= y->lnum;
            }

            lval_del(y);
        } else if (x->type == LVAL_FLOAT && y->type == LVAL_FLOAT) {
            if (strcmp(op, "+") == 0) { x->fnum += y->fnum; }
            if (strcmp(op, "-") == 0) { x->fnum -= y->fnum; }
            if (strcmp(op, "*") == 0) { x->fnum *= y->fnum; }
            if (strcmp(op, "/") == 0) {
                if (y->lnum == 0) {
                    lval_del(x);
                    lval_del(y);
                    x = lval_err("Division by Zero!"); break;
                }
                x->fnum /= y->fnum;
            }

            lval_del(y);
        } else if (x->type == LVAL_LONG && y->type == LVAL_FLOAT) {
            x = lval_ltof(x);
            if (strcmp(op, "+") == 0) { x->fnum += y->fnum; }
            if (strcmp(op, "-") == 0) { x->fnum -= y->fnum; }
            if (strcmp(op, "*") == 0) { x->fnum *= y->fnum; }
            if (strcmp(op, "/") == 0) {
                if (y->lnum == 0) {
                    lval_del(x);
                    lval_del(y);
                    x = lval_err("Division by Zero!"); break;
                }
                x->fnum /= y->fnum;
            }

            lval_del(y);
        }

    }

    lval_del(a);
    return x;
}

// builtin load
lval* builtin_load(lenv* e, lval* a) {
    LASSERT_NUM("load", a, 1);
    LASSERT_TYPE("load", a, 0, LVAL_STR);

    // parse file given by string name
    mpc_result_t r;
    if (mpc_parse_contents(a->cell[0]->str, Slither, &r)) {

        // read contents
        lval* expr = lval_read(r.output);
        mpc_ast_delete(r.output);

        // evaluate each expression
        while (expr->count) {
            lval* x = lval_eval(e, lval_pop(expr, 0));
            // if evaluation leads to error print it
            if (x->type == LVAL_ERR) { lval_println(x); }
            lval_del(x);
        }

        // delete expressions and arguments
        lval_del(expr);
        lval_del(a);

        // return empty list
        return lval_sexpr();
    } else {
        // get parse error as string
        char* err_msg = mpc_err_string(r.error);
        mpc_err_delete(r.error);

        // create new error message using it
        lval* err = lval_err("Could not load library %s", err_msg);
        free(err_msg);
        lval_del(a);

        // cleanup and return error
        return err;
    }
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
    LASSERT(a, (a->cell[0]->type == LVAL_QEXPR || a->cell[0]->type == LVAL_STR),
            "Function 'head' passed incorrect type for arg 0. "
            "Got %s, Expected %s or %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR), ltype_name(LVAL_STR));
    if (a->cell[0]->type == LVAL_QEXPR) {
        LASSERT_NOT_EMPTY("head", a, 0);
    }

    if (a->cell[0]->type == LVAL_STR) {
        lval* v = lval_str(&a->cell[0]->str[0]);
        lval_del(a);
        return v;
    }

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
    LASSERT(a, (a->cell[0]->type == LVAL_QEXPR || a->cell[0]->type == LVAL_STR),
            "Function 'tail' passed incorrect type for arg 0. "
            "Got %s, Expected %s or %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR),
            ltype_name(LVAL_STR));
    if (a->cell[0]->type == LVAL_QEXPR) {
        LASSERT_NOT_EMPTY("tail", a, 0);
    }

    if (a->cell[0]->type == LVAL_STR) {
        // make a new lval with the string starting from element 1 to the end
        lval* v = lval_str(&a->cell[0]->str[1]);
        lval_del(a);
        return v;
    }
    // take the first argument
    // note that this is taking the entire vector and modifying a new version (immutable?)
    // not sure
    lval* v = lval_take(a, 0);

    // delete first element and return
    lval_del(lval_pop(v, 0));
    return v;
}

lval* builtin_len(lenv* e, lval* a) {
    LASSERT(a, (a->cell[0]->type == LVAL_QEXPR) ||
        (a->cell[0]->type == LVAL_STR),
        "Function 'len' passed the wrong type for arg 0 "
        "Got %s, Expected %s or %s.",
        ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR), ltype_name(LVAL_STR));
    LASSERT(a, a->count == 1,
        "Function 'len' passed too many args. "
        "Got %i, Expected %i.",
        a->count, 1);
    // @TODO: will i need to clean memory for this one? not sure
    if (a->cell[0]->type == LVAL_STR) {
        int size = (int) strlen(a->cell[0]->str);
        lval_del(a);
        return lval_long(size);
    }
    lval* x = lval_long(a->cell[0]->count);
    // @TODO: should I delete a?
    // TODO: really see if deleting a is needed
    // I don't think so because we might still need it?
    lval_del(a);
    return x;
}

lval* builtin_list(lenv* e, lval* a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval* builtin_show(lenv* e, lval* a) {
    printf("%s", a->cell[0]->str);
    putchar('\n');
    // just return an empty sexpr after printing cstring
    lval_del(a);
    return lval_sexpr();
}

// builtin comparisons
// will return a 1 or a 0 as an lval_num
lval* builtin_ord(lenv* e, lval* a, char* op) {
    LASSERT_NUM(op, a, 2);
    LASSERT2TYPE(op, a, 0, LVAL_LONG, LVAL_FLOAT);
    LASSERT2TYPE(op, a, 1, LVAL_LONG, LVAL_FLOAT);
    // binary comparison operators
    // result to be used for storage
    int res;
    if (a->cell[0]->type == LVAL_FLOAT && a->cell[1]->type == LVAL_LONG) {
        a->cell[1] = lval_ltof(a->cell[1]);
        if (strcmp(op, ">") == 0) { res = (a->cell[0]->fnum > a->cell[1]->fnum); }
        if (strcmp(op, "<") == 0) { res = (a->cell[0]->fnum < a->cell[1]->fnum); }
        if (strcmp(op, ">=") == 0) { res = (a->cell[0]->fnum >= a->cell[1]->fnum); }
        if (strcmp(op, "<=") == 0) { res = (a->cell[0]->fnum <= a->cell[1]->fnum); }
        lval_del(a);
        return lval_long(res);
    } else if (a->cell[0]->type == LVAL_LONG && a->cell[1]->type == LVAL_FLOAT) {
        a->cell[0] = lval_ltof(a->cell[0]);
        if (strcmp(op, ">") == 0) { res = (a->cell[0]->fnum > a->cell[1]->fnum); }
        if (strcmp(op, "<") == 0) { res = (a->cell[0]->fnum < a->cell[1]->fnum); }
        if (strcmp(op, ">=") == 0) { res = (a->cell[0]->fnum >= a->cell[1]->fnum); }
        if (strcmp(op, "<=") == 0) { res = (a->cell[0]->fnum <= a->cell[1]->fnum); }
        lval_del(a);
        return lval_long(res);
    } else if (a->cell[0]->type == LVAL_FLOAT && a->cell[0]->type == LVAL_FLOAT) {
        if (strcmp(op, ">") == 0) { res = (a->cell[0]->fnum > a->cell[1]->fnum); }
        if (strcmp(op, "<") == 0) { res = (a->cell[0]->fnum < a->cell[1]->fnum); }
        if (strcmp(op, ">=") == 0) { res = (a->cell[0]->fnum >= a->cell[1]->fnum); }
        if (strcmp(op, "<=") == 0) { res = (a->cell[0]->fnum <= a->cell[1]->fnum); }
        lval_del(a);
        return lval_long(res);
    } else {
        if (strcmp(op, ">") == 0) { res = (a->cell[0]->lnum > a->cell[1]->lnum); }
        if (strcmp(op, "<") == 0) { res = (a->cell[0]->lnum < a->cell[1]->lnum); }
        if (strcmp(op, ">=") == 0) { res = (a->cell[0]->lnum >= a->cell[1]->lnum); }
        if (strcmp(op, "<=") == 0) { res = (a->cell[0]->lnum <= a->cell[1]->lnum); }

        lval_del(a);
        return lval_long(res);
    }
}

lval* builtin_gt(lenv* e, lval* a) {
    return builtin_ord(e, a, ">");
}

lval* builtin_lt(lenv* e, lval* a) {
    return builtin_ord(e, a, "<");
}

lval* builtin_gte(lenv* e, lval* a) {
    return builtin_ord(e, a, ">=");
}

lval* builtin_lte(lenv* e, lval* a) {
    return builtin_ord(e, a, "<=");
}

int lval_eq(lval* a, lval* b) {

    // if types do not line up then return 0 (false)
    if (a->type != b->type) { return 0; }
    switch (a->type) {
        case LVAL_LONG:
            return (a->lnum == b->lnum);
        break;
        case LVAL_FLOAT:
            return (a->fnum == b->fnum);
        case LVAL_SYM:
            return (a->sym == b->sym);
        break;
        case LVAL_ERR:
            return (a->err == b->err);
        case LVAL_SEXPR:
        case LVAL_QEXPR:
            // if counts of qexpr isnt the same 0 (false)
            if (a->count != b->count) { return 0; }
            for (int i = 0; i < a->count; i++) {
                if (!lval_eq(a->cell[i], b->cell[i])) { return 0; }
            }
            return 1;
        break;
        case LVAL_FUN:
            // if a and b are builtins compare the builtins
            if (a->builtin && b->builtin) {
                return (a->builtin == b->builtin);
            } else {
                return (a->formals == b->formals) && (a->body == b->body);
            }
        break;
        case LVAL_STR:
            return (strcmp(a->str, b->str) == 0);
        break;
    }
    // if nothing else just return false
    return 0;
}

lval* builtin_cmp(lenv* e, lval* a, char* op) {
    LASSERT_NUM(op, a, 2);
    LASSERT2TYPE(op, a, 0, LVAL_FLOAT, LVAL_LONG);
    LASSERT2TYPE(op, a, 1, LVAL_FLOAT, LVAL_LONG);
    // else do long comparisons
    if (a->cell[0]->type == LVAL_FLOAT && a->cell[1] == LVAL_LONG) {
        a->cell[1] = lval_ltof(a->cell[1]);
    } else if (a->cell[0]->type == LVAL_LONG && a->cell[1]->type == LVAL_FLOAT) {
        a->cell[0] = lval_ltof(a->cell[0]);
    }
    lval* res;
    if (strcmp(op, "==") == 0) {
        res = lval_long(lval_eq(a->cell[0], a->cell[1]));
    } else {
        res = lval_long(!lval_eq(a->cell[0], a->cell[1]));
    }
    lval_del(a);
    return res;
}

// TODO: cleaner way of doing these two
lval* builtin_eq(lenv* e, lval* a) {
    return builtin_cmp(e, a, "==");
}

lval* builtin_neq(lenv* e, lval* a) {
    return builtin_cmp(e, a, "!=");
}


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

lval* builtin_if(lenv* e, lval* a) {
    LASSERT_NUM("if", a, 3);
    LASSERT2TYPE("if", a, 0, LVAL_LONG, LVAL_FLOAT)
    LASSERT_TYPE("if", a, 1, LVAL_QEXPR);
    LASSERT_TYPE("if", a, 2, LVAL_QEXPR);

    // if stuff

    // convert both code cells to evaluatable
    lval* result;
    a->cell[1]->type = LVAL_SEXPR;
    a->cell[2]->type = LVAL_SEXPR;

    // TODO: this will break, need to emulate generics or something.
    if (a->cell[0]->lnum) {
        result = lval_eval(e, lval_pop(a, 1));
    } else {
        result = lval_eval(e, lval_pop(a, 2));
    }
    // else return the other thing
    lval_del(a);
    return result;
}

lval* lval_call(lenv* e, lval* f, lval* a) {
    // if builtin then simply call that
    if (f->builtin) { return f->builtin(e, a); }

    // record argument counts
    int given = a->count;
    int total = f->formals->count;

    // while args still remain to be processed
    while (a->count) {

        // if we've ran out of formal args to bind
        // i.e arg count too large for defined func
        if (f->formals->count == 0) {
            lval_del(a);
            return lval_err("Function passed too many arguments. "
                            "Got %i, Expected %i.", given, total);
        }

        // pop the first symbol from the formals
        lval* sym = lval_pop(f->formals, 0);

        // special case to deal with '&'
        if (strcmp(sym->sym, "&") == 0) {
            // ensure '&' is followed by another symbol
            if (f->formals->count != 1) {
                lval_del(a);
                return lval_err("Function format invalid. "
                    "Symbol '&' not followed by single symbol.");
            }

            // next formal should be bound to remaining args
            lval* nsym = lval_pop(f->formals, 0);
            lenv_put(f->env, nsym, builtin_list(e, a));
            lval_del(sym);
            lval_del(nsym);
            break;
        }

        // pop the next arg from the list
        lval* val = lval_pop(a, 0);

        // bind a copy into the functions env
        lenv_put(f->env, sym, val);

        // delete the symbol and value
        lval_del(sym);
        lval_del(val);
    }

    // if '&' remains in formal list bind to empty list
    if (f->formals->count > 0 &&
        strcmp(f->formals->cell[0]->sym, "&") == 0) {

        // check to ensure that & is not passed invalidly
        if (f->formals->count != 2) {
            return lval_err("Function format invalid. "
                "Symbol '&' not followed by single symbol.");
        }

        // pop and delete '&' symbol
        lval_del(lval_pop(f->formals, 0));

        // pop next symbol and create empty list
        lval* sym = lval_pop(f->formals, 0);
        lval* val = lval_qexpr();

        // bind to environment and delete
        lenv_put(f->env, sym, val);
        lval_del(sym);
        lval_del(val);
    }
    // arg list is now bound so can be cleared up
    lval_del(a);

    // if all formals have been bound evaluate
    if (f->formals->count == 0) {
        // set env parent to evaluation env
        f->env->par = e;

        // evaluate and return
        return builtin_eval(f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
    } else {
        // otherwise return partially evaluated function
        return lval_copy(f);
    }
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

lval* str_join(lval* a, lval* b) {
    // join two strings
    // +1 for null terminator
    char* result = malloc(strlen(a->str) + strlen(b->str)+1);
    strcpy(result, a->str);
    strcat(result, b->str);
    // clean up args
    lval_del(a);
    lval_del(b);
    return lval_str(result);
}

// TODO: adapt join, tail, head, to work on strings
lval* builtin_join(lenv* e, lval* a) {
    // if args are a string
    if (a->cell[0]->type == LVAL_STR) {
        for (int i = 0; i < a->count; i++) {
            // TODO: maybe make this clearer?
            LASSERT(a, a->cell[i]->type == LVAL_STR,
                "Function 'join' passed incorrect type for arg %i ",
                "Got %s, Expected %s.",
                ltype_name(a->cell[i]->type), ltype_name(LVAL_STR));

        }
        // join first arg into x to kick it off
        lval* x = lval_pop(a, 0);
        while (a->count) {
            x = str_join(x, lval_pop(a, 0));
        }
        lval_del(a);
        return x;
    } else {
        for (int i = 0; i < a->count; i++) {
            LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
                "Function 'join' passed incorrect type for arg %i ",
                "Got %s, Expected %s.",
                ltype_name(a->cell[i]->type), ltype_name(LVAL_QEXPR));
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

lval* builtin_lambda(lenv* e, lval* a) {
    // check two arguments, each of which are q expressions
    LASSERT_NUM("fn", a, 2);
    LASSERT_TYPE("fn", a, 0, LVAL_QEXPR);
    LASSERT_TYPE("fn", a, 1, LVAL_QEXPR);

    // check the first Q-Expression contains only symbols
    for (int i = 0; i < a->cell[0]->count; i++) {
        LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
                "Cannot define non-symbol. Got %s, Expected %s.",
                ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_SYM));
    }

    // pop the first two arguments and pass them to lval_lambda
    lval* formals = lval_pop(a, 0);
    lval* body = lval_pop(a, 0);
    lval_del(a);

    return lval_lambda(formals, body);
}

lval* builtin_var(lenv* e, lval* a, char* func) {
    LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

    lval* syms = a->cell[0];
    for (int i = 0; i < syms->count; i++) {
        LASSERT(a, (syms->cell[i]->type == LVAL_SYM),
                "Function '%s' cannot define non-symbol. "
                "Got %s, Expected %s.", func,
                ltype_name(syms->cell[i]->type),
                ltype_name(LVAL_SYM));
    }

    LASSERT(a, (syms->count == a->count-1),
            "Function '%s' passed too many arguments for symbols. "
            "Got %i, Expected %i.", func, syms->count, a->count-1);

    for (int i = 0; i < syms->count; i++) {
        // if 'def' define in globally. If 'put' define in locally
        if (strcmp(func, "def") == 0) {
            lenv_def(e, syms->cell[i], a->cell[i+1]);
        }

        if (strcmp(func, "=") == 0) {
            lenv_put(e, syms->cell[i], a->cell[i+1]);
        }
    }

    lval_del(a);
    return lval_sexpr();
}

lval* builtin_not(lenv* e, lval* a) {
    // if '!' operator expect 1 arg of num type
    LASSERT_NUM("!", a, 1);
    LASSERT_TYPE("!", a, 0, LVAL_LONG);
    int res;
    res = !(a->cell[0]->lnum);
    lval_del(a);
    return lval_long(res);
}

lval* builtin_logic(lenv* e, lval* a, char* op) {
    // else other logic operators expect 2 num args
    if (strcmp(op, "!") == 0) { return builtin_not(e, a); }
    LASSERT_NUM(op, a, 2);
    LASSERT_TYPE(op, a, 0, LVAL_LONG);
    LASSERT_TYPE(op, a, 1, LVAL_LONG);

    // TODO: arg amount checking
    int res;
    if (strcmp(op, "||") == 0) {
        res = (a->cell[0]->lnum || a->cell[1]->lnum);
    }
    if (strcmp(op, "&&") == 0) {
        res = (a->cell[0]->lnum && a->cell[1]->lnum);
    }
    lval_del(a);
    return lval_long(res);
}

lval* builtin_or(lenv* e, lval* a) {
    return builtin_logic(e, a, "||");
}

lval* builtin_and(lenv* e, lval* a) {
    return builtin_logic(e, a, "&&");
}


lval* builtin_def(lenv* e, lval* a) {
    return builtin_var(e, a, "def");
}

lval* builtin_put(lenv* e, lval* a) {
    return builtin_var(e, a, "=");
}

lval* builtin_print(lenv* e, lval* a) {
    // print each argument followed by a space
    for (int i = 0; i < a->count; i++) {
        lval_print(a->cell[i]);
        putchar(' ');
    }

    // print a newline and delete args
    putchar('\n');
    lval_del(a);

    return lval_sexpr();
}

lval* builtin_error(lenv* e, lval* a) {
    LASSERT_NUM("error", a, 1);
    LASSERT_TYPE("error", a, 0, LVAL_STR);

    // construct error from first argument
    lval* err = lval_err(a->cell[0]->str);

    // delete arguments and return
    lval_del(a);
    return err;
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
    lenv_add_builtin(e, "fn", builtin_lambda);
    lenv_add_builtin(e, "def", builtin_def);
    lenv_add_builtin(e, "=", builtin_put);

    // comparison functions
    lenv_add_builtin(e, ">", builtin_gt);
    lenv_add_builtin(e, "<", builtin_lt);
    lenv_add_builtin(e, ">=", builtin_gte);
    lenv_add_builtin(e, "<=", builtin_lte);
    lenv_add_builtin(e, "==", builtin_eq);
    lenv_add_builtin(e, "!=", builtin_neq);

    // control (if else etc.)
    lenv_add_builtin(e, "if", builtin_if);

    // logical operators
    lenv_add_builtin(e, "||", builtin_or);
    lenv_add_builtin(e, "&&", builtin_and);
    lenv_add_builtin(e, "!", builtin_not);

    // string functions
    lenv_add_builtin(e, "load", builtin_load);
    lenv_add_builtin(e, "error", builtin_error);
    lenv_add_builtin(e, "print", builtin_print);
    lenv_add_builtin(e, "show", builtin_show);
}

lval* builtin(lenv* e, lval* a, char* func) {
    if (strcmp("list", func) == 0) { return builtin_list(e, a); }
    if (strcmp("head", func) == 0) { return builtin_head(e, a); }
    if (strcmp("tail", func) == 0) { return builtin_tail(e, a); }
    if (strcmp("join", func) == 0) { return builtin_join(e, a); }
    if (strcmp("eval", func) == 0) { return builtin_eval(e, a); }
    if (strcmp("cons", func) == 0) { return builtin_cons(e, a); }
    if (strcmp("len", func) == 0) { return builtin_len(e, a); }
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
        lval* err = lval_err(
            "S-Expression starts with incorrect type. "
            "Got %s, Expected %s.",
            ltype_name(f->type), ltype_name(LVAL_FUN));
        lval_del(f);
        lval_del(v);
        return err;
    }

    // call builtin with operator
    lval* result = lval_call(e, f, v);
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
    // already forward declared
    Long = mpc_new("long");
    Float = mpc_new("float");
    Symbol = mpc_new("symbol");
    String = mpc_new("string");
    Comment = mpc_new("comment");
    Sexpr = mpc_new("sexpr");
    Qexpr = mpc_new("qexpr");
    Expr = mpc_new("expr");
    Slither = mpc_new("slither");

    // define them with the following language
    mpca_lang(MPCA_LANG_DEFAULT,
            "                                                                                     \
            float    : /-?[0-9]+\\.?[0-9]+/ ;                                                              \
            long     : /-?[0-9]+/ ;                                                               \
            symbol   : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&|]+/;                                         \
            string   : /\"(\\\\.|[^\"])*\"/ ;                                                     \
            comment  : /;[^\\r\\n]*/ ;                                                            \
            sexpr    : '(' <expr>* ')' ;                                                          \
            qexpr    : '{' <expr>* '}' ;                                                          \
            expr     : <float> | <long> | <symbol> | <sexpr> | <qexpr> | <string> | <comment> ;   \
            slither  : /^/ <expr>* /$/ ;                                                          \
            ",
            Float, Long, Symbol, String, Comment, Sexpr, Qexpr, Expr, Slither);

    // create environment
    lenv* e = lenv_new();
    lenv_add_builtins(e);

    // load std lib no matter prompt or file loaded
    // NOTE this filepath is relative to the slither binary
    lval* stdlib = lval_add(lval_sexpr(), lval_str("/Users/jake/Projects/slither/lib/slither/std.slr"));
    lval* load = builtin_load(e, stdlib);
    if (load->type == LVAL_ERR) {
        lval_println(load);
        lval_del(load);
    }
    // interactive prompt
    if (argc == 1) {
        /* Print version and exit info */
        puts("Slither version 0.1.1");
        puts("Press ctrl+c to exit\n");

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
    }

    // supplied with a list of files
    if (argc >= 2) {
        // loop over each supplied filename (starting from 1)
        for (int i = 1; i < argc; i++) {
            // arg list with a single argument, the filename
            lval* args = lval_add(lval_sexpr(), lval_str(argv[i]));

            // pass to builtin load and get the result
            lval* x = builtin_load(e, args);

            // if the result is an error be sure to print it
            if (x->type == LVAL_ERR) { lval_println(x); }
            lval_del(x);
        }
    }

    lenv_del(e);
    lval_del(load);
    //lval_del(stdlib);
    // undefine and delete parsers
    mpc_cleanup(9, Float, Long, Symbol, String, Comment, Sexpr, Qexpr, Expr, Slither);
    return 0;
}
