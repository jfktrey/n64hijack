/***************************************************************************************************
|| Project: n64hijack
|| File: n64hijack.h
|| Author: jungerman <jungerman.mail@gmail.com>
||
|| This file contains function signatures and bits of ASM used by n64hijack.
***************************************************************************************************/

#include <cstdint>

// Code that is inserted at 0x1000 in the rom to start up the patcher
uint8_t jumpInstructions[] = {
    0x3C, 0x08, 0x00, 0x00,     // lui  t0, $0000   ; Value will be changed from 0
    0x01, 0x00, 0x60, 0x09,     // jalr t0, t4
    0x00, 0x00, 0x00, 0x00      // nop
};

// Length of the instructions to be inserted for the jump at the start of the ROM
const uint8_t jumpInstructionsLength = sizeof(jumpInstructions);


/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////


// Code that is inserted at the end of __osPiGetAccess to jump to pi handler comms
uint8_t piHookInstructions[] = {
    0x3C, 0x04, 0x00, 0x00,     // lui  a0, $0000   ; Value will be changed from 0
    0x34, 0x84, 0x00, 0x00,     // ori  a0, $0000   ; Value will be changed from 0
    0x00, 0x80, 0x00, 0x08      // jr   a0
};

// Length of the instructions to be inserted for hijacking __osPiGetAccess
const uint8_t piHookInstructionsLength = sizeof(piHookInstructions);


/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////


// Signature for osCreatePiManager, taken from the pimgr.o file in libultra
uint8_t piManagerSignature[] = {
    0x0C, 0x00, 0x06, 0xB8,
    0x00, 0x00, 0x00, 0x00,
    0x3C, 0x05, 0x00, 0x00,
    0x3C, 0x06, 0x22, 0x22,
    0x34, 0xC6, 0x22, 0x22,
    0x24, 0xA5, 0x1A, 0xB0,
    0x0C, 0x00, 0x06, 0xB9,
    0x24, 0x04, 0x00, 0x08,
    0x24, 0x18, 0xFF, 0xFF,
    0xAF, 0xB8, 0x00, 0x28,
    0x0C, 0x00, 0x06, 0xBA,
    0x00, 0x00, 0x20, 0x25
};

// Mask that ignores bytes which will change as a result of linking
bool piManagerSignatureMask[] = {
    false, false, false, false,
    true,  true,  true,  true,
    true,  true,  false, false,
    true,  true,  true,  true,
    true,  true,  true,  true,
    true,  true,  false, false,
    false, false, false, false,
    true,  true,  true,  true,
    true,  true,  true,  true,
    true,  true,  true,  true,
    false, false, false, false,
    true,  true,  true,  true
};

// Length of the instructions that make up the signature for osCreatePiManager
const uint8_t piManagerSignatureLength = sizeof(piManagerSignature);

// Offsets
const uint8_t piGetAccessOffset = 0x50;     // From the start of __osPiCreateAccessQueue to __osPiGetAccess
const uint8_t hookOffset        = 0x34;     // From the start of __osPiGetAccess to where the hook should be inserted
const uint8_t entryPointOffset  = 0x8;      // From the start of the ROM to the entry point
