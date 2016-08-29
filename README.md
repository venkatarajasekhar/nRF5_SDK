# nRF5_SDK

## Version

11.0.0_89a8197

## Overview

The nRF5 SDK provides a rich and well tested software development environment for the nRF51 Series and nRF52 Series devices. The nRF5 SDK is intended be used as a foundation for any generic Bluetooth low energy, ANT or proprietary 2.4Ghz product development. It includes a broad selection of drivers, libraries, examples for peripherals, SoftDevices, and proprietary radio protocols of the nRF51 Series and nRF52 series.

## Components

### FreeRTOS (V8.2.1)

FreeRTOS is designed to be small and simple. The kernel itself consists of only three C files. To make the code readable, easy to port, and maintainable, it is written mostly in C, but there are a few assembly functions included where needed (mostly in architecture-specific scheduler routines).

FreeRTOS provides methods for multiple threads or tasks, mutexes, semaphores and software timers. A tick-less mode is provided for low power applications. Thread priorities are supported. In addition there are four schemes of memory allocation provided.

### Segger RTT

SEGGER's Real Time Transfer (RTT) is the new technology for interactive user I/O in embedded applications. It combines the advantages of SWO and semihosting at very high performance.

With RTT it is possible to output information from the target microcontroller as well as sending input to the application at a very high speed without affecting the target's real time behavior.