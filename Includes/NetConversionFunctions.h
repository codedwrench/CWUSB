#pragma once

/* Copyright (c) 2020 [Rick de Bondt] - NetConversionFunctions.h
 *
 * This file contains some general conversion functions for network related things.
 *
 **/

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <stdlib.h>
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#else
#include <byteswap.h>  // bswap_16 bswap_32 bswap_64
#endif

#include <array>
#include <cstring>
#include <iomanip>
#include <ios>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>


/**
 * Helper function to get raw data more easily.
 * @param aPacket - Packet to grab data from.
 */
template<typename Type> static Type GetRawData(std::string_view aPacket, unsigned int aIndex)
{
    return (*reinterpret_cast<const Type*>(aPacket.data() + aIndex));
}

/**
 * Helper function to get raw data as string more easily.
 */
static std::string GetRawString(std::string_view aPacket, unsigned int aIndex, unsigned int aLength)
{
    const char* lData{reinterpret_cast<const char*>(aPacket.data() + aIndex)};
    return std::string(lData, aLength);
}

/**
 * Swaps endianness of Mac.
 * @param aMac - Mac to swap.
 * @return swapped mac.
 */
static uint64_t SwapMacEndian(uint64_t aMac)
{
    // Little- to Big endian
    aMac = bswap_64(aMac);
    return aMac >> 16U;
}

/**
 * Converts string to a pretty hex string for easy reading.
 * @param aData - Data to prettify.
 * @return prettified data as string.
 */
static std::string PrettyHexString(std::string_view aData)
{
    std::stringstream lFormattedString;

    // Start on new line immediately
    lFormattedString << std::endl;
    lFormattedString << "000000 ";

    // Loop through the packet and print it as hexidecimal representations of octets
    for (unsigned int lCount = 0; lCount < aData.size(); lCount++) {
        // Start printing on the next line after every 64 octets
        if ((lCount != 0) && (lCount % 64 == 0)) {
            lFormattedString << std::endl;
            lFormattedString << std::hex << std::setfill('0') << std::setw(6) << lCount << " ";
        } else if (lCount != 0) {
            lFormattedString << " ";
        }

        lFormattedString << std::hex << std::setfill('0') << std::setw(2) << (0xFF & aData.at(lCount));
    }

    return lFormattedString.str();
}
