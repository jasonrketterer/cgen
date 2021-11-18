//
// Created by Gang-Ryung Uh on 11/15/20.
//
#include <regex>
#include <iostream>
#include "quad.h"
#include "sym.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support//TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;

typedef SmallVector<BasicBlock *, 16> BBList;
typedef SmallVector<Value *, 16> ValList;

/*
 * The order of the declaration of static variables matter.
 */
static LLVMContext TheContext;
static IRBuilder<> Builder(TheContext);
static std::unique_ptr<Module> TheModule;

/* https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl08.html#choosing-a-target */
void InitializeModuleAndPassManager() {

    auto TargetTriple = sys::getDefaultTargetTriple();
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    std::string Error;
    auto Target = TargetRegistry::lookupTarget(TargetTriple, Error);
    if (!Target) {
        errs() << Error;
        exit(1);
    }
    auto CPU = "generic";
    auto Features = "";
    TargetOptions opt;
    auto RM = Optional<Reloc::Model>();
    auto TargetMachine = Target->createTargetMachine(
            TargetTriple, CPU, Features, opt, RM);

    // Open a new module.
    TheModule = std::make_unique<Module>("QuadReader", TheContext);
    TheModule->setDataLayout(TargetMachine->createDataLayout());
    TheModule->setTargetTriple(TargetTriple);

    // We rely on printf function call
    char str[] = "printf";
    struct id_entry *iptr = install(str, GLOBAL);
    FunctionType* printfTy = FunctionType::get(Builder.getInt32Ty(),
            Builder.getInt8PtrTy(), true);
    iptr->v.f = Function::Create(printfTy,Function::ExternalLinkage,
            str, *TheModule);
    iptr->v.f->setCallingConv(CallingConv::C);
    iptr->v.f->getArg(0)->setName("f");
}

void OutputModule() {
    TheModule->print(outs(), nullptr);
}

static
void createGlobal(struct id_entry *iptr) {

    if (iptr->i_type & T_ARRAY) {
        if (iptr->i_type & T_INT) {
            auto vecType = ArrayType::get(
                    Type::getInt32Ty(TheContext),iptr->i_numelem);
            iptr->u.ltype = vecType;
        }
        else {
            auto vecType = ArrayType::get(
                    Type::getDoubleTy(TheContext),iptr->i_numelem);
            iptr->u.ltype = vecType;
        }
    }
    else {
        if (iptr->i_type & T_INT)
            iptr->u.ltype = Builder.getInt32Ty();
        else
            iptr->u.ltype = Builder.getDoubleTy();
    }
    TheModule->getOrInsertGlobal(iptr->i_name, iptr->u.ltype);
    iptr->gvar = TheModule->getNamedGlobal(iptr->i_name);
    iptr->gvar->setLinkage(GlobalVariable::CommonLinkage);
    iptr->gvar->setAlignment(MaybeAlign(16));
    if (iptr->u.ltype->isArrayTy())
        iptr->gvar->setInitializer(ConstantAggregateZero::get(iptr->u.ltype));
}

static
void createFunction(struct id_entry *fn, struct quadline **ptr) {
    std::vector<std::string> args;
    std::vector<llvm::Type *> typeVec;

    for (*ptr = (*ptr)->next; *ptr && (*ptr)->type == FORMAL_ALLOC; *ptr = (*ptr)->next) {
        auto iptr = lookup((*ptr)->items[1], PARAM);
        assert(iptr && "parameter is missing");
        if (iptr->i_type & T_INT) {
            iptr->u.ltype = Builder.getInt32Ty();
        }
        else {
            iptr->u.ltype = Builder.getDoubleTy();
        }
        typeVec.push_back(iptr->u.ltype);
        args.push_back(iptr->i_name);
    }

    if (fn->i_type & T_INT)
        fn->u.ftype = FunctionType::get(Builder.getInt32Ty(),
                                              typeVec, false);
    else
        fn->u.ftype = FunctionType::get(Builder.getDoubleTy(),
                                              typeVec, false);

    Function *F =
            Function::Create(fn->u.ftype, Function::ExternalLinkage,
                             fn->i_name, TheModule.get());

    unsigned Idx=0;
    for (auto &Arg:F->args())
        Arg.setName(args[Idx++]);
    fn->v.f = F;
}

