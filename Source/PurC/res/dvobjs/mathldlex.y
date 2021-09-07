%name-prefix "mathld"
%{
#include <stdio.h>
#include "private/instance.h"
#include "private/errors.h"
#include "private/debug.h"
#include "private/utils.h"
#include "private/edom.h"
#include "private/html.h"
#include "purc-variant.h"
%}

%code requires {
  #include "tools.h"
  typedef void *yyscan_t;
}

%define api.pure
%parse-param { struct pcdvobjs_mathld_param * myparam }
%param { yyscan_t arg }  /* because of reentrant in .l */
%define parse.error verbose

%union {
    long double d;
}


/* declare tokens */
%token NUMBER
%token ADD SUB MUL DIV
%token OP CP
%token EOL
%right '='
%left '+' '-'
%left '*'

%type <d> calclist exp factor term NUMBER

%{
  /* put here, just after all tokens defined above */
  #include "mathldlex.lex.h"
  void yyerror (struct pcdvobjs_mathld_param *, yyscan_t, const char *); /* first: %parse-param; second: %param */
%}

%start calclist
%%

calclist:
  %empty           { }
| calclist exp EOL { myparam->result = $2; }
| calclist EOL {  } /* blank line or a comment */
;

exp: factor
| exp ADD exp { $$ = $1 + $3; }
| exp SUB factor { $$ = $1 - $3; }
;

factor: term
| factor MUL term { $$ = $1 * $3;}
| factor DIV term { if ($3 == 0.0) YYABORT; $$ = $1 / $3; }
;

term: NUMBER
| OP exp CP { $$ = $2; }
;
%%

void yyerror (struct pcdvobjs_mathld_param *p, yyscan_t arg, const char *s)
{
  (void)p;
  (void)arg;

  printf ("math long double bison error: %s\n", s);
}

