/* symbol table management */

#include "sym.h"
#include "misc.h"
#include "quad.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define STABSIZE 119 /* hash table size for strings */
#define ITABSIZE 37  /* hash table size for identifiers */

#define MAXARGS 50
#define MAXLOCS 50

int formalnum;             /* number of formal arguments */
char formaltypes[MAXARGS]; /* types of formal arguments  */
int localnum;              /* number of local variables  */
char localtypes[MAXLOCS];  /* types of local variables   */
int localwidths[MAXLOCS];  /* widths of local variables  */

int level = 0; /* current block level */

struct s_chain {
    char *s_ptr;               /* string pointer */
    struct s_chain *s_next;    /* next in chain */
} * str_table[STABSIZE] = {0}; /* string hash table */

struct id_entry *id_table[ITABSIZE] = {0}; /* identifier hash table */

/*
 * install - install name with block level blev, return ptr 
 */
struct id_entry *install(char *name, int blev) {
    struct id_entry *ip, **q;

    if (blev < 0)
        blev = level;

    /* allocate space */
    ip = (struct id_entry *) alloc(sizeof(struct id_entry));
    ip->u.ltype = nullptr;
    ip->v.b = nullptr;

    /* set fields of symbol table */
    strcpy(ip->i_name,name);
    ip->i_blevel = blev;
    for (q = &id_table[hash(name) % ITABSIZE]; *q; q = &((*q)->i_link))
        if (blev >= (*q)->i_blevel)
            break;
    ip->i_link = *q;
    *q = ip;
    return (ip);
}

/*
 * lookup - lookup name, return ptr; use default scope if blev == 0
 */
struct id_entry *lookup(char *name, int blev) {
    struct id_entry *p;

    for (p = id_table[hash(name) % ITABSIZE]; p; p = p->i_link)
        if (strcmp(name, p->i_name)==0 && (blev == 0 || blev == p->i_blevel))
            return (p);
    return (NULL);
}

/*
 * sdump - dump string table to f
 */
void sdump(FILE *f) {
    struct s_chain **s, *p;

    fprintf(f, "Dumping string table\n");
    for (s = str_table; s < &str_table[STABSIZE]; s++)
        for (p = *s; p; p = p->s_next)
            fprintf(f, "%s\n", p->s_ptr);
}

/*
 * slookup - lookup str in string table, install if necessary, return ptr 
 */
char *slookup(char str[]) {
    struct s_chain *p;
    int i, k;

    for (k = i = 0; i < 5; i++) /* simple hash function */
        if (str[i])
            k += str[i];
        else
            break;

    k %= STABSIZE;
    for (p = str_table[k]; p; p = p->s_next)
        if (strcmp(str, p->s_ptr) == 0)
            return (p->s_ptr);
    p = (struct s_chain *) alloc(sizeof(struct s_chain));
    p->s_next = str_table[k];
    str_table[k] = p;
    p->s_ptr = (char *) alloc((unsigned) strlen(str) + 1);
    p->s_ptr = strcpy(p->s_ptr, str);
    return (p->s_ptr);
}

/*
 * hash - hash name, turn address into hash number
 */
int hash(char *s) {
//    int a = 0;
//    for (int i = 0; s[i] != 0; i++)
//        a += (int)(s[i]-'0');
//    return a;
    int h, a = 117;
    for (h = 0; *s != 0; s++)
        h = a*h + *s;
    return h;
}

/*
 * enterblock - enter a new block
 */
void enterblock() {
    level++;
}

/*
 * leaveblock - exit a block
 */
void leaveblock() {
    struct id_entry **i, *p, *tmp;

    if (level > 0) {
        for (i = id_table; i < &id_table[ITABSIZE]; i++) {
            for (p = *i; p; p = tmp)
                if (p->i_blevel > level)
                    break;
                else {
                    tmp = p->i_link;
                    free(p);
                }
            *i = p;
        }
        level--;
    }
}

/*
 * tsize - return size of type
 */
int tsize(int type) {
    if (type == T_INT)
        return (4);
    else if (type == T_DOUBLE)
        return (8);
    else
        return (0);
}

/*
 * dclr - insert attributes for a declaration
 */
struct id_entry *dclr(char *name, int type, int width) {
    struct id_entry *p;
    extern int level;
    char msg[80];

    if ((p = lookup(name, 0)) == NULL || p->i_blevel != level)
        p = install(name, -1);
    else {
        sprintf(msg, "identifier %s previously declared", name);
        fprintf(stderr, "%s\n", msg);
        return (p);
    }
    p->i_type = type;
    p->i_width = width;
    return (p);
}

/*
 * dcl - adjust the offset or allocate space for a global
 */
struct id_entry *dcl(struct id_entry *p, int type, int scope) {
    extern int level;

    p->i_type += type;
    if (scope != 0)
        p->i_scope = scope;
    else if (p->i_width > 0 && level == 2)
        p->i_scope = GLOBAL;
    else
        p->i_scope = LOCAL;
    if (level > 2 && p->i_scope == PARAM) {
        //p->i_offset = formalnum;
        if (p->i_type == T_DOUBLE)
            formaltypes[formalnum++] = 'f';
        else
            formaltypes[formalnum++] = 'i';
        if (formalnum > MAXARGS) {
            fprintf(stderr, "too many arguments\n");
            exit(1);
        }
    } else if (level > 2 && p->i_scope != PARAM) {
        //p->i_offset = localnum;
        localwidths[localnum] = p->i_width;
        if (p->i_type & T_DOUBLE)
            localtypes[localnum++] = 'f';
        else
            localtypes[localnum++] = 'i';
        if (localnum > MAXLOCS) {
            fprintf(stderr, "too many locals\n");
            exit(1);
        }
    } else if (p->i_width > 0 && level == 2)
        printf("alloc %s %d\n", p->i_name,
               p->i_width * tsize(p->i_type & ~T_ARRAY));
    return p;
}