static
void allocaFormals(struct quadline **ptr, llvm::Function *fn) {
    for (; (*ptr != NULL) && ((*ptr)->type == FORMAL_ALLOC);
           *ptr = (*ptr)->next) {
        auto id_ptr = lookup((*ptr)->items[1], PARAM);
        assert(id_ptr && "local is missing");
        //Kaleidoscope addresses the initializer at this point, but we can't do that yet...
        id_ptr->v.v = Builder.CreateAlloca(
                id_ptr->u.ltype,nullptr, id_ptr->i_name);

        for (auto  &Arg: fn->args()) {
            auto name = Arg.getName();
            if (strcmp(name.data(), id_ptr->i_name) == 0) {
                Builder.CreateStore(&Arg, id_ptr->v.v);
                break;
            }
        }
    }
}

static
void allocaLocals(struct quadline **ptr) {
    for (; (*ptr != NULL) && ((*ptr)->type == LOCAL_ALLOC);
           *ptr = (*ptr)->next) {
        auto id_ptr = lookup((*ptr)->items[1], LOCAL);
        assert(id_ptr && "local is missing");
        if (id_ptr->i_type & T_INT)
            id_ptr->v.v = Builder.CreateAlloca(llvm::Type::getInt32Ty(
                    TheContext), nullptr, id_ptr->i_name);
        else
            id_ptr->v.v = Builder.CreateAlloca(llvm::Type::getDoubleTy(
                    TheContext), nullptr, id_ptr->i_name);
    }
}

/*
 *  Create constant (int) value
 */
void createAssign(struct quadline *ptr) {
    struct id_entry *id_ptr;
    int val = atoi(ptr->items[2]);
    id_ptr = install(ptr->items[0], LOCAL);
    id_ptr->v.v = llvm::ConstantInt::get(llvm::Type::getInt32Ty(TheContext), val);
}

void createLoad(struct quadline *ptr) {
    auto loadAddr = lookup(ptr->items[3], LOCAL);
    auto loadVal = install(ptr->items[0], LOCAL);
    assert(loadAddr && loadVal && "Load instruction generation fails");
    loadVal->v.v = Builder.CreateLoad(loadAddr->v.v,ptr->items[0]);
}

void createStore(struct quadline *ptr) {
    struct id_entry *lhs, *rhs;

    rhs = lookup(ptr->items[4], LOCAL);
    lhs = lookup(ptr->items[2], LOCAL);
    Builder.CreateStore(rhs->v.v, lhs->v.v);
}

void createRef(struct quadline *ptr, int scope) {
    struct id_entry *refVar, *refAddr;

    refVar = lookup(ptr->items[3], scope);
    refAddr = install(ptr->items[0], scope);
    refAddr->v.v = refVar->v.v;
    if (scope == GLOBAL) {
        refAddr->gvar = refVar->gvar;
        refAddr->i_scope = GLOBAL;
    }
}

void createReturn(struct quadline *ptr) {
    struct id_entry *id_ptr;

    id_ptr = lookup(ptr->items[1], LOCAL);
    Builder.CreateRet(id_ptr->v.v);
}

