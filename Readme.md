# STM32 Bootloader + Application Project (HAL-Based)

---

## Overview

This repository contains a complete implementation of a **custom STM32 bootloader + application system** using HAL.

The system allows:

* UART-based firmware updates
* Safe flash programming
* CRC-based validation
* Fail-safe boot mechanism

Target hardware:

* STM32F411CEU6 (BlackPill)
* ST-Link V2 debugger

---

## How to Download the Repository

### Option 1 — Clone using Git

```bash
git clone <your-repo-link>
cd <repo-folder>
```

---

### Option 2 — Download ZIP (Recommended for beginners)

1. Click **Code → Download ZIP**
2. Extract the ZIP file
3. Open the extracted folder

---

## Repository Structure

```
Root
├── Application/       → Application firmware (LED blink)
├── Boot_HAL/          → Bootloader project
```

Detailed structure: 

---

### Explanation

#### Boot_HAL/

* Contains bootloader logic
* Handles UART, flash write, CRC, jump

#### Application/

* Simple firmware
* Runs from `0x08004000`

---

## Required Software

Install these before running:

* STM32CubeIDE
* STM32CubeMX
* STM32CubeProgrammer

---

## How to Open the Project

### Step 1 — Open STM32CubeIDE

### Step 2 — Import Projects

Go to:

```
File → Import → Existing Projects into Workspace
```

Select the root folder containing:

* `Boot_HAL`
* `Application`

Click **Finish**

---

## Build Instructions

### 1. Build Bootloader

* Open `Boot_HAL`
* Click **Build (hammer icon)**

---

### 2. Build Application

* Open `Application`
* Click **Build**

---

## Flashing Instructions

### Step 1 — Connect Hardware

* Connect ST-Link V2 to BlackPill
* Connect USB-UART (for firmware updates)

---

### Step 2 — Flash Bootloader

* Open `Boot_HAL`
* Click **Debug**
* Let it flash

---

### Step 3 — Flash Application

* Open `Application`
* Click **Debug**

---

### Step 4 — Run

* Reset board
* Press button (PA0) for bootloader mode
* Release button for application mode

---

## How Firmware Update Works

1. Press button → bootloader mode
2. Send firmware via UART
3. Bootloader:

   * Erases flash
   * Writes data
   * Computes CRC
4. On completion:

   * Header written
   * Firmware marked VALID
5. On reset:

   * Bootloader jumps to application

---

## Common Problems & Fixes

---

### 1. Flashing Error

```
Error finishing flash operation
```

**Cause:**

* MCU stuck in bootloader loop
* Flash busy

**Fix:**
Use STM32CubeProgrammer:

* Select **ST-Link**
* Enable **Connect Under Reset**
* Click **Full Chip Erase**

---

### 2. GDB Connection Error

```
Failed to connect to device
```

**Cause:**

* MCU stuck in infinite loop

**Fix:**

* Set BOOT0 HIGH
* Connect using CubeProgrammer
* Erase full chip

---

### 3. Project Not Opening

```
.project file missing
```

**Fix:**

* Re-import project properly
* Ensure correct root folder is selected

---

### 4. Code Not Running After Flash

**Cause:**

* Wrong linker address in Application

**Fix:**
Ensure:

```
Flash start = 0x08004000
```

---

### 5. Bootloader Not Jumping

**Cause:**

* Invalid firmware
* CRC mismatch

**Fix:**

* Ensure correct firmware transfer
* Check UART data integrity

---

### 6. Debugger Lock Issue

**Cause:**

* Continuous flash writing

**Fix:**

* Use **Connect Under Reset**
* Perform full erase

---

## Important Notes

* Always flash **Bootloader first**
* Then flash **Application**
* Never overwrite bootloader region

---

## Memory Layout

```
0x08000000 → Bootloader
0x08004000 → Firmware Header
0x08004014 → Application Code
```

---

## Key Features

* UART firmware update
* Flash write protection
* CRC32 verification
* Fail-safe update mechanism
* Safe application jump

---

## Final Notes

This project demonstrates:

* Real embedded firmware update system
* Bootloader design concepts
* Debugging techniques

It can be extended further with:

* OTA updates
* Secure boot
* Dual firmware partitions

---


