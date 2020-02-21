/* A Bison parser, made by GNU Bison 3.3.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2019 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Undocumented macros, especially those whose name start with YY_,
   are private implementation details.  Do not rely on them.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "3.3.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1

/* Substitute the type names.  */
#define YYSTYPE         YEX_STYPE
/* Substitute the variable and function names.  */
#define yyparse         yex_parse
#define yylex           yex_lex
#define yyerror         yex_error
#define yydebug         yex_debug
#define yynerrs         yex_nerrs

#define yylval          yex_lval
#define yychar          yex_char


# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* In a future release of Bison, this section will be replaced
   by #include "ex_parser.tab.h".  */
#ifndef YY_YEX_SRC_TOOLS_EX_CF5F17F_EXBUILD_EXE_EX_PARSER_TAB_H_INCLUDED
# define YY_YEX_SRC_TOOLS_EX_CF5F17F_EXBUILD_EXE_EX_PARSER_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YEX_DEBUG
# if defined YYDEBUG
#if YYDEBUG
#   define YEX_DEBUG 1
#  else
#   define YEX_DEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define YEX_DEBUG 0
# endif /* ! defined YYDEBUG */
#endif  /* ! defined YEX_DEBUG */
#if YEX_DEBUG
extern int yex_debug;
#endif
/* "%code requires" blocks.  */
#line 22 "../src/tools/ex/ex_parser.y" /* yacc.c:352  */

    #include "kvec.h"
    #include "system4/ex.h"

    kv_decl(block_list, struct ex_block*);
    kv_decl(field_list, struct ex_field*);
    kv_decl(value_list, struct ex_value*);
    kv_decl(row_list,   value_list*);
    kv_decl(node_list,  struct ex_tree*);

    struct ex *ex_data;


#line 133 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:352  */

/* Token type.  */
#ifndef YEX_TOKENTYPE
# define YEX_TOKENTYPE
  enum yex_tokentype
  {
    INT = 258,
    FLOAT = 259,
    STRING = 260,
    TABLE = 261,
    LIST = 262,
    TREE = 263,
    CONST_INT = 264,
    CONST_FLOAT = 265,
    CONST_STRING = 266
  };
#endif

/* Value type.  */
#if ! defined YEX_STYPE && ! defined YEX_STYPE_IS_DECLARED

union YEX_STYPE
{
#line 3 "../src/tools/ex/ex_parser.y" /* yacc.c:352  */

    int token;
    int i;
    float f;
    struct string *s;
    struct ex *ex;
    struct ex_block *block;
    struct ex_table *table;
    struct ex_field *field;
    struct ex_list *list;
    struct ex_tree *tree;
    struct ex_value *value;
    block_list *blocks;
    field_list *fields;
    row_list *rows;
    value_list *values;
    node_list *nodes;

#line 176 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:352  */
};

typedef union YEX_STYPE YEX_STYPE;
# define YEX_STYPE_IS_TRIVIAL 1
# define YEX_STYPE_IS_DECLARED 1
#endif


extern YEX_STYPE yex_lval;

int yex_parse (void);

#endif /* !YY_YEX_SRC_TOOLS_EX_CF5F17F_EXBUILD_EXE_EX_PARSER_TAB_H_INCLUDED  */

/* Second part of user prologue.  */
#line 36 "../src/tools/ex/ex_parser.y" /* yacc.c:354  */


#include <stdio.h>
#include "system4.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "ast.h"

extern int yex_lex();
extern unsigned long yex_line;
void yex_error(const char *s) { ERROR("at line %lu: %s", yex_line, s); }


#line 206 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:354  */

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

#ifndef YY_ATTRIBUTE
# if (defined __GNUC__                                               \
      && (2 < __GNUC__ || (__GNUC__ == 2 && 96 <= __GNUC_MINOR__)))  \
     || defined __SUNPRO_C && 0x5110 <= __SUNPRO_C
#  define YY_ATTRIBUTE(Spec) __attribute__(Spec)
# else
#  define YY_ATTRIBUTE(Spec) /* empty */
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# define YY_ATTRIBUTE_PURE   YY_ATTRIBUTE ((__pure__))
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# define YY_ATTRIBUTE_UNUSED YY_ATTRIBUTE ((__unused__))
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && ! defined __ICC && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN \
    _Pragma ("GCC diagnostic push") \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")\
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif


#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YEX_STYPE_IS_TRIVIAL && YEX_STYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYSIZE_T yynewbytes;                                            \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / sizeof (*yyptr);                          \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, (Count) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYSIZE_T yyi;                         \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  16
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   155

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  22
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  22
/* YYNRULES -- Number of rules.  */
#define YYNRULES  64
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  140

