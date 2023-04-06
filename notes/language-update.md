Updates to the System 4 language since Evenicle
===============================================

SLC's System 4 decompiler/compiler was last updated to work with Evenicle. There have been
significant changes to the System 4 language since. What follows is a list of features which
have been added.

Namespaces
----------

Added in Tsumamigui 3. This is purely a compile time feature. If you look at the global
variable and function names, you will notice lots of `::`, e.g. `time::detail::g_PrevPlayTime`.

Lambdas
-------

Added in Heartful Maman. I'm not sure how these work, but if you look at the disassembled
code there are now functions embedded within functions with names like `<lambda ...>`.

Generic Arrays
--------------

Added in Ixseal. The old array types have been replaced with a new generic array type, with
the syntax `array<type>`. The newly added types 79 and 80 were added to support this feature
(type 79 is the regular array type, and type 80 is the reference-to-array type).

There also appears to be a `foreach` loop construct added which operates on the new arrays.
I believe the newly added type 82 is some kind of iterator type which is used in these loops.
The syntax is likely something like:

    foreach (var : array) {
        // do somthing with var
    }

where `var` is a type 82.

Array methods are now implemented via the `Array` library rather than as dedicated bytecode
instructions.

Properties (Getters/Setters)
----------------------------

Added in Ixseal. Some struct/class members now have their names surrounded in angle brackets,
e.g. `<PropertyName>`. These members have get/set methods defined for them, with names like
`ClassName@PropertyName::get`/`ClassName@PropertyName::set`. Most likely these methods are
automatically generated.

Interfaces & vtables
--------------------

Added in Ixseal. If you look at the struct declarations, you will find empty structs with
names like `IParts`. Embedded in the struct data in the ain file there is a list of interfaces
for each struct, listing the implemented interfaces, along with indices into the struct's
vtable corresponding to the location of the methods for that interface.

All structures which implement an interface have an `array<int>` as their first member with
the name `<vtable>`. This is a table listing the virtual (interface) methods of the struct.

`CALLMETHOD` now takes the function index as a stack-passed argument, rather than a static
instruction argument. The instruction argument to `CALLMETHOD` is changed to be the number of
arguments that are passed to the method. The calling convention is as follows:

    PUSH <struct page>
    PUSH <function index>
    PUSH <argument 0>
    ...
    PUSH <argument n>
    CALLMETHOD <n>

This new calling convention enables calling compile-time-indeterminate methods through the
vtable.

To support variables which represent an abstract *instance of an interface*, the new type 89
was added. This is a 2-valued type in which the first value is a struct page, and the second
value is an index into the struct's vtable corresponding to the location of the interface's
methods. Thus, methods can be called on interface objects as follows:

    PUSH <page>    ; [A] (A is page of type-89 variable)
    PUSH <var>     ; [A,B] (B is index of type-89 variable)
    REFREF         ; [C,D] (C is page of object; D is index into object's vtable)
    DUP_U2         ; [C,D,C]
    PUSH 0         ; [C,D,C,0]
    REF            ; [C,D,E] (E is the vtable of object C)
    SWAP           ; [C,E,D]
    PUSH <n>       ; [C,E,D,n]
    ADD            ; [C,E,D+n]
    REF            ; [C,F] (F is global index of method)
    PUSH <arg 0>   ; [C,F,a0] (a0 is the first argument)
    ...
    PUSH <arg n>   ; [C,F,a0,...,an] (an is the last argument)
    CALLMETHOD <n> ; [stack now contains return value of method]

Generic Structures
------------------

Added in Rance X. This is purely a compile time feature. If you look at the struct names you
will find names like `MapPair<int, QuestMapPosition>`, where the type names inside the angled
brackets appear again in the struct members.

Function Overloading
--------------------

Added in Rance X. This is purely a compile time feature. If you look at the function
signatures you will notice duplicate names with differing argument lists. In fact, there are
even duplicate function names with *exactly the same argument list*. I'm not sure why.

Enums
-----

Added in Rance X. Enum types have the following automatically generated static methods:

* `string EnumType@String(EnumType value)`
* `EnumType EnumType::Parse(string value)`
* `EnumType EnumType::Parse(int value)`
* `array<EnumType> EnumType::GetList(void)`
* `bool EnumType::IsExist(int value)`
* `int EnumType::NumOf(void)`

There are 3 new types associated with enums. Types 92 and 93 are the regular enum and
reference-to-enum types, respectively. They store an index into the enum table in the struct
type field of their type descriptor.

Type 91 is only used in the return value from the EnumType::Parse methods. It's wrapped in an option
type (new type 86).

Option Types
------------

Added in Rance X. The new type 86 serves this function. Type 86 is a 2-valued type where the first
value is the wrapped value, and the second value indicates sucess/failure.

In Rance X, a value of -1 as the second value indicates failure, and a value of 0 indicates success.
In Evenicle 2 and later games, a value of 1 as the second value indicates failure.

Unclear
-------

Variables in the ain file now have two names. Usually these names are the same string, but
occasionally there are minor differences between them, and the differences don't reveal much
about what the purpose of the second name is.
