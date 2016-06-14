/***************************************************************************************************
|| Project: n64hijack
|| File: n64hijack.cpp
|| Author: jungerman <jungerman.mail@gmail.com>
||
|| Patches ROMs to run pieces of code before a given game starts.
|| Included is a simple ASM file to hijack the GEH and run cheats like a GameShark.
***************************************************************************************************/

#include "crc.h"
#include <cstdio>
#include <cstring>
#include <fstream>

// Code that is inserted at 0x1000 in the rom to start up the patcher
uint8_t jumpInstructions[] = {
    0x3C, 0x08, 0x00, 0x00,     // lui  t0, $0000   ;Address needs to be changed from 0
    0x01, 0x00, 0x60, 0x09,     // jalr t0, t4
    0x00, 0x00, 0x00, 0x00      // nop
};

const uint8_t jumpInstructionsLength = sizeof(jumpInstructions);


// Read the ROM and update a buffer to point to it
uint32_t getFile (const char* filename, char* charBuffer[]) {
    std::ifstream romStream (filename, std::ifstream::binary);
    if (!romStream) {
        printf("[!] Unable to open input file.\n");
        exit(3);
    }
    
    romStream.seekg(0, romStream.end);
    uint32_t length = romStream.tellg();
    romStream.seekg(0, romStream.beg);

    *charBuffer = new char[length];
    romStream.read(*charBuffer, length);

    if (!romStream) {
        printf("[!] Unable to read all input stream data.\n");
        exit(4);
    }

    romStream.close();

    return length;
}


// Write a char buffer to a file
void writeBuffer (const char* filename, char charBuffer[], uint32_t length) {
    std::ofstream romStream (filename, std::ofstream::binary);
    if (!romStream) {
        printf("[!] Unable to open output file.\n");
        exit(6);
    }
    romStream.write(charBuffer, length);
    romStream.close();
}


// Recalculate the CRC and update the buffer accordingly
void updateCrc (uint8_t buffer[]) {
    bool didRecalculate[2];
    uint32_t crc[2];
    uint16_t cic;

    if (n64crc::recalculate(buffer, crc, &cic, didRecalculate)) {
        printf("[!] Error checking/recalculating CRC.\n");
        exit(2);
    }

    printf("[i] Detected CIC:     %d\n", cic);
    printf("[i] CRC");
        printf((didRecalculate[0] || didRecalculate[1]) ? " [fixed]:" : " [ok]:  ");
        printf("      0x%08X, 0x%08X\n", crc[0], crc[1]);
}


// Insert the jump to the patcher at the beginning of the game's code
void insertJump (uint32_t patcherAddress, uint8_t buffer[]) {
    jumpInstructions[2] = 0xB0 + (patcherAddress & 0xFF);
    jumpInstructions[3] = patcherAddress >> 16;

    for (uint8_t i = 0; i < jumpInstructionsLength; i++) {
        buffer[n64crc::GAME_START + i] = jumpInstructions[i];
    }
}


