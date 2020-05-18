# cpp-qspi-flash-driver

Crossplatform template-based QSPI flash driver with customizable OS and HAL interface.

**IMPORTANT** this driver relies on our other repository _cpp-memory-manager_.

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

The flash driver assumes little endian numbers and 8-bit bytes in its internal accounting stored in the flash.

The flash driver assumes pages as the minimal write unit and sectors (multiple of pages) as the minimal erase unit.

An ff in the first byte of any page signs that this page has been erased and not yet written.
