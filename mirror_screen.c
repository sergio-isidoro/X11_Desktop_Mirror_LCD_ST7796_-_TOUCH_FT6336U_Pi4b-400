#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h> 	// Faster screen capture using Shared Memory
#include <sys/shm.h>             	// System Shared Memory
#include <bcm2835.h>            	// Broadcom GPIO/SPI/I2C Library
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* --- Rotation Settings ---
 * 0: Portrait (320x480)
 * 1: Landscape (480x320)
 * 2: Portrait Inverted
 * 3: Landscape Inverted 
 */
#define ORIENTATION 1

/* Dynamic resolution handling based on the chosen orientation */
#if (ORIENTATION == 1 || ORIENTATION == 3)
    #define DISP_W 480
    #define DISP_H 320
#else
    #define DISP_W 320
    #define DISP_H 480
#endif

/* Hardware Pins (Broadcom GPIO numbering) */
#define PIN_DC  24   	 		/* Data/Command control */
#define PIN_RST 22    			/* Hardware Reset */
#define PIN_BL  17    			/* Backlight PWM/Switch */
#define PIN_CS  8     			/* SPI Chip Select (CS0) */
#define TOUCH_I2C_ADDR 0x38 	/* I2C Address for FT6336/FT6236 */

/* Buffers: Aligned to 32 bytes for optimized DMA/CPU cache performance */
uint16_t curr_fb[DISP_W * DISP_H] __attribute__((aligned(32))); 	/* Current frame pixels */
uint16_t last_fb[DISP_W * DISP_H] __attribute__((aligned(32))); 	/* Previous frame for diffing */
int last_touch_state = 0; 											/* Tracks if a finger was already pressed */

/* --- LCD Auxiliary Functions --- */

/* Sends a single byte command to the display controller */
void st7789_cmd(uint8_t cmd) {
    bcm2835_gpio_write(PIN_DC, LOW); 				// DC Low = Command mode
    bcm2835_spi_transfer(cmd);
}

/* Sends a single byte of data to the display controller */
void st7789_data(uint8_t data) {
    bcm2835_gpio_write(PIN_DC, HIGH); 				// DC High = Data mode
    bcm2835_spi_transfer(data);
}

/* Defines the active drawing area (window) in the internal RAM */
void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    st7789_cmd(0x2A); 								// Column Address Set
    st7789_data(x0 >> 8); st7789_data(x0 & 0xFF);
    st7789_data(x1 >> 8); st7789_data(x1 & 0xFF);
    st7789_cmd(0x2B); 								// Row Address Set
    st7789_data(y0 >> 8); st7789_data(y0 & 0xFF);
    st7789_data(y1 >> 8); st7789_data(y1 & 0xFF);
    st7789_cmd(0x2C); 								// Memory Write (pixels follow this)
}

/* ST7796 Initialization Sequence */
void init_st7796() {
    /* Perform a physical hardware reset */
    bcm2835_gpio_write(PIN_RST, LOW);  bcm2835_delay(120);
    bcm2835_gpio_write(PIN_RST, HIGH); bcm2835_delay(120);
    st7789_cmd(0x01); bcm2835_delay(150); 					// Software Reset

    /* Unlock Command Set 2 (Specific to ST7796) */
    st7789_cmd(0xF0); st7789_data(0xC3);
    st7789_cmd(0xF0); st7789_data(0x96);

    /* MADCTL (0x36): Memory Access Control (Rotation & Color Order) */
    st7789_cmd(0x36);
    if (ORIENTATION == 0)      st7789_data(0x48); 								// Portrait
    else if (ORIENTATION == 1) st7789_data(0x28); 								// Landscape
    else if (ORIENTATION == 2) st7789_data(0x88); 								// Portrait Inv
    else                       st7789_data(0xE8); 								// Landscape Inv

    st7789_cmd(0x3A); st7789_data(0x55); 										// Set pixel format to 16-bit RGB565
    st7789_cmd(0xB4); st7789_data(0x01); 										// Display Inversion Control
    st7789_cmd(0xB6); st7789_data(0x80); st7789_data(0x02); st7789_data(0x3B); 	// Display Function Control
    
    /* Panel driving & Gamma settings (Standard for 3.5" LCDs) */
    st7789_cmd(0xE8); 
    st7789_data(0x40); st7789_data(0x8A); st7789_data(0x00); 
    st7789_data(0x00); st7789_data(0x29); st7789_data(0x19); 
    st7789_data(0xA5); st7789_data(0x33);

    st7789_cmd(0xC1); st7789_data(0x06); 					// Power Control 2
    st7789_cmd(0xC2); st7789_data(0xA7); 					// Power Control 3
    st7789_cmd(0xC5); st7789_data(0x18);				 	// VCOM Control
    
    st7789_cmd(0x11); bcm2835_delay(150); 					// Sleep Out (Wake up)
    st7789_cmd(0x29); bcm2835_delay(50);  					// Display ON
}

