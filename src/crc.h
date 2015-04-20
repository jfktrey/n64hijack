/* snesrc - SNES Recompiler
 *
 * Mar 23, 2010: addition by spinout to actually fix CRC if it is incorrect
 *
 * Copyright notice for this file:
 *  Copyright (C) 2005 Parasyte
 *
 * Based on uCON64's N64 checksum algorithm by Andreas Sterbenz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

 // Modified for n64hijack


#include <cstdlib>
#include <stdint.h>

namespace n64crc {

////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
// CONSTANTS

const uint32_t N64_BOOTCODE_START = 0x40;
const uint32_t N64_BOOTCODE_SIZE  = (0x1000 - N64_BOOTCODE_START);

const uint32_t N64_CRC1 = 0x10;
const uint32_t N64_CRC2 = 0x14;

const uint32_t GAME_START           = 0x00001000;
const uint32_t GAME_CHECKSUM_LENGTH = 0x00100000;

const uint32_t CHECKSUM_CIC6102 = 0xF8CA4DDC;
const uint32_t CHECKSUM_CIC6103 = 0xA3886759;
const uint32_t CHECKSUM_CIC6105 = 0xDF26F436;
const uint32_t CHECKSUM_CIC6106 = 0x1FEA617A;

inline uint32_t _ROL (uint32_t i, uint32_t b) {
    return (((i) << (b)) | ((i) >> (32 - (b))));
}

inline uint32_t _BYTES2LONG (uint8_t *b) {
    return ( (b)[0] << 24 | 
             (b)[1] << 16 | 
             (b)[2] <<  8 | 
             (b)[3] );
}

inline void _WRITE32 (uint8_t *Buffer, uint32_t Offset, uint32_t Value) {
    Buffer[Offset] = (Value & 0xFF000000) >> 24;
    Buffer[Offset + 1] = (Value & 0x00FF0000) >> 16;
    Buffer[Offset + 2] = (Value & 0x0000FF00) >> 8;
    Buffer[Offset + 3] = (Value & 0x000000FF);
}

uint32_t crc_table[256];


////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
// INTERNAL

// Using some magic, generate a table that is used in CRC recalculation
void _generateTable() {
    uint32_t crc, poly, i, j;

    poly = 0xEDB88320;
    for (i = 0; i < 256; i++) {
        crc = i;

        for (j = 8; j > 0; j--) {
            if (crc & 1) crc = (crc >> 1) ^ poly;
            else crc >>= 1;
        }

        crc_table[i] = crc;
    }
}


////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////
// EXPOSED FUNCTIONS

// Calculates the CRC of the ROM's boot code (should be from 0x40 to 0xFFF)
uint32_t calculateHeaderCrc(uint8_t *bootCode) {
    uint32_t crc = ~0;
    uint32_t i;

    for (i = 0; i < N64_BOOTCODE_SIZE; i++) {
        crc = (crc >> 8) ^ crc_table[(crc ^ bootCode[i]) & 0xFF];
    }

    return ~crc;
}


// Guesses which CIC should be installed based on the game's boot code CRC.
// Failing a match, it defaults to 6105.
uint16_t cicFromCrc(uint8_t *data) {
    switch (calculateHeaderCrc(&data[N64_BOOTCODE_START])) {
        case 0x6170A4A1: return 6101;
        case 0x90BB6CB5: return 6102;
        case 0x0B050EE0: return 6103;
        case 0x98BC2C86: return 6105;
        case 0xACC8580A: return 6106;
    }

    return 6105;
}

// Calculates the CRC of the game's actual code.
// The CRC only takes into account the first 1MB of the game, so only values
//   from 0x1000 to 0x00101000 in the ROM are checked.
int calculateGameCrc(uint32_t *crc, uint8_t *data, uint16_t cic) {
    uint32_t i, seed;

    uint32_t t1, t2, t3;
    uint32_t t4, t5, t6;
    uint32_t r, d;

    switch (cic) {
        case 6101:
        case 6102:
            seed = CHECKSUM_CIC6102;
            break;
        case 6103:
            seed = CHECKSUM_CIC6103;
            break;
        case 6105:
            seed = CHECKSUM_CIC6105;
            break;
        case 6106:
            seed = CHECKSUM_CIC6106;
            break;
        default:
            return 1;
    }

    t1 = t2 = t3 = t4 = t5 = t6 = seed;

    i = GAME_START;
    while (i < (GAME_START + GAME_CHECKSUM_LENGTH)) {
        d = _BYTES2LONG(&data[i]);
        if ((t6 + d) < t6) t4++;
        t6 += d;
        t3 ^= d;
        r = _ROL(d, (d & 0x1F));
        t5 += r;
        if (t2 > d) t2 ^= r;
        else t2 ^= t6 ^ d;

        if (cic == 6105) t1 += _BYTES2LONG(&data[N64_BOOTCODE_START + 0x0710 + (i & 0xFF)]) ^ d;
        else t1 += t5 ^ d;

        i += 4;
    }
    if (cic == 6103) {
        crc[0] = (t6 ^ t4) + t3;
        crc[1] = (t5 ^ t2) + t1;
    }
    else if (cic == 6106) {
        crc[0] = (t6 * t4) + t3;
        crc[1] = (t5 * t2) + t1;
    }
    else {
        crc[0] = t6 ^ t4 ^ t3;
        crc[1] = t5 ^ t2 ^ t1;
    }

    return 0;
}

uint8_t recalculate(uint8_t *buffer, uint32_t *crc, uint16_t *cic, bool *didRecalculate) {
    _generateTable();
    *cic = cicFromCrc(buffer);

    // Try to calculate game CRC
    if (calculateGameCrc(crc, buffer, *cic)) {
        return 3;
    } else {
        didRecalculate[0] = didRecalculate[1] = false;

        if (crc[0] != _BYTES2LONG(&buffer[N64_CRC1])) {
            didRecalculate[0] = true;
            _WRITE32(buffer, N64_CRC1, crc[0]);
        }

        if (crc[1] != _BYTES2LONG(&buffer[N64_CRC2])) {
            didRecalculate[1] = true;
            _WRITE32(buffer, N64_CRC2, crc[1]);
        }
    }

    return 0;
}


} // end namespace n64crc
