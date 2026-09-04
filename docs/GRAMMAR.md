# BaristaScript Grammar Specification

This document is the authoritative, exhaustive grammar specification for **BaristaScript**
(`.barista`), a gradually-typed scripting language for **stock Godot**, delivered as a
GDExtension.

It is written to be detailed enough to serve as a blueprint for implementing the front-end
(tokenizer + parser), in particular a
[Pratt / precedence-climbing parser](https://en.wikipedia.org/wiki/Operator-precedence_parser)
for the expression layer.

BaristaScript is a derivative of **Foundry Script**, which is itself a derivative of GDScript.
It is deliberately specified as a *near-superset of GDScript* and a *near-subset of Foundry
Script*: every construct below either behaves exactly as the corresponding Foundry Script
construct does, or is listed in the delta table of §0.2. Where this document is silent and
Foundry Script's `GRAMMAR.md` is not, Foundry Script's text is normative.

> **Keep this in sync.** Any change to BaristaScript that affects the grammar (new tokens,
> keywords, operators, precedence, statements, declarations, type syntax) **must** be
> reflected here in the same change.

---

## 0. Relationship to Foundry Script

### 0.1 The governing constraint

Foundry Script is implemented inside a fork of the Godot engine, so it is free to change the
engine's own type system. It does: it adds a `Variant::UINT` carrier, it extends
`ContainerType` with reified generic arguments, and it adds trait virtuals to the base
`Script` and `ScriptLanguage` classes.

BaristaScript runs on **unmodified Godot**. It reaches the engine only through the public
GDExtension surface — `ScriptLanguageExtension`, `ScriptExtension`, `ScriptInstanceExtension`
and `ResourceFormatLoader`. That surface is fixed, so BaristaScript cannot add a Variant type,
cannot attach type arguments to a typed container, and cannot teach the engine what a trait is.

Every difference in §0.2 traces back to that one constraint. Nothing else about the language
changes.

### 0.2 Delta table

| # | Area | Foundry Script | BaristaScript | Why |
|---|------|----------------|---------------|-----|
| D1 | Integer types | Four types (`int`, `uint`, `long`, `ulong`) over two carriers, with `U`/`L`/`UL` literal suffixes and the `as!` reinterpret operator | **One** integer type, `int`, on the signed 64-bit `Variant::INT` carrier. `uint`, `ulong`, `long`, the suffixes and `as!` are **reserved and rejected** | `Variant`'s type enum is frozen ABI — and the tower is unused even in Foundry (§0.4) |
| D2 | Generic representation | Type arguments reified on the instance | **Monomorphized**: each specialization is emitted as a distinct script extending a shared raw base | Stock `ContainerType` has no `type_arguments` |
| D3 | Generic construction | `Crate.new()` legal; arguments may be absent | Constructing a generic type **requires** a complete type-argument vector: `Crate[int].new()` | A monomorphized instance must know its specialization |
| D4 | Gradual store of generics | A value with absent arguments satisfies a specialized slot | **Removed.** A store is an ordinary nominal check | D3 leaves no value with absent arguments |
| D5 | Trait visibility | Traits are visible to the engine's global-class table | Traits are **private to BaristaScript**. `is`/`as` against a trait work; GDScript and the editor cannot see them | Stock `Script` has no trait virtual |
| D6 | Global name kinds | `trait_name`/`enum_name`/`tuple_name` register with the engine | Only `class_name` registers with the engine. The other three register in BaristaScript's **own** declaration index | Stock `add_global_class` has no `is_trait`/`is_enum` flag |
| D7 | Native namespaces | Native engine classes may be namespaced | **Removed.** `namespace`/`import` govern BaristaScript declarations only; native classes are always flat global names | Stock `ClassDB` is flat |
| D8 | `@autoload` | Engine-native autoload registration | Retained as an annotation, applied by a **build step** that writes `project.godot`'s `[autoload]` section | A GDExtension cannot inject autoloads at load time |
| D9 | GDScript interop | No GDScript exists in the engine | BaristaScript **consumes** GDScript: it may `extends` a GDScript class and call its members. GDScript cannot see BaristaScript traits, generics or tagged unions | Consume-only by design |

Everything not listed above — the whole lexical grammar, all declaration forms, the entire
expression grammar and precedence table, all statements, `match` and its exhaustiveness rules,
traits, tagged unions, tuples, type unions, nullability, `Self`, retroactive conformance,
namespaces, custom annotations, `async`/`await`/`Coroutine[T]`, named arguments and variadics —
is identical to Foundry Script.

### 0.3 What is unchanged and why it matters

Three Foundry Script features that look like they would need engine support do not, and are
retained in full:

- **Type unions** already have no runtime representation in Foundry Script (§7). They erase
  identically here, so `int | String`, `Number`, and union normalization carry over verbatim.
- **Tuples** already erase to a read-only `Array` (§6.1). Unchanged.
- **Tagged unions** already erase to `[tag, payload...]` read-only arrays (§4.4). Unchanged.

### 0.4 Why the integer tower was dropped rather than degraded (D1)

D1 is the one delta not forced solely by the host. The host constraint is real — a `Variant::UINT`
carrier cannot be added — but it would still have been possible to keep all four type *names* as
compile-time constraints over one carrier, retaining range checking and the conversion lattice.
That was specified and then rejected on evidence:

- **The tower has no users.** Across 720 hand-written `.fs` files in FoundryLib, FoundryKit,
  Foundry-Tools and Foundry-Examples, `uint` and `ulong` appear in **zero** of them. Every
  occurrence of `uint`, `ulong`, `as!`, `is uint` and the `U`/`UL` suffixes in the entire Foundry
  tree lives under `modules/foundry_script/tests/` — code whose purpose is to test the feature.
- **The strongest use case already opted out.** `protoc-gen-foundryscript` maps *every* protobuf
  integer — `uint32`, `uint64` and `fixed64` included — to plain `int`
  (`Foundry-Tools/.../generator/types.go`). The protocol-fidelity argument for the tower is not
  being exercised.
- **On BaristaScript it would ship weakened.** No runtime distinctness, `ulong` capped at `2^63-1`,
  and no container element width. Carrying a degraded version of an unadopted feature is the
  weakest case available, and it is also the most expensive thing in the port — Foundry spends
  ~950 lines of numeric ops, ~3,700 lines of test suites and 28 source files on it.
- **It buys a cleaner boundary.** With one `int` on the signed 64-bit carrier, BaristaScript's
  `int` is bit-identical to GDScript's, so the consume-only interop of D9 needs no conversion
  semantics at the edge.

The removed spellings are **reserved rather than freed** (§2.5, §7.1) so that ported Foundry Script
fails loudly instead of silently changing meaning, and so that reintroducing a numeric tower later
stays a purely additive change.

---

## 1. Notation

The grammar is described using **ISO/IEC 14977 Extended Backus–Naur Form (EBNF)** with the
conventions below.

| Notation        | Meaning                                                     |
|-----------------|-------------------------------------------------------------|
| `=`             | definition                                                  |
| `,`             | concatenation (sequence)                                    |
| `\|`            | alternation (choice)                                        |
| `[ x ]`         | optional: zero or one `x`                                   |
| `{ x }`         | repetition: zero or more `x`                                |
| `( x )`         | grouping                                                    |
| `"x"`           | terminal: a literal token / keyword / punctuation           |
| `UPPER_CASE`    | a lexical token class produced by the tokenizer (§2)        |
| `lower_case`    | a non-terminal (grammar production)                         |
| `(* ... *)`     | comment                                                     |

Because BaristaScript is **indentation-sensitive**, the tokenizer emits three synthetic layout
tokens — `NEWLINE`, `INDENT`, `DEDENT` — exactly like Python. These are treated as ordinary
terminals in the syntactic grammar (§4+). Their generation rules are described in §2.3.

The expression grammar (§5) is **not** expressed purely as EBNF productions; it is specified as
a Pratt parser with an explicit precedence table. The EBNF for expressions is informative.

---

## 2. Lexical grammar

The tokenizer reads UTF-32 code points and produces a stream of tokens.

### 2.1 Whitespace, line continuation, and comments

```ebnf
whitespace        = " " | "\t" ;
line_comment      = "#", { any_char - newline } ;
line_continuation = "\\", newline ;
```

- **Indentation** is significant only at the start of a logical line (§2.3). Other whitespace
  separates tokens and is otherwise insignificant.
- `#` begins a comment running to the end of the physical line. A comment starting with `##`
  is a **documentation comment**, captured separately for tooling; it does not affect grammar.
- A backslash immediately before a newline is a **line continuation**: the logical line
  continues and no `NEWLINE` is produced.
- Inside an open bracket pair (`(`, `[`, `{`) the tokenizer is in **multiline mode**: newlines
  and indentation are ignored and no layout tokens are produced until the matching closing
  bracket. The parser also enables multiline mode explicitly around call and parameter lists.

### 2.2 The cursor / completion

In editor/tooling builds the tokenizer tracks a cursor position for code completion. This does
not affect the language grammar and can be ignored by a standalone implementation.

### 2.3 Layout tokens: `NEWLINE`, `INDENT`, `DEDENT`

- `NEWLINE` — emitted at the end of a logical line that contains tokens. Blank and
  comment-only lines produce none. Suppressed in multiline mode and across a line continuation.
- `INDENT` — emitted when a logical line's leading indentation is **greater** than the top of
  the indentation stack.
- `DEDENT` — emitted (possibly several at once) when leading indentation is **less** than the
  top of the stack, unwinding to a matching level.

Indentation may use spaces or tabs, but the **first** indentation character in a file fixes the
indent character for the whole file; mixing is an error. Default tab size is 4. A block is
introduced by `:` followed by `NEWLINE` and `INDENT`, and ends at the matching `DEDENT`.

Lambdas manipulate the indentation stack specially so a multi-line lambda body can appear
inside an expression.

### 2.4 Identifiers

```ebnf
identifier      = id_start, { id_continue } ;
id_start        = unicode_xid_start  | "_" ;
id_continue     = unicode_xid_continue | "_" ;
```

Identifiers follow Unicode identifier rules. A lone `_` is the dedicated `UNDERSCORE` token
(the match wildcard), not an identifier.

### 2.5 Keywords

Reserved keywords, each mapping to a dedicated token type:

```
abstract  as        and       assert    await
break     breakpoint
class     class_name const     continue
elif      else      enum      enum_name extends
final     for       func
if        import    in        is
match
namespace not
or
pass      preload
return
self      signal    static    super
trait     trait_name tuple     tuple_name
uses
var       void
while     when
yield
```

Built-in numeric constants are also keyword tokens: `INF`, `NAN`, `PI`, `TAU`.

**Contextual keywords** (lexed as ordinary `IDENTIFIER`, given meaning only by position):

- `annotation` — starts a custom annotation declaration at a root-level declaration position (§4.7).
- `extend` — starts a retroactive conformance at a root-level declaration position (§4.8).
  Distinct from the `extends` keyword token.
- `type` — starts a type alias where a member declaration is valid (§4.4b).
- `async` — function modifier immediately preceding `func` or another modifier (§4.5).
- `targets` — separates an annotation declaration's parameter list from its target list.
- `get` / `set` — property accessor names.
- `Self` — the contextual receiver type name (§7.1).
- `CLASS`, `METHOD`, `VARIABLE`, `SIGNAL`, `CONSTANT`, `PARAMETER` — annotation target names.

**Keywords usable as identifiers / node names:** `match`, `when`, `uses`, and the constant
keywords `PI`/`TAU`/`INF`/`NAN` are accepted where an identifier is expected. A broad set of
keywords is accepted as **node names** after `$`/`%`/`/` in a get-node path, and as attribute
names after `.`.

`yield` is reserved but always an error (use `await`).

**(D1) Reserved integer spellings.** `uint`, `ulong`, and `long` are **reserved type names**: they
are recognized in type position and always rejected — *"`uint` is reserved. BaristaScript stores
every integer on one signed 64-bit carrier; write `int`."* They are not keywords and remain usable
as ordinary identifiers elsewhere, exactly as `int` is. Reserving them keeps ported Foundry Script
loud rather than silently reinterpreted, and keeps a future numeric tower additive.

### 2.6 Literals

All of the following produce a `LITERAL` token carrying the parsed value and type.

#### 2.6.1 Numbers

```ebnf
number       = integer | float ;

integer      = int_dec | int_hex | int_bin ;

int_dec      = digit, { digit | "_" } ;
int_hex      = "0", ("x" | "X"), hex_digit, { hex_digit | "_" } ;
int_bin      = "0", ("b" | "B"), bin_digit, { bin_digit | "_" } ;

float        = ( dec_part, ".", [ dec_part ] | ".", dec_part | dec_part ),
               [ exponent ] ;
dec_part     = digit, { digit | "_" } ;
exponent     = ("e" | "E"), [ "+" | "-" ], digit, { digit | "_" } ;

digit        = "0".."9" ;
hex_digit    = digit | "a".."f" | "A".."F" ;
bin_digit    = "0" | "1" ;
```

Rules enforced by the tokenizer:

- `_` digit separators are allowed but **not** adjacent (`1__0` is an error), not immediately
  after a base prefix (`0x_`, `0b_`), and not immediately after a decimal point (`10._`).
- Hex and binary literals need at least one digit after the prefix and admit no decimal point
  or exponent.
- A decimal point may not appear twice; `..` is the range/rest token, so `1..2` is `1`, `..`, `2`.
- A letter immediately following a number is an "invalid numeric notation" error, except for
  the integer suffixes below.
- Decimal literals with a `.` or exponent are floats; otherwise integers. Hex and binary are
  integers.
- **(D1) There are no integer literal suffixes.** Every integer literal has type `int`, the signed
  64-bit carrier. A literal outside `[-9223372036854775808, 9223372036854775807]` is an error.
- **(D1)** A trailing `U`, `L`, or `UL` on an integer literal is **reserved and rejected**, not an
  "invalid numeric notation" error: *"Integer literal suffixes are reserved. BaristaScript has one
  integer type, `int`; write `42`."* Lowercase and misordered forms (`1u`, `1l`, `1LU`) report the
  same thing. This keeps a ported `42UL` loud rather than silently becoming `42` followed by an
  identifier.
- A digit is only the start of a number when the preceding token cannot end a value. After a
  value token (`IDENTIFIER`, a literal, `)`, `]`, or a numeric constant keyword), `.` followed
  by a digit is a `PERIOD` token (tuple index access, e.g. `t.0`), not the start of a float.
- A digit immediately following a `PERIOD` token lexes as a decimal integer **only**: no
  fractional part, exponent, base prefix, or trailing letter, so `t.0.1` is nested member
  access and `t.0e5` / `t.0x1` are lexer errors.
- A `+` or `-` **immediately** followed by a digit (no intervening whitespace) is folded into
  that numeric literal whenever the preceding token cannot end a value. Whitespace between the
  sign and the digit prevents folding and yields an ordinary unary `PLUS`/`MINUS` token instead.
  After a value-ending token the same characters are always the binary operator, even when the
  digit is adjacent (`a-2` is `a`, `-`, `2`). This is what makes `-9223372036854775808` writable
  (§5.2, §7.1): the sign is lexical content of the literal, not a unary applied to an out-of-range
  positive magnitude.

#### 2.6.2 Strings

```ebnf
string        = [ string_prefix ], ( short_string | long_string ) ;
string_prefix = "r" | "&" | "^" ;     (* raw, StringName, NodePath *)
short_string  = '"',  { schar  | escape }, '"'
              | "'",  { schar' | escape }, "'" ;
long_string   = '"""', { any - '"""' }, '"""'
              | "'''", { any - "'''" }, "'''" ;
```

- `"..."` and `'...'` are equivalent. Triple-quoted forms are **multiline**.
- Prefixes: `r"..."` **raw** (backslashes literal except `\"`, `\'`, `\\`); `&"..."`
  **StringName**; `^"..."` **NodePath**.
- **Escapes** in non-raw strings: `\a \b \f \n \r \t \v \' \" \\`, line-continuation
  `\` + newline, and Unicode escapes `\uXXXX` and `\UXXXXXX`.
- Invisible bidirectional control characters inside a string are rejected.
- A standalone string literal used as a statement is permitted as a block "comment" in class
  bodies and at the top of a file.

#### 2.6.3 Boolean / null

`true`, `false`, and `null` are **not** keywords; they are ordinary identifiers resolved to
constant values during analysis. Syntactically they are `IDENTIFIER`.

### 2.7 Annotations token

```ebnf
ANNOTATION = "@", identifier, { ".", identifier } ;
```

The `@` plus following dotted name is lexed as a single `ANNOTATION` token (e.g. `@export`,
`@onready`, `@cafecito.test.timeout`). The token's literal includes the leading `@`.

A `.` only extends the annotation name when *immediately* followed by an `id_start` character;
otherwise it is left unconsumed. As an editor-tooling exception, in completion mode a trailing
`.` immediately before the cursor *is* absorbed so qualified-name completion can offer the
declarations under that namespace. That carve-out is tooling behavior, not static grammar.

### 2.8 Operators and punctuation

```
Comparison:     <   <=   >   >=   ==   !=
Logical:        and  or   not  &&   ||   !
Bitwise:        &    |    ~    ^    <<   >>
Arithmetic:     +    -    *    **   /    %
Assignment:     =    +=   -=   *=   **=  /=   %=
                <<=  >>=  &=   |=   ^=
Brackets:       (  )   [  ]   {  }
Punctuation:    ,   ;   .   ..   ...   :   $   ->   _
Other:          ?   `    (backtick)
```

Notes:

- **(D1)** `as!` is **not** an operator. The `as` keyword immediately followed by `!` is reserved
  and rejected: *"`as!` is reserved. It reinterprets between integer widths, which BaristaScript
  does not distinguish."* (`as !x` — with whitespace — remains an ordinary cast to a negated
  expression and is unaffected.)
