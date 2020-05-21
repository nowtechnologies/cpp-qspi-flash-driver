# cpp-qspi-flash-driver

Crossplatform template-based QSPI flash driver with customizable OS and HAL interface.

**IMPORTANT** this driver relies on our other repository _cpp-memory-manager_.

Currently only the configuration management logic is implemented, without the necessary application- or architecture-dependent low-level interface.

## Requirements

* Shall work with STM32 H7, G4, L4, F4 series.
* Flash use cases, possibly in some partitions
  * rare transient bulk storage, like firmware update. Implemented in load balancing partition.
  * rare long-term bulk storage in key-value pairs
  * OTP
  * Application configuration in key-value pairs
  * On-time counter using load balancing
  * error counters using load balancing
  * regular log / black box storage using load balancing
* Flash special features, considering Winbond W25Q128JV es example
  * write protection ?
    * Device resets when VCC is below threshold
    * Time delay write disable after Power-up - automatic for Tpuw
    * Software and Hardware (/WP pin) write protection using Status Registers - may be interesting
    * Write Protection using Power-down instruction - until Release power-down
  * array protection?
  * device UUID
  * erasing needs to be polled for completion
  * How to ensure CSneg tracks Vcc on power up and down? By circuit design.

## Physical storage

The flash driver assumes **little endian** numbers and 8-bit bytes in its internal accounting stored in the flash.

The flash driver assumes pages as the minimal write unit and sectors (multiple of pages) as the minimal erase unit.

An **ff** in the first byte of any page signs that this page has been **erased and not yet written**.

Magic bytes:

Value |Description
------|-------------
0     |configuration page
1     |LBD page
2     |temporary bulk storage start
3     |temporary bulk storage remaining
4     |on-time counter only
5     |log entry(es) and on-time counter
6     |error counter entry(es) and on-time counter
ff    |page in erased sector - no need to erase

### Common page header for config and long-term bulk storage

The driver supports **1** or **2** copies of each. This is **decided in the first version** of the application, and can’t be changed afterwards (or the system will report error and the application can try to fix it). The config or long-term bulk storage (LBD) is stored in key-value pairs, where each value is an `uint8_t` array. The key-value approach ensures that the config format will remain valid with subsequent firmware versions. Page header:

Data type   |Name    |Description
------------|--------|----------------
`uint8_t`   |magic   |Magic byte
`uint16_t`  |count   |Number of items started in this page, or ffff for the subsequent pages. For config, it can’t be 0 or ffff.
`uint16_t`  |checksum|Checksum on the contents of this page, excluding the checksum field itself.

#### Configuration

Item header:

Data type    |Name  |Description
-------------|------|--------------
`uint16_t`   |id    |Key to identify this item, less than ffff
`uint16_t`   |count |Length of this item in bytes.
`uint8_t[]`  |data  |the stored config data, serialized or some other way converted to byte array

Note, a new item will start in the current page only if its header and all the data fits in the current page. Otherwise it will start in a new page. It is up to the application to perform serialization and de-serialization to and from `uint8_t`.

#### Long-term bulk storage

Item header:

Data type   |Name  |Description
------------|------|-----------------
`uint8_t`   |id    |Key to identify this item
`uint24_t`  |count |Count of bytes
`uint8_t[]` |data  |The actual data. The occupied bytes must be less than 16M.

Note, a new item will start in the current page only if its header and all the data fits in the current page. Otherwise it will start in a new page. It is up to the application to perform serialization and de-serialization to and from `uint8_t`.

### Common page header for pages in the load-balancing partition

Data type   |Name    |Description
------------|--------|----------------
`uint8_t`   |magic   |Magic byte
`uint16_t`  |count   |Number of items (log entries of error counter key-value pairs) / bytes in this page, or 1 if non-applicable
`uint16_t`  |checksum|Checksum on the contents of this page, excluding the checksum field itself.

#### Temporary bulk data header

Arbitrary many temporary bulk-stored data (TBD) items may be present. They are written to end just before the oldest entry of the log / error counter / on-time (LEO) series of pages.

