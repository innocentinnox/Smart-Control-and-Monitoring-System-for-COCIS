/* Board wiring: the single place that knows which GPIO does what.
 * Target: ESP32-S3-DevKitC-1 (Wokwi "board-esp32-s3-devkitc-1").
 *
 * firmware/tools/gen_diagram.py reads this file to generate diagram.json, so
 * the simulated wiring can never disagree with the firmware.
 *
 * The assignment follows the board's physical pin order so the diagram
 * stays tidy:
 *   left header  (top -> bottom): rooms, SD card, buzzer
 *   right header (top -> bottom): RFID readers, I2C (LCD + RTC), doors,
 *                                 break-glass, granted LED
 * LDRs must be on ADC1 (GPIO 1-10): ADC2 is shared with the Wi-Fi radio and
 * cannot be read reliably while it is on. Strapping pins 0, 3, 45, 46 are
 * read by the chip at reset to pick the boot mode, so they are left unused.
 *
 * Only #defines live here (no code), so changing a pin is a one-line edit.
 */
#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"

/* ================= Left header ================= */

/* ---------------- Lecture rooms ----------------
 * Per room: LDR = light-dependent resistor (analog light level, read through
 * the ADC), PIR = passive-infrared motion sensor (digital, high on motion),
 * LIGHT = LED that stands in for the room lamps (dimmed with PWM, see LEDC
 * below). */
#define PIN_ROOM1_LDR       GPIO_NUM_4      /* ADC1_CH3 */
#define PIN_ROOM1_PIR       GPIO_NUM_5
#define PIN_ROOM1_LIGHT     GPIO_NUM_6
#define PIN_ROOM2_LDR       GPIO_NUM_7      /* ADC1_CH6 */
#define PIN_ROOM2_PIR       GPIO_NUM_15
#define PIN_ROOM2_LIGHT     GPIO_NUM_16

/* ---------------- SD card on SPI2 (routed through the GPIO matrix) ----------------
 * The SD card is driven in SPI mode (4 wires). The ESP32-S3 GPIO matrix lets
 * any pin carry an SPI signal, so these were picked for neat wiring. Only the
 * storage gatekeeper task ever talks to the card (see storage.h). */
#define PIN_SD_MISO         GPIO_NUM_10
#define PIN_SD_SCK          GPIO_NUM_11
#define PIN_SD_MOSI         GPIO_NUM_12
#define PIN_SD_CS           GPIO_NUM_13

/* Passive buzzer: driven with a PWM square wave, the PWM frequency is the pitch. */
#define PIN_BUZZER          GPIO_NUM_14

/* ================= Right header ================= */

/* ---------------- RFID readers: two MFRC522 on their own SPI bus (SPI3) ----------------
 * The entrance has one reader on the entry side and one on the exit side.
 * Both share the bus wires (SCK, MOSI, MISO) and the reset line; each has its
 * own chip-select (the reader's "SDA" pin), so the driver talks to one reader
 * at a time. A separate bus from the SD card means the card reads never wait
 * for log writes. Only the rfid task uses this bus (see rfid.h). */
#define PIN_RFID_IN_CS      GPIO_NUM_1      /* entry reader SDA (chip select) */
#define PIN_RFID_SCK        GPIO_NUM_2
#define PIN_RFID_MOSI       GPIO_NUM_42
#define PIN_RFID_MISO       GPIO_NUM_41
#define PIN_RFID_RST        GPIO_NUM_40     /* both readers, active low */
#define PIN_RFID_OUT_CS     GPIO_NUM_39     /* exit reader SDA (chip select) */
#define RFID_SPI_HZ         4000000         /* 4 MHz; the MFRC522 allows up to 10 MHz */

/* ---------------- Shared I2C bus: LCD2004 (0x27) + DS1307 RTC (0x68) ----------------
 * I2C is a 2-wire bus (data + clock) where several chips share the same wires
 * and are told apart by their 7-bit address. I2C_FREQ_HZ is the clock speed
 * in Hz (100 kHz = I2C "standard mode", supported by both chips). */
#define PIN_I2C_SDA         GPIO_NUM_36
#define PIN_I2C_SCL         GPIO_NUM_35
#define I2C_FREQ_HZ         100000
#define I2C_ADDR_LCD        0x27
#define I2C_ADDR_RTC        0x68

/* ---------------- Entrance terminal ----------------
 * Two servo motors stand in for the entry and exit door locks. Break-glass is
 * the emergency button (opens the doors). LED_GRANTED lights on an accepted
 * card. */
#define PIN_DOOR_IN_SERVO   GPIO_NUM_48
#define PIN_DOOR_OUT_SERVO  GPIO_NUM_47
#define PIN_BREAK_GLASS     GPIO_NUM_21
#define PIN_LED_GRANTED     GPIO_NUM_20

/* ---------------- LEDC (PWM) allocation: one owner per channel ----------------
 * LEDC is the ESP32 PWM peripheral. A "timer" sets the PWM frequency; a
 * "channel" connects a timer to one pin and sets its duty cycle (on-time %).
 * Several channels may share a timer if they need the same frequency.
 * Each channel below is used by exactly one module, so they never clash.
 *   servos: 50 Hz is the standard hobby-servo frame rate; the pulse width
 *           (about 1-2 ms) sets the angle.
 *   lights: 5 kHz is far above what the eye can see, so no flicker.
 */
#define PWM_TIMER_SERVO     LEDC_TIMER_0    /* 50 Hz  */
#define PWM_TIMER_LIGHT     LEDC_TIMER_1    /* 5 kHz  */
#define PWM_TIMER_BUZZER    LEDC_TIMER_2    /* tone frequency, changed per beep */

#define PWM_CH_DOOR_IN      LEDC_CHANNEL_0
#define PWM_CH_DOOR_OUT     LEDC_CHANNEL_1
#define PWM_CH_ROOM1_LIGHT  LEDC_CHANNEL_2
#define PWM_CH_ROOM2_LIGHT  LEDC_CHANNEL_3
#define PWM_CH_BUZZER       LEDC_CHANNEL_4
