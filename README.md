# Portable Scanner & Thermal Printing System

A portable scanning and thermal-printing system built as a Final Year
Project using a dual-processor architecture. The ESP32-CAM captures
images, while the ESP32-S3 processes and transfers image data to a
thermal printer.

## Project Overview

The system captures an image using the ESP32-CAM, processes it for
monochrome thermal printing, and sends the resulting data to the
ESP32-S3 over UART. The ESP32-S3 controls the thermal printer using
ESC/POS commands.

The project focuses on embedded image processing, inter-processor
communication, reliable data transfer, and real-time task management
with FreeRTOS.

## System Architecture

-   **ESP32-CAM:** Handles image capture and image-side processing.
-   **ESP32-S3:** Receives image data and manages thermal printer
    communication.
-   **UART:** Connects the two processors.
-   **Thermal printer:** Prints the processed image using ESC/POS
    commands.
-   **Power system:** A 7.4V battery with a buck converter provides a
    regulated 5V supply for the system.

## Key Features

-   Portable image capture and thermal printing.
-   Dual-processor design using ESP32-CAM and ESP32-S3.
-   Floyd-Steinberg dithering for converting images into printable
    monochrome output.
-   Mixed region-adaptive thresholding to improve image rendering across
    different image regions.
-   Chunked UART data transfer using start markers, length headers, and
    acknowledgements.
-   FreeRTOS USB daemon and client tasks on the ESP32-S3.
-   ESC/POS-based thermal printer control.
-   Capture LEDs activate only during image capture to reduce
    unnecessary power consumption.

## Image Processing

Captured images are converted into a format suitable for a monochrome
thermal printer. The processing pipeline uses:

-   **Floyd-Steinberg dithering:** Distributes quantization error to
    neighboring pixels to represent grayscale detail using black and
    white dots.
-   **Mixed region-adaptive thresholding:** Applies thresholding with
    regional adaptation to help preserve details under varying image
    conditions.

 
<img width="422" height="1247" alt="WhatsApp Image 2026-05-12 at 11 39 42 PM" src="https://github.com/user-attachments/assets/5d38cdf7-04c7-4831-a292-b96908e892bb" />
## UART Communication

Image data is transferred from the ESP32-CAM to the ESP32-S3 in chunks
to make transmission more reliable.

The transfer sequence uses:

1.  A start marker to indicate the beginning of a transfer.
2.  A length header to communicate the size of the next data portion.
3.  An acknowledgement (ACK) from the receiver.
4.  The sender transmits the data chunk.
5.  The receiver acknowledges the received chunk before the next portion
    is sent.

This acknowledgement-based approach helps the sender and receiver stay
synchronized and reduces the chance of continuing before a data portion
has been received.

## FreeRTOS and Printer Control

The ESP32-S3 uses FreeRTOS tasks for USB-related handling, including USB
daemon and client tasks. The thermal printer is controlled through
ESC/POS commands to configure and print the received image data.

## Power and Capture Sequence

The system is powered by a 7.4V battery. A buck converter regulates the
supply to 5V for the ESP32-CAM, ESP32-S3, and other 5V components.

At startup, the camera and ESP32-S3 are powered and synchronized. The
capture LEDs remain off until the capture button is pressed. Once image
capture is complete, the camera signals the LEDs to turn off.

## Build and Hardware

This section can be completed with the exact hardware revisions, wiring,
and build steps used in your implementation.

### Main Components

-   ESP32-CAM
-   ESP32-S3 development board
-   Thermal printer
-   7.4V battery
-   5V buck converter
-   Capture LEDs
-   Push button and required wiring

### Software and Technologies

-   C/C++
-   ESP-IDF / Arduino framework *(specify the framework and version
    used)*
-   FreeRTOS
-   UART
-   ESC/POS
-   Image processing: Floyd-Steinberg dithering and mixed
    region-adaptive thresholding

## Project Gallery

Add your project photos to the `images/` directory and update the paths
below.

### Final System

<img width="740" height="1600" alt="WhatsApp Image 2026-05-17 at 9 14 59 PM" src="https://github.com/user-attachments/assets/ee6611d8-995b-4a3c-905a-e3d0c0e29574" />
*Final assembled scanner and thermal printer system.*


### Thermal Print Results

The following images document the progression of the thermal print
output.

  ----------------------------------------------------------------------------------------------------------
  Initial print                       Improved print                       Final print
  ----------------------------------- ------------------------------------ ---------------------------------
  <img width="1600" height="1200" alt="im6" src="https://github.com/user-attachments/assets/c9c3e2c7-f3d6-4c7e-9bbf-ba3d758951b2" /> <img width="1600" height="990" alt="WhatsApp Image 2026-05-05 at 3 37 07 AM" src="https://github.com/user-attachments/assets/06d5cdbd-e160-4fc7-866f-107f2105a418" />
<img width="842" height="1280" alt="selected3" src="https://github.com/user-attachments/assets/aac600fd-f70a-4fe4-b87d-598ebf1c2a33" />




  ----------------------------------------------------------------------------------------------------------

This project was developed as a Final Year Project in Computer
Engineering.