// Insert the patcher at the specified address in the buffer.
// The patcher is first created by assembling asmFile using hcs's assembler u64asm.
void insertPatcher (uint32_t patcherAddress, uint8_t buffer[], const char* asmFile) {
    char *charPatcherBuffer;
    uint8_t *patcherBuffer;
    uint32_t patcherLength;
    char overwrittenAssembly[17 * jumpInstructionsLength + 1] = {0};
    char orgAssembly[30] = {0};

    for (uint8_t i = 0; i < (jumpInstructionsLength / 4); i++)
        sprintf(
            &overwrittenAssembly[17 * i],
            "    dw $%02X%02X%02X%02X\n",
            buffer[n64crc::GAME_START + (4 * i)    ],
            buffer[n64crc::GAME_START + (4 * i) + 1],
            buffer[n64crc::GAME_START + (4 * i) + 2],
            buffer[n64crc::GAME_START + (4 * i) + 3]);
    writeBuffer("overwritten.asm", overwrittenAssembly, sizeof(overwrittenAssembly));

    sprintf(orgAssembly, "PATCHER_ADDRESS equ $%08X\n", patcherAddress);
    writeBuffer("patcher_address.asm", orgAssembly, sizeof(orgAssembly));

#if defined __unix__ || defined __APPLE__
    if (system((std::string("./u64asm ") + asmFile + " -ohijack_temp.bin > assembly.log").c_str())) {
#elif defined _WIN32 || defined _WIN64
    if (system((std::string("u64asm.exe ") + asmFile + " -ohijack_temp.bin > assembly.log").c_str())) {
#else
#error "unknown platform"
#endif
        printf("\n[!] Error during assembly. See assembly.log for details\n");
        exit(5);
    }

    patcherLength = getFile("./hijack_temp.bin", &charPatcherBuffer);
    patcherBuffer = (uint8_t *) charPatcherBuffer;

    remove("overwritten.asm");
    remove("patcher_address.asm");
    remove("hijack_temp.bin");
    remove("assembly.log");

    for (uint16_t i = 0; i < patcherLength; i++)
        buffer[patcherAddress + i] = patcherBuffer[i];
}


// Games are padded with 0xFFFFFFFF until their size is a multiple of 2MiB.
// This loops through the buffer holding the ROM, last byte to the first. Stop looping when a non-0xFFFFFFFF word is found.
// That means that the last bit of game code is encountered.
// Return an address in the ROM that is the first one that ends in 0000 after the address where the last code was encountered.
// Assumes there will be an address with those qualifications.
uint32_t findPatcherLocation (uint8_t buffer[], uint32_t length) {
    uint32_t insertLocation = 0;
    for (uint32_t i = length - 4; i >= n64crc::GAME_START; i -= 4) {
        if (
            (buffer[i    ] != 0xFF) ||
            (buffer[i + 1] != 0xFF) ||
            (buffer[i + 2] != 0xFF) ||
            (buffer[i + 3] != 0xFF)
        ) {
            insertLocation = i + 4;
            break;
        }
    }

    insertLocation = insertLocation + 0x10000;      // These two lines take the address where the first non-0xFFFFFFFF word is found
    insertLocation = insertLocation & 0xFFFF0000;   //   and gets the next address that ends in 0000 where we can insert the patcher.

    printf("[i] Patcher address:  0x%08X\n", insertLocation);

    return insertLocation;
}


// Replace any instructions that move a value to the CP0 WatchLo or WatchHi register.
// In commercial games, they are surely an attempt to disable the GameShark.
// Removes the need for F1-type codes.
void killWatchInstructions (uint8_t buffer[], uint32_t length) {
    for (uint32_t i = n64crc::GAME_START; i < length; i += 4) {
        // Instruction format:
        //   mtc0 $rt, $rd
        //   ... where $rt is stored in $rd.
        //   Binary equivalent is 01000000 100[$rt] [$rd]000 00000000
        //   Remember that registers are identified by 5 bits.
        // Searching for:
        //   mtc0 [any register], watchlo
        //   mtc0 [any register], watchhi
        // Values:
        //   watchlo = 18 = 0b10010
        //   watchhi = 19 = 0b10011
        //   mask to match both = 0b11110
        if (
            ((buffer[i    ]             ) == 0b01000000) &&     // Upper bits should always be this.
            ((buffer[i + 1] & 0b11100000) == 0b10000000) &&     // Mask out the value of the $rt register, we don't care.
            ((buffer[i + 2] & 0b11110111) == 0b10010000) &&     // Mask out the bit that is different between watchlo and watchhi so we match both for $rd.
            ((buffer[i + 3]             ) == 0b00000000)        // Lower bits should always be this.
        ) {
            // NOP it!
            buffer[i    ] = 0x00;
            buffer[i + 1] = 0x00;
            buffer[i + 2] = 0x00;
            buffer[i + 3] = 0x00;
            printf("[i] Killed a watch disabler at 0x%08X\n", i);
        }
    }
}


// Run this whenever n64hijack is run with incorrect arguments.
void argumentError () {
    printf("Usage: n64hijack infile outfile asmfile [--noWatchKill]\n");
    printf("See README.md for help.\n");
    exit(1);
}


// Main function
// The user passes in three arguments
// The first argument is the input ROM to be patched
// The second is the filename to store the output ROM with
// The third is a bit of assembly to be run when the ROM starts.
//   - Included with this project is an asm file to enable using GameShark-like cheats for a game.
//   - Note that it only works correctly on real hardware and the cen64 emulator.
// The fourth disables the GameShark WATCH interrupt protection (on by default). The GameShark requires the usage of a WATCH
//   interrupt to correctly override the General Exception Handler. Some games attempt to prevent the GameShark from working
//   by overwriting the WATCH interrupt defined by the GameShark. The WatchKill functionality automatically patches these out.
//   Normally, this is worked around by using F1-type GameShark codes, but leaving this enabled removes the need for them.
int main (int argc, char** argv) {
    char* charRomBuffer;
    uint8_t* romBuffer;
    uint32_t length, patcherLocation;
    bool doWatchKill = true;
    const char* hijackAsmFilename;

    if (argc == 5) {
        if (strcmp(argv[4], "--noWatchKill")) argumentError();
    } else if ((argc > 5) || (argc < 4)) {
        argumentError();
    }

    if (argc == 4) doWatchKill = false;

    hijackAsmFilename = argv[3];

    length = getFile((const char *)argv[1], &charRomBuffer);
    romBuffer = (uint8_t *) charRomBuffer;

    char *romTitle = new char[21];
    memcpy(romTitle, &charRomBuffer[0x20], 20);
    printf("[i] Image name:       %s\n", romTitle);

    patcherLocation = findPatcherLocation(romBuffer, length);
    if (doWatchKill) killWatchInstructions(romBuffer, patcherLocation);  // patcherLocation is used as length of the buffer since everything after it is padding.
    insertPatcher(patcherLocation, romBuffer, hijackAsmFilename);
    insertJump(patcherLocation, romBuffer);
    updateCrc(romBuffer);
    writeBuffer((const char *)argv[2], charRomBuffer, length);

    delete[] romTitle;
    delete[] charRomBuffer;
    return 0;
}