- `..` is `PERIOD_PERIOD` (match rest / dictionary rest); `...` is `PERIOD_PERIOD_PERIOD`
  (rest/variadic parameter).
- `_` alone is `UNDERSCORE` (match wildcard).
- `$` is `DOLLAR` (get-node). `%` is both modulo and a unique-name node prefix in get-node paths.
- `->` is `FORWARD_ARROW` (function return type).
- `?` is `QUESTION_MARK`: the nullable-type suffix (§7), rejected as a standalone operator with
  a hint to use the `if/else` ternary.
- Backtick and VCS conflict markers exist only to produce better error messages.

---

## 3. Source file structure (top level)

A `.barista` file is an implicit class (the **head class**).

```ebnf
program        = { script_annotation | string NEWLINE },
                 [ namespace_decl ], { import_decl },
                 { head_modifier },
                 [ class_name_decl | trait_name_decl | enum_name_decl | tuple_name_decl ],
                 [ extends_decl [ uses_decl ] | uses_decl ],
                 { class_annotation | string NEWLINE },
                 class_body ;
```

Ordering rules:

1. Script-level annotations (`@tool`, `@icon`, `@static_unload`, `@autoload`) and class-level
   annotations may appear first. Class-level annotations are buffered and attached to either the
   head class or the first inner declaration.
2. `namespace` (at most once) must come before any `import`.
3. `import` declarations follow `namespace`.
4. `class_name`/`trait_name`/`enum_name`/`tuple_name` are mutually exclusive and may be used at
   most once. `final`/`abstract` may precede `class_name`/`trait_name`/`extends` to mark the
   head class (a trait cannot be `final`).
5. `extends` may be used once and must come before `uses`.
6. The class body follows.

A file with neither `class_name` nor `extends` is still a valid class implicitly extending
`RefCounted`.

### 3.1 Namespace and import

```ebnf
namespace_decl = "namespace", dotted_name, NEWLINE ;
import_decl    = "import",    dotted_name, NEWLINE ;
dotted_name    = identifier, { ".", identifier } ;
```

**(D7)** Namespaces govern **BaristaScript declarations only**. Native engine classes and
GDScript global classes are always flat global names, reachable by their bare name and never by
a dotted chain or through `import`. There is no native namespace registry to consult.

### 3.2 Global names

```ebnf
class_name_decl = "class_name", identifier, [ type_parameters ],
                  [ extends_decl [ uses_decl ] | uses_decl ], NEWLINE ;
trait_name_decl = "trait_name", identifier, [ type_parameters ],
                  [ extends_decl [ uses_decl ] | uses_decl ], NEWLINE ;
enum_name_decl  = "enum_name", identifier, [ type_parameters ], ":", enum_body ;
tuple_name_decl = "tuple_name", identifier,
                  "(", tuple_field, { ",", tuple_field }, [ "," ], ")", NEWLINE ;
```

`extends`/`uses` may appear on the same line as `class_name`/`trait_name`. An `enum_name` file
may contain only the enum declaration, and a `tuple_name` file only its tuple declaration
(§4.4a). A `tuple_name` file declares a global tuple type rather than a script: it has no base
class.

**(D6) Registration.** Only a **non-generic** `class_name` is registered with the engine's
global-class table, which is what makes it usable from GDScript, the editor's node-creation
dialog, and `@export` hints. `trait_name`, `enum_name`, `tuple_name`, and every generic
`class_name` are registered **only in BaristaScript's own declaration index**: they are fully
usable from BaristaScript and invisible to the engine. A generic `class_name` is excluded
because its monomorphized specializations (D2) are the things that exist at runtime, and the
engine's table has one slot per name.

### 3.3 Extends and uses

```ebnf
extends_decl   = "extends",
                 ( STRING [ ".", dotted_name ] | dotted_name ),
                 [ type_arguments ] ;
uses_decl      = "uses", trait_use, { ",", trait_use } ;
trait_use      = dotted_name, [ type_arguments ] ;
type_arguments = "[", type, { ",", type }, "]" ;
```

- `extends` accepts a path string (`extends "res://base.barista"`), an inheritance chain of
  identifiers (`extends A.B.C`), or a path string followed by an inner-class chain. A generic
  base may carry `type_arguments` (`extends List[int]`).
- **(D9)** A base may be a native engine class, another BaristaScript class, **or a GDScript
  class** — by global `class_name` or by `preload`ed path. The reverse does not hold: Godot does
  not support cross-language script inheritance, so a `.gd` file cannot `extends` a BaristaScript
  class regardless of what BaristaScript registers. GDScript reaches a BaristaScript object the
  way it reaches any other `Object` — by calling its methods and reading its properties.
- An `extends` edge into a generic class must spell **every** type argument; a bare generic
  base (`class Bad extends Box` for `class Box[T]`) is an error reported at the base class name.
  The edge defines the inherited member types of the whole subclass, so unbound base parameters
  would silently erase every inherited member that depends on them. The rule holds for local,
  nested, preloaded, global, and namespaced BaristaScript bases; it applies only once the base
  resolves and is known to declare type parameters, so native, GDScript, and non-generic bases
  are unaffected. Forwarded child parameters (`class Child[T] extends Box[T]`) and F-bounded
  declarations (`class Recursive[T: Recursive]`) stay legal.
- `uses` mixes in one or more traits. Type arguments are optional only for a non-generic trait:
  a `uses` entry naming a generic trait must spell that trait's type arguments at matching arity
  (`uses Storage[int]`, or `uses Storage[T]` forwarding an enclosing parameter). A bare entry is
  an error reported at the trait name — including when the same trait is bound with arguments
  through another entry or a supertrait on the same class. Applying a non-generic trait that
  binds a generic supertrait at its own declaration site stays legal. The rule covers every
  position accepting a `uses` clause, the `extend` conformance clause included.
- A trait's type arguments are fixed by the first class in an inheritance chain that applies it.
  A subclass may re-apply the same trait only with the same arguments. A position either side
  leaves on an unreified type parameter (including `Self`) carries no argument and never
  conflicts.

---

## 4. Declarations

### 4.1 Class body and members

