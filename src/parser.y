%{
#include <stdio.h>
#include <stdlib.h>

#include "diagnostics.h"
#include "errors.h"
#include "scanner.h"
#include "symtab.h"

int yylex(void);
void yyerror(const char *msg);

static void end_declaration(void);
static void note_param(const char *name);
static int begin_function(const char *name);
static void recover_top_level(void);

/* True while parsing a declaration that contains 'typedef' */
static int decl_is_typedef = 0;
%}

%locations
%define parse.error verbose

%union {
    const char *str;   /* interned identifier (never freed by the parser) */
    int ival;
}

/* ---------- Keywords ---------- */

/* Types */
%token KW_VOID "void" KW_CHAR "char" KW_SHORT "short" KW_INT "int" KW_LONG "long"
%token KW_FLOAT "float" KW_DOUBLE "double" KW_SIGNED "signed" KW_UNSIGNED "unsigned"
%token KW_BOOL "_Bool" KW_COMPLEX "_Complex" KW_IMAGINARY "_Imaginary"

/* Storage classes */
%token KW_AUTO "auto" KW_REGISTER "register" KW_STATIC "static"
%token KW_EXTERN "extern" KW_TYPEDEF "typedef"

/* Qualifiers */
%token KW_CONST "const" KW_VOLATILE "volatile" KW_RESTRICT "restrict" KW_INLINE "inline"

/* Aggregate types */
%token KW_STRUCT "struct" KW_UNION "union" KW_ENUM "enum"

/* Control flow */
%token KW_IF "if" KW_ELSE "else" KW_SWITCH "switch" KW_CASE "case" KW_DEFAULT "default"
%token KW_WHILE "while" KW_DO "do" KW_FOR "for"
%token KW_GOTO "goto" KW_CONTINUE "continue" KW_BREAK "break" KW_RETURN "return"

/* Other */
%token KW_SIZEOF "sizeof"

/* ---------- Identifiers ---------- */
%token <str> IDENTIFIER "identifier"
%token <str> TYPE_NAME "type name"

/* ---------- Literals ---------- */
%token DEC_LITERAL "decimal constant"
%token OCT_LITERAL "octal constant"
%token HEX_LITERAL "hexadecimal constant"
%token FLOAT_LITERAL "floating constant"
%token STRING_LITERAL "string literal"
%token CHAR_LITERAL "character constant"

/* ---------- Arithmetic operators ---------- */
%token PLUS "+" MINUS "-" STAR "*" SLASH "/" PERCENT "%"
%token INCREMENT "++" DECREMENT "--"

/* ---------- Assignment operators ---------- */
%token ASSIGN "=" ADD_ASSIGN "+=" SUB_ASSIGN "-=" MUL_ASSIGN "*=" DIV_ASSIGN "/="
%token MOD_ASSIGN "%=" SHL_ASSIGN "<<=" SHR_ASSIGN ">>="
%token AND_ASSIGN "&=" XOR_ASSIGN "^=" OR_ASSIGN "|="

/* ---------- Relational operators ---------- */
%token EQ "==" NEQ "!=" LT "<" GT ">" LE "<=" GE ">="

/* ---------- Logical operators ---------- */
%token LOGICAL_AND "&&" LOGICAL_OR "||" BANG "!"

/* ---------- Bitwise operators ---------- */
%token AMPERSAND "&" PIPE "|" CARET "^" TILDE "~" SHL "<<" SHR ">>"

/* ---------- Member access and conditional ---------- */
%token DOT "." ARROW "->" QUESTION "?" COLON ":"

/* ---------- Delimiters ---------- */
%token LPAREN "(" RPAREN ")" LBRACKET "[" RBRACKET "]" LBRACE "{" RBRACE "}"
%token SEMICOLON ";" COMMA "," ELLIPSIS "..."

/* ---------- Lexical errors ----------
 * Returned by Get_Token(); yylex() reports and drops them, so no grammar
 * rule uses them. */
%token LEX_BAD_CHAR LEX_NON_ASCII LEX_BAD_OCTAL LEX_BAD_NUMBER
%token LEX_UNTERMINATED_STRING LEX_EMPTY_CHAR LEX_UNTERMINATED_CHAR

%type <str> declarator direct_declarator declarator_decl
%type <ival> function_head compound_open

/* Dangling else: shift the else to the nearest if */
%precedence IF_WITHOUT_ELSE
%precedence "else"

%start translation_unit

%%

