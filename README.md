# X11 Desktop Mirror & Touch for ST7796 + FT6336 (Raspberry Pi 4B)

Real-time desktop mirroring from X11 to a 3.5" **ST7796** (320x480) display and capacitive touch support for **FT6336**. This project uses the `bcm2835` hardware library and `MIT-SHM` (XShm) for low-latency screen capture and I2C for touch coordinate output.

---

# Key Features
- **Real-time Mirroring**: Captures X11 root window and mirrors it to SPI.
- **Partial Updates**: Sends only modified regions (dirty rectangles) to save SPI bandwidth.
- **ST7796 Optimized**: Full 320x480 resolution support with 16-bit RGB565.
- **Capacitive Touch**: Real-time X/Y coordinate logging via I2C (FT6336/FT6236).
- **Synchronized Rotation**: Integrated rotation defines for both LCD and Touch.
- **High Performance**: Uses Shared Memory (XShm) to minimize CPU overhead.

---

![20260214_004719(1)](https://github.com/user-attachments/assets/8d0ee738-0d41-430c-8842-3369d28ac0ac)

---

# Hardware Setup

### Wiring Diagram (GPIO BCM)



[Image of Raspberry Pi 4 GPIO pinout diagram]


| Function | Pin (Physical) | GPIO (BCM) | Notes |
|----------|----------------|------------|-------|
| **VCC** | 1 or 17        | 3.3V       | Power |
| **GND** | 6, 9, 14...    | GND        | Ground |
| **SCL** | 23             | 11         | SPI Clock |
| **SDA** | 19             | 10         | SPI MOSI |
| **CS** | 24             | 8          | SPI Chip Select |
| **DC** | 18             | 24         | Data/Command |
| **RST** | 15             | 22         | Reset |
| **BL** | 11             | 17         | Backlight |
| **I2C SDA**| 3            | 2          | Touch Data |
| **I2C SCL**| 5            | 3          | Touch Clock |

---

# Requirements

## Software
- **Raspberry Pi OS**: X11 environment (Wayland is not supported).
- **SPI & I2C enabled**: Enable via `sudo raspi-config`.
- **bcm2835 library**: Hardware access library.
- **X11 Libraries**: For screen capture.

### 1. Install Dependencies
```bash
sudo apt update
sudo apt install libx11-dev libxext-dev
```

Install bcm2835 Library
```bash
wget [http://www.airspayce.com/mikem/bcm2835/bcm2835-latest.tar.gz](http://www.airspayce.com/mikem/bcm2835/bcm2835-latest.tar.gz)
tar zxvf bcm2835-latest.tar.gz
cd bcm2835-*
./configure
make
sudo make install
```
## Compilation & Run
Using Makefile
```bash
make run
```

Manual Compilation
```bash
gcc -O3 -o mirror_screen mirror_screen.c -lX11 -lXext -lbcm2835 -lXtst
sudo ./mirror_screen
```

---

# Configuration

## Rotation & Resolution
Adjust the `ORIENTATION` define at the top of the code to match your mounting:

* **0**: Portrait (320x480)
* **1**: Landscape (480x320)
* **2**: Portrait Inverted (320x480)
* **3**: Landscape Inverted (480x320)

> The code automatically swaps `DISP_W`/`DISP_H` and re-calculates touch coordinates based on this value to ensure they always match the visual output.

## Touch Controller
The default I2C address:
* **For FT6336U**: Use `0x38`.

---

# How It Works

1.  **Init**: Initializes the BCM2835 library, setting up SPI (32MHz) for the display and I2C (100kHz) for the touch panel.
2.  **LCD Sync**: Unlocks the ST7796 specific extended command set (Command 2) and configures the `MADCTL` register for the selected rotation.
3.  **Capture**: Utilizes the `XShmGetImage` extension for high-speed desktop framebuffer grabbing directly from the X11 server.
4.  **Processing**: Downscales the captured frame to the display resolution and converts pixels from RGB888 to RGB565 Big-Endian.
5.  **Diffing**: Compares the current frame with the previous one to identify "dirty" regions (rectangles that actually changed).
6.  **Touch**: Polls the I2C bus for touch events, extracts raw data, and applies rotation matrices to align coordinates with the LCD.
7.  **Output**: Prints real-time touch coordinates to the terminal and pushes visual updates to the LCD via SPI.

---

# License
Free to use for personal and educational projects.