```ebnf
class_body      = { member } ;

member          = { declaration_modifier },
                  ( variable_decl
                  | constant_decl
                  | signal_decl
                  | function_decl
                  | inner_class_decl
                  | trait_decl
                  | enum_decl
                  | tuple_decl
                  | type_alias_decl
                  | annotation_declaration
                  | conformance_declaration
                  | class_annotation
                  | standalone_annotation
                  | "pass" NEWLINE
                  | string NEWLINE ) ;

declaration_modifier = "final" | "abstract" | "static" | "async" ;
```

Modifiers form a leading run collected before the declaration keyword. Which are legal depends
on the member:

| Member    | final | abstract | static | async | Notes |
|-----------|:-----:|:--------:|:------:|:-----:|-------|
| `var`     |  yes  |    no    |  yes   |  no   | |
| `const`   |  no   |    no    |   no   |  no   | |
| `signal`  |  no   |    no    |   no   |  no   | |
| `func`    |  yes  |   yes    |  yes   |  yes  | `abstract`+`static` only in a trait; `abstract`+`async` allowed |
| `class`   |  yes  |   yes    |   no   |  no   | |
| `trait`   |  no   |   yes    |   no   |  no   | |
| `enum`    |  no   |    no    |   no   |  no   | |
| `tuple`   |  no   |    no    |   no   |  no   | |
| `type`    |  no   |    no    |   no   |  no   | Contextual; see §4.4b |

`final`+`abstract` is always contradictory. `abstract`+`static` is only allowed inside a trait.

### 4.2 Inner classes and traits

```ebnf
inner_class_decl = "class", identifier, [ type_parameters ],
                   [ "extends", ... ], [ "uses", ... ], ":", block_or_inline ;
trait_decl       = "trait", identifier, [ type_parameters ],
                   [ "extends", ... ], [ "uses", ... ], ":", block_or_inline ;
```

`extends`/`uses` may appear on the declaration line and/or as the first lines inside the
indented body. `block_or_inline` is either a single inline statement/member or a
`NEWLINE INDENT ... DEDENT` block.

**(D5) Traits are private to BaristaScript.** A trait is a real nominal type: it participates in
`uses`, in `is`/`as`, in type-parameter bounds, in retroactive conformance, and in overload
resolution, and all of that is answered by BaristaScript's own runtime. What it does *not* do is
appear in the engine's global-class table, so a trait name is not a valid `@export` type, not
selectable in the editor's node dialog, and not visible to GDScript. A trait applied to a class
does not change that class's engine-visible base.

### 4.3 Type parameters (generics)

```ebnf
type_parameters = "[", type_parameter, { ",", type_parameter }, [ "," ], "]" ;
type_parameter  = identifier, [ ":", type ] ;   (* optional upper bound *)
```

Generics appear on classes (`class Box[T]`, `class_name Pair[K, V]`), traits
(`trait Container[T]`), functions (`func swap[T](...)`), and named tagged unions
(`enum Result[T, E]`, `enum_name Tree[T]`). A bound constrains the parameter (`[T: Resource]`).
A trailing comma is allowed.

A bound is an ordinary type, so it may be a type union or an alias naming one
(`[T: int | String]`, `[T: Number]`). Satisfaction against a union bound is asymmetric: a
**concrete** argument satisfies it when it satisfies **at least one** alternative; a **union**
argument satisfies it only when **every** alternative does; and a **type-parameter** argument
satisfies it only when its own bound proves it does, so an unbounded parameter never satisfies a
concrete bound.

A class's `static var` may not be typed by a class type parameter, directly or nested. Static
storage is one slot per declaring class and specializing a class does not create a distinct one.
A generic trait may declare one (`trait Slotted[T]: static var slot: T`) — each implementer
flattens it into a slot of its own — but the implementer must fix the argument concretely.

#### 4.3.1 Monomorphization (D2, D3, D4)

This is BaristaScript's single largest departure from Foundry Script, and it is a change of
*representation*, not of surface syntax. Every generic declaration in §4.3 is written exactly as
it is in Foundry Script.

**Representation.** A generic class `Crate[T]` is emitted as:

- one **raw base** script named `Crate`, holding every member whose type does not mention `T`; and
- one **specialization** script per distinct type-argument vector appearing in the program,
  each extending the raw base, with `T` substituted throughout.

`Crate[int]` and `Crate[String]` are therefore two distinct runtime scripts that share a base.
Type arguments are consequently **reified for free**: they are the identity of the script the
value carries.

**What this preserves.** Because a specialization is a real, distinct script, the whole of
Foundry Script's generic `is`/`as` semantics (§5.5) survives unchanged, including invariance,
recursive argument comparison, projection through inheritance, `Self`, generic traits, and
class-handle tests. This is the reason monomorphization was chosen over erasure.

**What this changes.**

- **(D3) Construction requires a complete type-argument vector.** `Crate[int].new()` is the only
  construction form. A bare `Crate.new()` is an error: *"`Crate` declares type parameters, so a
  complete type-argument list is required to construct it. Write `Crate[...].new()`."* There is
  no runtime object whose arguments are absent, because every object is an instance of some
  specialization.
- **(D4) The gradual store rule is removed.** Foundry Script accepts
  `var slot: Crate[int] = Crate.new()` because the source value's arguments are absent and
  absence is accepted gradually. D3 leaves no such value, so a store into a specialized slot is
  an ordinary nominal check: the value must be an instance of that specialization or one of its
  subclasses. `is`, `as`, and a declaration therefore all ask the *same* question, where Foundry
  Script has `is` strictly stronger than a store.
- **Raw `Crate` remains a legal type.** As an annotation, a bound, a container element type, and
  an `is`/`as` target, `Crate` names the raw base and therefore means "any specialization".
  `Crate[int].new() is Crate` is true. Reading a member off a raw receiver whose type mentions an
  unbound parameter is reported as an ordinary unsafe boundary, exactly as in Foundry Script.
- **The specialization set must be statically closed.** A specialization is emitted only if some
  source position names it. Because a type argument may itself be a specialization, a program can
  in principle demand infinitely many (`class Nest[T]` whose body names `Nest[Nest[T]]`). The
  compiler bounds specialization depth (`MAX_SPECIALIZATION_DEPTH`) and reports an error naming
  the cycle rather than expanding forever.

**Generic functions are unchanged.** A method's own type parameters are *not* monomorphized and
*not* reified in the call frame — precisely as in Foundry Script. `func swap[T](...)` compiles
once, `T` is erased at runtime, and a rest `Array` whose element type depends on a method type
parameter is runtime-erased. Only classes, traits, and named tagged unions monomorphize.

### 4.4 Variables, constants, signals, enums

```ebnf
variable_decl   = "var", identifier,
                  [ ":", ( type | (* inferred *) ) ],
                  [ "=", expression ],
                  [ property_clause ],
                  NEWLINE ;

constant_decl   = "const", identifier, [ ":", [ type ] ], "=", expression, NEWLINE ;

signal_decl     = "signal", identifier,
                  [ "(", [ parameter, { ",", parameter }, [ "," ] ], ")" ],
                  NEWLINE ;

enum_decl       = "enum", [ identifier, [ type_parameters ] ], ":", enum_body ;
enum_body       = NEWLINE, INDENT,
                  ( "pass", NEWLINE
                  | enum_value_line, { enum_value_line }, { enum_function_decl }
                  | enum_function_decl, { enum_function_decl } ),
                  DEDENT ;
enum_value_line = identifier, [ enum_case_payload ], [ "=", expression ], NEWLINE ;
enum_case_payload = "(", enum_payload_field, { ",", enum_payload_field }, [ "," ], ")" ;
enum_payload_field = identifier, ":", type ;   (* names are required on payload fields *)
enum_function_decl = { function_annotation }, { enum_function_modifier }, function_decl ;
enum_function_modifier = "static" | "async" ;
function_annotation = ANNOTATION, [ "(", [ annotation_args ], ")" ], [ NEWLINE ] ;
```

- A `var`'s type may be written explicitly (`var x: int = ...`), **inferred** when `:` is
  immediately followed by `=` (`var x := value`), or omitted entirely (`var x = value`). The same
  applies to `const` and parameters.
- Signal parameters may have a type annotation but **not** a default value.
- An **unnamed** enum (`enum:`) injects its values as constants into the enclosing class; a
  **named** enum (`enum Dir:`) defines an enum type. Only named enums may contain functions.
- Named enum declarations use the constant annotation target, allowing `@keep_name`; enum values
  remain unsupported annotation targets.
- Every value in a plain (non-tagged-union) enum must provide an explicit integer expression
  (`NAME = expression`). Values receive no implicit numbers, and members are separated by
  newlines rather than commas. Commas remain valid inside a value expression.
- A case may declare a **payload**: a parenthesized, comma-separated field list
  (`Move(x: int, y: int)`), reusing the tuple field form (§4.4a) except every payload field must
  be named. A bare positional payload field is a parse error, as is a duplicate field name within
  one case's payload. A trailing comma is allowed once the payload has at least one field.
- If **any** case declares a payload, the whole enum is a **tagged union** and `= expression` is
  a parse error on *every* case — tags are ordinal by declaration order (0-based). Cases with and
  without payloads may be freely mixed. Whether an enum is a tagged union is only known once its
  whole body is parsed, so this is validated after the body.
- A tagged union must be a **named** enum, because its cases are only reachable as `Name.Case`.
- A tagged union's values are `[tag, payload...]` read-only arrays, not integers, so a case value
  never participates in integer contexts. A payload-less case is a value on its own; a
  payload-carrying case is only usable when constructed with its declared field types
  (`Name.Case(argument, ...)`), and its payload fields are not reachable directly on a value of
  the union type.
- A tagged union's payload field types **may reference the union itself**, directly
  (`Link(next: Chain)`) or through a typed collection (`Branch(children: Array[Tree])`). Valid in
  both the inner `enum` and whole-file `enum_name` forms. Recursion terminates at runtime because
  a value is finite. Int-backed enums are unaffected.
- A named enum may declare **type parameters** between its name and the `:`
  (`enum Result[T, E: Resource]:`), making it a generic tagged union whose payload field types may
  reference those parameters. Type parameters on an unnamed enum, and on an enum whose completed
  body declares no payload-bearing case, are both rejected.
- Inside a generic tagged union's own declaration, the union names itself either bare
  (`Link(next: Tree)`) or with its exact parameter vector in declaration order; both spell the
  same open self type. A generic tagged union has no bare form anywhere else, and its type
  parameters shadow same-named class parameters while being shadowed by an enum function's own.
- Everywhere else a generic tagged union is named by **applying a full type-argument vector**, in
  a type position (`func take(value: Result[int, String])`) and in an expression position
  (`var handle = Result[int, String]`). The argument count must equal the declared parameter
  count, each argument must satisfy its bound, and a union declaring no parameters accepts none.
  Type arguments are invariant.
- **(D2)** A generic tagged union monomorphizes exactly as a generic class does (§4.3.1): each
  full argument vector is a distinct runtime type. Because a tagged union's values are read-only
  arrays rather than script instances, the specialization identity is carried by the union's
  declared type rather than by the array, so a specialized `is` against a *value* of a generic
  union tests the tag and the statically known specialization. A value that crosses a `Variant`
  boundary loses its specialization evidence and satisfies only the raw union on the way back.
  This is the one place monomorphization does not fully recover Foundry Script's reification.
- Enum values must appear before enum functions. A functions-only named enum is valid. Enum
  functions reuse ordinary signatures and bodies, allow `static` and `async`, and reject
  `abstract` and `final`. Variables, constants, signals, nested classes/enums/traits, and
  conformances are not valid enum-body declarations.
- The builtin `Result[T, E]` union ships at `barista://builtin/result.barista` with stable case
  order: `Ok` is tag 0 and `Err` is tag 1.
- An empty enum uses `pass` as its only body statement.
- `enum_name` (§3.2) declares a file-level named enum using the same indented body.

#### Property accessors