/* ---------- Expressions ---------- */

constant
    : DEC_LITERAL
    | OCT_LITERAL
    | HEX_LITERAL
    | FLOAT_LITERAL
    | CHAR_LITERAL
    ;

string_literal
    : STRING_LITERAL
    | string_literal STRING_LITERAL
    ;

member_name
    : IDENTIFIER
    | TYPE_NAME
    ;

primary_expression
    : IDENTIFIER
    | constant
    | string_literal
    | "(" expression ")"
    ;

postfix_expression
    : primary_expression
    | postfix_expression "[" expression "]"
    | postfix_expression "(" ")"
    | postfix_expression "(" argument_expression_list ")"
    | postfix_expression "." member_name
    | postfix_expression "->" member_name
    | postfix_expression "++"
    | postfix_expression "--"
    | "(" type_name ")" "{" initializer_list "}"
    | "(" type_name ")" "{" initializer_list "," "}"
    ;

argument_expression_list
    : assignment_expression
    | argument_expression_list "," assignment_expression
    ;

unary_expression
    : postfix_expression
    | "++" unary_expression
    | "--" unary_expression
    | unary_operator cast_expression
    | "sizeof" unary_expression
    | "sizeof" "(" type_name ")"
    ;

unary_operator
    : "&"
    | "*"
    | "+"
    | "-"
    | "~"
    | "!"
    ;

cast_expression
    : unary_expression
    | "(" type_name ")" cast_expression
    ;

multiplicative_expression
    : cast_expression
    | multiplicative_expression "*" cast_expression
    | multiplicative_expression "/" cast_expression
    | multiplicative_expression "%" cast_expression
    ;

additive_expression
    : multiplicative_expression
    | additive_expression "+" multiplicative_expression
    | additive_expression "-" multiplicative_expression
    ;

shift_expression
    : additive_expression
    | shift_expression "<<" additive_expression
    | shift_expression ">>" additive_expression
    ;

relational_expression
    : shift_expression
    | relational_expression "<" shift_expression
    | relational_expression ">" shift_expression
    | relational_expression "<=" shift_expression
    | relational_expression ">=" shift_expression
    ;

equality_expression
    : relational_expression
    | equality_expression "==" relational_expression
    | equality_expression "!=" relational_expression
    ;

and_expression
    : equality_expression
    | and_expression "&" equality_expression
    ;

exclusive_or_expression
    : and_expression
    | exclusive_or_expression "^" and_expression
    ;

inclusive_or_expression
    : exclusive_or_expression
    | inclusive_or_expression "|" exclusive_or_expression
    ;

logical_and_expression
    : inclusive_or_expression
    | logical_and_expression "&&" inclusive_or_expression
    ;

logical_or_expression
    : logical_and_expression
    | logical_or_expression "||" logical_and_expression
    ;

conditional_expression
    : logical_or_expression
    | logical_or_expression "?" expression ":" conditional_expression
    ;

assignment_expression
    : conditional_expression
    | unary_expression assignment_operator assignment_expression
    ;

assignment_operator
    : "="
    | "*="
    | "/="
    | "%="
    | "+="
    | "-="
    | "<<="
    | ">>="
    | "&="
    | "^="
    | "|="
    ;

expression
    : assignment_expression
    | expression "," assignment_expression
    ;

constant_expression
    : conditional_expression
    ;

/* ---------- Declarations ---------- */

declaration
    : declaration_specifiers ";"                     { end_declaration(); }
    | declaration_specifiers init_declarator_list ";" { end_declaration(); }
    ;

declaration_specifiers
    : storage_class_specifier
    | storage_class_specifier declaration_specifiers
    | type_specifier
    | type_specifier declaration_specifiers
    | type_qualifier
    | type_qualifier declaration_specifiers
    | function_specifier
    | function_specifier declaration_specifiers
    ;

init_declarator_list
    : init_declarator
    | init_declarator_list "," init_declarator
    ;

init_declarator
    : declarator_decl
    | declarator_decl "=" initializer
    ;

/* The name is recorded right after the declarator, so a typedef is already
 * visible to the very next token. */
declarator_decl
    : declarator    { symtab_declare($1, decl_is_typedef); $$ = $1; }
    ;

storage_class_specifier
    : "typedef"     { decl_is_typedef = 1; }
    | "extern"
    | "static"
    | "auto"
    | "register"
    ;

