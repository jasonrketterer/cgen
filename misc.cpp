/*
 * miscellaneous support functions
 */
#include <cassert>
#include <cstdio>
#include <cstdlib>

#define MAXLINE 81

#include "misc.h"
#include "quad.h"
#include <cctype>
#include <csetjmp>
#include <cstring>

/*
 * alloc - allocates space and checks the status of the allocation
 */
void *alloc(unsigned int bytes) {
    void *ptr;

    if ((ptr = malloc(bytes)))
        return ptr;
    fprintf(stderr, "alloc: ran out of space\n");
    quit(1);

    /* should never reach here */
    return (void *) 0;
}

/*
 * allocstring - allocate space for a string and copy the string to that
 *               location
 */
char *allocstring(char *str) {
    char *dst;

    dst = (char *) alloc(strlen(str) + 1);
    strcpy(dst, str);
    return dst;
}

/*
 * replacestring - replace a dynamically allocated string
 */
void replacestring(char **s1, char *old, char *news) {
    int i;
    char *p, *s, *d;
    char t[MAXLINE];

    if (*s1) {
        p = strstr(*s1, old);
        for (s = *s1, d = t; s != p; *d++ = *s++)
            ;
        strcpy(d, news);
        d += strlen(news);
        for (i = 0; i < strlen(old); i++)
            s++;
        while ((*d++ = *s++))
            ;
        if (strlen(old) >= strlen(news))
            strcpy(*s1, t);
        else {
            free(*s1);
            *s1 = allocstring(t);
        }
        return;
    }
    fprintf(stderr, "replacestring - dst string not yet allocated\n");
    quit(1);
}

/*
 * isconst - determines if a string is a constant
 */
int isconst(char *s) {
    return isdigit((int) *s) || (*s == '-' && isdigit((int) *(s + 1)));
}

/*
 * assignlabel - assigns a label to a basic block
 */
void assignlabel(struct bblk *cblk, char *label) {
    /* assign label */
    if (cblk->label)
        free(cblk->label);
    if (label)
        cblk->label = allocstring(label);
    else
        cblk->label = (char *) NULL;
}

/*
 * newblk - allocate a new basic block
 */
struct bblk *newblk(char *label) {
    struct bblk *tblk;

    /* allocate the space for the block */
    tblk = (struct bblk *) alloc(sizeof(struct bblk));

    /* initialize the fields of the block */
    tblk->label = (char *) NULL;
    assignlabel(tblk, label);
    tblk->num = 0;
    tblk->lines = (struct quadline *) NULL;
    tblk->lineend = (struct quadline *) NULL;
    tblk->preds = (struct blist *) NULL;
    tblk->succs = (struct blist *) NULL;
    tblk->up = (struct bblk *) NULL;
    tblk->down = (struct bblk *) NULL;
    tblk->lbblk = (llvm::BasicBlock *) NULL;

    /* return the pointer to the block */
    return tblk;
}

/*
 * freeblk - frees up the space for a basic block
 */
void freeblk(struct bblk *cblk) {
    struct quadline *ptr, *dptr, *ptr2, *dptr2;

    /* free label */
    if (cblk->label)
        free(cblk->label);

    /* free blists */
    freeblist(cblk->preds);
    freeblist(cblk->succs);

    /* free assemlines */
    for (ptr = cblk->lines; ptr; ptr = dptr) {
        dptr = ptr->next;

        for (ptr2 = ptr; ptr2;) {
            dptr2 = ptr2;
            ptr2 = ptr2->next;
            freeline(dptr2);
        }
    }
    free(cblk);
}

/*
 * inblist - check if a block is in a blist
 */
int inblist(struct blist *head, struct bblk *cblk) {
    struct blist *bptr;

    for (bptr = head; bptr; bptr = bptr->next)
        if (bptr->ptr == cblk)
            return true;
    return false;
}

/*
 * freeblist - free up the space for a blist
 */
void freeblist(struct blist *head) {
    struct blist *bptr, *dptr;

    for (bptr = head; bptr;) {
        dptr = bptr;
        bptr = bptr->next;
        free(dptr);
    }
}

/*
 * inbplist - check if a label exists in the list
 */
struct bplist *inbplist(struct bplist *head, char *blabel) {

    struct bplist *bpptr;
    for (bpptr = head; bpptr; bpptr = bpptr->next)
        if (strcmp(bpptr->ptr->bl, blabel) == 0)
            return bpptr;
    return nullptr;
}

/*
 * freebplist - free up the space for a blist
 */