```ebnf
property_clause     = ":", ( property_block | inline_property ) ;
property_block      = NEWLINE, INDENT, accessor, { accessor }, DEDENT ;
inline_property     = accessor_setget, [ ",", accessor_setget ] ;

accessor            = getter_inline | setter_inline ;
getter_inline       = "get", [ "(", ")" ], ":", block ;
setter_inline       = "set", "(", identifier, ")", ":", block ;

accessor_setget     = "get", "=", identifier
                    | "set", "=", identifier ;
```

A property combines a backing variable with a getter and/or setter. Two styles exist: inline
bodies (`get:` / `set(value):` with indented blocks) and pointer style (`get = method`,
`set = method`). `get` and `set` may appear in either order. The style is chosen by whether `=`
follows the accessor name.

### 4.4a Tuple declarations

```ebnf
tuple_decl      = "tuple", identifier, "(", tuple_field, { ",", tuple_field }, [ "," ], ")",
                   NEWLINE ;
tuple_field     = [ identifier, ":" ], type ;   (* a bare type is a positional field *)
```

- Declares a named, fixed-arity tuple type at class-body level (single-line; no indented body).
  Each field is either **named** (`x: float`) or **positional** (a bare type); the two forms may
  be mixed freely (`tuple Player(name: String, int, bool)`).
- Arity must be at least 2. A trailing comma is allowed once arity is >= 2.
- A named field's identifier must be unique within the declaration.
- `tuple` is a fully reserved keyword token. The whole-file form uses `tuple_name` (§3.2).
- Named tuple declarations use the constant annotation target, allowing `@keep_name`.

### 4.4b Type alias declarations

```ebnf
type_alias_decl = "type", identifier, "=", type, NEWLINE ;
```

`type` is a **contextual keyword**: it is lexed as an ordinary identifier and only introduces an
alias where a member declaration is valid and the next token is an identifier. The decision is
exactly that two-token lookahead, which steals nothing, since no declaration position admits an
expression statement. `var type = 5`, `type = 5`, `type.value`, and `type(x)` all remain
ordinary uses of the identifier.

- Aliases may be declared at **file scope and in a class body only**.
- An alias takes **no type parameters**: `type Pair[T] = ...` is a parse error. A *member* may
  still be a generic specialization (`type Numbers = Array[int]`).
- An alias declares no runtime member, so **no annotation may be applied to it**.
- An alias is **transparent**: it names a type, it does not create one. The name is not an
  expression, not a constructor, and not an `extends`/`uses` target.
- An alias shares the **member name space** of the class body it is declared in.
- An alias is **file-local**: not a global name, not reachable through `import`/`namespace`.
- An alias name may not be a built-in type or native class name.
- A **type handle** cannot represent a union: `Type[A | B]` is an error.
- Alias visibility is **lexical, not nominal**: the name belongs to the body that declares it and
  to bodies nested inside it. Extending a class does not carry its aliases along. A retroactive
  conformance witness is written in the conformance's own file, so that file's aliases are in
  scope in the witness body even though the target type is declared elsewhere.

### 4.5 Functions and parameters

```ebnf
function_decl   = "func", identifier, [ type_parameters ],
                  "(", [ parameter_list ], ")",
                  [ "->", return_type ],
                  ( ":", block
                  | (* only when the "abstract" modifier is present *) NEWLINE ) ;

parameter_list  = param_item, { ",", param_item }, [ "," ] ;
param_item      = [ "..." ], parameter_annotation*, parameter ;
parameter       = identifier, [ ":", ( type | (* inferred *) ) ], [ "=", expression ] ;
parameter_annotation = ANNOTATION, [ "(", [ annotation_args ], ")" ] ;

return_type     = type | "void" ;
```

- At most one **rest** parameter (`...name`), which must be last and cannot have a default.
- A rest parameter describes the **collected array**, so its annotation must resolve to `Array`
  or `Array[T]`. The element-oriented spelling `...name: T` is not accepted.
- With `Array[T]`, `T` is the expected type of every **surplus** call argument: arguments past
  the fixed parameter list are checked against `T` under the same conversion, nullability,
  strict-dynamic and diagnostic rules as a fixed parameter, and the diagnostic names `T` rather
  than `Array[T]`. Inside the body the parameter has exactly the declared `Array[T]` type, and a
  call supplying no surplus arguments still receives a typed empty array whenever `T` is
  representable as a typed container.
- `...name`, `...name: Array`, and `...name: Array[Variant]` are gradual: surplus arguments are
  unconstrained, indistinguishable from a native untyped vararg.
- The rest parameter can never be passed **by name**. Surplus arguments are always positional,
  and no positional argument may follow a named one.
- The rest element type is **contravariant** across an override, an abstract requirement, a trait
  witness, a callable assignment, and a signal connection.
- A **generic** method infers its type parameters from every rest argument, so
  `func collect[T](...values: Array[T])` solves `T` with no fixed parameter, and an explicit type
  application (`collect[int]()`) supports an empty call. Method type arguments are not reified in
  the call frame (§4.3.1), so a rest `Array` whose element depends on a method type parameter is
  **runtime-erased**. A rest element that does not depend on a method type parameter stays typed.
- Parameters with defaults must follow parameters without defaults (except the rest parameter).
- `void` is allowed only as a return type.
- An **abstract** function has no body: the signature is terminated by `NEWLINE` instead of `:` +
  block. The `abstract` modifier is **required** for that form — a body-less `func` is an error
  even inside a trait, where every requirement must be written `abstract func`.
- `async func` declares a coroutine; `await` in a body also makes a function a coroutine.
- The special static constructor `_static_init` must be `static` and parameterless.

### 4.6 Annotations (usage)

```ebnf
class_annotation      = ANNOTATION, [ "(", [ annotation_args ], ")" ], [ NEWLINE ] ;
standalone_annotation = ANNOTATION, [ "(", [ annotation_args ], ")" ], NEWLINE ;

annotation_args       = annotation_arg, { ",", annotation_arg }, [ "," ] ;
annotation_arg        = [ identifier, "=" ], expression ;
```

- An annotation precedes the declaration or statement it applies to, or a method/signal parameter
  name in a parameter list. The newline after an annotation is optional.
- Named arguments (`name = value`) are accepted only for **custom** annotations and the built-in
  `@autoload`; all other built-ins are positional.
- Placement is validated against each annotation's allowed targets. See §8.

### 4.7 Custom annotation declarations

```ebnf
annotation_declaration = "annotation", identifier,
                         [ "(", [ annotation_decl_params ], ")" ],
                         "targets", target_list, NEWLINE ;

annotation_decl_params = adp_item, { ",", adp_item }, [ "," ] ;
adp_item               = [ "..." ], parameter ;

target_list            = target_name, { ",", target_name } ;
target_name            = "CLASS" | "METHOD" | "VARIABLE" | "SIGNAL" | "CONSTANT" | "PARAMETER" ;
```

`annotation` is contextual: it only starts a declaration at the **root** of a script where a
declaration is valid; elsewhere it is an ordinary identifier. The declaration defines a reusable
custom annotation with typed parameters (defaults must be constant) and a set of valid targets.

### 4.8 Retroactive conformance (`extend`)

```ebnf
conformance_declaration = "extend", conformance_target,
                          "uses", trait_use, { ",", trait_use },
                          ":", conformance_body ;
conformance_target = dotted_name ;          (* unspecialized; NO type_arguments *)
conformance_body = NEWLINE, INDENT, conformance_member, { conformance_member }, DEDENT
                 | conformance_member ;
conformance_member = { "static" | "async" }, function_decl ;
```

`extend` is contextual and root-only. A conformance declares that an existing type retroactively
conforms to one or more traits, supplying the required methods externally as witnesses.

The target must be **unspecialized**: writing type arguments is a parse error, because a
conformance applies to **all** specializations of a generic base. The `uses` clause reuses
`trait_use` from §3.3. The body contains **only** function / accessor members. Witness methods
may carry `static`/`async`. Inside the witnesses, `self` is typed as the target. For builtin
value-type targets (`extend int uses ...`), witness `self` is a copy for scalars and strings but
shares storage for `Array` and `Dictionary`.

**Targets.** A conformance target may be a BaristaScript class, a **native engine class**, a
builtin value type, or **(D9)** a GDScript class reachable by global `class_name`. Conformance
is answered by BaristaScript's runtime, so conforming a GDScript or native class does not make
the trait visible to that class's own language.

**Witness scope.** A witness signature and body resolve names in a **dual scope**: first the
target's own member/type scope (its members, its base chain, its inner types, `Self`, and the
target's existing lexical outer chain), and then — only for names the target scope did not
supply — the lexical **type** scope of the file declaring the `extend`. The target always wins on
a collision. The declaration-site half exposes only what a lexical outer class contributes as
*types*: inner classes and traits, enums, named tuples, and constants denoting a type. The
declaring file's variables, functions, signals, and plain value constants do **not** become
members of the target. Declaration order does not matter.

**Coherence.** Duplicate `(target, trait)` conformances are rejected, same file or cross-file.
**Witness method-name collisions** on the same target are also rejected: runtime witness dispatch
keys on `(target alias, method name)` only. Requirements satisfied by the target's own existing
methods without a supplied witness do not participate in this check.

**Inheritance-chain shadowing** is legal: conforming the same trait on a base type and on a
derived type is allowed. Witness dispatch walks the instance's class chain most-derived-first and
uses the first matching witness; `is`/`as` against the trait succeed if any level declares the
conformance. Colliding witness names on different levels follow this shadowing rule.

A conformance may not record arguments that differ from a binding already present on the target's
class chain. A position left on an unreified type parameter carries no argument and never
conflicts.

**Reach.** A conformance takes effect for the files that **load** its declaring file, not for the
whole project. A file loads it when it:

1. declares it itself;
2. `preload`s (or `extends`) the declaring file, directly or transitively; or
3. is in the same **named** namespace as the declaring file, or `import`s that namespace.

The global namespace is excluded from rule 3. Reach is a load edge, not just a name-resolution
one: a file reaching a conformance through rule 3 keeps that file loaded even though nothing in
its emitted code names it. A call to a witness supplied by a conformance the file does **not**
reach is an analysis error naming the declaring file — never an accepted call that misses at run
time.

---

## 5. Expressions (Pratt parser)

### 5.1 Algorithm

```
parse_precedence(min_prec, can_assign):
    token = current
    prefix = rule(token).prefix
    if prefix == null: return null            (* not the start of an expression *)
    advance()
    left = prefix(can_assign)
    while min_prec <= rule(current).precedence:
        token = advance()
        infix = rule(token).infix
        left = infix(left, can_assign)
    return left
```

- A full expression is parsed with `min_prec = PREC_ASSIGNMENT`.
- `can_assign` controls whether an assignment infix is permitted (assignment is only allowed as a
  statement-level expression, not nested).
- Binary operators are **left-associative**: a binary operator parses its right operand with
  `precedence + 1`. This uniform rule applies even to `**`, so `2 ** 3 ** 2` parses as
  `(2 ** 3) ** 2` — left-associative, unlike mathematical convention. The exceptions are the
  prefix unary operators (`-` `+` `~` `not` `!`, which recurse at their own precedence and are
  effectively right-associative) and the ternary (right-associative).

### 5.2 Precedence levels

From lowest to highest. Higher binds tighter.

