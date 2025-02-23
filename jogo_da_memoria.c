#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/adc.h"
#include <ctype.h>
#include <string.h>
#include "pico/binary_info.h"
#include "ws2818b.pio.h" // Biblioteca para controlar os LEDs WS2818B
#include "inc/ssd1306.h" // Biblioteca para controlar o display OLED
#include "hardware/i2c.h"

// Definições de constantes
#define LED_COUNT 25          // Número total de LEDs na matriz
#define LED_PIN 7             // Pino GPIO conectado aos LEDs
#define MATRIX_WIDTH 5        // Largura da matriz de LEDs
#define MATRIX_HEIGHT 5       // Altura da matriz de LEDs
#define BUTTON_A 5            // Pino GPIO do botão A
#define BUTTON_B 6            // Pino GPIO do botão B
#define DEADZONE 500          // Zona morta para o joystick
#define MAX_LENGTH 16         // Comprimento máximo do cursor
#define SEQUENCE_LENGTH 10    // Comprimento máximo da sequência do jogo

// Pinos do joystick
const int vRX = 26;           // Pino GPIO do eixo X do joystick
const int VRY = 27;           // Pino GPIO do eixo Y do joystick
const int ADC_CHANNEL_0 = 0;  // Canal ADC para o eixo X
const int ADC_CHANNEL_1 = 1;  // Canal ADC para o eixo Y

// Constantes para o display OLED
const uint I2C_SDA = 14;      // Pino SDA do I2C
const uint I2C_SCL = 15;      // Pino SCL do I2C

// Estrutura para representar um LED RGB
typedef struct {
    uint8_t G, R, B;
} npLED_t;

// Estrutura para representar uma posição na matriz de LEDs
typedef struct {
    int x, y;
} Position;

// Variáveis globais
npLED_t leds[LED_COUNT];      //  Array que armazena o estado (cores) de cada LED na matriz.
PIO np_pio;                   // PIO usado para controlar os LEDs
uint sm;                      // State machine do PIO

Position cursor[MAX_LENGTH];  // Array para armazenar as posições do cursor
int length = 1;               // Comprimento atual do cursor

int sequence[SEQUENCE_LENGTH]; // Array para armazenar a sequência de LEds que o jogador deve memorizar.
int current_sequence_length = 1; // Comprimento atual da sequência
int current_step = 0;          // Passo atual na sequência durante a verificação da entrada do jogador

// Cores para os LEDs (R, G, B)
uint8_t colors[SEQUENCE_LENGTH][3] = {
    {163, 85, 85},   // Vermelho
    {75, 209, 144},  // Verde
    {108, 109, 168}, // Azul
    {194, 186, 97},  // Amarelo
    {163, 85, 136},  // Magenta
    {149, 210, 245}, // Ciano
    {150, 98, 179},  // Roxo
    {222, 165, 104}, // Laranja
    {128, 128, 128}, // Cinza
    {82, 156, 87}    // Verde escuro
};

// Funções para o display OLED
void display_init() {
    i2c_init(i2c1, 400 * 1000); // Configura I2C com 400 kHz para comunicação com o display OLED.
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); //Define o pino SDA para a função I2C.
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); //Define o pino SCL para a função I2C.
    gpio_pull_up(I2C_SDA); //Ativa resistores de pull-up nos pinos I2C.
    gpio_pull_up(I2C_SCL); //Ativa resistores de pull-up nos pinos I2C.

    ssd1306_init(); // Inicializa o display OLED
}

void display_message(const char *line1, const char *line2) {
    // Definir a área de renderização
    struct render_area frame_area = {
        start_column : 0,
        end_column : ssd1306_width - 1,
        start_page : 0,
        end_page : ssd1306_n_pages - 1
    };
    calculate_render_area_buffer_length(&frame_area);

    // Limpar o buffer
    uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, ssd1306_buffer_length);

    // Criar cópias não constantes das strings
    char line1_copy[MAX_LENGTH];
    char line2_copy[MAX_LENGTH];
    strncpy(line1_copy, line1, MAX_LENGTH - 1);
    line1_copy[MAX_LENGTH - 1] = '\0'; // Garantir terminação nula
    strncpy(line2_copy, line2, MAX_LENGTH - 1);
    line2_copy[MAX_LENGTH - 1] = '\0'; // Garantir terminação nula

    // Desenhar as strings no buffer
    ssd1306_draw_string(ssd, 5, 10, line1_copy); // Primeira linha
    ssd1306_draw_string(ssd, 5, 20, line2_copy); // Segunda linha

    // Renderizar no display
    render_on_display(ssd, &frame_area);
}

// Funções para os LEDs
void npInit(uint pin) {
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;
    sm = pio_claim_unused_sm(np_pio, false);
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
}

int get_led_index(int x, int y) {
    return (y % 2 == 0) ? (y * MATRIX_WIDTH + x) : (y * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - x));
}

void npSetLED(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    int index = get_led_index(x, y);
    if (index >= 0 && index < LED_COUNT) {
        leds[index].R = r;
        leds[index].G = g;
        leds[index].B = b;
    }
}

