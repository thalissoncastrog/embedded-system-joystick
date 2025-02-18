#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"

#include "lib/ssd1306.h"

#define LED_R_PIN 13
#define LED_G_PIN 11
#define LED_B_PIN 12

#define BTN_A 5

#define JOY_X 26
#define JOY_Y 27
#define JOY_BTN 22

#define DISPLAY_SDA 14
#define DISPLAY_SCL 15

#define ADC_WRAP 4096
#define CLK_DIV 16.0

#define ADC_CHANNEL_0 0
#define ADC_CHANNEL_1 1

#define DEAD_ZONE 200
#define CENTER_VALUE 2048

#define SSD1306_HEIGHT 64
#define SSD1306_WIDTH 128

#define BORDER_HEIGHT 63
#define BORDER_WIDTH 127

// variable to manage the display
uint8_t ssd[ssd1306_buffer_length];

// struct canva to the display
struct render_area frame_area = {
    start_column : 0,
    end_column : BORDER_WIDTH,
    start_page : 0,
    end_page : ssd1306_n_pages - 1
};

// Time to debounce
static uint32_t last_time = 0;

// state varuables for the LEDs
static volatile bool led_r_state = true;
static volatile bool led_g_state = false;
static volatile bool led_b_state = true;

// flag to control the border display
static volatile bool display_border_alternate = false;
static volatile bool display_border_on = false;
typedef enum {FIRST_BORDER, SECOND_BORDER, THIRD_BORDER, NO_BORDER} border_type;
static border_type current_border = FIRST_BORDER;

/* 
    Measure the display width and height without the border
    display width - 8 - 1(to don't touch in the border) = 119
    display height - 8 - 1(to don't touch in the border) = 55
*/

static uint height_with_square = 55;
static uint width_with_square = 119;

// functions prototypes
static void gpio_irq_handler(uint gpio, uint32_t events);
static bool debounce(uint32_t *last_time);
static uint setup_pwm(uint pwm_pin);
static uint16_t read_joystick_axis(uint16_t *vrx, uint16_t *vry);
static void init_hardware();
static uint16_t adjust_value(uint16_t value);
static void draw_square(uint x0, uint y0, bool set);
static void draw_border(bool set);
static void draw_dashed_border(bool set);
static void draw_double_line_border(bool set);
static int map(uint raw_coord_x, uint raw_coord_y);


// global variables
uint slice_r, slice_b;
uint16_t vrx_value, vry_value;
uint x,y;

int main()
{
    init_hardware();

    int prev_x = 0;
    int prev_y = 0;
    bool first = true;

    while (true) {
        
        // read the converted values from the joystick
        read_joystick_axis(&vrx_value, &vry_value);

        // mapping the values from the joystick to the display
        map(vrx_value, vry_value);

        // adjust PWM values
        vrx_value = adjust_value(vrx_value);
        vry_value = adjust_value(vry_value);

        // update the intensity of the red LED
        pwm_set_gpio_level(LED_R_PIN, vrx_value);
        // update the intensity of the blue LED
        pwm_set_gpio_level(LED_B_PIN, vry_value);

        // clear the previous square
        draw_square(prev_x, prev_y, false);
        
        // draw the buffer square
        draw_square(x,y, true);        

        // check if the joystick button is pressed
        if(display_border_alternate) {
            switch (current_border) {
            case FIRST_BORDER:
                printf("%s primeira borda\n", display_border_on ? "Desenhando" : "Apagando");
                draw_border(display_border_on);
                break;
            case SECOND_BORDER:
                printf("Desenhando segunda borda\n");
                gpio_put(LED_G_PIN, true);
                draw_dashed_border(display_border_on);
                break;
            case THIRD_BORDER:
                printf("%s terceira borda\n", display_border_on ? "Desenhando" : "Apagando");
                draw_double_line_border(display_border_on);
                break;
            case NO_BORDER:
                printf("Sem borda\n");
                draw_double_line_border(false);
            default:
                break;
            }
            current_border = (current_border + 1) % 4; // switch between the borders
            display_border_alternate = !display_border_alternate;
        }
        // save the coordinates to clear the square in the next iteration
        prev_x = x;
        prev_y = y;
    
        // render the buffer on the display
        render_on_display(ssd, &frame_area);
        sleep_ms(10);
    }
}

