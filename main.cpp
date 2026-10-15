/*
 * Robbery Alert System - mbed LPC1768
 *
 * Lock the system with a 4-key password. While it is locked, the ADXL345
 * accelerometer watches for movement; if the board is moved, the buzzer and
 * red LED go off until the same password is entered again.
 *
 * Wiring (mbed pin / LPC1768 pin):
 *   Nokia 5110 LCD : DIN p5 (P0.9), CLK p7 (P0.7), CE p8 (P0.6), RST p9 (P0.0), DC p10 (P0.1)
 *   ADXL345 (SSP0) : MOSI p11 (P0.18), MISO p12 (P0.17), SCLK p13 (P0.15), CS p14 (P0.16)
 *   Keypad rows    : R1 p23 (P2.3), R2 p28 (P0.10), R3 p27 (P0.11), R4 p25 (P2.1)
 *   Keypad columns : C1 p24 (P2.2), C2 p22 (P2.4), C3 p26 (P2.0)
 *   Buzzer + LED   : p21 (P2.5, PWM1.6)
 */

#include "TextLCD_NOKIA_5110.h"
#include "mbed.h"
#ifdef __USE_CMSIS
#include "LPC17xx.h"
#endif
#include <stdio.h>
#include <cmath>

// Pin configuration
PinName mosi = p5;
PinName sclk = p7;
PinName dc = p10;
PinName cse = p8;
PinName reset = p9;

TextLCD5110 lcd(mosi,sclk,dc,cse,reset);

volatile int locked = 0;
volatile int place = 0;          // accelerometer reading saved when the system is locked
volatile int read_password = 0;  // set by the interrupt to start password entry

char saved_password[5];          // 4 keys + '\0'

char scan_keypad(void);
char* get_password();
int read_accelerometer();
void buzzer(int);
void led(int);

extern "C" {
    void EINT3_IRQHandler() {
        LPC_GPIOINT->IO2IntClr = p19;

        read_password = 1; // Set flag to read password in main loop
    }
}

int main(void) {

    LPC_GPIO2->FIODIR |= (1<<3) | (1<<1);      // P2.3 (R1), P2.1 (R4) as output
    LPC_GPIO0->FIODIR |= (1<<10) | (1<<11);    // P0.10 (R2), P0.11 (R3) as output
    LPC_GPIO2->FIODIR &= ~((1<<2) | (1<<4) | (1<<0)); // P2.2 (C1), P2.4 (C2), P2.0 (C3) as input

    // Enable the port-2 GPIO interrupt (EINT3) that starts password entry
    LPC_GPIOINT->IO2IntEnR |= p19;
    NVIC_EnableIRQ(EINT3_IRQn);

    lcd.locate(0, 0);
    printf("MAIN\n");
    lcd.cls();
    lcd.printf(" \nUnlocked");
    while (1) {
        if (read_password) {
            read_password = 0;

            char* entered_password = get_password();

            if (locked) {
                int match = 1;
                for (int i = 0; i < 4; i++) {
                    if (entered_password[i] != saved_password[i]) {
                        match = 0;
                        break;
                    }
                }

                if (match) {
                    locked = 0;
                    led(0);
                    buzzer(0);
                    lcd.cls();
                    lcd.printf(" \nUnlocked");
                }

            } else {
                for (int i = 0; i < 4; i++) {
                    saved_password[i] = entered_password[i];
                }

                place = read_accelerometer();
                locked = 1;
                led(0);
                buzzer(0);
                lcd.cls();
                lcd.printf(" \nLocked");
            }
            read_password=0;
        }

        if (locked) {
            int current = read_accelerometer();
            if (abs(current - place) > 50) {
                buzzer(1);
                led(1);
            }
        }
    }
}

// ---- Keypad and Input ----

char* get_password() {
    static char password[5]; // 4 digits
    int idx = 0;
    printf("Keypad\n");

    while (idx < 4) {
        char key = scan_keypad();
        if (key != '\0') {
            password[idx++] = key;
            printf("Pressed: %c\n", key);
            wait_us(100000);
        }
    }

    password[4] = '\0'; // null-terminate
    return password;
}

