//
// Created by Gang-Ryung Uh on 10/23/20.
//

#ifndef QUADREADER_QUAD_H
#define QUADREADER_QUAD_H

#include "llvm/IR/LLVMContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/LegacyPassManager.h"

#define MAXLINE 81
#define MAXNUMITEMS 20

/* symbol table entry */
struct id_entry {
    struct id_entry *i_link;    /* pointer to next entry on hash chain */
    char i_name[MAXLINE];       /* name in string table */
    int i_type;                 /* type code */
    int i_blevel;               /* block level */
    int i_width;                /* number of words occupied */
    int i_numelem;              /* number of elements if array type */
    int i_scope;                /* scope */
    struct bblk *blk;           /* pointer to basic block */
    llvm::GlobalVariable *gvar; /* llvm GlobalVariable */
    union {
        llvm::Type *ltype;         /* llvm type */
        llvm::FunctionType *ftype; /* llvm function type */
    } u;
    union {
        llvm::Value *v;      /* llvm Value */
        llvm::Function *f;   /* llvm Function */
        llvm::BasicBlock *b; /* llvm Basic Block */
    } v;
};

/* scopes *** do not rearrange *** */
#define LOCAL 0
#define PARAM 1
#define GLOBAL 2

/* internal types *** do not rearrange *** */
#define T_INT (1 << 0) /* integer */
#define T_STR (1 << 1) /* string */
#define T_DOUBLE (1 << 2) /* double */
#define T_PROC (1 << 3) /* procedure */
#define T_ARRAY (1 << 4) /* array */
#define T_ADDR (1 << 5) /* address */
#define T_LBL (1 << 6) /* label */

/*
 *  item array type
 */
typedef char **itemarray;

/* Quad Instruction Type *** d not rearrange */

typedef enum inst_type {
    ASSIGN = 0,
    UNARY,
    BINOP,
    JUMP,
    BRANCH,
    LOCAL_ALLOC,
    LOCAL_REF,
    FORMAL_ALLOC,
    PARAM_REF,
    GLOBAL_ALLOC,
    GLOBAL_REF,
    CONSTANT,
    STRING,
    FUNC_BEGIN,
    FUNC_END,
    FUNC_CALL,
    ADDR_ARRAY_INDEX,
    STORE,
    LOAD,
    RETURN,
    CVF,
    CVI,
    NONE
} inst_type;

typedef enum relational_type {
    EQ = 0,
    NE,
    LT,
    GT,
    LE,
    GE,
    NONE_RE
} relational_type;

typedef enum arithematic_type {
    ADD = 0,
    SUB,
    MUL,
    DIV,
    MOD,
    LSHIFT,
    RSHIFT,
    NONE_AR
} arithematic_type;

struct quadline {
    char *text;
    struct quadline *next;
    struct quadline *prev;
    inst_type type;
    short numitems;
    itemarray items;
    struct bblk *blk;
    llvm::Value *val;
};

struct bblk {
    char *label;
    unsigned short num;
    struct quadline *lines;
    struct quadline *lineend;
    struct blist *preds;
    struct blist *succs;
    struct bblk *up;
    struct bblk *down;
    llvm::BasicBlock *lbblk;
};

struct blist {
    struct bblk *ptr;
    struct blist *next;
};

struct bpair {
    char bl[MAXLINE];
    char tl[MAXLINE];
};

struct bplist {
    struct bpair *ptr;
    struct bplist *next;
};

#endif//QUADREADER_QUAD_H