void init_hardware() {
    stdio_init_all();

    // Initialize the button A pin
    gpio_init(BTN_A);
    gpio_set_dir(BTN_A, GPIO_IN);
    gpio_pull_up(BTN_A);

    // Initialize the button Joystick pin
    gpio_init(JOY_BTN);
    gpio_set_dir(JOY_BTN, GPIO_IN);
    gpio_pull_up(JOY_BTN);

    //LEDs
    slice_r = setup_pwm(LED_R_PIN);
    slice_b = setup_pwm(LED_B_PIN);
    // Initialize the green LED pin
    gpio_init(LED_G_PIN);
    gpio_set_dir(LED_G_PIN, GPIO_OUT);

    // Inicializando joystick
    adc_init();
    adc_gpio_init(JOY_X);
    adc_gpio_init(JOY_Y);

    //display
    i2c_init(i2c1, 1000000);
    gpio_set_function(DISPLAY_SDA, GPIO_FUNC_I2C);
    gpio_set_function(DISPLAY_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(DISPLAY_SDA);
    gpio_pull_up(DISPLAY_SCL);
    ssd1306_init();

    calculate_render_area_buffer_length(&frame_area);
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);

    // Acionando interrupcoes
    gpio_set_irq_enabled_with_callback(BTN_A, GPIO_IRQ_EDGE_FALL, true, gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(JOY_BTN, GPIO_IRQ_EDGE_FALL, true, gpio_irq_handler);
}

static uint setup_pwm(uint pwm_pin) {
    gpio_set_function(pwm_pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pwm_pin);

    pwm_set_clkdiv(slice, CLK_DIV);
    pwm_set_wrap(slice, ADC_WRAP);
    pwm_set_gpio_level(pwm_pin, 0);
    pwm_set_enabled(slice, true);

    //freq = 1,9 KHz

    return slice;
}

static void gpio_irq_handler(uint gpio, uint32_t events) {
    if (debounce(&last_time)) {
        // disable PWM from the blue and red LEDs
        if(gpio == BTN_A) {
            printf(
                "Botao a pressionado\n"
                "Alternando estado dos PWMs\n"
            );

            led_r_state = !led_r_state;
            led_b_state = !led_b_state;

            pwm_set_enabled(slice_r, led_r_state);
            pwm_set_enabled(slice_b, led_b_state);
            gpio_put(LED_R_PIN, led_r_state);
            gpio_put(LED_B_PIN, led_b_state);
        } else {
            // change the green led state and the border display
            printf(
                    "Botao joystick pressionado\n"
                    "Alternando estado do LED verde\n"
                    "Alternando borda\n"
                );

            led_g_state = !led_g_state;
            gpio_put(LED_G_PIN, led_g_state);
            
            display_border_alternate = !display_border_alternate;
            display_border_on = !display_border_on;
        }
    }
}

// debounce function
static bool debounce(uint32_t *last_time) {
    uint32_t current_time = to_us_since_boot(get_absolute_time());

    if(current_time - *last_time > 200000) {
        *last_time = current_time;
        return true;
    }
    return false;
}

// function to adjust the value of the joystick
static uint16_t read_joystick_axis(uint16_t *vrx, uint16_t *vry) {
    adc_select_input(ADC_CHANNEL_0);
    sleep_us(2);
    *vry = adc_read();

    adc_select_input(ADC_CHANNEL_1);
    sleep_us(2);
    *vrx = adc_read();
}

// function to set the dead zone of the joystick and control leds intensity
static uint16_t adjust_value(uint16_t value) {
    if (value > CENTER_VALUE + DEAD_ZONE) {
        return value - (CENTER_VALUE + DEAD_ZONE);
    } else if (value < CENTER_VALUE - DEAD_ZONE) {
        return (CENTER_VALUE - DEAD_ZONE) - value;
    } else {
        return 0;
    }
}

static void draw_square(uint x0, uint y0, bool set) {
    ssd1306_draw_line(ssd, x0, y0, x0 + 8, y0, set);
    ssd1306_draw_line(ssd, x0, y0, x0, y0 + 8, set);
    ssd1306_draw_line(ssd, x0, y0 + 8, x0 + 8, y0 + 8, set);
    ssd1306_draw_line(ssd, x0 + 8, y0, x0 + 8, y0 + 8, set);
}

// Border 1
static void draw_border(bool set) {
    ssd1306_draw_line(ssd, 0, 0, BORDER_WIDTH, 0, set);
    ssd1306_draw_line(ssd, 0, 0, 0, BORDER_HEIGHT, set);
    ssd1306_draw_line(ssd, BORDER_WIDTH, 0, BORDER_WIDTH, BORDER_HEIGHT, set);
    ssd1306_draw_line(ssd, 0, BORDER_HEIGHT, BORDER_WIDTH, BORDER_HEIGHT, set);
}

// Border 2
static void draw_dashed_border(bool set) {
    for (int i = 0; i < BORDER_WIDTH; i += 4) {
        ssd1306_set_pixel(ssd, i, 0, set);
        ssd1306_set_pixel(ssd, i, BORDER_HEIGHT, set);
    }
    for (int i = 0; i < BORDER_HEIGHT; i += 4) {
        ssd1306_set_pixel(ssd, 0, i, set);
        ssd1306_set_pixel(ssd, BORDER_WIDTH, i, set);
    }
}

// Border 3
static void draw_double_line_border(bool set) {
    // Outer border
    ssd1306_draw_line(ssd, 0, 0, BORDER_WIDTH, 0, set);
    ssd1306_draw_line(ssd, 0, 0, 0, BORDER_HEIGHT, set);
    ssd1306_draw_line(ssd, BORDER_WIDTH, 0, BORDER_WIDTH, BORDER_HEIGHT, set);
    ssd1306_draw_line(ssd, 0, BORDER_HEIGHT, BORDER_WIDTH, BORDER_HEIGHT, set);

    // Inner border
    ssd1306_draw_line(ssd, 3, 3, BORDER_WIDTH - 3, 3, set);
    ssd1306_draw_line(ssd, 3, 3, 3, BORDER_HEIGHT - 3, set);
    ssd1306_draw_line(ssd, BORDER_WIDTH - 3, 3, BORDER_WIDTH - 3, BORDER_HEIGHT - 3, set);
    ssd1306_draw_line(ssd, 3, BORDER_HEIGHT - 3, BORDER_WIDTH - 3, BORDER_HEIGHT - 3, set);
}

static int map(uint raw_coord_x, uint raw_coord_y) {
    uint min_x, min_y;
    uint max_x, max_y; 

    // Garante que o quadrado nao toque nas bordas
    if (display_border_on || FIRST_BORDER + 1) {
        min_x = 1;
        min_y = 1;
        max_x = width_with_square - 1;
        max_y = height_with_square - 1;
    }
    if (current_border == THIRD_BORDER + 1) {
        min_x = 4;
        min_y = 4;
        max_x = width_with_square - 4;
        max_y = height_with_square - 4;
    }
    
    x = (raw_coord_x * max_x) / 4084;
    y = max_y - (raw_coord_y * max_y) / 4084; // Invertendo o eixo Y

    // Garantir que x e y não ultrapassem os limites
    if (x < min_x) x = min_x;
    if (y < min_y) y = min_y;
    if (x > max_x) x = max_x;
    if (y > max_y) x = max_y;
}