| # | Level                       | Operators / forms (infix unless noted)             | Assoc. |
|---|-----------------------------|----------------------------------------------------|--------|
| 1 | `PREC_ASSIGNMENT`           | `=` `+=` `-=` `*=` `**=` `/=` `%=` `<<=` `>>=` `&=` `\|=` `^=` | right (stmt-level) |
| 2 | `PREC_CAST`                 | `as` (and the invalid `?` handler)                 | left |
| 3 | `PREC_TERNARY`              | `value if cond else value`                         | right |
| 4 | `PREC_LOGIC_OR`             | `or` `\|\|`                                        | left |
| 5 | `PREC_LOGIC_AND`            | `and` `&&`                                         | left |
| 6 | `PREC_LOGIC_NOT`            | `not` `!` (prefix)                                 | right |
| 7 | `PREC_CONTENT_TEST`         | `in`, `not in`                                     | left |
| 8 | `PREC_COMPARISON`           | `<` `<=` `>` `>=` `==` `!=`                         | left |
| 9 | `PREC_BIT_OR`               | `\|`                                               | left |
| 10| `PREC_BIT_XOR`              | `^`                                                | left |
| 11| `PREC_BIT_AND`              | `&`                                                | left |
| 12| `PREC_BIT_SHIFT`            | `<<` `>>`                                          | left |
| 13| `PREC_ADDITION_SUBTRACTION` | `+` `-`                                            | left |
| 14| `PREC_FACTOR`               | `*` `/` `%`                                        | left |
| 15| `PREC_SIGN`                 | unary `+` `-` (prefix)                             | right |
| 16| `PREC_BIT_NOT`              | unary `~` (prefix)                                 | right |
| 17| `PREC_POWER`                | `**`                                               | left  |
| 18| `PREC_TYPE_TEST`            | `is`, `is not`                                     | left |
| 19| `PREC_AWAIT`                | `await` (prefix)                                   | right |
| 20| `PREC_CALL`                 | `(` … `)` call                                     | left |
| 21| `PREC_ATTRIBUTE`            | `.` attribute access                               | left |
| 22| `PREC_SUBSCRIPT`            | `[` … `]` subscript / type args                    | left |
| 23| `PREC_PRIMARY`              | literals, identifiers, grouping, primaries         | —    |

> `**` sits at `PREC_POWER` and binds tighter than unary sign, so `-a ** b` parses as
> `-(a ** b)`. Because every binary operator recurses with `precedence + 1`, `**` is
> **left**-associative.
>
> A *numeric literal* is the exception, and deliberately: a `+` or `-` immediately followed by a
> digit is part of the literal whenever the preceding token cannot end a value (§2.6.1), so `-2`
> never becomes a unary operator for `**` to bind tighter than, and `-2 ** 2` is `(-2) ** 2`.
> Whitespace blocks that fold (`- 2 ** 2` is `-(2 ** 2)`), and a sign after a value-ending token
> stays binary even when glued to the digit (`a-2 ** 2` is `a - (2 ** 2)`). This is not a
> precedence rule and cannot be changed by one — it is what makes `-9223372036854775808`, the
> lower bound §7.1 states, writable at all: as unary minus applied to `9223372036854775808` the
> magnitude has no `int` to live in. Foundry Script and GDScript lex the adjacent fold the same
> way.

### 5.3 Prefix (null-denotation) forms

```ebnf
primary =
      LITERAL
    | identifier
    | "self"
    | "PI" | "TAU" | "INF" | "NAN"
    | unary_op
    | "(", expression, ")"                          (* grouping *)
    | tuple_literal
    | array_literal
    | dictionary_literal
    | lambda
    | "await", expression
    | "preload", "(", expression, [ "," ], ")"
    | get_node
    | contextual_enum_case
    | "super", super_tail                            (* only as call base *)
    ;

contextual_enum_case = ".", identifier ;

tuple_literal = "(", expression, ",", expression, { ",", expression }, [ "," ], ")" ;
                                                      (* arity >= 2; see §9 *)

unary_op = ( "-" | "+" | "~" | "not" | "!" ), expression ;
```

Token → prefix-rule mapping:

| Token              | Prefix meaning                                  |
|--------------------|-------------------------------------------------|
| `IDENTIFIER`       | identifier reference                            |
| `LITERAL`          | literal value                                   |
| `SELF`             | `self`                                          |
| `CONST_PI/TAU/INF/NAN` | numeric constant                            |
| `MINUS`/`PLUS`     | unary sign (`PREC_SIGN`)                         |
| `TILDE`            | bitwise complement (`PREC_BIT_NOT`)             |
| `NOT`/`BANG`       | logical not (`PREC_LOGIC_NOT`)                  |
| `PARENTHESIS_OPEN` | grouping, or a tuple literal if a `,` follows the first element |
| `BRACKET_OPEN`     | array literal                                   |
| `BRACE_OPEN`       | dictionary literal                              |
| `FUNC`             | lambda                                          |
| `AWAIT`            | await expression                                |
| `PRELOAD`          | preload expression                              |
| `DOLLAR`           | get-node (`$`)                                  |
| `PERCENT`          | get-node unique-name shorthand (`%`)            |
| `PERIOD`           | contextual tagged-union case (`.Case`)          |
| `SUPER`            | super call/access                               |
| `YIELD`            | error (removed)                                 |

`PERIOD` is the one token with both a prefix and an infix rule: in prefix position it starts a
contextual tagged-union case, in infix position it is attribute access. The two never compete,
because a prefix rule is only consulted where an expression may start. `.` followed by a digit
never reaches the prefix rule: in expression-start position the tokenizer produces a single float
`LITERAL` (`.5` is `0.5`), and after a value it produces the infix `PERIOD` of a tuple index.

A contextual case is accepted syntactically wherever an expression is, but is only *valid* where
the surrounding consumer supplies a complete specialized tagged-union expected type: a variable or
constant initializer with a declared type, a `return`, an assignment, a call argument (including a
variadic slot and a defaulted parameter), an element of a typed array literal, a key or value of a
typed dictionary literal, the operand of an `as` cast, or a branch of a conditional expression
whose expected type is a union. Inferred, untyped, and `Variant` targets are rejected. This is a
semantic requirement, resolved after parsing.

In a `match` case pattern (§6.1) and on the right of `is` (§5.5), the union is supplied by the
subject rather than by an expected type.

### 5.4 Infix (left-denotation) forms

| Token(s)                              | Infix rule          | Precedence            |
|---------------------------------------|---------------------|-----------------------|
| `<` `<=` `>` `>=` `==` `!=`           | binary comparison   | `PREC_COMPARISON`     |
| `and` `&&`                            | logical and         | `PREC_LOGIC_AND`      |
| `or` `\|\|`                           | logical or          | `PREC_LOGIC_OR`       |
| `not`                                 | `not in` content test | `PREC_CONTENT_TEST` |
| `in`                                  | content test        | `PREC_CONTENT_TEST`   |
| `&`                                   | bit and             | `PREC_BIT_AND`        |
| `\|`                                  | bit or              | `PREC_BIT_OR`         |
| `^`                                   | bit xor             | `PREC_BIT_XOR`        |
| `<<` `>>`                             | bit shift           | `PREC_BIT_SHIFT`      |
| `+` `-`                               | add/subtract        | `PREC_ADDITION_SUBTRACTION` |
| `*` `/` `%`                           | multiply/divide/mod | `PREC_FACTOR`         |
| `**`                                  | power               | `PREC_POWER`          |
| `=` and compound assignments          | assignment          | `PREC_ASSIGNMENT`     |
| `if`                                  | ternary             | `PREC_TERNARY`        |
| `as`                                  | checked cast        | `PREC_CAST`           |
| `is`                                  | type test           | `PREC_TYPE_TEST`      |
| `(`                                   | call                | `PREC_CALL`           |
| `.`                                   | attribute access    | `PREC_ATTRIBUTE`      |
| `[`                                   | subscript/type args | `PREC_SUBSCRIPT`      |
| `?`                                   | invalid (error)     | `PREC_CAST`           |

### 5.5 Specific expression forms

#### Ternary

```ebnf
ternary = expression, "if", expression, "else", expression ;
```

Parsed as an infix on `if`: the already-parsed left operand becomes the *true* branch, then the
condition, then `else`, then the *false* branch.

#### Assignment

```ebnf
assignment = assign_target, assign_op, expression ;
assign_target = identifier | attribute_access | subscript ;
assign_op  = "=" | "+=" | "-=" | "*=" | "**=" | "/=" | "%="
           | "<<=" | ">>=" | "&=" | "|=" | "^=" ;
```

Assignment is parsed as an expression for convenience but is only valid as a statement; the target
must be an identifier, attribute, or subscript. It is rejected inside other expressions.

#### Cast and type test

```ebnf
cast          = expression, "as", type ;
type_test     = expression, "is", [ "not" ], type_test_type, [ case_bind_list ] ;
type_test_type = type | contextual_enum_case ;
case_bind_list = "(", case_bind, { ",", case_bind }, ")" ;
case_bind     = identifier | "_" ;
```

`as` is the checked cast, and **(D1)** the only cast: there is no bit-reinterpret operator,
because BaristaScript has one integer type and therefore no width to reinterpret across.

`x is not int` is parsed as `not (x is int)`. The negation node records that it came from this
sugar, so the formatter reprints the source form; an author-written `not (x is int)` keeps its own
spelling. Nothing in the semantics distinguishes the two.

A `case_bind_list` may only follow a type naming a tagged-union case, `msg is Message.Move(x, y)`;
a case name is accepted in this position only, never as a type annotation. The list is rejected
after `is not`, must be non-empty, must not repeat a bind name, and must have exactly one entry per
payload field of the case (`_` skips a field). A bind-carrying test is only valid as the condition
of `if`, `elif`, `while`, or `assert`, either directly or as an operand of `and` within that
condition; the binds become locals of the guarded suite (of the enclosing suite for `assert`).
Without a bind list, `msg is Message.Move` tests the case tag, and `msg is Message` tests
membership in the enum.

The tested type may also be the contextual case shorthand (`msg is .Move(x, y)`, `opt is .None`).
It is only valid where the operand's type is a complete tagged-union specialization, names exactly
one case, and takes neither a type-argument list nor a longer dotted chain.

Because the binds of an `assert` outlive the assertion, a bind-carrying `assert` condition is still
evaluated in builds where assertions are stripped; only the failure check is removed.

##### (D1) Integer type tests

`is int` is an ordinary, fully supported type test: it asks whether the value is an integer, and
because `int` is the only integer type that question is complete. Foundry Script's carrier test
`is long` is spelled `is int` here, and it is what a plain enum's exhaustiveness rule (§6.1) tests
against.

`is uint`, `is ulong`, and `is long` are rejected by the reserved-name rule of §2.5 rather than by
any rule of their own. `Number` and every other multi-member union remain invalid `is` operands for
the reason they already are in Foundry Script: a union has no runtime carrier.

##### Generic type tests and casts

**(D2)** Because each specialization is a distinct runtime script, type arguments are observable
by `is`/`as` exactly as they are in Foundry Script. `as` succeeds exactly when the corresponding
`is` holds: on success it yields the original value unchanged, on failure `null`. Both forms are
shallow — they compare runtime type identity and never read members or walk container contents.

- A raw target asks only the nominal question. `Crate[int].new() is Crate` is true.
- A specialized target requires the value's script to be that specialization or a subclass of it.
  Arguments are invariant, so `Crate[int]` and `Crate[String]` are unrelated in both directions.
- Inheritance projects the value's specialization onto the tested base through the declared
  bindings, not by argument position. `class IntCrate extends Crate[int]` satisfies `Crate[int]`,
  and `class Derived[U] extends Crate[U]` specialized as `Derived[int]` satisfies `Crate[int]`.
- Instance tests (`value is Crate[int]`) and class-handle tests (`handle is Type[Crate[int]]`)
  apply the same rules to their respective runtime values.
- `Self` resolves to the frame's exact receiver before the rules above apply, at every nesting
  depth, so a method reached through `Crate[int]` accepts a `Crate[int]` for `is Self` and rejects
  a `Crate[String]` and a raw `Crate`.
- A generic **trait** target follows the same two questions, the nominal one being conformance
  rather than inheritance. With `trait Holder[T]` and `class StringHolder: uses Holder[String]`, a
  `StringHolder` satisfies `Holder` and `Holder[String]` but not `Holder[int]`. Conformance
  arguments project the same way inheritance does.
- A **retroactive** conformance records the arguments it declared, so a value conforming solely
  through it answers a specialized target from them. A conformance that supplied no arguments
  satisfies only the raw target.
