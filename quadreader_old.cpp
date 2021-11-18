#include "misc.h"
#include "quad.h"
#include "sym.h"
#include "bitcodegen.h"
#include <cassert>
#include <cstdbool>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

struct bblk *top = (struct bblk *) NULL;// top block in the function
struct bblk *bot = (struct bblk *) NULL;// end block in the function
struct bplist *gbp = (struct bplist *) NULL;

bool readinginfunc;     /* indicates if reading in func */
static char quad_type_names[][MAXLINE] = {
        "ASSIGN","UNARY","BINOP","JUMP","BRANCH","LOCAL_ALLOC","LOCAL_REF",
        "FORMAL_ALLOC","PARAM_REF","GLOBAL_ALLOC","GLOBAL_REF","CONSTANT",
        "STRING","FUNC_BEGIN","FUNC_END","FUNC_CALL","ADDR_ARRAY_INDEX",
        "STORE","LOAD","RETURN","NONE"
};

void dumpblk(struct bblk *cblk) {
    struct quadline *ptr;
    char locallabel[MAXLINE];
    if (cblk->label) {
        sprintf(locallabel, "$%s:", cblk->label);
        fprintf(stdout, "%s\n", locallabel);
    }
    for (ptr = cblk->lines; ptr; ptr = ptr->next) {
        fprintf(stdout, "\t%s\t;%s\n", ptr->text,quad_type_names[ptr->type]);
    }
}

void dumpfunc() {
    struct bblk *cblk;
    for (cblk = top; cblk; cblk = cblk->down) {
        dumpblk(cblk);
    }
}

void dumpbplist() {
    struct bplist *bptr;
    for (bptr = gbp; bptr; bptr = bptr->next) {
        struct bpair *bp = bptr->ptr;
        fprintf(stderr, "%s = %s\n", bp->bl, bp->tl);
    }
}

void backpatching() {
    struct bblk *cblk;
    struct bplist *bptr;
    for (cblk = top; cblk; cblk = cblk->down) {
        if (cblk->lineend && cblk->lineend->prev) {
            if (strcmp(cblk->lineend->prev->items[0], "bt") == 0) {
                bptr = inbplist(gbp, cblk->lineend->prev->items[2]);
                replacestring(&cblk->lineend->prev->text, bptr->ptr->bl,
                              bptr->ptr->tl);
                replacestring(&cblk->lineend->prev->items[2], bptr->ptr->bl,
                              bptr->ptr->tl);
                deletefrombplist(&gbp, bptr->ptr->bl);
            }
        }
        if (cblk->lineend && strcmp(cblk->lineend->items[0], "br") == 0) {
                bptr = inbplist(gbp, cblk->lineend->items[1]);
                replacestring(&cblk->lineend->text, bptr->ptr->bl,
                              bptr->ptr->tl);
                replacestring(&cblk->lineend->items[1], bptr->ptr->bl,
                              bptr->ptr->tl);
                deletefrombplist(&gbp, bptr->ptr->bl);
        }
    }
}

struct bblk *findtarget(char *label) {
    struct bblk *cblk = top;
    for (; cblk; cblk = cblk->down)
        if (cblk->label && strcmp(cblk->label, label) == 0)
            return cblk;
    return nullptr;
}

void setupcontrolflow() {
    struct bblk *cblk, *tblk;
    for (cblk = top; cblk; cblk = cblk->down) {
        if (cblk->lineend) {
            char *target = nullptr;
            if (strcmp(cblk->lineend->items[0], "br") == 0) {
                target = cblk->lineend->items[1];
                tblk = findtarget(target);
                assert(tblk && "setupcontrolflow cannot locate target block");
                addtoblist(&cblk->succs, tblk);
                addtoblist(&tblk->preds, cblk);
            } else if (strcmp(cblk->lineend->items[0], "bt") == 0) {
                target = cblk->lineend->items[2];
                tblk = findtarget(target);
                assert(tblk && "setupcontrolflow cannot locate target block");
                addtoblist(&cblk->succs, tblk);
                addtoblist(&tblk->preds, cblk);
                continue;
            } else if (strncmp(cblk->lineend->items[0], "ret", 3) == 0)
                continue;
        }
        addtoblist(&cblk->succs, cblk->down);
        if (cblk->down)
            addtoblist(&cblk->down->preds, cblk);
    }
};

/*
 * makeinstitems - make the items associated with an instruction
 */
void makeinstitems(char *text, short numitems, char stems[][MAXLINE],
                   itemarray *items) {
    int i;
    *items = (itemarray) alloc(numitems * sizeof(char *));
    for (i = 0; i < numitems; i++)
        (*items)[i] = allocstring(stems[i]);
}

