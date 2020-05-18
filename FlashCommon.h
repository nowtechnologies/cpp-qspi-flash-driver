#ifndef NOWTECH_FLASHCOMMON
#define NOWTECH_FLASHCOMMON

#include <cstdint>
#include <type_traits>

namespace nowtech::memory {

enum class FlashCopies : uint8_t {
  c1 = 1u,
  c2 = 2u
};

enum class SpiResult : uint32_t {
  cOk      = 0x00u,
  cError   = 0x01u,
  cBusy    = 0x02u,
  cTimeout = 0x03u,
  cMap     = 0x04u,
  cInvalid = 0x05u,
  cMissing = 0x06u
};

enum class FlashException : uint32_t {
  cCommunication        = 0u,
  cConfigBadCopy1       = 1u, // CRC or consistency
  cConfigBadCopy2       = 2u, // CRC or consistency
  cConfigBadCopies      = 3u, // CRC or consistency
  cConfigCopiesMismatch = 4u,
  cConfigInvalidId      = 5u,
  cConfigFull           = 6u,
  cConfigItemTooBig     = 7u,
  cFlashTransferError   = 8u
};

// TODO these magic things would belong in FlashCommon, but some weird rule prevents the subclasses from easily accessing them
enum class Magic : uint8_t {
  cConfig             =    0u,
  cLongtermBulk       =    1u,
  cTemporaryBulkStart =    2u,
  cTemporaryBulkOther =    3u,
  cOnTimeOnly         =    4u,
  cLogOnTime          =    5u,
  cErrorCounterOnTime =    6u,
  cErased             = 0xff
};

template<Magic tMagic>
static constexpr bool is(uint8_t const aValue) noexcept {
  return static_cast<Magic>(aValue) == tMagic;  // shortens to is<Magic::cErased>(value)
}

template<typename tType>
static tType getValue(uint8_t const * const aWhere) noexcept {
  static_assert(std::is_integral<tType>::value && std::is_unsigned<tType>::value);
  
  tType result = *aWhere;
  for(uint16_t i = 1u; i < sizeof(tType); ++i) {
    result |= aWhere[i] << (i << 3u);
  }
  return result;
}

template<typename tType>
static void setValue(uint8_t * const aWhere, tType const aValue) noexcept {
  static_assert(std::is_integral<tType>::value && std::is_unsigned<tType>::value);
  
  tType work = aValue;
  *aWhere = work;
  for(uint16_t i = 1u; i < sizeof(tType); ++i) {
    aWhere[i] = aValue >> (i << 3u);
  }
}

template<typename tInterface>
class FlashCommon {
protected:
  static constexpr uint32_t cPageSizeInBytes   = tInterface::getPageSizeInBytes();
  static constexpr uint32_t cSectorSizeInPages = tInterface::getSectorSizeInPages();
  static constexpr uint32_t cFlashSizeInPages  = tInterface::getFlashSizeInPages();

  static constexpr uint16_t cOffsetPageMagic    = 0u;
  static constexpr uint16_t cOffsetPageCount    = cOffsetPageMagic    + sizeof(uint8_t);
  static constexpr uint16_t cOffsetPageChecksum = cOffsetPageCount    + sizeof(uint16_t);
  static constexpr uint16_t cOffsetPageItems    = cOffsetPageChecksum + sizeof(uint16_t);
  static constexpr uint16_t cUnusedValue        = 0xffff;

  static constexpr uint8_t  cChecksumXorValue   = 0x5au;
  static constexpr uint8_t  cChecksumPrimeMask  =   15u;
  static constexpr uint8_t  cChecksumPrimeCount = cChecksumPrimeMask + 1u;
  static constexpr uint16_t cChecksumPrimeTable[cChecksumPrimeCount] = {0x049D, 0x0C07, 0x1591, 0x1ACF, 0x1D4B, 0x202D, 0x2507, 0x2B4B, 0x34A5, 0x38C5, 0x3D3F, 0x4445, 0x4D0F, 0x538F, 0x5FB3, 0x6BBF};

  static uint16_t calculateChecksum(uint8_t const * const aData) noexcept {
    uint16_t result     = 0u;
    uint16_t primeIndex = 0u;
    for(uint32_t arrayIndex = 0u; arrayIndex < cOffsetPageChecksum; ++arrayIndex) {
      result += (aData[arrayIndex] ^ cChecksumXorValue) * cChecksumPrimeTable[primeIndex];
      primeIndex = (primeIndex + 1u) & cChecksumPrimeMask;
    }
    for(uint32_t arrayIndex = cOffsetPageChecksum + sizeof(uint16_t); arrayIndex < tInterface::getPageSizeInBytes(); ++arrayIndex) {
      result += (aData[arrayIndex] ^ cChecksumXorValue) * cChecksumPrimeTable[primeIndex];
      primeIndex = (primeIndex + 1u) & cChecksumPrimeMask;
    }
    return result;
  }
};

template<typename tInterface>
constexpr uint16_t FlashCommon<tInterface>::cChecksumPrimeTable[FlashCommon<tInterface>::cChecksumPrimeCount];

}

#endif