void freebplist(struct bplist *head) {
    struct bplist *bptr, *dptr;

    for (bptr = head; bptr;) {
        dptr = bptr;
        bptr = bptr->next;
        free(dptr);
    }
}

/*
 * addtobplist - add a basic block to a blist
 */
void addtobplist(struct bplist **head, char *blabel, char *tlabel) {
    struct bplist *bptr;

    /* first check that the basic block is not already in the blist */
    for (bptr = *head; bptr; bptr = bptr->next)
        if (strcmp(bptr->ptr->bl, blabel) == 0)
            return;

    /* allocate the space for the blist element */
    struct bpair *t = (struct bpair *) alloc(sizeof(struct bpair));
    struct bplist *bp = (struct bplist *) alloc(sizeof(struct bplist));
    strcpy(t->bl, blabel);
    strcpy(t->tl, tlabel);
    bp->ptr = t;
    bp->next = *head;
    *head = bp;
}

/*
 * deletefrombplist - add a basic block to a blist
 */
void deletefrombplist(struct bplist **head, char *blabel) {
    struct bplist *bptr, *bprev;

    /* first check that the basic block is not already in the blist */
    if (!*head)
        return;

    if (strcmp((*head)->ptr->bl, blabel) == 0) {
        bptr = *head;
        *head = (*head)->next;
        free(bptr);
        return;
    }

    bprev = *head;
    for (bptr = bprev->next; bptr; bprev = bptr, bptr = bptr->next)
        if (strcmp(bptr->ptr->bl, blabel) == 0) {
            bprev->next = bptr->next;
            free(bptr);
            return;
        }
}

/*
 * newline - allocate a new assembly line
 */
struct quadline *newline(char *text) {
    struct quadline *tline;

    /* allocate space for the assembly line */
    tline = (struct quadline *) alloc(sizeof(struct quadline));

    /* initialize the other fields of the assembly line */
    tline->text = allocstring(text);
    tline->next = tline->prev = (struct quadline *) NULL;
    tline->type = NONE;
    tline->numitems = 0;
    tline->items = (itemarray) NULL;
    tline->blk = (struct bblk *) NULL;
    tline->val = (llvm::Value *) NULL;

    /* return the pointer to the assembly line */
    return tline;
}

/*
 * hookupline - hook up the assembly line within the basic block
 */
void hookupline(struct bblk *cblk, struct quadline *ptr,
                struct quadline *line) {
    /* sanity check */
    if (ptr && ptr->blk != cblk) {
        fprintf(stderr,
                "hookupline - inserting before inst not in correct blk\n");
        quit(1);
    }

    /* hook this assembly line into the basic block before ptr */
    if (!ptr) {
        if (!cblk->lineend) {
            cblk->lines = cblk->lineend = line;
            line->prev = line->next = (struct quadline *) NULL;
        } else {
            cblk->lineend->next = line;
            line->prev = cblk->lineend;
            line->next = (struct quadline *) NULL;
            cblk->lineend = line;
        }
    } else {
        line->next = ptr;
        if (!(line->prev = ptr->prev))
            cblk->lines = line;
        else
            line->prev->next = line;
        ptr->prev = line;
    }
    line->blk = cblk;
}

/*
 * unhookline - unhook an assembly line from a basic block
 */
void unhookline(struct quadline *ptr) {
    if (ptr->prev)
        ptr->prev->next = ptr->next;
    else
        ptr->blk->lines = ptr->next;
    if (ptr->next)
        ptr->next->prev = ptr->prev;
    else
        ptr->blk->lineend = ptr->prev;
    ptr->next = ptr->prev = (struct quadline *) NULL;
}

/*
 * insline - insert the line before the one in the argument
 */
struct quadline *
insline(struct bblk *cblk, struct quadline *ptr, char *text) {
    struct quadline *tline;

    /* allocate the assembly line */
    tline = newline(text);

    /* hook up the assembly line into the basic block */
    hookupline(cblk, ptr, tline);

    /* return the inserted assembly line */
    return tline;
}


/*
 * prevline - return the previous line
 */
struct quadline *prevline(struct quadline *currline) {
    if (currline->prev)
        return currline->prev;
    else if (currline->blk->preds && !currline->blk->preds->next)
        return currline->blk->preds->ptr->lineend;
    return (struct quadline *) NULL;
}

/*
 * delline - delete the specified line in the basic block
 */
void delline(struct quadline *ptr) {
    unhookline(ptr);
    freeline(ptr);
}

/*
 * freeitemarray - delete the itemarray for this quadline
 */