- `null` is never an instance or a class handle, so it fails every such test.
- **(D3/D4)** Foundry Script's rule that "missing evidence fails" has no BaristaScript analogue for
  instances, because D3 leaves no instance with missing evidence. It survives for **generic tagged
  unions** whose values have crossed a `Variant` boundary (§4.4): such a value satisfies only the
  raw union.

##### (D4) Stores of specialized types

A store asks **the same question** `is` does. A specialized type in a declaration — a variable,
constant, parameter, member, return type, typed-container element, or tuple element — requires the
value to be an instance of that specialization or a subclass of it.

This is a deliberate simplification of Foundry Script, which accepts a value whose arguments are
absent and therefore has `is` strictly stronger than a store. D3 removes the possibility of an
instance with absent arguments, so the two questions coincide and Foundry Script's per-component
knownness rules are unnecessary. A `Variant` source is checked at run time under the same rule.

A **class-handle** declaration behaves identically: `Type[Crate[int]]` admits only a `Crate[int]`
handle.

##### Members of a raw generic receiver

Naming a generic class or trait without type arguments stays legal everywhere a *type* is written,
and by itself is never diagnosed. What is diagnosed is a *use*: a member whose type still names one
of the parameters the receiver never bound denotes a value nothing decides, so every typed boundary
it crosses is reported exactly as the equivalent `Variant` crossing is.

- The declaration is silent. `var crate: Crate = Crate[String].new()`, a raw bound, a raw `is`/`as`
  target, and a raw container element type all keep their meaning and produce no diagnostic.
- A value read out of such a receiver is unsafe where it enters a typed slot: passed as an
  argument, assigned to a declared destination, or returned from a function with a declared return
  type.
- The signal is the analyzer's ordinary unsafe-boundary reporting, so it follows the same warning
  configuration as every other unsafe crossing.
- Precision is per component. Where a member's type is `Pair[T, String]` and `T` is unbound,
  reading the `T` component is unsafe and reading the `String` component is not.
- A type parameter belongs to the declaration that declares it. A slot typed by another
  declaration's parameter that merely shares a name is still a boundary the undecided value
  crosses.
- A receiver whose arguments are known is unaffected. Inside a generic declaration its own
  parameters are in scope, so the declaration's own bodies are unaffected as well.

Note the difference from Foundry Script: there, a raw receiver could be *constructed*
(`Crate.new()`). Here (D3) it can only be *obtained* — by widening a specialized value to the raw
base type.

#### `await`

```ebnf
await_expr = "await", expression ;   (* operand parsed at PREC_AWAIT *)
```

Makes the enclosing function a coroutine.

#### Calls and named arguments

```ebnf
call            = callee, "(", [ call_args ], ")" ;
callee          = expression | "super" [ ".", identifier ] | generic_application ;
call_args       = call_arg, { ",", call_arg }, [ "," ] ;
call_arg        = [ identifier, "=" ], expression ;     (* named argument *)
generic_application = ( identifier | attribute_access ), "[", type_arg_list, "]" ;
```

- `super(...)` calls the parent method of the same name; `super.name(...)` calls a named parent
  method.
- A **named argument** is `identifier = value`. This is unambiguous because assignment is a
  statement, never an expression.
- `name[TypeArgs](...)` / `receiver.method[TypeArgs](...)` is explicit generic-method application.
- A named argument may target only a **fixed** parameter.
- **(D3)** `Handle.new(...)` on a generic type requires the handle to be specialized.

#### Attribute access

```ebnf
attribute_access = expression, ".", identifier ;
```

A broad set of keywords is accepted as the attribute name (node-name rule).

#### Tuple index access

```ebnf
tuple_index_access = expression, ".", decimal_integer_literal ;
```

Valid on any expression, not only a tuple-typed one. The tokenizer lexes a digit directly following
a value-preceded `.` as a bare decimal-integer `LITERAL` rather than a float, so `t.0` is
unambiguous. The parser folds this into the same subscript node as `attribute_access`/`subscript`,
marked distinctly. `t.0.1` is nested member access, never a float.

#### Subscript and use-site type arguments

```ebnf
subscript      = expression, "[", index_or_type_args, "]" ;
index_or_type_args = expression
                   | type_arg, { ",", type_arg }, [ "," ] ;   (* >1 arg, or "?" markers *)
type_arg       = ( expression | tuple_type_arg | signature_type_arg ), [ "?" ] ;

tuple_type_arg     = "(", type_arg, ",", type_arg, { ",", type_arg }, [ "," ], ")" ;
signature_type_arg = ( "Callable" | "AsyncCallable" ),
                     "[", "[", [ type_arg, { ",", type_arg } ], "]", ",", type_arg, "]"
                   | "Signal", "[", "[", [ type_arg, { ",", type_arg } ], "]", "]"
                   | "Coroutine", "[", type_arg, "]"
                   | "Type", "[", type_arg, "]" ;
```

A single index is ordinary subscription. When the bracket list contains commas and/or a trailing
`?` nullable marker (`Pair[int, String]`, `id[Node?]`), it is a use-site type-argument list. The
first element always aliases the index. Parsing stops the index expression before a trailing `?` so
the nullable marker is not consumed as the invalid `?` operator.

A type argument is a type, so the spellings a type has that an expression does not are admitted
here too: an unnamed tuple type (`Box[(int, String)]`) and the built-in signature forms. These
shapes are read out of the expression grammar — a tuple type is lexed as a tuple literal and a
signature's parameter list as an array literal — and re-read as types once the head is known to take
type arguments. The two type-only spellings with no value-position form are `void` and a `Callable`
rest tail (`...Array[T]`); neither is spellable as a type argument.

#### Array and dictionary literals

```ebnf
array_literal      = "[", [ expression, { ",", expression }, [ "," ] ], "]" ;

dictionary_literal = "{", [ dict_entry, { ",", dict_entry }, [ "," ] ], "}" ;
dict_entry         = python_entry | lua_entry ;
python_entry       = expression, ":", expression ;       (* { key: value } *)
lua_entry          = ( identifier | STRING ), "=", expression ;  (* { key = value } *)
```

A dictionary uses **one** style consistently: Python (`key: value`) or Lua-table (`key = value`).
The style is decided by the first entry's separator; mixing is an error. In Lua style the key must
be an identifier or string literal and is treated as a constant `StringName`.

#### Lambdas

```ebnf
lambda = "func", [ identifier ], "(", [ parameter_list ], ")",
         [ "->", return_type ], ":", block ;
```

A lambda is `func` used as an expression, optionally named. Its body is a suite; the tokenizer
cooperates so a multi-line lambda body can appear inside a larger expression.

#### Get-node (`$` / `%`)

```ebnf
get_node       = "$", node_path
               | "%", node_path                  (* unique-name shorthand *)
               ;
node_path      = ( STRING | node_segment ), { "/", path_part } ;
path_part      = [ "%" ], ( STRING | node_segment ) ;
node_segment   = node_name ;
```

`$node/child`, `$"quoted/path"`, `%UniqueName`, and `$%Unique/child` are all valid. A leading `%`
marks a unique-name lookup and is only valid at the start of a name (after `$` or `/`).

#### Preload

```ebnf
preload_expr = "preload", "(", expression, [ "," ], ")" ;
```

`preload` takes a single resource-path expression (a trailing comma is tolerated). **(D9)** The
path may name a `.gd` file; the result is the GDScript class, usable as a base or a value.

### 5.6 Call evaluation order

Evaluating a call is observable whenever any of its sub-expressions has a side effect, so the order
is normative:

1. the callee **receiver** expression, when the call form has one;
2. the **argument** expressions, in source (left-to-right *written*) order;
3. any argument validation or conversion the call site injects;
4. the dispatch itself.

`a.f(b)` therefore evaluates `a` and then `b`, matching Python, C#, Kotlin, Java, and JavaScript.
Named arguments bind to parameters by name, but their expressions still evaluate in written order.

Because the receiver precedes step 3, a call rejected by an injected argument check still runs the
receiver expression first.

Call forms without a receiver expression — `super.method(...)`, a self or static call, a builtin
construction, and a utility-function call — start at step 2.

---

## 6. Statements

```ebnf
statement =
      pass_stmt
    | var_decl_stmt
    | const_stmt
    | destructure_stmt
    | if_stmt
    | for_stmt
    | while_stmt
    | match_stmt
    | break_stmt
    | continue_stmt
    | return_stmt
    | breakpoint_stmt
    | assert_stmt
    | annotation_stmt
    | expression_stmt ;

block          = NEWLINE, INDENT, statement, { statement }, DEDENT
               | statement ;                       (* single-line / inline suite *)
```

A statement ends at `NEWLINE`, `;`, or end-of-file. Multiple statements may be separated by `;` on
one line.

```ebnf
pass_stmt       = "pass", NEWLINE ;
break_stmt      = "break", NEWLINE ;               (* only inside a loop *)
continue_stmt   = "continue", NEWLINE ;            (* only inside a loop *)
breakpoint_stmt = "breakpoint", NEWLINE ;
return_stmt     = "return", [ expression ], NEWLINE ;

var_decl_stmt   = [ "final" ], "var", identifier,
                  [ ":", ( type | (* inferred *) ) ], [ "=", expression ], NEWLINE ;
const_stmt      = "const", identifier, [ ":", [ type ] ], "=", expression, NEWLINE ;

destructure_stmt = ( "var" | "const" ), "(", destructure_binding,
                   ",", destructure_binding, { ",", destructure_binding }, [ "," ],
                   ")", "=", expression, NEWLINE ;
destructure_binding = identifier | "_" ;

assert_stmt     = "assert", "(", expression, [ ",", expression [ "," ] ], ")", NEWLINE ;

if_stmt         = "if", expression, ":", block,
                  { "elif", expression, ":", block },
                  [ "else", ":", block ] ;

while_stmt      = "while", expression, ":", block ;

for_stmt        = "for", identifier, [ ":", type ], "in", expression, ":", block ;

annotation_stmt = ANNOTATION, [ "(", [ annotation_args ], ")" ], [ NEWLINE ] ;

expression_stmt = expression, NEWLINE ;
```

Notes:

- Local `var` may be `final var`. Local `final const` is rejected (`const` is already immutable).
- A destructuring declaration is a statement only; it has no class-body form. It is chosen over
  `var_decl_stmt`/`const_stmt` purely by a `(` following `var`/`const`.
- It must bind at least two elements, a trailing comma is allowed, and an initializer is mandatory.
  A `_` binding discards its element; repeating `_` is allowed.
- Bindings carry no type annotation: each takes the static type of the tuple element it reads. The
  initializer must be a statically known tuple of exactly the bound arity; `Variant` and every
  non-tuple type are rejected.
- `const` bindings are write-once locals, not compile-time constants.
- Nested destructuring, per-binding annotations, and destructuring in `for` are not part of the
  language.
- `for` may bind a typed loop variable (`for i: int in ...`).
- `assert` takes a condition and an optional message.
- Statement-level expressions are typically calls, assignments, or `await`; a bare standalone
  expression triggers a warning (except string "comments").

### 6.1 Match statement and patterns

```ebnf
match_stmt   = "match", expression, ":", NEWLINE, INDENT,
               { match_branch | "pass" NEWLINE | match_branch_annotation },
               DEDENT ;

match_branch = pattern, { ",", pattern },
               [ "when", expression ],            (* pattern guard *)
               ":", block ;

pattern      =
      "var", identifier                            (* bind *)
    | "_"                                           (* wildcard *)
    | ".."                                          (* rest (array/dict only) *)
    | "[", [ pattern, { ",", pattern } ], "]"       (* array pattern *)
    | "{", [ dict_pattern_entry, { ",", dict_pattern_entry } ], "}"  (* dict pattern *)
    | "(", pattern, ")"                             (* grouping *)
    | tuple_pattern
    | case_pattern
    | expression ;                                  (* literal or value pattern *)

tuple_pattern = "(", pattern, ",", [ pattern, { ",", pattern } ], [ "," ], ")" ;

case_pattern = case_reference,
               "(", case_payload_pattern, { ",", case_payload_pattern }, [ "," ], ")" ;

case_reference = qualified_case_reference | contextual_enum_case ;

qualified_case_reference = identifier, { ".", identifier }, [ case_type_arguments ],
                           ".", identifier, { ".", identifier } ;

case_type_arguments = "[", type_arg, { ",", type_arg }, "]" ;

case_payload_pattern =
      identifier                                    (* payload bind, no "var" needed *)
    | pattern ;

dict_pattern_entry =
      ".."                                          (* rest *)
    | expression, [ ":", pattern ] ;                (* key [: value pattern] *)
```