#define YYUNDEFTOK  2
#define YYMAXUTOK   266

/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                                \
  ((unsigned) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      20,    21,     2,     2,    16,    14,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    12,
       2,    13,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    18,     2,    19,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    15,     2,    17,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11
};

#if YEX_DEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
       0,    81,    81,    84,    85,    88,    89,    90,    91,    92,
      93,    96,    97,   100,   101,   104,   105,   108,   109,   112,
     113,   116,   117,   118,   119,   120,   121,   124,   125,   126,
     127,   128,   131,   132,   133,   136,   137,   140,   141,   144,
     145,   148,   149,   152,   153,   154,   157,   158,   161,   162,
     163,   164,   165,   166,   169,   170,   171,   174,   175,   178,
     179,   180,   181,   182,   183
};
#endif

#if YEX_DEBUG || YYERROR_VERBOSE || 0
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "INT", "FLOAT", "STRING", "TABLE",
  "LIST", "TREE", "CONST_INT", "CONST_FLOAT", "CONST_STRING", "';'", "'='",
  "'-'", "'{'", "','", "'}'", "'['", "']'", "'('", "')'", "$accept",
  "exdata", "stmts", "stmt", "int", "float", "table", "header", "fields",
  "field", "ftype", "body", "rows", "row", "cells", "cell", "list",
  "values", "value", "tree", "nodes", "node", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,    59,    61,    45,   123,    44,   125,    91,    93,
      40,    41
};
# endif

#define YYPACT_NINF -97

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-97)))

#define YYTABLE_NINF -1

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      90,    44,    56,    60,    67,    78,    80,    64,    90,    31,
      71,    86,    97,   111,   113,   114,   -97,   116,   -97,     6,
      22,   101,   110,   115,   117,   -97,   -97,   120,   -97,   -97,
     121,   -97,   -97,   118,   -97,    36,   -97,     2,   -97,   -97,
     -97,    99,   119,   -97,    29,   -97,    10,   -97,   -97,    32,
     -97,   123,   -97,    92,   -97,   -97,   -97,   -97,   126,   -97,
     -97,    98,   -97,   127,   124,   122,   125,   128,    52,   -97,
      65,    18,   -97,    16,     4,   -97,   129,    72,   100,   -97,
     110,   115,   117,   -97,   -97,   -97,   112,   -97,   -97,   -97,
     -97,   -97,     6,   -97,   -97,   -97,     6,    43,   -97,   104,
     -97,   -97,    53,   -97,   -97,   -97,   -97,   130,   131,   132,
     134,   -97,   106,    13,   -97,   -97,   -97,   110,   115,     6,
       6,    73,   -97,   -97,   -97,   -97,   -97,    25,    38,   -97,
       6,   118,     6,   -97,   135,   -97,   136,   118,   -97,   -97
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     2,     0,
       0,     0,     0,     0,     0,     0,     1,     0,     3,     0,
       0,     0,     0,     0,     0,     4,    11,     0,     5,    13,
       0,     6,     7,     0,     8,     0,     9,     0,    10,    12,
      14,     0,     0,    50,     0,    43,     0,    48,    49,     0,
      46,     0,    54,     0,    57,    27,    28,    29,     0,    30,
      31,     0,    19,     0,     0,     0,     0,     0,     0,    44,
       0,     0,    55,     0,     0,    17,    24,     0,     0,    35,
       0,     0,     0,    45,    47,    61,     0,    59,    60,    64,
      56,    58,     0,    21,    18,    20,     0,     0,    42,     0,
      39,    41,     0,    15,    51,    52,    53,     0,     0,     0,
       0,    32,     0,     0,    37,    16,    36,     0,     0,     0,
       0,     0,    33,    38,    40,    62,    63,     0,     0,    34,
       0,     0,     0,    25,     0,    22,     0,     0,    26,    23
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -97,   -97,   -97,   133,   -19,   -18,   -77,   -72,   -97,    66,
     -97,   -97,    37,   -96,   -97,    40,   -76,   -97,   -31,   -56,
     -97,    74
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
      -1,     7,     8,     9,    47,    48,    34,    42,    61,    62,
      63,    98,    78,    79,    99,   100,    36,    49,   101,    38,
      53,    54
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
      28,    93,    31,   104,    50,   105,   116,    55,    56,    57,
      58,    59,    60,    51,    89,    26,    65,    66,    67,    52,
      27,    94,    26,    29,    43,   116,   106,    44,    97,    51,
     123,    41,    29,    46,    92,    90,    30,    84,    39,    40,
     125,   130,   126,    18,   131,    26,    29,    43,    68,    69,
      44,    87,    88,    45,   132,    10,    46,   133,    77,   135,
     111,    26,    29,    43,    16,   139,    44,    11,    77,    83,
     115,    12,    46,   109,    26,    29,    85,   110,    13,    44,
      37,    26,    29,    43,    19,    86,    44,    97,    77,    14,
     129,    15,    46,     1,     2,     3,     4,     5,     6,    20,
     127,   128,    55,    56,    57,    58,    59,    60,    71,    72,
      21,   134,    32,   136,    74,    75,   102,   103,   107,   108,
     113,   114,   121,   122,    22,    33,    23,    24,    25,    39,
      35,    40,    37,    41,   112,    64,    70,    73,    76,    77,
      95,    17,     0,    80,     0,    91,    81,    96,   119,    82,
     120,   117,   118,   124,   137,   138
};