Data type   |Name         |Description
------------|-------------|------------
...         |...          |_Common header for pages in load-balancing partition_
`uint8_t`   |id           |Key to identify this item. This field is **only present** if magic is _temporary bulk storage start_.
`uint24_t`  |totalLength  |Total length of bulk item net data. This field is **only present** if magic is _temporary bulk storage start_.
`uint8_t[]` |data         |data

#### On-time counter only

This one can be saved on its own, or will be automatically incorporated when a log entry or new error counter value is saved.

Data type   |Name  |Description
------------|------|-------------
...         |...   |_Common header for pages in load-balancing partition_
`uint32_t`  |onTime|On-time counter value

#### Log entries and on-time counter only

Saving one more more log entry is always accompanied with an on-time counter.

Data type     |Name      |Description
--------------|----------|-------------
...           |...       |_Common header for pages in load-balancing partition_
`uint32_t`    |onTime    |On-time counter value
`uint8_t[L][]`|logEntries|an array of `uint8_t[L]` where _L_ is template parameter

#### Error counter entry(es) and on-time counter

Saving one more more error counter is always accompanied with an on-time counter.

Data type                   |Name      |Description
----------------------------|----------|-------------
...                         |...       |_Common header for pages in load-balancing partition_
`uint32_t`                  |onTime    |On-time counter value
`pair<uint16_t, uint32_t>[]`|errors    |an array of key-value pairs representing the error IDs and counter values.

### Partitioning

The flash is assumed not to exceed 4Gbyte. The following partitions will be available

* Config (already implemented)
* LBD, if needed
* Load-balancing

## Algorithms

### Config and LBD

#### Building an index of config and LBD

This happens on startup and is necessary to let the driver quickly find the needed data. Moreover, the driver will maintain a copy of the config in the memory. Config changes are updated in the memory and on the flash in write-through manner.

For LBD, only the pages containing the headers are read to save time, and the index will store start page indices.

It’s up to the application to ingore some items in the config or the LBD (deprecated contents). However, they are kept in the flash to ensure backward compatibility.

#### Checksum mismatch or copy mismatch

The following cases apply:

CRC1    |CRC2, if any |Copies equal, if apply |Valid content |What happens                                        |Cache
--------|-------------|-----------------------|--------------|----------------------------------------------------|-----------
**+**   |**+**        |**+**                  |both          |Valid values, no problem. **#**                     |retained
**-**   |**+**        |**-**                  |2nd           |Application is notified about 1. copy failure.      |retained
**+**   |**-**        |**-**                  |1st           |Application is notified about 2. copy failure.      |retained
**-**   |**-**        |**?**                  |-             |Application is notified about total failure. **#**  |cleared
**+**   |**+**        |**-**                  |-             |Application is notified about copies mismatch.      |cleared

**#** = these lines apply when there is only 1 copy.

#### Reading config pages

Happens only initially. Later, the memory instance is used.

#### Writing config or LBD pages

This happens on every copy one after the other, only on the involved pages together the other ones eresed during sector erase. If the item is already in the flash, the new one replaces it. Otherwise, the new one is appended, if the partition has enough space left.

If the config or the LBD gets completely ruined, the application **must be able to write the deprecated contents** (if any - or at least dummy values), so that the original structure can be restored.

Config IDs are assigned by the driver in order to reduce the memory requirement of internal accounting.

### Load-balancing

#### Finding the LEO series of pages

Initially, the driver needs to find the start and end of the LEO pages. Searching for it linearly takes just too much time. We know, that the LEO pages occupy a consecutive set of pages, this set having unknown length. We need to probe the whole load-balancing partition in a way that the probing soon hits a longer LEO set, later a smaller one, and eventually definitely finds a single LEO page, if there is only one.

Let the length of the load-balancing are be _B_. If we choose a displacement _d_ such that gcd(_B_, _d_) = 1, and probe the partition for every 0 <= _i_ < _B_ at _i_ * _d_ mod _B_, we definitely hit every page. Let _m_ be _B_ mod _d_. However, if _m_ is close to 0 or _d_, this does not guarantee a quick find: the unprobed intervals do not reduce quickly enough. If _B_/3 < _d_ < _B_/2 and _n_ = min(_m_, _d_ - _m_), by chosing a _d_ such that (_d_ - _n_) / _d_ approximates _n_ / (_d_ - _n_), the intervals will be divided according to the golden ratio, and be reduced quickly.

