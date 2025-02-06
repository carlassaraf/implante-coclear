#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "lcd.h"
#include "modify_clock.h"

// Optimizacion baja para que no se ignoren las variables en el debugger
#pragma GCC optimize("O0")

// GPIO para usar de entrada de datos
#define RX_GPIO     16
#define MAX_DUTY_TEST   0
#define PULSE_WIDTH 4

#define I2C_PORT    i2c_default
#define LCD_ON      0
#define LCD_ADDR    0x27 
#define SDA_GPIO    4
#define SCL_GPIO    5

/**
 * @brief Cantidad de microsegundos de ancho de pulso
 */
typedef enum {
    DUTY_BIT_ZERO = 1,
    DUTY_NO_BIT = 2,
    DUTY_BIT_ONE = 3
} duty_us_t;

// Variable de ancho de pulso en us
volatile uint64_t duty_us = 0;
// Booleano para habilitar el main
volatile bool bit_captured = false;

/**
 * @brief Callback para la interrupcion
 * @param gpio numero de GPIO que dispara la interrupcion
 * @param event_mask tipo de evento que ocurrio (GPIO_IRQ_EDGE_RISE o GPIO_IRQ_EDGE_FALL)
 */
void gpio_rx_irq_cb(uint gpio, uint32_t event_mask) {
    // Variables para marcas de tiempo
    static absolute_time_t t_rise;
    // Veo si esta alto el GPIO
    if(event_mask & GPIO_IRQ_EDGE_RISE) {
        // Marca de tiempo cuando sube
        t_rise = get_absolute_time();
        return;
    }
    // if(event_mask & GPIO_IRQ_EDGE_FALL) {
        // El GPIO esta bajo, marco el tiempo del pulso
    // Saco la diferencia
    duty_us = absolute_time_diff_us(t_rise, get_absolute_time()) / PULSE_WIDTH;
    // Aviso a main
    bit_captured = true;
    // }
}

void init_default_i2c(uint16_t f_khz);

/**
 * @brief Programa principal
 */
int main(void) {

    stdio_init_all();
    sleep_ms(4000);
    printf("Clock inical!\n");

    measure_freqs();
    
    clocks_enable_resus(&resus_callback);
    // Break PLL sys
    pll_deinit(pll_sys);
    while(!seen_resus);

    // bool status_clk = change_sys_clock(1);
    clock_stop(clk_adc);
    printf("\nNew Freqs:\n");
    measure_freqs();


    // Inicializacion del GPIO
    gpio_init(RX_GPIO);
    gpio_set_dir(RX_GPIO, false);
    gpio_pull_down(RX_GPIO);
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
    // Habilito interrupcion por flanco ascendente y descendente
    gpio_set_irq_enabled_with_callback(RX_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_rx_irq_cb);

    // I2C & LCD
    #if LCD_ON
        init_default_i2c(100);
        lcd_init(I2C_PORT, LCD_ADDR);
        // Limpia la pantalla
        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_string("Esperando...");
        sleep_ms(1000);
    #endif

    // Variable para armar la trama de datos
    uint16_t data = 0;
    // Contador para armar la trama
    uint8_t counter = 0;
    #if MAX_DUTY_TEST
        uint8_t counter_errors = 0;
        uint8_t logger_errors[MAX_DUTY_TEST];
        for (int i = 0; i < MAX_DUTY_TEST; i++) {
            logger_errors[i] = 0;
        }
    #endif
    // Variable para mostrar en lcd
    char text_data[MAX_CHARS + 1] = "";
    char aux_buffer[MAX_CHARS + 1] = "";

    while (true) {

        // Avanzo cuando la interrupcion haya capturado el bit
        if(bit_captured) {
            // Evaluo que ancho de pulso es
            switch(duty_us) {
                case DUTY_BIT_ZERO:
                    // Si es un cero, solo paso al siguiente bit
                    text_data[counter] = '0';
                    counter++;
                    break;
            
                case DUTY_BIT_ONE:
                    // Si es un uno, lo agrego a la trama
                    text_data[counter] = '1';
                    data |= 1 << (15 - counter++);
                    break;

                case DUTY_NO_BIT:
                    // Cuando no hay bit para analizar, se limpia
                    data = 0;
                    counter = 0;
                    #if MAX_DUTY_TEST
                        counter_errors = 0;
                        
                        for (int i = 0; i < MAX_DUTY_TEST; i++) {
                            logger_errors[i] = 0;
                        }
                    #endif
                    break;

                case 0:
                    break;
                default:
                    #if MAX_DUTY_TEST
                        if (duty_us < MAX_DUTY_TEST) {
                            logger_errors[duty_us]++;
                        }
                        counter_errors++;
                    #endif
                    break;
            }

            // Veo si termino la trama
            if(counter == 16) {
                #if MAX_DUTY_TEST
                    printf("Count Errors: %d\n\n", counter_errors);
                    for (int i = 0; i < MAX_DUTY_TEST; i++) {
                        printf("Errors DC %i: %i\n", i, logger_errors[i]);
                    }
                #endif
                // Muestro el valor en hexadecimal
                sprintf(aux_buffer, "Valor: 0x%X", data);
                printf(aux_buffer);
                printf("\n");
                #if LCD_ON
                    lcd_set_cursor(0, 0);
                    lcd_string(aux_buffer);
                        
                    lcd_set_cursor(1, 0);
                    lcd_string(text_data);
                #else
                    if (data == 0xa796 || data == 0x1234)
                        gpio_put(PICO_DEFAULT_LED_PIN, true);
                    else
                        gpio_put(PICO_DEFAULT_LED_PIN, false);
                #endif
            }

            // Espero el proximo bit
            bit_captured = false;
        }
    }
}


void init_default_i2c(uint16_t f_khz) {
    
    i2c_init(I2C_PORT, f_khz*1000);
    gpio_set_function(SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_GPIO);
    gpio_pull_up(SCL_GPIO);
}