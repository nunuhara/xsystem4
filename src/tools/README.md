alice-tools
===========

This is a collection of command-line tools for viewing and editing file formats
used in AliceSoft games. The currently included tools are:

* aindump  - used to extract information from .ain files (code, text, etc.)
* ainedit  - used to reinsert information back into an .ain file
* exdump   - used to extract information from .ex files (static data)
* exbuild  - used to rebuild an .ex file dumped by exdump
* alice-ar - used to extract .ald/.afa/.flat files

More tools are planned for future releases.

Usage
-----

### Editing .ain files

There are two common usage patterns: one for editing text, and one for editing
code. In either case, you will have to enter commands at a command prompt
(e.g. cmd.exe) and edit some (rather large) text files.

In the following examples, I assume you have a file named "Rance10.ain" in the
current directory which you intend to edit.

#### Editing Text

First, dump the text using aindump as follows:

    aindump -t -o out.txt Rance10.ain

This will create a file named "out.txt" containing all of the strings/messages
in the .ain file, sorted by function.

The syntax of this file is relatively simple. Anything following a ";"
character up until the end of a line is considered a comment and ignored by
ainedit when the file is read back in. Initially all lines are commented out,
meaning that this file will cause no change to the output .ain file when given
to ainedit. To change a line of text, you must first remove the leading ";"
character from that line.

An uncommented line should contain one of the following forms:

    s[<number>] = "<text>"
    m[<number>] = "<text>"

The first form represents a string (text used by the game code) while the
second represents a message (text displayed to the player). Simply edit
`<text>` to change the text.

`<number>` is the ID of the string/message. You should not change this. It may
happen that the same string appears multiple times in the section of text you
are editing. It is only necessary to change the string once. If you set
different values for multiple instances of the same string, only the last
instance will be reflected in the output .ain file.

Once you've finished editing the text, you can reinsert it back into the ain
file by issuing the following command:

    ainedit -t out.txt -o out.ain Rance10.ain

This will create a file called "out.ain", which is a modified version of
"Rance10.ain" containing the modified text from the file "out.txt". You can
then replace the .ain file in your game directory with this file.

#### Editing Code

First, dump the code using aindump as follows:

    aindump -c -o out.jam Rance10.ain

This will create a file named "out.jam" containing the disassembled bytecode
from the file "Rance10.ain".

A full tutorial on System 4 bytecode is outside the scope of this README.
Suffice to say, it's very low level and you probably don't want to make any
advanced mods using this method (but you're welcome to try). The syntax is
very close to what you'll see in the "Disassembled" and "Combined" views in
SomeLoliCatgirl's "AinDecompiler" tool.

Once you've finished editing out.jam, you can reinsert the code back into the
.ain file using the following command:

    ainedit -c out.jam -o out.ain Rance10.ain

This will create a file called "out.ain", which is a modified version of
"Rance10.ain" containing the modified code from the file "out.jam". You can
then replace the .ain file in your game directory with this file.

### Editing .ex files

First, dump the file using exdump as follows:

    exdump -o out.x Rance10EX.ex

This will create a file named "out.x" containing all of the data structures
from the file "Rance10EX.ex".

This file uses a C-like syntax. At the top level, there should be a series of
assignment statements of the form:

    <type> <name> = <data>;

where `<type>` is one of `int`, `float`, `string`, `table`, `tree` and `list`,
and `<data>` is an expression corresponding to the type of data. `<name>` is
just a name, and may be surrounded in quotation marks if the name contains
special characters.

Once you've finished editing out.x, you can rebuild the .ex file using the
following command:

    exbuild -o out.ex out.x

This takes the data from the file "out.x" (which you have just edited) and
builds the .ex file "out.ex". You can then replace the .ex file in your game
directory with this file.

If you are building a file for a game older than Evenicle (e.g. Rance 01) you
should pass the "--old" flag to exbuild. E.g.

    exbuild -o out.ex --old out.x

#### Splitting the dump into multiple files

If the -s,--split option is passed to exdump, the .ex file will be dumped to
multiple files (one per top-level data structure). E.g.

    exdump -o out.x -s Rance10EX.ex

After running this command, there should be a number of .x files created in
the current directory containing the data from "Rance10EX.ex". The file "out.x"
should contain a list of `#include "..."` directives which will stitch the full
dump back together when rebuilding with exbuild.

### Extracting archives

See [alice-ar-README.md](alice-ar-README.html)

Known Limitations/Bugs
----------------------

* Non-ASCII file paths aren't handled correctly on Windows. You will have to
  rename any files with Japanese characters in their name before using these
  tools on them.
* aindump only supports dumping to a single file, which can be quite large.

Source Code
-----------

The source code is available [on github](http://github.com/nunuhara/xsystem4).

Reporting Bugs
--------------

You can report bugs on the issue tracker at github, contact me via email at
nunuhara@haniwa.technology, or find me on /haniho/.

Version History
---------------

### [Version 0.5.0](https://haniwa.technology/alice-tools/alice-tools-0.5.0.zip)

* !!! Breaks bytecode compatibility with previous versions !!!
* Removed `--inline-strings` options from aindump and ainedit
* Strings are now inlined in `S_PUSH` instructions, etc.
* Added a few more bytecode macros

### [Version 0.4.0](https://haniwa.technology/alice-tools/alice-tools-0.4.0.zip)

* Added alice-ar tool for extracting archive files

### [Version 0.3.0](https://haniwa.technology/alice-tools/alice-tools-0.3.0.zip)

* Now supports ain files up to version 14 (Evenicle 2, Haha Ranman)
* Improved ex file compatibility, now works with Rance 03, Rance IX and Evenicle 2
* aindump now emits macro instructions by default (makes bytecode easier to read)
* Most error messages now include line numbers

### [Version 0.2.1](https://haniwa.technology/alice-tools/alice-tools-0.2.1.zip)

* Added `--input-encoding` and `--output-encoding` options to control the text
  encoding of input and output files
* Added a `--transcode` option to ainedit to change the text encoding of an ain
  file
* Fixed an issue where the `--split` option to exdump would produce garbled
  filenames on Windows

### [Version 0.2.0](https://haniwa.technology/alice-tools/alice-tools-0.2.0.zip)

* Added exdump and exbuild tools

### [Version 0.1.1](https://haniwa.technology/alice-tools/alice-tools-0.1.1.zip)

* Fixed an issue where non-ASCII characters could not be reinserted using
  `ainedit -t`

### [Version 0.1.0](https://haniwa.technology/alice-tools/alice-tools-0.1.0.zip)

* Initial release
* Supports dumping/editing .ain files up to version 12 (Rance X)