Rules:

- A branch may list multiple comma-separated patterns; a variable bind (`var x`) cannot be combined
  with multiple patterns. This includes payload binds of a case pattern.
- `..` (rest) is valid only inside array/dictionary patterns and must be last.
- A `when` guard adds a boolean condition; pattern binds are in scope in the guard and the body.
- Only `@warning_ignore` annotations are allowed on match branches.
- An expression pattern ordinarily matches by type-and-value equality against the subject, with one
  exception: when the expression is a type test whose operand is the **same identifier** as the
  match subject, and the subject itself is a plain identifier, the pattern is that type test. The
  subject then narrows to the tested type inside the branch, exactly as inside `if value is T:`. An
  unrelated operand, a non-identifier subject, and the negated form `value is not T` are all errors.
  The `is` operand rules of §5.5 apply unchanged, so a multi-member union on the right of `is`
  remains an error here too, and so does a reserved integer spelling (§2.5).
- A tuple pattern has arity >= 2 and matches element by element; `(p)` is grouping and `(p,)` is an
  error. Because a tuple erases to a read-only `Array`, an array pattern of the same arity
  (`[a, b]`) also matches a tuple value.
- A case pattern is a dotted name **immediately** followed by `(`, naming a tagged-union case; its
  sub-pattern count must equal the case's payload arity. A payload-less case is matched as the
  ordinary value it is (`Message.Quit`), without parentheses.
- The head may also be the contextual shorthand (`.Ok(value)`), naming a case of the union the
  subject already has. A payload-less contextual case (`.None`) is likewise matched without
  parentheses.
- A **generic** tagged union's case pattern applies the union's full type-argument vector on the
  head, before the case name: `Result[int, String].Ok(value)`. The payload binds take the
  specialized field types, and a pattern from a different specialization than the subject is
  rejected. Only the `(` following the dotted name tells a case reference apart from an indexed
  value pattern (`TABLE[INDEX]`), so `case_type_arguments` is the same `type_arg` list a subscript
  carries.
- A case reference carries its arguments on the name that owns them, which is the **last** name of
  the qualified head: `Outer.Result[int, String].Ok(...)`.
- Directly inside a case pattern's parentheses a bare identifier is a payload bind; `_` skips the
  position and any other expression stays a value pattern. Nested patterns follow the ordinary
  pattern rules, so a bind inside one needs `var` (`Shape.Rect((var w, var h))`).
- Exhaustiveness over a tagged union counts a case as handled by a bind or wildcard branch, by a
  payload-less case value, or by a case pattern whose sub-patterns are all irrefutable
  (binds/wildcards, recursively). A refutable sub-pattern such as `Move(0, y)` covers nothing.
- A guarded branch never contributes coverage, and a `match` over a nullable subject is exhaustive
  only when a `null` pattern (or a wildcard) is also present.
- A **plain enum is an open, integer-backed domain**. Its declared members name values; they are not
  proof that no other integer inhabits an enum-typed slot, since a cast such as `99 as Level` warns
  and proceeds. A `match` over a plain enum is therefore exhaustive only through a branch that
  cannot fail: an unguarded wildcard or bind, an unguarded `value is Variant` test, or an unguarded
  test against the enum's whole integer carrier (`value is int`), which admits the undeclared
  values as well. Listing every member, handling extra integer literals, or testing `value is Level`
  all leave the undeclared carrier values unhandled. Tagged unions and `bool` remain closed domains.
- An unguarded same-subject type-test pattern covers the whole subject when the tested type accepts
  every value the subject can hold, and the branch then counts exactly as a wildcard does. That is
  decided in three shapes: any subject tested against `Variant`; a closed domain tested against its
  own type — a non-nullable `bool` against `bool`, and a non-nullable tagged union against its own
  type; and a non-nullable plain enum tested against the whole of its integer carrier
  (`value is int`). Every other test covers nothing.
- Exhaustiveness is normative for flow analysis, not only for diagnostics: a `match` over a closed
  domain whose branches cover the whole domain and all terminate is itself terminating, so a
  value-returning function needs no trailing `return` after it.

---

## 7. Types

```ebnf
type = type_member, { "|", type_member } ;           (* `|` is the loosest type operator *)

type_member =
      "void"                                         (* only where allowed: return type *)
    | identifier, { ".", identifier }, [ type_suffix ], { ".", identifier }, [ "?" ]
    | tuple_type ;

type_name = identifier, { ".", identifier } ;

tuple_type = "(", type, ",", type, { ",", type }, [ "," ], ")", [ "?" ] ;

type_suffix =
      collection_args                                (* Array[int], Dictionary[String, int] *)
    | callable_signature                             (* Callable[[...], R], AsyncCallable[...] *)
    | signal_signature                               (* Signal[[...]] *)
    | coroutine_arg                                  (* Coroutine[T] *)
    | type_handle_arg ;                              (* Type[T] *)

collection_args   = "[", type, { ",", type }, "]" ;
callable_signature= "[", "[", [ callable_parameter_list ], "]", ",", type, "]" ;
callable_parameter_list
                  = callable_parameter, { ",", callable_parameter } ;
callable_parameter= type
                  | "...", type ;                    (* rest tail; final and at most once *)
signal_signature  = "[", "[", [ type, { ",", type } ], "]", "]" ;
coroutine_arg     = "[", type, "]" ;                 (* exactly one; void allowed *)
type_handle_arg   = "[", type, "]" ;                 (* exactly one *)
```

Details:

- A trailing `?` marks the type **nullable** (`Node?`, `Array[int]?`, `Callable[...]?`).
- **Type unions.** `A | B` denotes the static set of its alternatives. `|` is contextual: it unions
  types only while a type is being parsed, and expression-level `|` keeps its own precedence and
  meaning as bitwise OR. Within a type, `|` is the **loosest** operator — looser than `?` — so
  `int? | String` is the union of a nullable `int` and a `String`. There is no parenthesized type
  form, because `(A, B)` is already an unnamed tuple, so `(int | String)?` is not spellable; a
  nullable union is written by marking any one member. A union with a missing alternative is a parse error.
- **Union members.** Any type may be a member — built-ins, native/script classes, traits, enums and
  tagged unions, tuples, generic specializations, `Type[T]` handles, nullable forms of these, and
  aliases — except `void`, `Variant` (which already admits every type), and a **bare type
  parameter**. A type parameter still nests freely inside a member, so `Array[T] | int` is valid.
- **Union normalization.** A union denotes a canonical set: alias members are expanded, nested
  unions flattened, nullability **hoisted** onto the union, duplicates removed, and member order
  canonicalized. So `int? | String` and `int | String?` are the same type, as are `int | String`
  and `String | int`. A set normalizing to a **single** member is that member, indistinguishable
  from it thereafter, so `type Meters = float` behaves exactly like `float`.
- **Runtime erasure.** A multi-member union has no runtime representation: it produces no typed
  local, no typed parameter check, no runtime type test, and a `PropertyInfo` of `Variant::NIL`. A
  single-member alias keeps the member's runtime typing in full. *(Unchanged from Foundry Script —
  unions already erase there, so nothing was lost.)*
- Two consequences follow. A multi-member union is **not a valid typed-container element type**, and
  it is **not a valid `is` or `as` operand type**; an individual alternative is named instead.
- **Union compatibility.** A concrete value satisfies a union slot when it satisfies at least one
  alternative. A union-typed value satisfies a concrete slot only when **every** alternative does.
- A **type suffix binds to the last name of the dotted head**, so `Outer.Box[int]` applies `[int]`
  to `Box`. A type carries **at most one** suffix.
- Only `collection_args` is reachable after a dotted head. The built-in suffix forms stay
  **unqualified**, so `A.Callable[[int], bool]` is a parse error.
- A dotted tail may follow the suffix, and in practice only `collection_args` is ever followed by
  one: the tail applies a generic tagged union before naming one of its cases
  (`Result[int, String].Ok`). That tail is admitted only where a case reference is.

### 7.1 The integer type (D1)

BaristaScript has **one integer type, `int`**. It is the signed 64-bit `Variant::INT` carrier, with
inclusive range `-9223372036854775808` … `9223372036854775807` — identical to GDScript's `int`, so
an integer crossing the GDScript boundary (D5) needs no conversion and loses nothing. The lower
bound is writable only as a folded signed literal (§2.6.1, §5.2): `-9223372036854775808` is one
token; `- 9223372036854775808` (unary minus plus a positive magnitude) is out of range.

`uint`, `ulong`, and `long` are **reserved type names** (§2.5): recognized in type position and
always rejected, never silently treated as user identifiers in a type. Integer literal suffixes
(§2.6.1) and the `as!` operator (§2.8) are reserved on the same basis. §0.4 records the evidence
behind removing the tower rather than degrading it.

What follows from having one integer type:

- **Conversions.** `int` → `float` is implicit and value-preserving. `float` → `int` requires `as`
  and truncates toward zero. There are no integer-to-integer conversions, because there is one
  integer type.
- **Overflow.** Arithmetic wraps on the 64-bit carrier, as GDScript's does. A *constant expression*
  that provably overflows is a compile error; a runtime overflow is not checked.
- **Type tests.** `is int` is an ordinary complete test (§5.5).
- **Typed containers.** `Array[int]` and `Dictionary[String, int]` record `Variant::INT` and are
  enforced by the engine itself, exactly as GDScript's are. There is no element width to lose.
- **`Number`** is a compiler-provided, globally visible type naming the union of the numeric types:
  `int` and `float`. It is closed — no user declaration joins it. Like any other multi-member union
  it is type-position-only and erases at runtime (§7), which makes it useful as a type-parameter
  bound (`[T: Number]`) and invalid as an `is` operand. A declaration reusing the spelling
  (`class_name Number`, `type Number = ...`) is reported at its own declaration.
- **Protobuf.** Every protobuf integer field — `int32`, `int64`, `uint32`, `uint64`, `fixed64` and
  the rest — maps to `int`. This is what `protoc-gen-foundryscript` already does today, so the
  generated-code shape is unchanged. A `uint64` field carrying a value at or above `2^63` arrives
  with its bit pattern intact but reads as negative; a generator targeting such a field should emit
  an accessor pair rather than relying on the raw value.

### 7.2 Other type forms

- **Typed collections**: `Array[int]`, `Dictionary[String, int]` — one or more comma-separated
  element types. `void` is not allowed as an element type. **(D2)** An element type may be a
  monomorphized specialization (`Array[Crate[int]]`), which the engine records as an ordinary
  script-typed container.
- **`Callable[[P1, P2], R]`** — a parameter-type list in inner brackets, a comma, then the return
  type (`void` allowed). `AsyncCallable[...]` is the same shape but flagged async.
- **Variadic callable `Callable[[P1, ...Array[T]], R]`** — the parameter list may end with one rest
  entry spelled `"..."` followed by a type. Optional, at most once, must be **final**, and is
  **Callable-only**. Its type must resolve to `Array` or `Array[T]`. A middle-position rest entry, a
  repeated one, and a rest entry on a `Signal` are parse errors. An untyped `...Array` tail marks
  the callable variadic but stays gradual.
- **`Signal[[P1, P2]]`** — a parameter-type list only; no return type, always fixed arity.
- **`Coroutine[T]`** — exactly one result type (`void` allowed), the typed handle to an in-flight
  async computation.
