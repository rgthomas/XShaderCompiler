/*
 * GLSLConverter.cpp
 * 
 * This file is part of the XShaderCompiler project (Copyright (c) 2014-2016 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "GLSLConverter.h"
#include "GLSLHelper.h"
#include "AST.h"
#include "ASTFactory.h"
#include "Exception.h"
#include "Helper.h"


namespace Xsc
{


void GLSLConverter::Convert(
    Program& program, const ShaderTarget shaderTarget, const std::string& nameManglingPrefix)
{
    /* Store settings */
    shaderTarget_       = shaderTarget;
    nameManglingPrefix_ = nameManglingPrefix;

    /* Visit program AST */
    Visit(&program);
}


/*
 * ======= Private: =======
 */

/* ------- Visit functions ------- */

#define IMPLEMENT_VISIT_PROC(AST_NAME) \
    void GLSLConverter::Visit##AST_NAME(AST_NAME* ast, void* args)

IMPLEMENT_VISIT_PROC(Program)
{
    /* Register all input and output semantic variables are reserved identifiers */
    RegisterReservedVarIdents(ast->entryPointRef->inputSemantics.varDeclRefsSV);
    RegisterReservedVarIdents(ast->entryPointRef->outputSemantics.varDeclRefsSV);

    /* Default visitor */
    Visitor::VisitProgram(ast, args);
}

IMPLEMENT_VISIT_PROC(FunctionCall)
{
    if (ast->intrinsic == Intrinsic::Saturate)
    {
        /* Convert "saturate(x)" to "clamp(x, genType(0), genType(1))" */
        if (ast->arguments.size() == 1)
        {
            auto argTypeDen = ast->arguments.front()->GetTypeDenoter()->Get();
            if (argTypeDen->IsBase())
            {
                ast->intrinsic = Intrinsic::Clamp;
                ast->arguments.push_back(ASTFactory::MakeLiteralCastExpr(argTypeDen, DataType::Int, "0"));
                ast->arguments.push_back(ASTFactory::MakeLiteralCastExpr(argTypeDen, DataType::Int, "1"));
            }
            else
                RuntimeErr("invalid argument type denoter in intrinsic 'saturate'", ast->arguments.front().get());
        }
        else
            RuntimeErr("invalid number of arguments in intrinsic 'saturate'", ast);
    }
    else if (ast->intrinsic == Intrinsic::Undefined)
    {
        /* Remove arguments which contain a sampler state object, since GLSL does not support sampler states */
        EraseIf(ast->arguments,
            [&](const ExprPtr& expr)
            {
                return ExprContainsSampler(*expr);
            }
        );
    }

    /* Default visitor */
    Visitor::VisitFunctionCall(ast, args);
}

IMPLEMENT_VISIT_PROC(StructDecl)
{
    PushStructDeclLevel();
    {
        /* Default visitor */
        Visitor::VisitStructDecl(ast, args);
    }
    PopStructDeclLevel();
}

IMPLEMENT_VISIT_PROC(VarIdent)
{
    /* Has the variable identifier a next identifier? */
    if (ast->next && ast->symbolRef)
    {
        /* Does this identifier refer to a variable declaration? */
        if (auto varDecl = ast->symbolRef->As<VarDecl>())
        {
            /* Is its type denoter a structure? */
            auto varTypeDen = varDecl->declStmntRef->varType->typeDenoter.get();
            if (auto structTypeDen = varTypeDen->As<StructTypeDenoter>())
            {
                /* Must the structure be resolved? */
                if (MustResolveStruct(structTypeDen->structDeclRef))
                {
                    /* Remove first identifier */
                    ast->PopFront();
                }
                else
                {
                    /* Has a sub node a system value semantic? */
                    MakeVarIdentWithSystemSemanticLocal(ast);
                }
            }
        }
    }
}

/* --- Declarations --- */

IMPLEMENT_VISIT_PROC(VarDecl)
{
    /* Must this variable be renamed with name mangling? */
    if (MustRenameVarDecl(ast))
        RenameVarDecl(ast);

    /* Must the initializer type denoter changed? */
    if (ast->initializer)
    {
        /* Convert initializer expression if cast required */
        ConvertExprIfCastRequired(ast->initializer, *ast->GetTypeDenoter()->Get());
    }

    /* Default visitor */
    Visitor::VisitVarDecl(ast, args);
}

/* --- Declaration statements --- */