void freeitemarray(struct quadline *ptr) {
    int i;

    for (i = 0; i < ptr->numitems; i++)
        free(ptr->items[i]);
    free(ptr->items);
    ptr->items = NULL;
}

/*
 * freeline - deallocate an assembly line
 */
void freeline(struct quadline *ptr) {
    freeitemarray(ptr);
    free(ptr->text);
    free(ptr);
}

/*
 * addtoblist - add a basic block to a blist
 */
void addtoblist(struct blist **head, struct bblk *cblk) {
    struct blist *bptr;

    /* first check that the basic block is not already in the blist */
    for (bptr = *head; bptr; bptr = bptr->next)
        if (bptr->ptr == cblk)
            return;

    /* allocate the space for the blist element */
    bptr = (struct blist *) alloc(sizeof(struct blist));

    /* link in the block at the head of the list */
    bptr->ptr = cblk;
    bptr->next = *head;
    *head = bptr;
}


/*
 * sortblist - sort the blocks in blist by the block number
 */
void sortblist(struct blist *head) {
    struct blist *bptr, *bptr2;
    struct bblk *tblk;

    for (bptr = head; bptr; bptr = bptr->next)
        for (bptr2 = bptr->next; bptr2; bptr2 = bptr2->next)
            if (bptr->ptr->num > bptr2->ptr->num) {
                tblk = bptr->ptr;
                bptr->ptr = bptr2->ptr;
                bptr2->ptr = tblk;
            }
}

/*
 * orderpreds - make the predecessors of each block be in ascending order
 */
void orderpreds() {
    struct bblk *cblk;
    extern struct bblk *top;

    for (cblk = top; cblk; cblk = cblk->down)
        sortblist(cblk->preds);
}

/*
 * deleteblk - delete a basic block from the list of basic blocks
 */
void deleteblk(struct bblk *cblk) {
    extern struct bblk *bot;

    /* update bottom block if needed */
    if (cblk == bot)
        bot = cblk->up;

    /* unhook the "up" and "down" pointers */
    unlinkblk(cblk);

    /* unhook preds */
    delfrompreds_succs(cblk);

    /* unhook succs */
    delfromsuccs_preds(cblk);

    /* free up the memory */
    freeblk(cblk);
}

/*
 * unlinkblk - unhook a basic block from the list of basic blocks
 */
void unlinkblk(struct bblk *cblk) {
    extern struct bblk *top;

    /* relink a backward pointer to bypass the block to be deleted */
    if (cblk->down)
        cblk->down->up = cblk->up;

    /* set a forward pointer to bypass the block to be deleted */
    if (!cblk->up)
        top = cblk->down;
    else
        cblk->up->down = cblk->down;
}

/*
 * delfrompreds_succs - delete cblk from the successor list of all the
 *                      predecessors of cblk
 */
void delfrompreds_succs(struct bblk *cblk) {
    struct blist *curpred;

    for (curpred = cblk->preds; curpred; curpred = curpred->next)
        if (!delfromblist(&(curpred->ptr->succs), cblk)) {
            fprintf(stderr, "delfrompreds_succs(), basic block not found.\n");
            quit(1);
        }
}

/*
 * delfromsuccs_preds - delete cblk from the predecessor list of all the  
 *                      successors of cblk
 */
void delfromsuccs_preds(struct bblk *cblk) {
    struct blist *cursucc;

    for (cursucc = cblk->succs; cursucc; cursucc = cursucc->next)
        if (!delfromblist(&(cursucc->ptr->preds), cblk)) {
            fprintf(stderr, "delfromsuccs_preds(), basic block not found.\n");
            quit(1);
        }
}

/*
 * delfromblist - deletes a block from a blist
 */
struct bblk *delfromblist(struct blist **head, struct bblk *cblk) {
    struct bblk *tblk;
    struct blist *bptr, *pbptr;

    /* check if the basic block already has been allocated */
    tblk = (struct bblk *) NULL;
    pbptr = (struct blist *) NULL;
    for (bptr = *head; bptr; pbptr = bptr, bptr = bptr->next)
        if (bptr->ptr == cblk) {
            tblk = bptr->ptr;
            if (pbptr)
                pbptr->next = bptr->next;
            else
                *head = bptr->next;
            free(bptr);
            break;
        }
    return tblk;
}

/*
 * quit - exits the program
 */
void quit(int flag) {
    exit(flag);
}

/* free up the function's dynamically allocated structures */
void free_func_structs() {
    struct bblk *cblk, *next;
    extern struct bblk *top, *bot;

    for (cblk = top; cblk; cblk = next) {
        next = cblk->down;
        deleteblk(cblk);
    }
}