Shortly, we need a _d_ such that

* gcd(_B_, _d_) = 1
* _B_ / 3 < _d_ < _B_ / 2
* if _n_ = min(_m_, _d_ - _m_), _d_ * _d_ - 3 * _n_ * _d_ + _n_ * _n_ is close to 0.

This can be performed compile-time using a constexpr function.

Best if _d_ and _m_ are neighbouring Fibonacci numbers, but this can’t be always assured. If they the above sum is close to 0, mostly (but not always) one of the biggest intervals will be divided according to the golden ratio, and the size of the largest LEO able to hide in an uninspected interval approximately like _C_(i) = ceil(_B_ /  (_i_ + 1)). Well, this holds more-or-less for the beginning, but due to numeric reasons for some B later it cen lead to degenerate interval division. For others, it works excellent to the end. The solution would be either

* mathematically investigate the problem and find a better way to calculate the displacement.
* brute-force search for a _d_ for which the length of the uninxpected interval best approaches _C_(i).

To assure relatively quick search result, it may be desirable to insert dummy LEO pages during the first run. This will happen when no LEO record is found.

After a LEO record had been found using the above algorithm, the beginning and end of the LEO set must be found. This can be done using binary search. After it, the erased region right before the LEO set is searched using additional binary search to let the driver **skip unnecessary sector erases**.

#### Checksum mismatch or copy mismatch

When checksum mismatches for the LEO, the involved page is discarded and the application is notified.

#### Inserting LEO pages

This is quite simple: the new page(s) with the actual content has to be written right after the last LEO record. This ensures that the LEO set always remains consecutive, which is required for the above algorithm.

To avoid LEO pages filling the whole partition, the beginning can be adjusted such that the LEO set contains at most the desired amount of pages. This is after reaching each sector boundary.. This ensures that the LEO set **always start at sector boundary**.

#### Reading LEO pages

This can be performed by reading all records linearly. Every log page must be considered, but for the other two types, items for more recent pages overwrite already read items. If a page checksum mismatches, the page will be discarded.

#### Writing TBD pages

Writing starts such that the last TBD page of this data item will be just before the first TBD record of the last bulk data item written, or if none, the first LEO page. This way writing LEO records probably won’t immediately ruin the TBD. One can make sure by restricting the application writing LEO pages and knowing the amount of TBD to be written.

The driver will store the TBD start page indexes for easier reading back.

#### Reading TBD pages

Nothing special here. The TBD items can be read independent of each other, as long as they are not ruined by LEO pages.

## Driver API details

The driver will be divided into several classes in the following tiers:

* `FlashPartitioner` class used for partition size checking and partition start address calculations.
* Partition manager classes, each with blocking methods.
  * `FlashConfig` for config management
  * `FlashLongtermBulk` for long-term bulk data storage
  * `FlashLoadBalancing` for load-balancing usage:
    * Temporary bulk data storage
    * Logging
    * Error counters
    * On-time counter
* Application interface with memory allocation (using the API of `NewDelete`) and blocking QSPI transfer methods. If intended, the implementation may use a **separate thread** and semaphores to **avoid effective busy-wait**.
* Memory manager

The driver is heavily templated using static class members only. This enables compile-time checking of parameters using `static_assert`. API template parameters:

Type         |Name                        |Used by                  |Description
-------------|----------------------------|-------------------------|-------------------------
class        |_plugin*_                   |`FlashPartitioner`       |The actual plugins used by FlashPartitioner and this the application.
class        |_interface_                 |`FlashPartitioner`, `FlashConfig`, `FlashLongtermBulk`, `FlashLoadBalancing` |Interface towards flash device and OS
`uint32_t`   |_pagesNeeded_               |`FlashConfig`            |Number of total pages holding all copies of the config
`uint8_t`    |_copies_                    |`FlashConfig`            |Number of config copies, **1 or 2**.
`uint32_t`   |_readAheadSizeInPages_      |`FlashConfig`            |Size of the embedded read ahead buffer.
`uint32_t`   |_maxItemCount_              |`FlashConfig`            |Maximum possible config item count.
`uint32_t`   |_valueBufferSize_           |`FlashConfig`            |Size (in bytes) of local buffer in value items in memory-resident config copy, for which no further allocation occurs.
`uint32_t`   |_pagesNeeded_               |`FlashLongtermBulk`      |Number of total pages holding all copies of the LBD. Feature disabled if 0.
`uint8_t`    |_copies_                    |`FlashLongtermBulk`      |Number of LBD copies, **1 or 2.**
`uint32_t`   |_readAheadSizeInPages_      |`FlashLongtermBulk`      |Size of the embedded read ahead buffer.
`uint32_t`   |_pagesNeeded_               |`FlashLoadBalancing`     |Number of pages for the load-balancing partition
`uint16_t`   |_balancingInitialFillCount_ |`FlashLoadBalancing`     |Number of dummy pages to fill the load-balancing partition initially.
`uint32_t`   |_leoMaxCount_               |`FlashLoadBalancing`     |Number of maximal LEO page count, must be a multiple of (pages per sector).
`uint32_t`   |_readAheadSizeInPages_      |`FlashLoadBalancing`     |Size of the embedded read ahead buffer.

### Interface API

The Flash* classes use this API to interact with the flash and signal errors towards the application. The interface has two SPI-related operation modes:

* memory-mapped, in which only reads are permitted
* normal with all possible functions

Some interface calls return `SpiResult` type as result of the operation. The first 4 values are taken from the STM HAL and they mean what they mean there. The rest is added by me.

Enum name |Description 
----------|--------------
cOk       |Everything fine.
cError    |Some error occured during the SPI / HAL call.
cBusy     |Something was busy, whatever it may be.
cTimeout  |Timeout occured during the SPI / HAL call.
cMap      |The given function is not supported with the actual mapping mode.
cInvalid  |Invalid function parameter(s)
cMissing  |findPageWithDesiredMagic could nopt find any page.

The flash driver allocates and deallocates memory only via the interface. Here is the interface API, everything static:

Public method                                                       |Description
--------------------------------------------------------------------|----------------------------------------------------------------------------
`void init();`                                                      |Must be initialized before the driver is used. Called by the application, not the flash driver.
`void done();`                                                      |Must not be called as long the driver is being used. Called by the application, not the flash driver.
`constexpr uint32_t getPageSizeInBytes() noexcept;`                 |Returns the flash page size in bytes.
`constexpr uint32_t getSectorSizeInPages() noexcept;`               |Returns the flash sector size in pages.
`constexpr uint32_t getFlashSizeInPages() noexcept;`                |Returns the flash total size in pages.
`void badAlloc();`                                                  |Signs memory allocation failure to the aplication. It may set a flag or throw an exception if the application uses exceptions.
`void fatalError(nowtech::memory::FlashException const aException);`|Signs some error in the flash driver operation. It may set a flag or throw an exception if the application uses exceptions.
`template<typename tClass, typename ...tParameters> static tClass* _new(tParameters... aParameters);` |Allocates an object, possibly passing parameters to its constructor.
`template<typename tClass> static tClass* _newArray(uint32_t const aCount);` |Allocates an array of objects.
`template<typename tClass> static void _delete(tClass* aPointer);`  |Deallocates an object.
`template<typename tClass> static void _deleteArray(tClass* aPointer);` |Deallocates an array of objects.
`bool canMapMemory() noexcept;`                                     |Returns true if the interface supports mapped memory access.
`nowtech::memory::SpiResult setMappedMode(bool const aMapped) noexcept;` |Activates or deactivates mapped mode. This is called only on startup and only if the load balancing partition is active.
`nowtech::memory::SpiResult readMapped(uint32_t const aAddress, uint8_t aCount, uint8_t * const aData) noexcept;` |Reads aCount bytes from flash address aAddress into the array aData. This function is not intended for transfering big data chunks. The flash driver uses it only for searching some bytes – short sparse reads. These would be inefficient via HAL calls.
`nowtech::memory::SpiResult findPageWithDesiredMagic(uint32_t const aStartPage, uint32_t const aEndPage, uint8_t const aDesiredMagic, uint32_t * const aResultStart, uint32_t * const aResultEnd) noexcept` |When maped mode is not available, this call is intended to search the ends of a region of pages having a specific magic start byte aDesiredMagic. Only the partition limited by aStartPage (inclusive) and aEndPage (exclusive) is searched, and the result is placed in pointers aResultStart (inclusive) and aResultEnd (exclusive). When these are equal, all the pages have the desired value. Return value cMissing means none found.
`nowtech::memory::SpiResult eraseSector(uint32_t const aSector) noexcept;` |Erases the sector in question.
`nowtech::memory::SpiResult writePage(uint32_t const aPage, uint8_t const * const aData) noexcept;` |Writes the supplied data to the given page.
`nowtech::memory::SpiResult readPages(uint32_t const aStartPage, uint32_t const aPageCount, uint8_t * const aData) noexcept;` |Reads aPageCount pieces of page from aStartPage into aData. This uses normal mode (not memory mapped), as the flash driver does not switch modes in normal operation.