IMPLEMENT_VISIT_PROC(FunctionDecl)
{
    /* Is function reachable? */
    if (!ast->flags(AST::isReachable))
        return;

    /* Remove parameters which contain a sampler state object, since GLSL does not support sampler states */
    EraseIf(
        ast->parameters,
        [&](const VarDeclStmntPtr& varDeclStmnt)
        {
            return VarTypeIsSampler(*varDeclStmnt->varType);
        }
    );

    if (ast->flags(FunctionDecl::isEntryPoint))
    {
        isInsideEntryPoint_ = true;
        {
            /* Default visitor */
            Visitor::VisitFunctionDecl(ast, args);
        }
        isInsideEntryPoint_ = false;
    }
    else
    {
        /* Default visitor */
        Visitor::VisitFunctionDecl(ast, args);
    }
}

/* --- Statements --- */

IMPLEMENT_VISIT_PROC(ForLoopStmnt)
{
    /* Ensure a code block as body statement (if the body is a return statement within the entry point) */
    MakeCodeBlockInEntryPointReturnStmnt(ast->bodyStmnt);

    /* Default visitor */
    Visitor::VisitForLoopStmnt(ast, args);
}

IMPLEMENT_VISIT_PROC(WhileLoopStmnt)
{
    /* Ensure a code block as body statement (if the body is a return statement within the entry point) */
    MakeCodeBlockInEntryPointReturnStmnt(ast->bodyStmnt);

    /* Default visitor */
    Visitor::VisitWhileLoopStmnt(ast, args);
}

IMPLEMENT_VISIT_PROC(DoWhileLoopStmnt)
{
    /* Ensure a code block as body statement (if the body is a return statement within the entry point) */
    MakeCodeBlockInEntryPointReturnStmnt(ast->bodyStmnt);

    /* Default visitor */
    Visitor::VisitDoWhileLoopStmnt(ast, args);
}

IMPLEMENT_VISIT_PROC(IfStmnt)
{
    /* Ensure a code block as body statement (if the body is a return statement within the entry point) */
    MakeCodeBlockInEntryPointReturnStmnt(ast->bodyStmnt);

    /* Default visitor */
    Visitor::VisitIfStmnt(ast, args);
}

IMPLEMENT_VISIT_PROC(ElseStmnt)
{
    /* Ensure a code block as body statement (if the body is a return statement within the entry point) */
    MakeCodeBlockInEntryPointReturnStmnt(ast->bodyStmnt);

    /* Default visitor */
    Visitor::VisitElseStmnt(ast, args);
}

IMPLEMENT_VISIT_PROC(ExprStmnt)
{
    if (auto funcCall = ASTFactory::FindSingleFunctionCall(ast->expr.get()))
    {
        /* Is this a special intrinsic function call? */
        if (funcCall->intrinsic == Intrinsic::SinCos)
            ast->expr = ASTFactory::MakeSeparatedSinCosFunctionCalls(*funcCall);
    }

    /* Default visitor */
    Visitor::VisitExprStmnt(ast, args);
}

/* --- Expressions --- */

IMPLEMENT_VISIT_PROC(LiteralExpr)
{
    /* Replace 'h' and 'H' suffix with 'f' suffix */
    auto& s = ast->value;

    if (!s.empty())
    {
        if (s.back() == 'h' || s.back() == 'H')
        {
            s.back() = 'f';
            ast->dataType = DataType::Float;
        }
    }

    /* Default visitor */
    Visitor::VisitLiteralExpr(ast, args);
}

IMPLEMENT_VISIT_PROC(BinaryExpr)
{
    /* Default visitor */
    Visitor::VisitBinaryExpr(ast, args);

    /* Convert right-hand-side expression if cast required */
    ConvertExprIfCastRequired(ast->rhsExpr, *ast->lhsExpr->GetTypeDenoter()->Get());
}

IMPLEMENT_VISIT_PROC(UnaryExpr)
{
    /* Is the next sub expression again an unary expression? */
    if (ast->expr->Type() == AST::Types::UnaryExpr)
    {
        /* Insert bracket expression */
        auto bracketExpr = MakeShared<BracketExpr>(ast->area);
        
        bracketExpr->expr = ast->expr;

        ast->expr = bracketExpr;
    }

    /* Default visitor */
    Visitor::VisitUnaryExpr(ast, args);
}

IMPLEMENT_VISIT_PROC(VarAccessExpr)
{
    /* Default visitor */
    Visitor::VisitVarAccessExpr(ast, args);

    if (ast->assignExpr)
    {
        /* Convert assignment expression if cast required */
        ConvertExprIfCastRequired(ast->assignExpr, *ast->GetTypeDenoter()->Get());
    }
}

#undef IMPLEMENT_VISIT_PROC

/* --- Helper functions for conversion --- */

void GLSLConverter::PushStructDeclLevel()
{
    ++structDeclLevel_;
}

