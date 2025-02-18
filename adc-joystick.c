#include <stdio.h>
#include "pico/stdlib.h"

#define LED_R_PIN 13
#define LED_G_PIN 11
#define LED_B_PIN 12

#define BTN_A 5

int main()
{
    stdio_init_all();

    // Initialize the red LED pin
    gpio_init(LED_R_PIN);
    gpio_set_dir(LED_R_PIN, GPIO_OUT);

    // Initialize the green LED pin
    gpio_init(LED_G_PIN);
    gpio_set_dir(LED_G_PIN, GPIO_OUT);

    // Initialize the blue LED pin
    gpio_init(LED_B_PIN);
    gpio_set_dir(LED_B_PIN, GPIO_OUT);

    // Initialize the button A pin
    gpio_init(BTN_A);
    gpio_set_dir(BTN_A, GPIO_IN);
    gpio_pull_up(BTN_A);

    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}