type_specifier
    : "void"
    | "char"
    | "short"
    | "int"
    | "long"
    | "float"
    | "double"
    | "signed"
    | "unsigned"
    | "_Bool"
    | "_Complex"
    | "_Imaginary"
    | struct_or_union_specifier
    | enum_specifier
    | TYPE_NAME
    ;

/* Tags live in their own namespace, so a typedef name is a valid tag */
tag
    : IDENTIFIER
    | TYPE_NAME
    ;

struct_or_union_specifier
    : struct_or_union "{" struct_declaration_list "}"
    | struct_or_union "{" struct_declaration_list error "}"       { yyerrok; }
    | struct_or_union tag "{" struct_declaration_list "}"
    | struct_or_union tag "{" struct_declaration_list error "}"   { yyerrok; }
    | struct_or_union tag
    ;

struct_or_union
    : "struct"
    | "union"
    ;

struct_declaration_list
    : struct_declaration
    | struct_declaration_list struct_declaration
    ;

struct_declaration
    : specifier_qualifier_list ";"
    | specifier_qualifier_list struct_declarator_list ";"
    | error ";"                                     { yyerrok; }
    ;

specifier_qualifier_list
    : type_specifier
    | type_specifier specifier_qualifier_list
    | type_qualifier
    | type_qualifier specifier_qualifier_list
    ;

struct_declarator_list
    : struct_declarator
    | struct_declarator_list "," struct_declarator
    ;

struct_declarator
    : declarator
    | ":" constant_expression
    | declarator ":" constant_expression
    ;

enum_specifier
    : "enum" "{" enumerator_list "}"
    | "enum" "{" enumerator_list "," "}"
    | "enum" "{" enumerator_list error "}"                       { yyerrok; }
    | "enum" tag "{" enumerator_list "}"
    | "enum" tag "{" enumerator_list "," "}"
    | "enum" tag "{" enumerator_list error "}"                   { yyerrok; }
    | "enum" tag
    ;

enumerator_list
    : enumerator
    | enumerator_list "," enumerator
    ;

enumerator
    : IDENTIFIER                         { symtab_declare($1, 0); }
    | IDENTIFIER "=" constant_expression { symtab_declare($1, 0); }
    ;

type_qualifier
    : "const"
    | "restrict"
    | "volatile"
    ;

function_specifier
    : "inline"
    ;

declarator
    : pointer direct_declarator   { $$ = $2; }
    | direct_declarator
    ;

direct_declarator
    : IDENTIFIER
    | "(" declarator ")"                                        { $$ = $2; }
    | direct_declarator "[" "]"
    | direct_declarator "[" assignment_expression "]"
    | direct_declarator "[" type_qualifier_list "]"
    | direct_declarator "[" type_qualifier_list assignment_expression "]"
    | direct_declarator "[" "static" assignment_expression "]"
    | direct_declarator "[" "static" type_qualifier_list assignment_expression "]"
    | direct_declarator "[" type_qualifier_list "static" assignment_expression "]"
    | direct_declarator "[" "*" "]"
    | direct_declarator "[" type_qualifier_list "*" "]"
    | direct_declarator "(" parameter_type_list ")"
    | direct_declarator "(" identifier_list ")"
    | direct_declarator "(" ")"
    ;

pointer
    : "*"
    | "*" type_qualifier_list
    | "*" pointer
    | "*" type_qualifier_list pointer
    ;

type_qualifier_list
    : type_qualifier
    | type_qualifier_list type_qualifier
    ;

parameter_type_list
    : parameter_list
    | parameter_list "," "..."
    ;

parameter_list
    : parameter_declaration
    | parameter_list "," parameter_declaration
    ;

parameter_declaration
    : declaration_specifiers declarator             { note_param($2); }
    | declaration_specifiers abstract_declarator
    | declaration_specifiers
    ;

identifier_list
    : IDENTIFIER
    | identifier_list "," IDENTIFIER
    ;

type_name
    : specifier_qualifier_list
    | specifier_qualifier_list abstract_declarator
    ;

abstract_declarator
    : pointer
    | direct_abstract_declarator
    | pointer direct_abstract_declarator
    ;

direct_abstract_declarator
    : "(" abstract_declarator ")"
    | "[" "]"
    | "[" assignment_expression "]"
    | "[" "*" "]"
    | direct_abstract_declarator "[" "]"
    | direct_abstract_declarator "[" assignment_expression "]"
    | direct_abstract_declarator "[" "*" "]"
    | "(" ")"
    | "(" parameter_type_list ")"
    | direct_abstract_declarator "(" ")"
    | direct_abstract_declarator "(" parameter_type_list ")"
    ;