### Exceptions

It’s up to the application to decide if the driver will throw exceptions or signs the errors some other way. The _interface_’s `static void fatalError(FlashException const aException)` method is called in every case.

Exception                 |Cause
--------------------------|--------------------------
`cCommunication`          |SPI communication error
`cConfigBadCopy1`         |Copy 1 failed
`cConfigBadCopy2`         |Copy 2 failed
`cConfigBadCopies`        |Both copies failed
`cConfigCopiesMismatch`   |Copies read, but mismatch
`cConfigInvalidId`        |Using the ffff ID is not allowed
`cConfigFull`             |The item to be inserted won’t fit
`cConfigItemTooBig`       |The item does not fit a page.
`cFlashTransferError`     |There was some error during reading, writing or erasing the flash

### API

Currently only the configuration API is ready.

#### Config API

The `getConfig` method is thread-safe with itself. All other methods require mutual exclusion with any other.

Public methods                                                                   |Description
---------------------------------------------------------------------------------|----------------------------------------------------------------------------
`uint8_t const * getConfig(uint16_t const aId)`                                  |Returns the chunk of config data for the given id. The data itself is in the cache, and subsequent calls may change it. 
`uint16_t addConfig(uint8_t const * const aData, uint16_t const aCount)`         |Adds a chunk of config data with the given lengths (if fits in a page) to the cache and returns its id assigned by the driver. Marks the corresponding page as dirty. addConfig calls are required to only extend the stored item set of the previous version.
`void setConfig(uint16_t const aId, uint8_t const * const aData)`                |Changes a chunk of config data to the stuff pointed by the given pointer in the cache. Marks the corresponding page as dirty.
`void makeAllDirty()`                                                            |Marks all the pages as dirty. Useful for corrections when a copy is corrupted.
`void commit()`                                                                  |Writes all the dirty pages into the flash, erasing any sectors necessary. It performs minimal erase and write operations.
`void clear()`                                                                   |Clears the cache. Note, the flash is not intended to store fewer amount of items or changed sequence or sizes. This call should be followed by a complete re-addition of all the items and then writing it into the flash.

## Memory requirement

Each module reserves its own work memory only if given in `FlashPartitioner` as template parameter.

### Config

This module allocates the following stuff:

* amount of pages its _readAheadSizeInPages_ template parameter
* _copySizeInPages_-long array of `bool`
* _copySizeInPages_-long array of `ConfigItem`
* extra memory to hold the bigger config items not fitting _valueBufferSize_, including the unavoidable internal fragmentation of the underlying allocation algorithm used in _interface_.

### LBD

This module allocates an amount of pages _copies_ * _readAheadSizeInPages_ template parameters.

### TBD and LEO

This module allocates an amount of pages its _readAheadSizeInPages_ template parameter.

## Concurrency

It’s up to the application and the used _interface_ to provide mutual exclusion on physical flash access. Possible scenarios are:

* The application ensures a central locking mechanism for concurrent accesses on any flash area.
* The application may allow concurrent accesses of different partitions, or even concurrent read accesses of the same partition, but the _interface_ is implemented such that the actual flash operations (`eraseSector`, `writePage`, `readPages` and `readMapped`) are protected of each other.
