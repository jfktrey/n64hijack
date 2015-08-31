/***************************************************************************************************
|| Project: n64hijack
|| File: n64hijack.cpp
|| Author: jungerman <jungerman.mail@gmail.com>
||
|| Patches ROMs to run pieces of code before a given game starts.
|| Included is a simple ASM file to hijack the GEH and run cheats like a GameShark.
***************************************************************************************************/

#include "n64hijack.h"
#include "crc.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <fstream>


/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////


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
        printf("[!] Error checking/recalculating CRC for game with CIC of %d.\n", cic);
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
// Returns the patcher length.
uint32_t insertPatcher (uint32_t patcherAddress, uint8_t buffer[], const char* asmFile) {
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

    return patcherLength;
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


/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////


// Tests if a byte of the current word being tested is valid according to the signature and mask provided
inline bool checkSignatureByte (uint8_t byte, uint8_t index, uint8_t signature[], bool mask[]) {
    return (byte == signature[index]) || !mask[index];
}


// Finds the location of a given bit of code in the game's binary using a signature and a mask for that signature.
//   The signature determines the bytes that are searched for, while
//   the mask determines which bytes will change as a result of linking (and should be ignored)
// Also tries to determine if there will be multiple matches to the signature/mask and errors if that happens.
uint32_t findSignatureLocation (uint8_t buffer[], uint32_t length, uint8_t signature[], bool mask[], int signatureLength) {
    uint32_t signatureLocation = 0;
    uint8_t signatureIndex = 0;
    uint8_t foundSignatures = 0;

    for (uint32_t i = 0; i < length; i += 4) {
        if (
            checkSignatureByte(buffer[i    ], signatureIndex    , signature, mask) &&
            checkSignatureByte(buffer[i + 1], signatureIndex + 1, signature, mask) &&
            checkSignatureByte(buffer[i + 2], signatureIndex + 2, signature, mask) &&
            checkSignatureByte(buffer[i + 3], signatureIndex + 3, signature, mask)
        ) {
            // if (signatureIndex == 0x10) printf("\n");
            // if (signatureIndex >  0xC) printf("%2X\t%6X:\t %8X\n", signatureIndex, i, n64crc::_BYTES2LONG(&buffer[i]));
            signatureIndex += 4;
        } else if (signatureIndex > 0) {
            signatureIndex = 0;     // If the current word didn't match the signature for the current signatureIndex, re-run this loop for the current word,
            i -= 4;                 // but start at the first byte to test and see if the signature starts here. This handles the case where the first instruction
            continue;               // in the signature is repeated immediately before the start of the function (unlikely, but good to handle).
        } else {
            signatureIndex = 0;
        }

        if (signatureIndex == signatureLength) { // At the end of __osPiGetAccess. All bytes are correct, hook location found.
            foundSignatures++;
            signatureIndex = 0;
            signatureLocation = i;
        }
    }

    if (foundSignatures == 0) {
        printf("[!] Signature not found.\n");
        exit(7);
    } else if (foundSignatures > 1) {
        printf("[!] More than one signature found, %d in total.\n", foundSignatures);
        exit(7);
    } else {
        return signatureLocation - signatureLength + 4;
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////


// A little magic happening in this funciton. I've tried to keep the functionality of the assembly file and the functionality of
//   n64hijack totally separate, but a little message passing was necessary here to make things work for the PI hook.
// First, the offset of the usb_hook from code_engine_start is read - it should be the very last word in the assembled binary.
// The next-to-last word in the assembled binary should be the default code engine location, so that is read.
// Next, we determine if there are any FF-type codes that will move the lcoation of the code engine. We read the cheats from back-to-front
//   in search of them, and if any are found we update where we expect the code engine to be.
void updatePiHookInstructions (uint8_t buffer[], uint32_t length, uint32_t patcherEnd) {
    uint32_t piHandlerOffset = n64crc::_BYTES2LONG(&buffer[patcherEnd - 4]);   // Last byte of patcher is the offset of the PI handler from its start
    uint8_t cheatEngineAddressBytes[4];                                        // Default code engine location
    memcpy(cheatEngineAddressBytes, &buffer[patcherEnd - 8], 4);

    // Null doublewords are special in this context for a few reasons.
    // For the cheat list, two null words in a row represents the end of the list.
    // Going from the back of the cheat list to the front, we will encounter the two null words for the end of the in-game cheats first, then the
    //   two null words for the end of the boot cheats, then the two null words that are required before the start of the cheat lists.
    // When we've encountered two null doublewords, that means we're in the boot cheats.
    uint8_t nullDoublewordsFound = 0;

    uint32_t i = 0;
    for (i = patcherEnd - 8; nullDoublewordsFound < 3; i -= 8) {    // Loop until 3 null doublewords have been found, which means that
        if (                                                        //   we've hit the beginning of the boot cheats.
            (buffer[i    ] == 0x00) &&
            (buffer[i + 1] == 0x00) &&
            (buffer[i + 2] == 0x00) &&
            (buffer[i + 3] == 0x00) &&
            (buffer[i + 4] == 0x00) &&
            (buffer[i + 5] == 0x00) &&
            (buffer[i + 6] == 0x00) &&
            (buffer[i + 7] == 0x00)
        ) {
            nullDoublewordsFound++;
        } else if (nullDoublewordsFound == 2) {
            if (buffer[i] == 0xFF) {                            // Search through the boot cheats for an FF-type code
                cheatEngineAddressBytes[1] = buffer[i + 1];     // This code type moves the location of the cheat engine from the
                cheatEngineAddressBytes[2] = buffer[i + 2];     //   default location, which means that the USB hook location will
                cheatEngineAddressBytes[3] = buffer[i + 3];     //   be different (since it's copied in with the cheat engine).
                break;
            }
        }
    }

    uint32_t piHandlerAddress         = n64crc::_BYTES2LONG(cheatEngineAddressBytes) + piHandlerOffset;
    uint8_t  piHandlerAddressBytes[4] = {
        (piHandlerAddress & 0xFF000000) >> (8 * 3),
        (piHandlerAddress & 0x00FF0000) >> (8 * 2),
        (piHandlerAddress & 0x0000FF00) >> (8 * 1),
        (piHandlerAddress & 0x000000FF)
    };

    piHookInstructions[2] = piHandlerAddressBytes[0];
    piHookInstructions[3] = piHandlerAddressBytes[1];

    piHookInstructions[6] = piHandlerAddressBytes[2];
    piHookInstructions[7] = piHandlerAddressBytes[3];
}


// A lot of magic happening here. The location of __osPiGetAccess is determined so that the PI hook can be inserted in a safe location.
// We can't use findSignatureLocation to locate __osPiGetAccess directly, because there is a function that mirrors it for the SI
//   called __osSiGetAccess. The only difference between the two is the bit of memory they modify, the location of which changes
//   between games. The SI locking functions directly mirror every related PI locking function in this way. There is one PI function
//   that the SI doesn't have an equivalent for - osCreatePiManager. osCreatePiManager calls __osPiCreateAccessQueue, which has a
//   constant offset from __osPiGetAccess since they're in the same object file. So, using osCreatePiManager, we can determine the
//   location that __osPiCreateAccessQueue is located in RAM, which is then relatively easy to translate back to a location in ROM.
//   Using the constant offset, we can then get the location of __osPiGetAccess in ROM.
void insertPiHook (uint8_t buffer[], uint32_t length, uint32_t patcherEnd) {
    uint32_t entryPoint = n64crc::_BYTES2LONG(&buffer[entryPointOffset]) & 0x03FFFFFF;
    if      (entryPoint == 0x00100400) entryPoint = 0x00000400; // 6103 CIC lies to us
    else if (entryPoint == 0x00267000) entryPoint = 0x00067000; // 6106 CIC lies to us, too

    printf("[i] Real entry point: 0x%08X\n", entryPoint);

    // Intentionally overusing variables here for a bit more clarity. Should get optimized out, right?
    uint32_t piCaqJalAddress     = findSignatureLocation(buffer, length, piManagerSignature, piManagerSignatureMask, piManagerSignatureLength);
    uint32_t piCaqJalInstruction = n64crc::_BYTES2LONG(&buffer[piCaqJalAddress]);
    uint32_t piCaqMemoryLocation = (piCaqJalInstruction & 0x03FFFFFF) << 2;
    uint32_t piCaqRomLocation    = piCaqMemoryLocation - entryPoint + n64crc::GAME_START;
    uint32_t piGetAccessLocation = piCaqRomLocation + piGetAccessOffset;
    uint32_t piHookLocation      = piGetAccessLocation + hookOffset;

    printf("[i] __osPiGetAccess:  0x%08X\n", piGetAccessLocation);

    if (piHookLocation >= length - 0x100) {
        printf("[!] piHookLocation out of bounds.\n");
        exit(8);
    }

    updatePiHookInstructions(buffer, length, patcherEnd);
    for (uint8_t i = 0; i < piHookInstructionsLength; i++) {
        buffer[piHookLocation + i] = piHookInstructions[i];
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////


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


/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////


// Run this whenever n64hijack is run with incorrect arguments.
void argumentError () {
    printf("Usage: n64hijack infile outfile asmfile [--noWatchKill | --piHook]\n");
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
    uint32_t length, patcherLocation, patcherLength;
    const char* hijackAsmFilename;

    bool doWatchKill = true;
    bool doPiHook    = false;

    bool setWatchKill = false;
    bool setPiHook    = false;

    if (argc > 4) {
        for (uint8_t i = 4; i < argc; i++) {
            if (!setWatchKill && !strcmp(argv[i], "--noWatchKill")) {
                doWatchKill = false;
                setWatchKill = true;
            } else if (!setPiHook && !strcmp(argv[i], "--piHook")) {
                doPiHook = true;
                setPiHook = true;
            } else {
                argumentError();
            }
        }
    } else if (argc < 4) {
        argumentError();
    }

    hijackAsmFilename = argv[3];

    length = getFile((const char *)argv[1], &charRomBuffer);
    romBuffer = (uint8_t *) charRomBuffer;

    char *romTitle = new char[21];
    memcpy(romTitle, &charRomBuffer[0x20], 20);
    printf("[i] Image name:       %s\n", romTitle);

    patcherLocation = findPatcherLocation(romBuffer, length);

    if (doWatchKill)
        killWatchInstructions(romBuffer, patcherLocation);  // patcherLocation is used as length of the buffer since everything after it is padding.

    patcherLength = insertPatcher(patcherLocation, romBuffer, hijackAsmFilename);
    insertJump(patcherLocation, romBuffer);

    if (doPiHook)
        insertPiHook(romBuffer, length, patcherLocation + patcherLength);

    updateCrc(romBuffer);
    writeBuffer((const char *)argv[2], charRomBuffer, length);

    delete[] romTitle;
    delete[] charRomBuffer;
    return 0;
}