initializer
    : assignment_expression
    | "{" initializer_list "}"
    | "{" initializer_list "," "}"
    | "{" initializer_list error "}"   { yyerrok; }
    ;

initializer_list
    : designation_opt initializer
    | initializer_list "," designation_opt initializer
    ;

designation_opt
    : %empty
    | designator_list "="
    ;

designator_list
    : designator
    | designator_list designator
    ;

designator
    : "[" constant_expression "]"
    | "." member_name
    ;

/* ---------- Statements ---------- */

statement
    : labeled_statement
    | compound_statement
    | expression_statement
    | selection_statement
    | iteration_statement
    | jump_statement
    ;

labeled_statement
    : IDENTIFIER ":" statement
    | "case" constant_expression ":" statement
    | "default" ":" statement
    ;

/* Each block opens a scope so local names can shadow typedefs. The depth is
 * saved so the scope is restored correctly even after error recovery.
 * Recovery (panic mode): after an error, skip tokens up to a safe point:
 * a ';' (end of statement), a '}' (end of block) or a '{' (start of a block). */
compound_statement
    : "{" "}"
    | compound_open block_item_list "}"               { symtab_restore($1); }
    | compound_open block_item_list error "}"         { symtab_restore($1); yyerrok; }
    | compound_open error "}"                         { symtab_restore($1); yyerrok; }
    ;

compound_open
    : "{"   { $$ = symtab_depth(); symtab_enter_scope(); }
    ;

block_item_list
    : block_item
    | block_item_list block_item
    ;

block_item
    : declaration
    | statement
    | error ";"                  { end_declaration(); yyerrok; }
    | error compound_statement   { end_declaration(); yyerrok; }
    ;

expression_statement
    : ";"
    | expression ";"
    ;

selection_statement
    : "if" "(" expression ")" statement %prec IF_WITHOUT_ELSE
    | "if" "(" expression ")" statement "else" statement
    | "switch" "(" expression ")" statement
    ;

iteration_statement
    : "while" "(" expression ")" statement
    | "do" statement "while" "(" expression ")" ";"
    | "for" "(" expression_statement expression_statement ")" statement
    | "for" "(" expression_statement expression_statement expression ")" statement
    | "for" "(" declaration expression_statement ")" statement
    | "for" "(" declaration expression_statement expression ")" statement
    ;

jump_statement
    : "goto" IDENTIFIER ";"
    | "continue" ";"
    | "break" ";"
    | "return" ";"
    | "return" expression ";"
    ;

/* ---------- Top level ---------- */

translation_unit
    : %empty
    | translation_unit external_declaration
    ;

external_declaration
    : function_definition
    | declaration
    | ";"
    | error ";"                  { recover_top_level(); yyerrok; }
    | error compound_statement   { recover_top_level(); yyerrok; }
    | error "}"                  { recover_top_level(); yyerrok; }
    ;

function_definition
    : function_head compound_statement     { symtab_restore($1); }
    ;

/* The function name goes in the file scope; its parameters in a new scope
 * that stays open while the body is parsed. */
function_head
    : declaration_specifiers declarator                       { $$ = begin_function($2); }
    | declaration_specifiers declarator declaration_list      { $$ = begin_function($2); }
    ;

declaration_list
    : declaration
    | declaration_list declaration
    ;

%%

#define MAX_PENDING_PARAMS 128

static const char *pending_params[MAX_PENDING_PARAMS];
static int pending_count = 0;

/* Resets the per-declaration state */
static void end_declaration(void)
{
    decl_is_typedef = 0;
    pending_count = 0;
}

/* Remembers a parameter name until we know whether this is a function definition */
static void note_param(const char *name)
{
    if (pending_count < MAX_PENDING_PARAMS) pending_params[pending_count++] = name;
}

/* Opens the function scope and declares its parameters; returns the depth to restore */
static int begin_function(const char *name)
{
    int depth = symtab_depth();

    symtab_declare(name, 0);
    symtab_enter_scope();
    for (int i = 0; i < pending_count; i++) symtab_declare(pending_params[i], 0);

    end_declaration();
    return depth;
}

/* After an error at file level: drop every open scope and per-declaration state */
static void recover_top_level(void)
{
    symtab_restore(0);
    end_declaration();
}

void yyerror(const char *msg)
{
    report_syntax_error(msg);
}