void createBinOp(struct quadline *ptr) {
    struct id_entry *op1, *op2, *res;
    llvm::Value *resultVal;
    char op     [strlen(ptr->items[3])];
    char op_type[strlen(ptr->items[3])];
    char *resultName;

    op1 = lookup(ptr->items[2], LOCAL);
    op2 = lookup(ptr->items[4], LOCAL);

    // parse '<operator><operator_type>' from quadline
    strncpy(op, ptr->items[3], strlen(ptr->items[3])-1);
    op[strlen(ptr->items[3])-1] = '\0';
    op_type[0] = ptr->items[3][strlen(ptr->items[3])-1];
    op_type[1] = '\0';

    //std::cout << "op is " << op << '\n';
    //std::cout << "op_type is " << op_type << '\n';

    switch(*op) {
        case '+':
            if (op_type[0] == 'i')
                resultVal = Builder.CreateAdd(op1->v.v, op2->v.v);
            else// op_type == 'f'
                resultVal = Builder.CreateFAdd(op1->v.v, op2->v.v);
            break;
        case '-':
            if (op_type[0] == 'i')
                resultVal = Builder.CreateSub(op1->v.v, op2->v.v);
            else// op_type == 'f'
                resultVal = Builder.CreateFSub(op1->v.v, op2->v.v);
            break;
        case '*':
            if (op_type[0] == 'i')
                resultVal = Builder.CreateMul(op1->v.v, op2->v.v);
            else// op_type == 'f'
                resultVal = Builder.CreateFMul(op1->v.v, op2->v.v);
            break;
        case '/':
            if (op_type[0] == 'i')
                resultVal = Builder.CreateSDiv(op1->v.v, op2->v.v);
            else// op_type == 'f'
                resultVal = Builder.CreateFDiv(op1->v.v, op2->v.v);
            break;
        case '%':
            if (op_type[0] == 'i')
                resultVal = Builder.CreateSRem(op1->v.v, op2->v.v);
            else
                resultVal = Builder.CreateFRem(op1->v.v, op2->v.v);
            break;
        case '|':
            resultVal = Builder.CreateOr(op1->v.v, op2->v.v);
            break;
        case '>':
            if (op[1] == '>') // '>>'
                resultVal = Builder.CreateLShr(op1->v.v, op2->v.v);
            else { // '>'
                if (op_type[0] == 'i')
                    resultVal = Builder.CreateICmpSGT(op1->v.v, op2->v.v);
                else
                    resultVal = Builder.CreateFCmpOGT(op1->v.v, op2->v.v);
            }
            break;
        case '<':
            if (op[1] == '<') // '<<'
                resultVal = Builder.CreateShl(op1->v.v, op2->v.v);
            else { // '<'
                if (op_type[0] == 'i')
                    resultVal = Builder.CreateICmpSGT(op1->v.v, op2->v.v);
                else
                    resultVal = Builder.CreateFCmpOLT(op1->v.v, op2->v.v);
            }
            break;
        default:
            break;
    }
    res = install(ptr->items[0], LOCAL);
    res->v.v = resultVal;
}

void createAddrArrayIndx(struct quadline *ptr) {
    struct id_entry *arrayaddr, *arraybase, *arrayidx;

    arraybase = lookup(ptr->items[2], LOCAL);
    arrayidx = lookup(ptr->items[4], LOCAL);

    //char array_type = ptr->items[3][2];

    arrayaddr = install(ptr->items[0], LOCAL);
    if (arraybase->i_scope == GLOBAL)
        arrayaddr->v.v = Builder.CreateGEP(arraybase->gvar, std::vector<Value*>{arrayidx->v.v,
                                       ConstantInt::get(Type::getInt32Ty(TheContext), 0)});
    else
        arrayaddr->v.v = Builder.CreateGEP(arraybase->v.v, std::vector<Value*>{arrayidx->v.v,
                                            ConstantInt::get(Type::getInt32Ty(TheContext), 0)});
}

void createIntConversion(struct quadline *ptr) {
    struct id_entry *casting, *casted;

    casting = lookup(ptr->items[3], LOCAL);
    casted = install(ptr->items[0], LOCAL);
    casted->v.v = Builder.CreateFPToSI(casting->v.v, llvm::Type::getInt32Ty(TheContext));
}

void createFPConversion(struct quadline *ptr) {
    struct id_entry *casting, *casted;

    casting = lookup(ptr->items[3], LOCAL);
    casted = install(ptr->items[0], LOCAL);
    casted->v.v = Builder.CreateSIToFP(casting->v.v, llvm::Type::getDoubleTy(TheContext));
}

void createString(struct quadline *ptr) {
    struct id_entry *id_ptr;

    id_ptr = install(ptr->items[0], LOCAL);

    // create global string POINTER since printf will expect this
    id_ptr->v.v = Builder.CreateGlobalStringPtr(llvm::StringRef(ptr->items[2]));
}

void createFuncCall(struct quadline *ptr) {
    struct id_entry *f, *id_ptr;
    llvm::Value *retval;
    llvm::SmallVector<Value *, 4> args;

    // find function we want to call
    f = lookup(ptr->items[3], GLOBAL);

    // check if there are args and get them
    if (ptr->numitems > 4) {
        // get the arguments
        int numargs = atoi(ptr->items[4]);
        for (int i = 0; i < numargs; ++i) {
            id_ptr = lookup(ptr->items[5 + i], LOCAL);
            args.push_back(id_ptr->v.v);
        }
        // call the function
        retval = Builder.CreateCall(f->v.f, args);
    }
    else
        retval = Builder.CreateCall(f->v.f);

    // install result to symbol table
    id_ptr = install(ptr->items[0], LOCAL);
    id_ptr->v.v = retval;
}

