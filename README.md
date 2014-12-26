# n64hijack

n64hijack is a command-line executable that allows you to easily insert some assembly to be run right before a N64 game begins.

Included with the distribution of n64hijack is a file called patcher.asm which demonstrates how to run code every time the general exception handler is called in an N64 game. It implements a GameShark-like cheat engine using this technique based on the work of parasyte in his [alt64](https://github.com/parasyte/alt64) project. It should be easily modifiable to allow you to insert your own code to run at the GEH and cut out the GameShark. Alternatively, you can add GS cheats to assemble directly into the ROM at the bottom of the file.

*Note that some of the methods used in patcher.asm assume that the ROM being patched is a commercially-released game and not any sort of homebrew.*

n64hijack relies on the [u64asm](https://github.com/mikeryan/n64dev/tree/master/util/u64asm) assembler by [hcs](http://www.hcs64.com/). It's not a perfect assembler as it still contains some bugs with pseudoinstructions (notably ``li`` and a lack of an unconditional branch, ``b``), but it's the best assembler available that doesn't require the installation of a large toolchain.

## Running

Usage is ``n64hijack infile outfile asmfile`` where ``asmfile`` is the assembly to compile and run at the start of the game.