bool readinfunc(FILE *stdin) {
    struct quadline *ptr;
    struct bblk *tblk, *gblk;
    char line[MAXLINE], items[MAXNUMITEMS][MAXLINE];
    char *status;
    int linebytes;
    struct id_entry *id;
    int type, size, offset;

    gblk = newblk(nullptr);
    while ((status = fgets(line, MAXLINE, stdin)) != NULL) {

        line[strlen(line) - 1] = '\0';
        if ((sscanf(line, "alloc %s %d %d", items[1], &type, &size) == 3)) {
            ptr = insline(gblk, (struct quadline *) NULL, line);
            ptr->type = GLOBAL_ALLOC;
            strcpy(items[0],"alloc");
            ptr->numitems = 2;
            makeinstitems(ptr->text, 2, items, &ptr->items);
            id = install(items[1], GLOBAL);
            if (id == NULL) {
                fprintf(stderr, "error to enter");
                assert(0 && "adding global variable to symtab fails");
            } else {
                id->i_scope = GLOBAL;
                id->i_type = type;
                id->i_width = tsize(type & ~T_ARRAY);
                id->i_numelem = size / id->i_width;
            }
        } else if ((sscanf(line, "func %s %d", items[1], &type) == 2)) {
            ptr = insline(gblk, (struct quadline *) NULL, line);
            ptr->type = FUNC_BEGIN;
            strcpy(items[0],"func");
            ptr->numitems = 2;
            makeinstitems(ptr->text, 2, items, &ptr->items);
            if ((id = install(items[1], GLOBAL)) == NULL)
                assert(0 && "function cannot be redefined");
            readinginfunc = true;
            id->i_type = type | T_PROC;
            break;
        }
    }

    if (!status)
        return false;

    assert(readinginfunc && "No function is found");

    top = bot = newblk(items[1]);
    bot->lines = gblk->lines;
    for (ptr = bot->lines; ptr; ptr=ptr->next) {
        ptr->blk = bot;
        bot->lineend = ptr;
    }

    /* read in quadruples for the function */
    enterblock();
    while ((status = fgets(line, MAXLINE, stdin))) {
        linebytes = strlen(line);
        line[linebytes - 1] = '\0';

        if (sscanf(line, "localloc %s %d %d", items[1], &type, &size) == 3) {
            ptr = insline(bot, (struct quadline *) NULL, line);
            ptr->type = LOCAL_ALLOC;
            ptr->numitems = 2;
            strcpy(items[0],"localloc");
            makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);

            id = install(items[1], LOCAL);
            if (id == NULL)
                assert(0 && "local variable cannot be redefined");
            id->i_scope = LOCAL;
            id->i_type = type;
            id->i_width = tsize(type & ~T_ARRAY);
            id->i_numelem = size / id->i_width;

        }
        else if (sscanf(line, "%s := local %s %d",
                items[0], items[3], &offset) == 3) {
            ptr = insline(bot, (struct quadline *) NULL, line);
            ptr->type = LOCAL_REF;
            strcpy(items[1], ":=");
            strcpy(items[2], "local");
            ptr->numitems = 4;
            makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);
        }
        else if (sscanf(line, "formal %s %d %d", items[1], &type, &size) == 3) {
            ptr = insline(bot, (struct quadline *) NULL, line);
            ptr->type = FORMAL_ALLOC;
            ptr->numitems = 2;
            strcpy(items[0],"formal");
            makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);

            id = install(items[1], PARAM);
            if (id == NULL)
                assert(0 && "param variable cannot be redefined");
            id->i_scope = PARAM;
            id->i_type = type;
            id->i_width = tsize(type);
            id->i_width = tsize(type & ~T_ARRAY);
            id->i_numelem = size / id->i_width;
        }
        else if (sscanf(line, "%s := param %s %d",
                items[0],items[3],&offset) == 3) {
            ptr = insline(bot, (struct quadline *) NULL, line);
            ptr->type = PARAM_REF;
            strcpy(items[1], ":=");
            strcpy(items[2], "param");
            ptr->numitems = 4;
            makeinstitems(ptr->text,ptr->numitems, items, &ptr->items);
        }
        else if (sscanf(line, "%s %s \"%[^\"]\"",
                items[0],items[1],items[2]) == 3) {
            ptr = insline(bot, (struct quadline *) NULL, line);
            ptr->type = STRING;
            ptr->numitems = 3;
            makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);
        }
        else if ((sscanf(line, "%s := fi %s %s %s",
                items[0],items[3],items[4],items[5]) == 4) ||
                 (sscanf(line, "%s := ff %s %s %s",
                items[0],items[3],items[4], items[5]) == 4)) {

            ptr = insline(bot, (struct quadline *) NULL, line);
            ptr->type = FUNC_CALL;
            int numargs = atoi(items[4]);
            int i = 5;
            char *cptr = strstr(line, items[5]);
            cptr = strtok(cptr, " ");
            while (cptr) {
                strcpy(items[i++],cptr);
                cptr = strtok(NULL, " ");
            }
            strcpy(items[1],":=");
            if (strstr(ptr->text, "fi"))
                strcpy(items[2],"fi");
            else
                strcpy(items[2],"ff");
            ptr->numitems = numargs + 5;
            makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);
        }
        else if (sscanf(line, "%s %s %s %s %s",
                items[0], items[1], items[2], items[3], items[4]) == 5) {
            // binary operator
            ptr = insline(bot, (struct quadline *) NULL, line);
            ptr->numitems = 5;
            if (*items[3]=='[')
                ptr->type = ADDR_ARRAY_INDEX;
            else if (*items[3] == '=' &&
                     (items[3][1] == 'i' || items[3][1] == 'f'))
                ptr->type = STORE;
            else
                ptr->type = BINOP;
            makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);
        }
        else if (sscanf(line, "%s %s %s %s",
                items[0], items[1], items[2], items[3]) == 4) {
            ptr = insline(bot, (struct quadline *) NULL, line);
            if (strcmp(items[2],"global")==0)
                ptr->type = GLOBAL_REF;
            else if (*items[2]=='@')
                ptr->type = LOAD;
            else if ((strncmp(items[2], "cv", 2) == 0) &&
                    (items[2][2] == 'i' || items[2][2] == 'f')) {
                ptr->type = CONV;
            }
            else
                assert(0 && "Unknown quadruple type");
            ptr->numitems = 4;
            makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);
        }
        else if (sscanf(line, "%s %s %s",
                items[0], items[1], items[2]) == 3) {
            ptr = insline(bot, (struct quadline *) NULL, line);
            ptr->numitems = 3;
            makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);

            if (strcmp(items[0], "bt") == 0) {
                /* don't create a new basic block since br will follow right
                   after bt.*/
                ptr->type = BRANCH;
            }
            else
                ptr->type = ASSIGN;
        }
        else if (sscanf(line, "%s %s", items[0], items[1]) == 2) {
            if (strcmp(items[0], "bgnstmt") == 0)
                continue;
            if (strcmp(items[0], "br") == 0) {
                ptr = insline(bot, (struct quadline *) NULL, line);
                ptr->type = JUMP;
                ptr->numitems = 2;
                makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);
                tblk = newblk((char *) NULL);
                tblk->up = bot;
                bot->down = tblk;
                bot = tblk;
            } else if (strcmp(items[0], "label") == 0) {
                if (bot->lines || bot->label) {
                    tblk = newblk(items[1]);
                    tblk->up = bot;
                    bot->down = tblk;
                    bot = tblk;
                } else {
                    assignlabel(bot, items[1]);
                }
                auto ib = install(items[1],LOCAL);
                assert(ib && "symbol table insertion fails");
                ib->blk = bot;
            } else if (*items[0] == 'r') {
                ptr = insline(bot, (struct quadline *) NULL, line);
                ptr->type = RETURN;
                ptr->numitems = 2;
                makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);
            } else if (*items[0] == 'a') {
                continue;
            }
            else {
                assert(0 && "Unrecognized quad instruction format");
            }
        }
        else if (sscanf(line, "%s", items[0]) == 1) {
            //ptr = insline(bot,(struct quadline *)NULL, line);
            if (strcmp(items[0], "fend") == 0) {
                if (!ptr || ptr->type != RETURN) {
                    // insert return 0 statement
                    char templine[MAXLINE];
                    sprintf(templine,"retval := 0");
                    ptr = insline(bot, (struct quadline *)NULL, templine);
                    ptr->type = ASSIGN;
                    strcpy(items[0],"retval");
                    strcpy(items[1], ":=");
                    strcpy(items[2], "0");
                    ptr->numitems = 3;
                    makeinstitems(ptr->text,ptr->numitems, items, &ptr->items);
                    sprintf(templine,"reti retval");
                    ptr = insline(bot, (struct quadline *)NULL, templine);
                    ptr->type = RETURN;
                    strcpy(items[0],"reti");
                    strcpy(items[1], "retval");
                    ptr->numitems = 2;
                    makeinstitems(ptr->text,ptr->numitems, items, &ptr->items);
                }
                ptr = insline(bot, (struct quadline *) NULL, line);
                ptr->type = FUNC_END;
                /* clean up last empty block */
                if (!bot->lines) {
                    tblk = bot;
                    bot = bot->up;
                    deleteblk(tblk);
                }
                ptr->numitems = 1;
                makeinstitems(ptr->text, ptr->numitems, items, &ptr->items);
                readinginfunc = false;
                return true;
            } else {
                if (sscanf(line, "%[^=]=%[^=]", items[0], items[1]) == 2)
                    addtobplist(&gbp, items[0], items[1]);
                else
                    assert(0 && "unknown quadruple format");
            }
        }
        else {
            fprintf(stderr, "Unknown quadruple format");
            assert(0 && "Unexpected quadruple");
        }
    }

    if (!status) {
        fprintf(stderr, "unexpected end of file after function\n");
        quit(1);
    }

    /* clean up last empty block */
    if (!bot->lines) {
        tblk = bot;
        bot = bot->up;
        deleteblk(tblk);
    }
    readinginfunc = false;
    return true;
}

int main(int argc, char *argv[]) {

    InitializeModuleAndPassManager();
    while (readinfunc(stdin)) {
        backpatching();
        setupcontrolflow();
        //dumpfunc();  // this is for debugging
        bitcodegen();
        leaveblock(); //matching enterblock() call is made in readinfunc()
    }
    OutputModule();
    return 0;
}