void createUnaryOp(struct quadline *ptr) {
    struct id_entry *oper, *res;

    oper = lookup(ptr->items[3], LOCAL);
    res = install(ptr->items[0], LOCAL);

    char op = ptr->items[2][0];
    char op_type = ptr->items[2][1];

    switch(op) {
        case '-':
            if (op_type == 'i')
                res->v.v = Builder.CreateNeg(oper->v.v);
            else // == 'f'
                res->v.v = Builder.CreateFNeg(oper->v.v);
            break;
        case '~':
            res->v.v = Builder.CreateNot(oper->v.v);
            break;
        default:
            break;
    }
}

void createBitcode(struct quadline *ptr, struct id_entry *fn) {
    struct id_entry *refVar, *refVal;
    for(; ptr; ptr = ptr->next) {
        switch (ptr->type) {
            case ASSIGN:
                //std::cerr << ptr->text << " ;ASSIGN" << '\n';
                createAssign(ptr);
                break;
            case UNARY:
                //std::cerr << ptr->text << " ;UNARY" << '\n';
                createUnaryOp(ptr);
                break;
            case BINOP:
                //std::cerr << ptr->text << " ;BINOP" << '\n';
                createBinOp(ptr);
                break;
            case GLOBAL_REF:
                //std::cerr << ptr->text << " ;GLOBAL_REF" << '\n';
                createRef(ptr, GLOBAL);
                break;
            case PARAM_REF:
                //std::cerr << ptr->text << " ;PARAM_REF" << '\n';
                createRef(ptr, PARAM);
                break;
            case LOCAL_REF:
                //std::cerr << ptr->text << " ;LOCAL_REF" << '\n';
                createRef(ptr, LOCAL);
                break;
            case ADDR_ARRAY_INDEX:
                //std::cerr << ptr->text << " ;ADDR_ARRAY_INDEX" << '\n';
                createAddrArrayIndx(ptr);
                break;
            case FUNC_END:
                //std::cerr << ptr->text << " ;FUNC_END" << '\n';
                break;
            case STORE:
                //std::cerr << ptr->text << " ;STORE" << '\n';
                createStore(ptr);
                break;
            case LOAD:
                //std::cerr << ptr->text << " ;LOAD" << '\n';
                createLoad(ptr);
                break;
            case FUNC_CALL:
                //std::cerr << ptr->text << " ;FUNC_CALL" << '\n';
                createFuncCall(ptr);
                break;
            case STRING:
                //std::cerr << ptr->text << " ;STRING" << '\n';
                createString(ptr);
                break;
            case CVF:
                //std::cerr << ptr->text << " ;CVF" << '\n';
                createFPConversion(ptr);
                break;
            case CVI:
                //std::cerr << ptr->text << " ;CVI" << '\n';
                createIntConversion(ptr);
                break;
            case BRANCH:
                break;
            case JUMP:
                break;
            case RETURN:
                //std::cerr << ptr->text << " ;RETURN" << '\n';
                createReturn(ptr);
                break;
            default:
                break;
        }
    }
}

void bitcodegen() {
    struct bblk *blk;
    struct quadline *ptr;
    struct id_entry *iptr;
    extern struct bblk *top;

    // any global, then define
    for (ptr = top->lines; ptr && ptr->type == GLOBAL_ALLOC; ptr=ptr->next) {
        iptr = lookup(ptr->items[1],GLOBAL);
        assert(iptr && "global is not defined");
        createGlobal(iptr);
    }

    // generate function signature
    assert(ptr && (ptr->type == FUNC_BEGIN) && "Function definition is expected");
    auto fn = lookup(ptr->items[1], GLOBAL);
    assert(fn && "function name is not present");
    createFunction(fn, &ptr);

    BasicBlock *BB = BasicBlock::Create(TheContext, "entry", fn->v.f);
    top->lbblk = BB;
    Builder.SetInsertPoint(BB);

    // allocate storage for the param
    // allocate storage for locals
    // generate bitcode for globals, function header, params, and locals
    for (; ptr->prev && ptr->prev->type == FORMAL_ALLOC; ptr=ptr->prev);
    allocaFormals(&ptr, fn->v.f);
    allocaLocals(&ptr);
    createBitcode(ptr,fn);

    for (auto bblk = top->down; bblk ; bblk=bblk->down) {
        auto bb = lookup(bblk->label, LOCAL);
        if (bb->v.b == nullptr)
            bb->v.b = BasicBlock::Create(TheContext, bblk->label, fn->v.f);
        Builder.SetInsertPoint(bb->v.b);
        createBitcode(bblk->lines, fn);
    }
    return;
}