void npClear() {
    for (uint i = 0; i < LED_COUNT; ++i) {
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

void npwrite() {
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100);
}

// Funções para o joystick
void setup_joystick() {
    adc_init(); //Inicializa o módulo ADC para leitura dos valores analógicos do joystick.
    adc_gpio_init(vRX); //Configura os pinos do joystick como entradas ADC.
    adc_gpio_init(VRY); //Configura os pinos do joystick como entradas ADC.
}

void joystick_read_axis(uint16_t *x, uint16_t *y) {
    adc_select_input(ADC_CHANNEL_0);
    sleep_us(2);
    *y = adc_read();  // Eixo Y
    adc_select_input(ADC_CHANNEL_1);
    sleep_us(2);
    *x = adc_read();  // Eixo X
}

void move_cursor() {
    uint16_t valor_x, valor_y;
    joystick_read_axis(&valor_x, &valor_y);
    int new_x = cursor[0].x;
    int new_y = cursor[0].y;

    // Lógica para o eixo X
    if (valor_x < 1000 && (2048 - valor_x) > DEADZONE && new_x < MATRIX_WIDTH - 1) new_x++; // Direita
    else if (valor_x > 3000 && (valor_x - 2048) > DEADZONE && new_x > 0) new_x--; // Esquerda

    // Lógica para o eixo Y
    if (valor_y < 1000 && (2048 - valor_y) > DEADZONE && new_y > 0) new_y--; // Cima
    else if (valor_y > 3000 && (valor_y - 2048) > DEADZONE && new_y < MATRIX_HEIGHT - 1) new_y++; // Baixo

    // Atualiza a posição do cursor
    for (int i = length - 1; i > 0; i--) {
        cursor[i] = cursor[i - 1];
    }

    cursor[0].x = new_x;
    cursor[0].y = new_y;
}

// Funções para os botões
void setup_button() {
    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN); //Inicializa o pino do botão A como entrada.
    gpio_pull_up(BUTTON_A); //Ativa resistor de pull-up no pino do botão A
}

bool is_button_a_pressed() {
    return !gpio_get(BUTTON_A); // Retorna true se o botão estiver pressionado
}

void setup_button_b() {
    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);
}

bool is_button_b_pressed() {
    return !gpio_get(BUTTON_B); // Retorna true se o botão estiver pressionado
}

// Funções do jogo
void generate_sequence() {
    for (int i = 0; i < SEQUENCE_LENGTH; i++) {
        sequence[i] = rand() % LED_COUNT;
    }
}

void show_sequence() {
    for (int i = 0; i < current_sequence_length; i++) {
        int led_index = sequence[i];
        npSetLED(led_index % MATRIX_WIDTH, led_index / MATRIX_WIDTH, colors[i][0] / 10, colors[i][1] / 10, colors[i][2] / 10);
        npwrite();
        sleep_ms(500); // Tempo que o LED fica aceso
        npClear();
        npwrite();
        sleep_ms(200); // Tempo entre os LEDs
    }
}

bool check_player_input() {
    for (int i = 0; i < current_sequence_length; i++) {
        bool pressed_correctly = false;

        while (!pressed_correctly) {
            move_cursor();
            npClear();
            npSetLED(cursor[0].x, cursor[0].y, 2, 2, 2); // Cursor 
            npwrite();

            if (is_button_a_pressed()) {
                sleep_ms(200); // Debounce
                int led_index = cursor[0].y * MATRIX_WIDTH + cursor[0].x;

                if (led_index == sequence[i]) {
                    pressed_correctly = true; // Avança na sequência
                } else {
                    return false; // Se errar um LED, perde o jogo
                }
            }
            sleep_ms(100);
        }
    }
    return true;
}

int main() {
    stdio_init_all();
    npInit(LED_PIN);
    setup_joystick();
    setup_button();
    setup_button_b(); // Configura o botão B
    display_init(); // Inicializa o display OLED

    cursor[0].x = 2;
    cursor[0].y = 2;

    // Mensagem de boas-vindas
    display_message("Bem-vindos ao", "Luminus Memory");
    sleep_ms(4000);



    // Aguarda o jogador pressionar o botão para começar
    display_message("Aperte A para", "iniciar o jogo");
    while (!is_button_a_pressed()) {
        sleep_ms(100);
    }
    // Mensagens para ensinar o jogador
    display_message("MOVA O JOYSTICK", "PARA NAVEGAR");
    sleep_ms(4000);
    display_message("APERTE A PARA", "ESCOLHER O LED");
    sleep_ms(4000);
    display_message("PREPARAARR", "JAA");
    sleep_ms(3000);

    int acertos = 0; // Contador de acertos

    while (true) {
        generate_sequence();
        current_sequence_length = 1;

        while (current_sequence_length <= SEQUENCE_LENGTH) {
            show_sequence();

            if (!check_player_input()) {
                display_message("Voce perdeu!", "Tente novamente");
                npClear();
                npwrite();
                sleep_ms(2000);
                break;
            }

            acertos++; // Incrementa o contador de acertos

            if (acertos == 3) 
            {
                display_message("UAU CONTINUE", "DESSE JEITO!");
                sleep_ms(2000);
            } 
            else if(acertos == 5)
            {
                display_message("VOCE E", "IMPRESSIONANTE!");
                sleep_ms(2000);
            }
            else if( acertos == 7)
            {
                display_message("VOCE ESTA", "QUASE LA!!!");
                sleep_ms(2000);
            }
            else if (acertos == 10) {
                display_message("Voce ganhou!", "Parabens!");
                sleep_ms(2000);
                display_message("A - FINALIZAR", "B - CONTINUAR");
                npClear();
                npwrite();

                // Aguarda o jogador pressionar o botão A ou B
                while (true) {
                    if (is_button_a_pressed()) {
                        return 0; // Finaliza o jogo
                    } else if (is_button_b_pressed()) {
                        acertos = 0; // Reinicia o contador de acertos
                        break; // Reinicia o jogo
                    }
                    sleep_ms(100);
                }
            }
            current_sequence_length++;
        }
    }
    return 0;
}