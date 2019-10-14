System 4
========

Memory Management
-----------------

### The Stack

Arguments and return values are pushed and popped from a stack. This stack does
not actually grow that fast since function arguments are immediately removed
from the stack and stored in a "page" by the CALLFUNC instruction.

### Pages

Non-pointer variables are stored in "pages". There is a global page for global
variables, and a local page for each function call.

The SH_LOCAL* family of instructions implicitly operate on the current page.

### The Heap

The VM maintains an array of pointers to heap-backed objects (strings, structs,
pages). When a heap-backed object is stored in a variable or put on the stack,
an index into this array is used to represent the object.

The PUSHLOCALPAGE instruction pushes the heap-index of the current page object
to the stack.

### Automatic Memory Management

System 4 uses reference counting to track the lifetime of heap objects. Because
the System 4 language does not have weak references, it is possible to create
memory leaks by creating circular references. E.g. the following code leaks:

    struct a {
        ref b ref_b;
    };
    
    struct b {
        ref a ref_a;
    };
    
    void leak(void)
    {
        a local_a;
        b local_b;
        local_a.ref_b <- local_b;
        local_b.ref_a <- local_a;
        // the reference count of local_a and local_b never reach zero because
        // they each hold a reference to the other, even after they're both out
        // of scope
    }
    
Calling Convention
------------------

Arguments and return values are passed on the stack. First, the caller pushes
the arguments, in order, and then issues the CALLFUNC instruction with the ID
of the function to be called.

The CALLFUNC instruction pops the arguments off of the stack, stores them in a
fresh page object, then jumps to the beginning of the function. When the
function is finished executing, it pushes its return value onto the stack, then
issues the RETURN instruction. The RETURN instruction jumps back to the
instruction immediately following the CALLFUNC instruction in the caller.

### Passing Arguments By Reference

To pass a variable by reference, the caller pushes two values: first, the index
of the page object to which the variable belongs; second, the index of the
variable within the page. Variable references can be accessed with the REF (get)
and ASSIGN (set) instructions.