/* --- Touch Functions with Rotation --- */
void check_touch() {
    char reg = 0x02; 										// TD_STATUS register (number of touch points)
    char data[5];
    bcm2835_i2c_setSlaveAddress(TOUCH_I2C_ADDR);
    
    /* Request 1 byte to check if there are active touch points */
    if (bcm2835_i2c_read_register_rs(&reg, data, 1) == BCM2835_I2C_REASON_OK) {
        int points = data[0] & 0x0F;
        if (points > 0) {
            char coord_reg = 0x03; 																// Start reading from X coordinate high byte
            if (bcm2835_i2c_read_register_rs(&coord_reg, data, 4) == BCM2835_I2C_REASON_OK) {
                int event = (data[0] & 0xC0) >> 6; 												// 0=Down, 1=Up, 2=Contact
                int raw_x = ((data[0] & 0x0F) << 8) | data[1];		 							// 12-bit X
                int raw_y = ((data[2] & 0x0F) << 8) | data[3]; 									// 12-bit Y

                int tx, ty;
                /* Math to align Touch sensor coordinates with LCD Orientation */
                if (ORIENTATION == 0)      { tx = raw_x; ty = raw_y; }
                else if (ORIENTATION == 1) { tx = raw_y; ty = 320 - raw_x; }
                else if (ORIENTATION == 2) { tx = 320 - raw_x; ty = 480 - raw_y; }
                else                       { tx = 480 - raw_y; ty = raw_x; }

                /* Only print when the finger first touches the screen */
                if (event == 0 && last_touch_state == 0) {
                    printf("\r[TOUCH] X: %3d | Y: %3d | Mode: %d    \n", tx, ty, ORIENTATION);
                    fflush(stdout);
                    last_touch_state = 1;
                }
            }
        } else {
            last_touch_state = 0; // Finger lifted
        }
    }
}

int main() {
    /* Initialize the BCM2835 hardware driver */
    if (!bcm2835_init()) return 1;
    
    /* SPI Setup: LCD uses Mode 0 (CPOL 0, CPHA 0) at 24MHz */
    bcm2835_spi_begin();
    bcm2835_spi_set_speed_hz(24000000);
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
    
    /* I2C Setup: Touch uses standard 100kHz */
    if (!bcm2835_i2c_begin()) return 1;
    bcm2835_i2c_set_baudrate(100000);

    /* GPIO Pin Configuration */
    bcm2835_gpio_fsel(PIN_DC, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_RST, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_BL, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(PIN_CS, BCM2835_GPIO_FSEL_OUTP);
    
    bcm2835_gpio_write(PIN_BL, HIGH); 						// Turn on Backlight
    bcm2835_gpio_write(PIN_CS, LOW);  						// Select the SPI device

    init_st7796(); // Initialize LCD

    /* X11 Connection: Connect to the local X Server */
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { printf("X11 Error: Ensure X is running.\n"); return 1; }
    
    Window root = DefaultRootWindow(dpy);
    XWindowAttributes attr;
    XGetWindowAttributes(dpy, root, &attr);

    /* Shared Memory (XShm) Setup for high-speed screen capture */
    XShmSegmentInfo shminfo;
    XImage *img = XShmCreateImage(dpy, attr.visual, attr.depth, ZPixmap, NULL, &shminfo, attr.width, attr.height);
    shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height, IPC_CREAT | 0777);
    shminfo.shmaddr = img->data = shmat(shminfo.shmid, 0, 0);
    XShmAttach(dpy, &shminfo);

    while (1) {
        check_touch(); 										// Poll I2C touch sensor

        /* Get the current desktop screenshot into shared memory */
        XShmGetImage(dpy, root, img, 0, 0, AllPlanes);
        
        /* Region of interest (Dirty Rectangle) tracking */
        int x_min = DISP_W, y_min = DISP_H, x_max = -1, y_max = -1;
        int changed = 0;

        /* Process every pixel with downscaling and color conversion */
        for (int y = 0; y < DISP_H; y++) {
            /* Map display Y to source screen Y */
            uint32_t py = (y * attr.height) / DISP_H;
            uint32_t *line = (uint32_t *)(img->data + py * img->bytes_per_line);

            for (int x = 0; x < DISP_W; x++) {
                /* Map display X to source screen X */
                uint32_t px = (x * attr.width) / DISP_W;
                uint32_t p = line[px]; 						// Read 32-bit pixel (BGRX/RGBX)
                
                /* Extract 8-bit components */
                uint8_t r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;

                /* RGB888 -> RGB565 (5 bits Red, 6 bits Green, 5 bits Blue) */
                uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                
                /* Endianness Swap: SPI transmits byte by byte, ST7796 expects Big Endian */
                uint16_t final_color = (color >> 8) | (color << 8);
                
                /* Compare with previous frame to see if we need to update this pixel */
                if (final_color != last_fb[y * DISP_W + x]) {
                    curr_fb[y * DISP_W + x] = final_color;
                    if (x < x_min) x_min = x; if (x > x_max) x_max = x;
                    if (y < y_min) y_min = y; if (y > y_max) y_max = y;
                    changed = 1;
                }
            }
        }

        /* Update the display only if changes were detected */
        if (changed) {
            set_window(x_min, y_min, x_max, y_max);
            bcm2835_gpio_write(PIN_DC, HIGH); 				// Data Mode
            int block_w = (x_max - x_min) + 1;
            
            /* Send the changed block line by line */
            for (int r = y_min; r <= y_max; r++) {
                bcm2835_spi_transfern((char*)&curr_fb[r * DISP_W + x_min], block_w * 2);
            }
            /* Sync the buffers for the next comparison */
            memcpy(last_fb, curr_fb, sizeof(curr_fb));
        }
    }
    return 0;
}