- **`Type[T]`** — exactly one represented instance type (a class/type handle).
- **Unnamed tuple type `(T1, T2, ...)`** — structural, arity >= 2; a trailing comma allowed once
  arity is >= 2. An empty `()` or single-element `(T)` tuple type is a parse error. Nesting is
  allowed: `((int, int), bool)`.
- Inner type nesting is depth-bounded to avoid stack overflow on pathological input.

Types appear in: variable/constant/parameter annotations, `for` loop variable annotations, return
types, casts (`as type`), type tests (`is type`), `extends`/`uses` type arguments, and
type-parameter bounds.

### 7.3 `Self`

`Self` is a contextual type name, not a keyword: an ordinary identifier that only has this meaning
inside a class, trait, or `extend` body. It is written wherever a `type_name` is, may not be
qualified (`Self.Inner`) or specialized (`Self[T]`), and nests like any other type: `Array[Self]`,
`Dictionary[String, Self]`, `Type[Self]`, `Callable[[Self], Self]`, `Crate[Self]`.

`Self` also appears in **expression** position inside a function body, denoting the class handle of
the receiver the running call was made through. `Self.new()` is the ordinary class-handle
construction form, and its result type is `Self`.

`self` is the receiver itself, typed `Self` in every body of a class, unconditionally. The type of
the expression is a property of the expression: it never depends on whether the enclosing function
happens to name `Self` elsewhere. The two receivers that are not an open instance of the enclosing
class are typed as what they are: an enum host types `self` as the enum value, and a builtin
conformance target types it as the builtin it extends.

**Receiver rule.** For a static call made through a class handle whose exact runtime type is `R`,
every occurrence of `Self` in the invoked implementation denotes `R`, at every nesting depth. This
holds when method lookup selects an implementation declared on an ancestor, and equally when it
selects a retroactive conformance witness.

- `Derived.make()` uses `Derived`, even when `make` is inherited from `Base`.
- An explicit `Base.make()` uses `Base`.
- An inherited implementation delegating through an unqualified call or `super` preserves the
  incoming receiver.
- **(D2)** A specialized generic receiver keeps all of its concrete type arguments in `Self`, which
  under monomorphization is simply the identity of the specialization script.
- In an instance method, `Self` is the class the running member was materialized for.

Runtime validation uses that exact specialization. Argument and return checks, typed containers,
generic arguments, class handles, construction, casts, and type tests all resolve against it, and
none widen `Self` to `Variant`, to the declaring class, or to the conformance target.

**Parameter positions.** What satisfies a parameter whose type mentions `Self` follows from how
exactly the call site knows the receiver.

- When the receiver is exact — a call through a class handle, or an instance of a `final` class —
  `Self` denotes that class, and a parameter mentioning it admits exactly the values that class
  admits, in every position and container.
- When the receiver is open — an instance typed as the declaring class, an overriding subclass, a
  conformance target, or `self`/`super` — `Self` denotes the runtime leaf, which the call site does
  not know. A value merely typed as the static class is rejected. What is admitted instead is the
  receiver itself, and identity must be provable where the call is written.
- Tuple element positions carry that rule inward, positionally and recursively. Nullability is
  unwrapped first, so a `(int, Self)?` parameter also admits `null`.
- Typed containers and generic type arguments of `Self` are checked as a carrier's declared element
  type, invariantly. A carrier written at the call site out of `self`, such as `take_items([self])`,
  is admitted.

Whether a parameter is satisfied never depends on the calling function's own signature or body.

**Deferred execution.** The receiver rule is unaffected by a call and its execution being separated
in time.

- Reading a static function off a class handle without calling it yields a callable paired with that
  exact receiver. A callable extracted from a specialized generic handle keeps that specialization.
- A call that suspends resumes with the receiver it began on.
- A callable or resumption whose receiver is gone is an error, never a call resolved against the
  declaring class.
- A lambda created while a static frame runs captures that frame's receiver lexically, immutably.

---

## 8. Built-in annotations

| Annotation | Targets | Notable args |
|------------|---------|--------------|
| `@tool` | script | — |
| `@icon` | script | `icon_path` |
| `@static_unload` | script | — |
| `@autoload` | script | `depends_on`, `order_id` (named args allowed) |
| `@keep_name` | class, variable, function, signal, constant, named enum | — |
| `@noreturn` | function | — |
| `@onready` | variable | — |
| `@export` | variable | — |
| `@export_enum` | variable | `names...` (vararg) |
| `@export_file` / `@export_file_path` / `@export_dir` | variable | optional `filter` |
| `@export_global_file` / `@export_global_dir` | variable | optional `filter` |
| `@export_multiline` / `@export_placeholder` | variable | text/placeholder |
| `@export_range` | variable | `min, max, step, extra_hints...` |
| `@export_exp_easing` | variable | `hints...` |
| `@export_color_no_alpha` | variable | — |
| `@export_node_path` | variable | `type...` |
| `@export_flags` | variable | `names...` |
| `@export_flags_2d_render` / `_2d_physics` / `_2d_navigation` | variable | — |
| `@export_flags_3d_render` / `_3d_physics` / `_3d_navigation` | variable | — |
| `@export_flags_avoidance` | variable | — |
| `@export_storage` | variable | — |
| `@export_custom` | variable | `hint, hint_string, usage` |
| `@export_tool_button` | variable | `text, icon` |
| `@export_category` / `@export_group` / `@export_subgroup` | standalone | `name[, prefix]` |
| `@warning_ignore` | class-level + statement | `warning...` (vararg) |
| `@warning_ignore_start` / `@warning_ignore_restore` | standalone | `warning...` (vararg) |
| `@rpc` | function | `mode, sync, transfer_mode, transfer_channel` |

`@deprecated`, `@experimental`, and `@tutorial` are intentionally **not** annotations; they are
written in `##` doc comments and produce errors if used as annotations.

**(D8) `@autoload`.** The annotation is retained with its full syntax, including `depends_on` and
`order_id`, but a GDExtension cannot register an autoload at load time. It is applied by a **build
step**: the compiler records every `@autoload` declaration in its project index, topologically sorts
them by `depends_on` and `order_id`, and writes the resulting order into `project.godot`'s
`[autoload]` section. A stale or hand-edited `[autoload]` section that disagrees with the index is
reported as a project error naming both.

**(D5) `@export` and traits.** A variable typed by a trait, by a generic specialization, by a tagged
union, or by a tuple cannot be exported, because the engine's Inspector has no way to describe it.
`@export` on such a variable is an error naming the type. `@export_storage` is permitted on them,
since it only asks for serialization.

---

## 9. Parsing notes and ambiguities

- **Single-token lookahead.** The parser uses only the current token and one buffered lookahead.
- **Recursion bounds.** Expression, statement, type, and pattern nesting are each bounded so deeply
  nested input reports an error instead of overflowing the native stack. **(D2)** Generic
  specialization depth is separately bounded (§4.3.1).
- **`|` in a type vs. bitwise OR.** `|` is a type-union operator only while a type is being parsed.
  Because the right-hand side of `is` and `as` is a type position, `x is int | String` unions the
  two types there rather than applying bitwise OR — and is then rejected for the ordinary reason
  that a union has no runtime carrier.
- **`?` nullable vs. operator.** `?` is only valid as a type suffix. Subscript index parsing stops
  before a trailing `?` so `Box[Node?]` works; a bare `?` elsewhere is an error pointing to the
  `if/else` ternary.
- **Named arguments are unambiguous** because assignment is never an expression: inside an argument
  list, `IDENTIFIER` `=` is always a named argument, while `==` is comparison.
- **Dictionary style detection** is based on the first entry's separator (`:` vs `=`).
- **Contextual keywords** (`annotation`, `extend`, `type`, `async`, `targets`, `get`, `set`, `Self`,
  `CLASS`/`METHOD`/`VARIABLE`/`SIGNAL`/`CONSTANT`/`PARAMETER`) are lexed as identifiers and only gain
  meaning from position. `extend` is distinct from the reserved `extends` keyword token.
- **Multiline mode** inside brackets and around lambda bodies suspends layout-token generation.
- **`.` after a value token is always member access, never a float.** After an `IDENTIFIER`, a
  literal, `)`, `]`, or a numeric constant keyword, `.<digit>` lexes as `PERIOD` then a plain
  decimal-integer literal, so `t.0`, `t.0.1`, and `(f()).0` are member access, while `.5` at the
  start of an expression is still the float `0.5`.
- **`(a)` is grouping; `(a,)` and `()` are errors.** BaristaScript has no 1-tuples or empty tuples.
  The parser only commits to the tuple-literal shape once it sees a `,` after the first element. The
  same arity-2 minimum applies to the unnamed tuple *type* (§7), a `tuple Name(...)` *declaration*
  (§4.4a), and a destructuring binding list (§6).
- **`Crate[int]` in expression position** is a specialized class handle, not a subscript, once the
  head resolves to a generic declaration. The bracket contents are re-read as types at that point,
  exactly as §5.5 describes for use-site type arguments.

---

## 10. Host integration constraints

This section is not grammar. It records the GDExtension facts that the language design above
depends on, so that a change to either is checked against the other.

BaristaScript reaches Godot only through:

| Host class | Used for |
|---|---|
| `ScriptLanguageExtension` | language registration, validation, completion, highlighting, debugger, profiler |
| `ScriptExtension` | one compiled `.barista` script: members, signals, properties, RPC config, base type |
| `ScriptInstanceExtension` | one live object's property get/set, method dispatch, notifications |
| `ResourceFormatLoader` / `ResourceFormatSaver` | reading and writing `.barista` files |
| `EditorPlugin` / `EditorExportPlugin` | the `@autoload` build step (D8), bytecode export |

What that surface **does** provide, and which language features therefore work unchanged: script
properties and the Inspector (`_get_script_property_list`), signals (`_get_script_signal_list`),
methods and their metadata (`_get_script_method_list`, `_get_method_info`), constants
(`_get_constants`), RPC configuration (`_get_rpc_config`), documentation (`_get_documentation`),
class icons (`_get_class_icon_path`), `@tool` scripts, and the global-class table for non-generic
`class_name` declarations.

What it **does not** provide, and which therefore constrains the language:

1. **No new `Variant` type.** A language cannot add an unsigned integer carrier. → D1, together
   with the adoption evidence in §0.4.
2. **No `type_arguments` on `ContainerType`.** → D2, and with it D3 and D4.
3. **No trait concept on `Script`.** → D5, and the `@export` restriction in §8.
4. **No `is_trait` / `is_enum` flag on the global-class table.** → D6.
5. **No native class namespaces in `ClassDB`.** → D7.
6. **No load-time autoload registration.** → D8.

D1 is the only delta the host constraint does not fully determine: a four-name tower over one
carrier was possible, and was rejected on adoption evidence rather than necessity (§0.4).

Two host facilities BaristaScript must implement for itself rather than inherit, because the engine
reserves them for its built-in languages:

- **Coroutines.** `await` cannot use the engine's internal script-function-state machinery. The VM
  owns its own suspended-frame object, resumed by a signal connection or by an awaited
  `Coroutine[T]`, and surfaced to the engine as an ordinary `RefCounted`.
- **Witness and trait dispatch.** `is`/`as` against a trait, and retroactive conformance lookup, are
  answered by BaristaScript's own registry keyed on `(target, trait)` and `(target alias, method
  name)`, never by `ClassDB`.

---

## 11. Keeping this document in sync

This file is a normative specification. **Whenever a change to BaristaScript affects its grammar** —
adding/removing/renaming tokens or keywords, changing operator precedence or associativity, altering
statement/declaration/type/expression/pattern syntax, or changing the built-in annotation set —
update this document in the **same** change.

Two additional obligations follow from BaristaScript being a derivative:

1. **Any new divergence from Foundry Script must be added to the §0.2 delta table**, with the host
   limitation that forces it named in §10. A divergence that no host limitation forces is a design
   change and needs its own justification.
2. **When Foundry Script's `GRAMMAR.md` changes**, this document is reviewed against it. Sections
   here that duplicate Foundry Script's text are intended to stay verbatim; the delta table is the
   only place the two are meant to differ.
