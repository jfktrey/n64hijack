# n64hijack

n64hijack allows you to easily insert some assembly to be run before a game starts. Included with the distribution of n64hijack is a file called patcher.asm which demonstrates how to run code every time the general exception handler is called. It implements a GameShark-like cheat engine using this technique.

## Running

Make sure you've compiled both n64hijack and u64asm and placed the executables in the same folder. From u64asm, you only need the file that is output from compiling its source. None of the .exe files within its folder are necessary.