static const yytype_int16 yycheck[] =
{
      19,    73,    20,    80,    35,    81,   102,     3,     4,     5,
       6,     7,     8,    11,    70,     9,     6,     7,     8,    17,
      14,    17,     9,    10,    11,   121,    82,    14,    15,    11,
      17,    15,    10,    20,    18,    17,    14,    68,     9,    10,
     117,    16,   118,    12,    19,     9,    10,    11,    16,    17,
      14,    70,    70,    17,    16,    11,    20,    19,    15,   131,
      17,     9,    10,    11,     0,   137,    14,    11,    15,    17,
      17,    11,    20,    92,     9,    10,    11,    96,    11,    14,
      15,     9,    10,    11,    13,    20,    14,    15,    15,    11,
      17,    11,    20,     3,     4,     5,     6,     7,     8,    13,
     119,   120,     3,     4,     5,     6,     7,     8,    16,    17,
      13,   130,    11,   132,    16,    17,    16,    17,     6,     7,
      16,    17,    16,    17,    13,    15,    13,    13,    12,     9,
      15,    10,    15,    15,    97,    16,    13,    11,    11,    15,
      74,     8,    -1,    21,    -1,    71,    21,    18,    16,    21,
      16,    21,    21,   113,    19,    19
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     4,     5,     6,     7,     8,    23,    24,    25,
      11,    11,    11,    11,    11,    11,     0,    25,    12,    13,
      13,    13,    13,    13,    13,    12,     9,    14,    26,    10,
      14,    27,    11,    15,    28,    15,    38,    15,    41,     9,
      10,    15,    29,    11,    14,    17,    20,    26,    27,    39,
      40,    11,    17,    42,    43,     3,     4,     5,     6,     7,
       8,    30,    31,    32,    16,     6,     7,     8,    16,    17,
      13,    16,    17,    11,    16,    17,    11,    15,    34,    35,
      21,    21,    21,    17,    40,    11,    20,    26,    27,    41,
      17,    43,    18,    29,    17,    31,    18,    15,    33,    36,
      37,    40,    16,    17,    28,    38,    41,     6,     7,    26,
      26,    17,    34,    16,    17,    17,    35,    21,    21,    16,
      16,    16,    17,    17,    37,    28,    38,    26,    26,    17,
      16,    19,    16,    19,    26,    29,    26,    19,    19,    29
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    22,    23,    24,    24,    25,    25,    25,    25,    25,
      25,    26,    26,    27,    27,    28,    28,    29,    29,    30,
      30,    31,    31,    31,    31,    31,    31,    32,    32,    32,
      32,    32,    33,    33,    33,    34,    34,    35,    35,    36,
      36,    37,    37,    38,    38,    38,    39,    39,    40,    40,
      40,    40,    40,    40,    41,    41,    41,    42,    42,    43,
      43,    43,    43,    43,    43
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     2,     3,     4,     4,     4,     4,     4,
       4,     1,     2,     1,     2,     5,     6,     3,     4,     1,
       3,     3,     8,    10,     2,     7,     9,     1,     1,     1,
       1,     1,     2,     3,     4,     1,     3,     3,     4,     1,
       3,     1,     1,     2,     3,     4,     1,     3,     1,     1,
       1,     4,     4,     4,     2,     3,     4,     1,     3,     3,
       3,     3,     6,     6,     3
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256



/* Enable debugging if requested.  */
#if YEX_DEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo, int yytype, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YYUSE (yyoutput);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyo, yytoknum[yytype], *yyvaluep);
# endif
  YYUSE (yytype);
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo, int yytype, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  yy_symbol_value_print (yyo, yytype, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yytype_int16 *yyssp, YYSTYPE *yyvsp, int yyrule)
{
  unsigned long yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[yyssp[yyi + 1 - yynrhs]],
                       &yyvsp[(yyi + 1) - (yynrhs)]
                                              );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YEX_DEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YEX_DEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
yystrlen (const char *yystr)
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return (YYSIZE_T) (yystpcpy (yyres, yystr) - yyres);
}
# endif

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
                    yysize = yysize1;
                  else
                    return 2;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    YYSIZE_T yysize1 = yysize + yystrlen (yyformat);
    if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
      yysize = yysize1;
    else
      return 2;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          yyp++;
          yyformat++;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
{
  YYUSE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;


/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yynewstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  *yyssp = (yytype_int16) yystate;

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    goto yyexhaustedlab;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = (YYSIZE_T) (yyssp - yyss + 1);

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        YYSTYPE *yyvs1 = yyvs;
        yytype_int16 *yyss1 = yyss;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * sizeof (*yyssp),
                    &yyvs1, yysize * sizeof (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yytype_int16 *yyss1 = yyss;
        union yyalloc *yyptr =
          (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
# undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
                  (unsigned long) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 81 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { ex_data = ast_make_ex((yyvsp[0].blocks)); }
#line 1369 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 3:
#line 84 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.blocks) = ast_make_block_list((yyvsp[-1].block)); }
#line 1375 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 4:
#line 85 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.blocks) = ast_block_list_push((yyvsp[-2].blocks), (yyvsp[-1].block)); }
#line 1381 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 5:
#line 88 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.block) = ast_make_int_block((yyvsp[-2].s), (yyvsp[0].i)); }
#line 1387 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 6:
#line 89 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.block) = ast_make_float_block((yyvsp[-2].s), (yyvsp[0].f)); }
#line 1393 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 7:
#line 90 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.block) = ast_make_string_block((yyvsp[-2].s), (yyvsp[0].s)); }
#line 1399 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 8:
#line 91 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.block) = ast_make_table_block((yyvsp[-2].s), (yyvsp[0].table)); }
#line 1405 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 9:
#line 92 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.block) = ast_make_list_block((yyvsp[-2].s), (yyvsp[0].list)); }
#line 1411 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 10:
#line 93 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.block) = ast_make_tree_block((yyvsp[-2].s), (yyvsp[0].tree)); }
#line 1417 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 11:
#line 96 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.i) =   (yyvsp[0].i); }
#line 1423 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 12:
#line 97 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.i) = - (yyvsp[0].i); }
#line 1429 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 13:
#line 100 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.f) =   (yyvsp[0].f); }
#line 1435 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 14:
#line 101 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.f) = - (yyvsp[0].f); }
#line 1441 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 15:
#line 104 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.table) = ast_make_table((yyvsp[-3].fields), (yyvsp[-1].rows)); }
#line 1447 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 16:
#line 105 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.table) = ast_make_table((yyvsp[-4].fields), (yyvsp[-2].rows)); }
#line 1453 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 17:
#line 108 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.fields) = (yyvsp[-1].fields); }
#line 1459 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 18:
#line 109 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.fields) = (yyvsp[-2].fields); }
#line 1465 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 19:
#line 112 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.fields) = ast_make_field_list((yyvsp[0].field)); }
#line 1471 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 20:
#line 113 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.fields) = ast_field_list_push((yyvsp[-2].fields), (yyvsp[0].field)); }
#line 1477 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 21:
#line 116 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.field) = ast_make_field(EX_TABLE, (yyvsp[-1].s), 0,  0,  0,  (yyvsp[0].fields)); }
#line 1483 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 22:
#line 117 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.field) = ast_make_field(EX_TABLE, (yyvsp[-6].s), (yyvsp[-4].i), (yyvsp[-2].i), 0,  (yyvsp[0].fields)); }
#line 1489 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 23:
#line 118 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.field) = ast_make_field(EX_TABLE, (yyvsp[-8].s), (yyvsp[-6].i), (yyvsp[-4].i), (yyvsp[-2].i), (yyvsp[0].fields)); }
#line 1495 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 24:
#line 119 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.field) = ast_make_field((yyvsp[-1].token), (yyvsp[0].s), 0,  0,  0,  NULL); }
#line 1501 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 25:
#line 120 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.field) = ast_make_field((yyvsp[-6].token), (yyvsp[-5].s), (yyvsp[-3].i), (yyvsp[-1].i), 0,  NULL); }
#line 1507 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 26:
#line 121 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.field) = ast_make_field((yyvsp[-8].token), (yyvsp[-7].s), (yyvsp[-5].i), (yyvsp[-3].i), (yyvsp[-1].i), NULL); }
#line 1513 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 27:
#line 124 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.token) = EX_INT; }
#line 1519 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 28:
#line 125 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.token) = EX_FLOAT; }
#line 1525 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 29:
#line 126 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.token) = EX_STRING; }
#line 1531 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 30:
#line 127 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.token) = EX_LIST; }
#line 1537 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 31:
#line 128 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.token) = EX_TREE; }
#line 1543 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 32:
#line 131 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.rows) = NULL; }
#line 1549 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 33:
#line 132 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.rows) = (yyvsp[-1].rows); }
#line 1555 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 34:
#line 133 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.rows) = (yyvsp[-2].rows); }
#line 1561 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 35:
#line 136 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.rows) = ast_make_row_list((yyvsp[0].values)); }
#line 1567 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 36:
#line 137 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.rows) = ast_row_list_push((yyvsp[-2].rows), (yyvsp[0].values)); }
#line 1573 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 37:
#line 140 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.values) = (yyvsp[-1].values); }
#line 1579 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 38:
#line 141 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.values) = (yyvsp[-2].values); }
#line 1585 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 39:
#line 144 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.values) = ast_make_value_list((yyvsp[0].value)); }
#line 1591 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 40:
#line 145 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.values) = ast_value_list_push((yyvsp[-2].values), (yyvsp[0].value)); }
#line 1597 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 41:
#line 148 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.value) = (yyvsp[0].value); }
#line 1603 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 42:
#line 149 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.value) = ast_make_subtable((yyvsp[0].rows)); }
#line 1609 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 43:
#line 152 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.list) = ast_make_list(NULL); }
#line 1615 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 44:
#line 153 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.list) = ast_make_list((yyvsp[-1].values)); }
#line 1621 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 45:
#line 154 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.list) = ast_make_list((yyvsp[-2].values)); }
#line 1627 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 46:
#line 157 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.values) = ast_make_value_list((yyvsp[0].value)); }
#line 1633 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 47:
#line 158 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.values) = ast_value_list_push((yyvsp[-2].values), (yyvsp[0].value)); }
#line 1639 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 48:
#line 161 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.value) = ast_make_int((yyvsp[0].i)); }
#line 1645 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 49:
#line 162 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.value) = ast_make_float((yyvsp[0].f)); }
#line 1651 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 50:
#line 163 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.value) = ast_make_string((yyvsp[0].s)); }
#line 1657 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 51:
#line 164 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.value) = ast_make_table_value((yyvsp[0].table)); }
#line 1663 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 52:
#line 165 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.value) = ast_make_list_value((yyvsp[0].list)); }
#line 1669 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 53:
#line 166 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.value) = ast_make_tree_value((yyvsp[0].tree)); }
#line 1675 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 54:
#line 169 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.tree) = ast_make_tree(NULL); }
#line 1681 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 55:
#line 170 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.tree) = ast_make_tree((yyvsp[-1].nodes)); }
#line 1687 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 56:
#line 171 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.tree) = ast_make_tree((yyvsp[-2].nodes)); }
#line 1693 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 57:
#line 174 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.nodes) = ast_make_node_list((yyvsp[0].tree)); }
#line 1699 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 58:
#line 175 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.nodes) = ast_node_list_push((yyvsp[-2].nodes), (yyvsp[0].tree)); }
#line 1705 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 59:
#line 178 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.tree) = ast_make_leaf_int((yyvsp[-2].s), (yyvsp[0].i)); }
#line 1711 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 60:
#line 179 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.tree) = ast_make_leaf_float((yyvsp[-2].s), (yyvsp[0].f)); }
#line 1717 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 61:
#line 180 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.tree) = ast_make_leaf_string((yyvsp[-2].s), (yyvsp[0].s)); }
#line 1723 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 62:
#line 181 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.tree) = ast_make_leaf_table((yyvsp[-5].s), (yyvsp[0].table)); }
#line 1729 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 63:
#line 182 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.tree) = ast_make_leaf_list((yyvsp[-5].s), (yyvsp[0].list)); }
#line 1735 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;

  case 64:
#line 183 "../src/tools/ex/ex_parser.y" /* yacc.c:1652  */
    { (yyval.tree) = (yyvsp[0].tree); (yyval.tree)->name = (yyvsp[-2].s); }
#line 1741 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
    break;


#line 1745 "src/tools/ex/cf5f17f@@exbuild@exe/ex_parser.tab.c" /* yacc.c:1652  */
      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYTERROR;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;


#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif


/*-----------------------------------------------------.
| yyreturn -- parsing is finished, return the result.  |
`-----------------------------------------------------*/
yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  return yyresult;
}
#line 186 "../src/tools/ex/ex_parser.y" /* yacc.c:1918  */