void GLSLConverter::PopStructDeclLevel()
{
    --structDeclLevel_;
}

bool GLSLConverter::IsInsideStructDecl() const
{
    return (structDeclLevel_ > 0);
}

bool GLSLConverter::ExprContainsSampler(Expr& ast) const
{
    return ast.GetTypeDenoter()->Get()->IsSampler();
}

bool GLSLConverter::VarTypeIsSampler(VarType& ast) const
{
    return ast.typeDenoter->IsSampler();
}

bool GLSLConverter::MustResolveStruct(StructDecl* ast) const
{
    return MustResolveStructForTarget(shaderTarget_, ast);
}

bool GLSLConverter::MustRenameVarDecl(VarDecl* ast) const
{
    /* Variable must be renamed if it's not inside a structure declaration and its name is reserved */
    return (
        !IsInsideStructDecl() &&
        !ast->flags(VarDecl::isShaderInput) &&
        std::find(reservedVarIdents_.begin(), reservedVarIdents_.end(), ast->ident) != reservedVarIdents_.end()
    );
}

void GLSLConverter::RenameVarDecl(VarDecl* ast)
{
    /* Set new identifier for this variable */
    ast->ident = nameManglingPrefix_ + ast->ident;
}

bool GLSLConverter::HasVarDeclOfVarIdentSystemSemantic(VarIdent* varIdent) const
{
    /* Has variable identifier a system reference? */
    if (varIdent->symbolRef)
    {
        /* Is this symbol reference a variable declaration? */
        if (auto varDecl = varIdent->symbolRef->As<VarDecl>())
        {
            /* Is semantic a system semantic? */
            return varDecl->flags(VarDecl::isSystemValue);
        }
    }
    return false;
}

void GLSLConverter::MakeVarIdentWithSystemSemanticLocal(VarIdent* ast)
{
    auto root = ast;

    while (ast)
    {
        /* Has current variable declaration a system semantic? */
        if (HasVarDeclOfVarIdentSystemSemantic(ast))
        {
            /*
            Remove all leading AST nodes until this one, to convert this
            variable identifer to an identifier for a local variable
            */
            while (root && !HasVarDeclOfVarIdentSystemSemantic(root))
            {
                root->PopFront();
                root = root->next.get();
            }

            /* Stop conversion process */
            break;
        }

        /* Continue search in next node */
        ast = ast->next.get();
    }
}

void GLSLConverter::MakeCodeBlockInEntryPointReturnStmnt(StmntPtr& bodyStmnt)
{
    /* Is this statement within the entry point? */
    if (isInsideEntryPoint_)
    {
        if (auto returnStmnt = bodyStmnt->As<ReturnStmnt>())
        {
            auto codeBlockStmnt = MakeShared<CodeBlockStmnt>(bodyStmnt->area);

            codeBlockStmnt->codeBlock = MakeShared<CodeBlock>(bodyStmnt->area);
            codeBlockStmnt->codeBlock->stmnts.push_back(bodyStmnt);

            bodyStmnt = codeBlockStmnt;
        }
    }
}

void GLSLConverter::RegisterReservedVarIdents(const std::vector<VarDecl*>& varDecls)
{
    for (auto& varDecl : varDecls)
        reservedVarIdents_.push_back(varDecl->ident);
}

std::unique_ptr<DataType> GLSLConverter::MustCastExprToDataType(TypeDenoter& targetTypeDen, TypeDenoter& sourceTypeDen)
{
    if (auto baseTargetTypeDen = targetTypeDen.As<BaseTypeDenoter>())
    {
        if (auto baseSourceTypeDen = sourceTypeDen.As<BaseTypeDenoter>())
        {
            if (baseTargetTypeDen->dataType == DataType::UInt)
            {
                /* Cast to 'uint' */
                if (baseSourceTypeDen->dataType == DataType::Int)
                    return MakeUnique<DataType>(DataType::UInt);
            }
            else if (baseTargetTypeDen->dataType == DataType::Int)
            {
                /* Cast to 'int' */
                if (baseSourceTypeDen->dataType == DataType::UInt)
                    return MakeUnique<DataType>(DataType::Int);
            }
        }
    }
    return nullptr;
}

void GLSLConverter::ConvertExprIfCastRequired(ExprPtr& expr, TypeDenoter& targetTypeDen)
{
    if (auto dataType = MustCastExprToDataType(targetTypeDen, *expr->GetTypeDenoter()->Get()))
    {
        /* Convert to cast expression with target data type if required */
        expr = ASTFactory::MakeBaseTypeCastExpr(*dataType, expr);
    }
}


} // /namespace Xsc



// ================================================================================