char scan_keypad(void) {
 char keypad[4][3] = {
     {'1','2','3'},
     {'4','5','6'},
     {'7','8','9'},
     {'*','0','#'}
 };

    for (int row = 0; row < 4; row++) {
        // Set all rows high
        LPC_GPIO2->FIOSET = (1<<3) | (1<<1);
        LPC_GPIO0->FIOSET = (1<<10) | (1<<11);

        // Set current row low
        if (row == 0) LPC_GPIO2->FIOCLR = 1<<3;      // R1
        if (row == 1) LPC_GPIO0->FIOCLR = 1<<10;     // R2
        if (row == 2) LPC_GPIO0->FIOCLR = 1<<11;     // R3
        if (row == 3) LPC_GPIO2->FIOCLR = 1<<1;      // R4

        wait_us(2000) ; // debounce

        // Read columns
        int c1 = (LPC_GPIO2->FIOPIN & (1<<2)) == 0;
        int c2 = (LPC_GPIO2->FIOPIN & (1<<4)) == 0;
        int c3 = (LPC_GPIO2->FIOPIN & (1<<0)) == 0;

        if (c1) return keypad[row][0];
        if (c2) return keypad[row][1];
        if (c3) return keypad[row][2];
    }

    return '\0'; // No key pressed
}

// ---- Accelerometer ----

int adxl345_read(int address){
    int data = 0;
    LPC_GPIO0->FIOCLR  = (1 << 16);
    address = address | 1 << 7;
    LPC_SSP0->DR = address;
    while(LPC_SSP0->SR & 1 << 4);
    LPC_SSP0->DR;  LPC_SSP0->DR = 0;
    while(LPC_SSP0->SR & 1 << 4);
    data = LPC_SSP0->DR;
    LPC_GPIO0->FIOSET = (1 << 16);
    return data;
}

int read_accelerometer(){
    int16_t x1 = 0, x2 = 0, y1 = 0, y2 = 0, z1 = 0, z2 = 0;

    LPC_SSP0->CR0 |= 7 | 1 << 6 | 1 << 7;
    LPC_SSP0->CR1 |= 2;
    LPC_GPIO0->FIODIR |= 1 << 16;
    LPC_PINCON->PINSEL0 |= 1U << 31;
    LPC_PINCON->PINSEL1 |= 1 << 3 | 1 << 5;

    LPC_GPIO0->FIOCLR = (1 << 16);
    LPC_SSP0->DR = 0x2D;
    while(LPC_SSP0->SR & 1 << 4);
    LPC_SSP0->DR;
    LPC_SSP0->DR = 8;
    while(LPC_SSP0->SR & 1 << 4);
    LPC_SSP0->DR;
    LPC_GPIO0->FIOSET = (1 << 16);

    x1 = adxl345_read(0x32); x2 = adxl345_read(0x33);
    y1 = adxl345_read(0x34); y2 = adxl345_read(0x35);
    z1 = adxl345_read(0x36); z2 = adxl345_read(0x37);

    int16_t x = (x2 << 8) | x1;
    int16_t y = (y2 << 8) | y1;
    int16_t z = (z2 << 8) | z1;

    printf("X = %d  ,  Y = %d  , Z = %d \n", x, y, z);

    return x + y + z;
}

// ---- Output ----

void buzzer(int status) {
    if (status) {
        LPC_SC->PCONP |= (1 << 6);
        LPC_PINCON->PINSEL4 &= ~(3 << 10);
        LPC_PINCON->PINSEL4 |=  (1 << 10);
        LPC_PWM1->PCR |= (1 << 14);
        LPC_PWM1->MR0 = 4000000;
        LPC_PWM1->MR6 = 1000000;
        LPC_PWM1->MCR = (1 << 1);
        LPC_PWM1->TCR = (1 << 1);
        LPC_PWM1->TCR = (1 << 0) | (1 << 3);
    } else {
        LPC_PWM1->PCR &= ~(1 << 14);
    }
}

void led(int status) {
    if (status) {
        LPC_SC->PCONP |= (1 << 6);
        LPC_PINCON->PINSEL4 &= ~(3 << 10);
        LPC_PINCON->PINSEL4 |=  (1 << 10);
        LPC_PWM1->PCR |= (1 << 14);
        LPC_PWM1->MR0 = 4000000;
        LPC_PWM1->MR6 = 1000000;
        LPC_PWM1->MCR = (1 << 1);
        LPC_PWM1->TCR = (1 << 1);
        LPC_PWM1->TCR = (1 << 0) | (1 << 3);
    } else {
        LPC_PWM1->PCR &= ~(1 << 14);
    }
}
