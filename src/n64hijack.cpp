/***************************************************************************************************
|| Project: n64hijack
|| File: hijack.cpp
|| Author: jungerman <jungerman.mail@gmail.com>
||
|| Patches ROMs to run pieces of code before a given game starts.
|| Included is a simple ASM file to hijack the GEH and run cheats like a GameShark.
***************************************************************************************************/

#include "crc.h"
#include <cstdio>
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

    if (system((std::string("./u64asm ") + asmFile + " -ohijack_temp.bin > assembly.log").c_str())) {
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


// Main function
// The user passes in three arguments
// The first argument is the input ROM to be patched
// The second is the filename to store the output ROM with
// The third is a bit of assembly to be run when the ROM starts.
//   Included with this project is an asm file to enable using GameShark-like cheats for a game.
//   Note that it only works correctly on real hardware, as no N64 emulators support WATCH exceptions.
int main (int argc, char** argv) {
    char* charRomBuffer;
    uint8_t* romBuffer;
    uint32_t length, patcherLocation;
    const char* hijackAsmFilename;

    if (argc != 4) {
        printf("Usage: n64hijack infile outfile asmfile\n");
        exit(1);
    }

    hijackAsmFilename = argv[3];

    length = getFile((const char *)argv[1], &charRomBuffer);
    romBuffer = (uint8_t *) charRomBuffer;

    char *romTitle = new char[21];
    memcpy(romTitle, &charRomBuffer[0x20], 20);
    printf("[i] Image name:       %s\n", romTitle);

    patcherLocation = findPatcherLocation(romBuffer, length);
    insertPatcher(patcherLocation, romBuffer, hijackAsmFilename);
    insertJump(patcherLocation, romBuffer);
    updateCrc(romBuffer);
    writeBuffer((const char *)argv[2], charRomBuffer, length);

    delete[] romTitle;
    delete[] charRomBuffer;
    return 0;
}