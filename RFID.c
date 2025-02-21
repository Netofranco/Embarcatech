//Autor: Arthur Franco Neto
//Data: 19 de Fevereiro de 2025
//VS1 - Validacao em Bancada

//Programa para monitoramento de criancas no tranporte escolar atraves de etiquetas RFID

//**********************************************************************************************/
//Inclusao de Bibliotecas
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <string.h> //some functions need NULL to be defined
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "inc/ssd1306.h"
#include "hardware/i2c.h"
#include "inc/ssd1306_i2c.c"

// Biblioteca gerada pelo arquivo .pio durante compilação.
#include "ws2818b.pio.h"

//Include criados para organizacao do codigo
#include "mensagens.c"                      //Definicoes de Mensagens para o Display OLED

//**********************************************************************************************/
//Definicoes do sistema

//Definicoes de Pinos GPIO
#define LED_RGB_G 11            //define pino de controle do LED RGB cor Verde como GPIO 11
#define LED_RGB_B 12            //define pino de controle do LED RGB cor Azul como GPIO 12
#define LED_RGB_R 13            //define pino de controle do LED RGB cor Vermelho como GPIO 11

#define BOTAO_AVANCAR 5         //define BOTAO AVANCAR (BOTAO A) como GPIO 5

#define BUZZER_PIN 21           //define pino de controle do Buzzer como GPIO 21

#define LED_COUNT 25            //define o numero de leds da matriz
#define LED_PIN 7               //define pino de controle da Matriz NEOPIXEL
#define max_alunos LED_COUNT    //define o max de alunos permitidos no sistema, limitado a matriz de led, para quantidades maiores sera necessario outra matriz

// Definicoes SPI para Leitor RFID-RC522
//#define SPI_PORT spi0
//#define RESET_PIN 20
/*
static const uint cs_pin = 17;
static const uint sck_pin = 18;
static const uint mosi_pin = 19;
static const uint miso_pin = 16;
*/

#define I2C_SDA 14
#define I2C_SCL 15

//**********************************************************************************************/
//Constantes utilizadas no programa e variaveis globais

//Utilizados no PWM do Buzzer
#define BUZZER_FREQUENCY 4000   //Configuração da frequência do buzzer (em Hz)
const uint16_t PERIODO = 2000;  //Periodo do PWM, valor maximo do contador
const float DIVISOR_PWM = 625.0; //Divisor fracional do clock para o PWM

//Variaveis para controle dos alunos
uint8_t n_alunos_embarc = 0;    //Armazena o numero de alunos presente no onibus
uint8_t n_alunos_desemb = 0;    //Manipulacao do numero de alunos nas demais etapas do sistema

//Flags
volatile bool led_on_off = false;   //Flag para indicar se o led RGB esta aceso ou apagado (true - ligado / false - desligado)
volatile bool led_ativo = false;    //Flag para indicar se o sistema pode trabalhar com o LED RGB (true - ON / false - OFF)
volatile bool botao_liberado = false;   //Flag para indicar se o BOTAO AVANCAR tem efeito, ou se sera ignorado
volatile bool alarme_disparado = false; //flag para indicar se o alarme esta disparado ou nao
volatile bool alarme_on = false;    //Flag para indicar se o alarme esta ligado ou nao
volatile bool mudanca_etapa = false;    //Flag Indicando mudanca de etapa dop sistema
volatile uint8_t Etapa = 1;             //Variavel que controla o as etapas do sistema

uint8_t ssd[ssd1306_buffer_length]; //Definicao do tamanho do Buffer
//**********************************************************************************************/
//******************************** Definicoes de funcoes ***************************************/
//**********************************************************************************************/

//Rotinas para utilizacao da Matriz NEOPIXEL
// Definição de pixel GRB
struct pixel_t {
    uint8_t G, R, B;            // Três valores de 8-bits compõem um pixel.
};

typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t;        // Mudança de nome de "struct pixel_t" para "npLED_t" por clareza.

npLED_t leds[LED_COUNT];        // Declaração do buffer de pixels que formam a matriz.

// Variáveis para uso da máquina PIO.
PIO np_pio;
uint sm;

//Inicializa a máquina PIO para controle da matriz de LEDs.
void npInit(uint pin){
    // Cria programa PIO.
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;

    // Toma posse de uma máquina PIO.
    sm = pio_claim_unused_sm(np_pio, false);
    if (sm < 0) {
        np_pio = pio1;
        sm = pio_claim_unused_sm(np_pio, true); // Se nenhuma máquina estiver livre, panic!
    }

    // Inicia programa na máquina PIO obtida.
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);

    // Limpa buffer de pixels.
    for (uint i = 0; i < LED_COUNT; ++i) {
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

//Atribui uma cor RGB a um LED.
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

//Limpa o buffer de pixels.
void npClear() {
    for (uint i = 0; i < LED_COUNT; ++i)
    npSetLED(i, 0, 0, 0);
}

//Escreve os dados do buffer nos LEDs.
void npWrite() {
    // Escreve cada dado de 8-bits dos pixels em sequência no buffer da máquina PIO.
    for (uint i = 0; i < LED_COUNT; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100); // Espera 100us, sinal de RESET do datasheet.
}

//----------------------------------------------------------------------------------
//manipulacoes da visualizacao dos alunos atraves da matriz
//as rotinas recebem um valor inteiro correspondente a posicao que deve preencher

void entrada_matriz_verm(int posicao_led){
    npSetLED(posicao_led, 0x40, 0, 0);    //acende o led na posicao especificada na cor vermelho
    npWrite();
}

void entrada_matriz_amar(int posicao_led){
    npSetLED(posicao_led, 0x40, 0x40, 0);   //acende o led na posicao especificada na cor amarelo
    npWrite();
}

void entrada_matriz_verd(int posicao_led){
    npSetLED(posicao_led, 0, 0x40, 0);    //acende o led na posicao especificada na cor verde
    npWrite();
}

void entrada_matriz_desl(int posicao_led){
    npSetLED(posicao_led, 0, 0, 0);         //desliga o led na posicao especificada
    npWrite();
}

//**********************************************************************************************/
//Funcoes de manipulacao do Led RGB para indicacao do Status da Operacao com as etiquetas

//Configuracao dos Pinos do LED RGB
void config_GPIO_OUT (uint GPIO_LED){
    gpio_init(GPIO_LED);
    gpio_set_dir(GPIO_LED, GPIO_OUT);
    gpio_put(GPIO_LED, 0);
}

//Funcao de desligar as tres cores do Led
int64_t Desliga_LedRGB(){
    led_ativo = false;
    gpio_put(LED_RGB_R, 0); //
    gpio_put(LED_RGB_B, 0); //
    gpio_put(LED_RGB_G, 0); //
    return 0;
}

//Funcao que Liga o Led Vermelho e Desliga todos os outros
void Liga_LedRGB_Red(){
    gpio_put(LED_RGB_R, 1); //
    gpio_put(LED_RGB_B, 0); //
    gpio_put(LED_RGB_G, 0); //
}

//Funcao que Liga o Led Verde e Desliga todos os outros
void Liga_LedRGB_Green(){
    gpio_put(LED_RGB_R, 0); //
    gpio_put(LED_RGB_B, 0); //
    gpio_put(LED_RGB_G, 1); //
}

//Funcao que Liga o Led Azul e Desliga todos os outros
void Liga_LedRGB_Blue(){
    gpio_put(LED_RGB_R, 0); //
    gpio_put(LED_RGB_B, 1); //
    gpio_put(LED_RGB_G, 0); //    
}

//Funcao que Liga o Led Amarelo (Vermelho e Verde) e Desliga todos os outros
void Liga_LedRGB_Yellow(){
    gpio_put(LED_RGB_R, 1); //
    gpio_put(LED_RGB_B, 0); //
    gpio_put(LED_RGB_G, 1); //    
}

//Funcao para piscar Led Verde atraves de alarmes
int64_t inverte_led_Green(){
    if(led_ativo == true){              //So inverte o sinal do Led se a flag ativo for verdadeira
        if(led_on_off == true){         // se o led estiver ligado
            gpio_put(LED_RGB_G, 0);     // desliga o Led
            led_on_off = false;         // coloca a flag de led apagado       
        }
        else if(led_on_off == false){   //senao, se o led estiver desligado
            gpio_put(LED_RGB_G, 1);     // liga o led
            led_on_off = true;          // coloca a flag de led ligado
        }
    // inicia um novo alarme para inverter o led dentro do periodo estipulado
        add_alarm_in_ms(100, inverte_led_Green, NULL, false);
    }
    return 0;
}

//Funcao para piscar Led Verde atraves de alarmes
int64_t inverte_led_Red(){
    if(led_ativo == true){              //So inverte o sinal do Led se a flag ativo for verdadeira
        if(led_on_off == true){         // se o led estiver ligado
            gpio_put(LED_RGB_R, 0);     // desliga o Led
            led_on_off = false;         // coloca a flag de led apagado       
        }
        else if(led_on_off == false){   // senao, se o led estiver desligado
            gpio_put(LED_RGB_R, 1);     // liga o led
            led_on_off = true;          // coloca a flag de led ligado
        }
    // inicia um novo alarme para inverter o led dentro do periodo estipulado
        add_alarm_in_ms(100, inverte_led_Red, NULL, false);
    }
    return 0;    
}

//**********************************************************************************************/
//Funcoes para utilizacao do Buzzer

//Configuracao PWM do Buzzeer
void setup_pwm_buzzer(){
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);   // Configurar o pino como saída de PWM
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN); // Obter o slice do PWM associado ao pino

    // Configurar o PWM com frequência desejada
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / (BUZZER_FREQUENCY * 4096)); // Divisor de clock
    pwm_init(slice_num, &config, true); //Inicializacao do PWM

    pwm_set_gpio_level(BUZZER_PIN, 0);  //Configura o Duty Cicle inicial para 0
}

//Funcao para bipar(Manter meio tempo ligado e meio tempo desligado)
void beep(int quantidade, uint duration_ms){
    for (int i = 0; i < quantidade; i++){
        pwm_set_gpio_level(BUZZER_PIN, 1000);   // Configurar o duty cycle para 100% (ativo)
        sleep_ms(duration_ms);                  // Temporização
        pwm_set_gpio_level(BUZZER_PIN, 0);      // Desativar o sinal PWM (duty cycle 0)
        sleep_ms(duration_ms);                  // Temporizacao
    }
}

//Funcao para indicar um erro 3 bips longos
void bipar_erro(){
    beep(3, 200);
}

//Funcao para indicar Sucesso 2 bips curtos
void bipar_ok(){
    beep(2, 50);
}

//Função para indicar mudança de Etapa 3 bips médios
void bipar_mudanca_etapa(){
    beep(3, 100);
}

//Funcao para alarme sonoro
int64_t disparar_alarme(){
    if(alarme_disparado == true) {                          //So inverte o Duty Cicle se a flag alarme disparado for verdadeira
        if (alarme_on == true){                             //se o alarme esta ligado
            pwm_set_gpio_level(BUZZER_PIN, 0);              //Configura Duty Cicle para 0
            alarme_on = false;                              //Coloca Flag como Buzzer Desligado
        }
        else  if (alarme_on == false){                      //Senao se o alarme esta desligado
            pwm_set_gpio_level(BUZZER_PIN, 2000);           //Configura Duty Cicle em 100%
            alarme_on = true;                               //Coloca Flag como Buzzer ligado
        }
        add_alarm_in_ms(250, disparar_alarme, NULL, false); //periodo dividido por 2, pois metade do tempo fica aceso e metade apagado
    }
    return 0;
}

//Funcao para desligar Alarme
void desligar_alarme(){
    pwm_set_gpio_level(BUZZER_PIN, 0);      //Coloca o Duty Cicle do Buzzer em 0
}

//**********************************************************************************************/
   // Preparar área de renderização para o display (ssd1306_width pixels por ssd1306_n_pages páginas)
struct render_area frame_area = {
    start_column : 0,
    end_column : ssd1306_width - 1,
    start_page : 0,
    end_page : ssd1306_n_pages - 1
};

void StatusRead_OK(){
    led_ativo = true;
    Liga_LedRGB_Green();
    add_alarm_in_ms(600, Desliga_LedRGB, NULL, false);
    add_alarm_in_ms(100, inverte_led_Green, NULL, false);
    bipar_ok();
}

void StatusRead_Error(){
    led_ativo = true;
    Liga_LedRGB_Red();
    add_alarm_in_ms(1200, Desliga_LedRGB, NULL, false);
    add_alarm_in_ms(200, inverte_led_Red, NULL, false);
    bipar_erro();
    sleep_ms(1000);
}

//Funcoes para o Display SSD
void zerar_display(){
    // zera o display inteiro
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
}

int64_t ssd1306_turn_off(){
    uint8_t commands[] = {
    ssd1306_set_display | 0x00,
    };
    ssd1306_send_command_list(commands, count_of(commands));
    return 0;
}
    
void ssd1306_turn_on(){
    uint8_t commands[] = {
    ssd1306_set_display | 0x01,
    };
    ssd1306_send_command_list(commands, count_of(commands));
}

int64_t limpa_erro(){
    int y = 24;
    int x = 0;
    for (int i =0; i < 2; i++){
        for (int j = 0; j < 15; j++){
            ssd1306_draw_char(ssd, x, y, ' ');
            render_on_display(ssd, &frame_area);
            x = x+8;
        }
        x = 0;
        y = y+8;
    }
    return 0;
}

void gpio_callback(uint gpio, uint32_t events) {
    if (botao_liberado == true && alarme_disparado == false && mudanca_etapa == false){  //Interrupcao causada pelo Botao Avancar, para mudar o nivel o alarme nao pode estar disparado
        if(Etapa < 8){
            Etapa++;
        }
        else{
            Etapa = 8;
        }
        mudanca_etapa = true;
        botao_liberado = false;  
    }
    printf("Etapa %d", Etapa);
}

//**********************************************************************************************/
//**********************************************************************************************/
void Display_MSG_Cartao_Motorista(){
    //Criacao dos eixos x e y para o desenho do display
    int x = 0;      //Eixo horizontal do display
    int y = 0;      //Eixo Vertical do display
    //Exibe mensagem inicial para posicionar cartao do motorista
    for (uint i = 0; i < count_of(msg_cartao_motorita); i++) {
        ssd1306_draw_string(ssd, 5, y, msg_cartao_motorita[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
}

void Display_ERROR_Cartao_Invalido(){
    Desliga_LedRGB();
   //Criacao dos eixos x e y para o desenho do display
    int x = 0;      //Eixo horizontal do display
    int y = 24;      //Eixo Vertical do display
    //Exibe mensagem inicial para posicionar cartao do motorista
    for (uint i = 0; i < count_of(msg_cartao_motorista_invalido); i++) {
        ssd1306_draw_string(ssd, 5, y, msg_cartao_motorista_invalido[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
    add_alarm_in_ms(2000, limpa_erro, NULL, false);
}

void Display_ERROR_Leitura_Tag(){
    Desliga_LedRGB();
   //Criacao dos eixos x e y para o desenho do display
    int x = 0;      //Eixo horizontal do display
    int y = 24;      //Eixo Vertical do display
    //Exibe mensagem inicial para posicionar cartao do motorista
    for (uint i = 0; i < count_of(msg_erro_leitura); i++) {
        ssd1306_draw_string(ssd, 5, y, msg_erro_leitura[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
    add_alarm_in_ms(2000, limpa_erro, NULL, false);
}

void Display_MSG_Etapa1(){
    //Criacao dos eixos x e y para o desenho do display
    int x = 0;      //Eixo horizontal do display
    int y = 0;      //Eixo Vertical do display
    //Exibe mensagem inicial para posicionar cartao do motorista
    for (uint i = 0; i < count_of(msg_Etapa1); i++) {
        ssd1306_draw_string(ssd, 5, y, msg_Etapa1[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);

}

void Display_MSG_Etapa2(){
    bipar_mudanca_etapa();
    //Criacao dos eixos x e y para o desenho do display
    int x = 0;      //Eixo horizontal do display
    int y = 0;      //Eixo Vertical do display
    //Exibe mensagem inicial para posicionar cartao do motorista
    for (uint i = 0; i < count_of(msg_Etapa2); i++) {
        ssd1306_draw_string(ssd, 5, y, msg_Etapa2[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
    sleep_ms(100);
}

void Display_MSG_Etapa3(){
    bipar_mudanca_etapa();
    //Criacao dos eixos x e y para o desenho do display
    int x = 0;      //Eixo horizontal do display
    int y = 0;      //Eixo Vertical do display
    //Exibe mensagem inicial para posicionar cartao do motorista
    for (uint i = 0; i < count_of(msg_Etapa3); i++) {
        ssd1306_draw_string(ssd, 5, y, msg_Etapa3[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
    sleep_ms(100);
}

void Display_MSG_Etapa4(){
    bipar_mudanca_etapa();
    //Criacao dos eixos x e y para o desenho do display
    int x = 0;      //Eixo horizontal do display
    int y = 0;      //Eixo Vertical do display
    //Exibe mensagem inicial para posicionar cartao do motorista
    for (uint i = 0; i < count_of(msg_Etapa4); i++) {
        ssd1306_draw_string(ssd, 5, y, msg_Etapa4[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
    sleep_ms(100);
}

void Display_MSG_Final(){
    bipar_mudanca_etapa();
    //Criacao dos eixos x e y para o desenho do display
    int x = 0;      //Eixo horizontal do display
    int y = 0;      //Eixo Vertical do display
    //Exibe mensagem inicial para posicionar cartao do motorista
    for (uint i = 0; i < count_of(msg_Final); i++) {
        ssd1306_draw_string(ssd, 5, y, msg_Final[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
    sleep_ms(100);
}


void Display_MSG_Verificacao(){
    //Criacao dos eixos x e y para o desenho do display
    int x = 0;      //Eixo horizontal do display
    int y = 0;      //Eixo Vertical do display
    //Exibe mensagem inicial para posicionar cartao do motorista
    for (uint i = 0; i < count_of(msg_Verificar_Alunos); i++) {
        ssd1306_draw_string(ssd, 5, y, msg_Verificar_Alunos[i]);
        y += 8;
    }
    render_on_display(ssd, &frame_area);
}

void Display_ERROR_Linha_Incorreta(){
    Desliga_LedRGB();
    //Criacao dos eixos x e y para o desenho do display
     int x = 0;      //Eixo horizontal do display
     int y = 24;      //Eixo Vertical do display
     //Exibe mensagem inicial para posicionar cartao do motorista
     for (uint i = 0; i < count_of(msg_Linha_incorreta); i++) {
         ssd1306_draw_string(ssd, 5, y, msg_Linha_incorreta[i]);
         y += 8;
     }
     render_on_display(ssd, &frame_area);
     add_alarm_in_ms(2000, limpa_erro, NULL, false);
}

void Display_ERROR_Tag_Invalida(){
    Desliga_LedRGB();
    //Criacao dos eixos x e y para o desenho do display
     int x = 0;      //Eixo horizontal do display
     int y = 24;      //Eixo Vertical do display
     //Exibe mensagem inicial para posicionar cartao do motorista
     for (uint i = 0; i < count_of(msg_tag_invalida); i++) {
         ssd1306_draw_string(ssd, 5, y, msg_tag_invalida[i]);
         y += 8;
     }
     render_on_display(ssd, &frame_area);
     add_alarm_in_ms(2000, limpa_erro, NULL, false);
}

void Display_ERROR_Aluno_Repetido(){
    Desliga_LedRGB();
    //Criacao dos eixos x e y para o desenho do display
     int x = 0;      //Eixo horizontal do display
     int y = 24;      //Eixo Vertical do display
     //Exibe mensagem inicial para posicionar cartao do motorista
     for (uint i = 0; i < count_of(msg_aluno_repetido); i++) {
         ssd1306_draw_string(ssd, 5, y, msg_aluno_repetido[i]);
         y += 8;
     }
     render_on_display(ssd, &frame_area);
     add_alarm_in_ms(2000, limpa_erro, NULL, false);    
}
//**********************************************************************************************/
//**********************************************************************************************/
//Definicao de estrutura para salvar os dados das Tags dos Alunos
typedef struct {
    char nome[10];
    char escola_linha[10];
    uint8_t tag[4];
    bool embarque;
} meusAlunos;
meusAlunos alunos[max_alunos];

//Include para comunicacao com o Dispositivo MFRC522 que faz a leitura das Tags
#include "mfrc522.c"

//**********************************************************************************************/
//******************************** Programa Principal ******************************************/
//**********************************************************************************************/
void main() {
    stdio_init_all();
    
    //Inicializacao dos Pinos para controle do LED RGB que indica o Status do Leitor
    config_GPIO_OUT(LED_RGB_G); //Led Verde
    config_GPIO_OUT(LED_RGB_R); //Led Vermelho
    config_GPIO_OUT(LED_RGB_B); //Led Azul

    // Configura o pino do botão como entrada com resistor de pull-up interno.
    gpio_init(BOTAO_AVANCAR);                         // inicia botao A
    gpio_set_dir(BOTAO_AVANCAR, GPIO_IN);             // configura GPIO do botao como entrada
    gpio_pull_up(BOTAO_AVANCAR);                      // Habilita o resistor pull-up interno para evitar leituras incorretas.

    //Configuracao do Buzzer
    setup_pwm_buzzer();

    // Inicializa matriz de LEDs NeoPixel.
    npInit(LED_PIN);
    npClear();

    //Inicia Valores Struct alunos
    for(int i=0; i<max_alunos; i++) {
        strcpy(alunos[i].nome, "\0");
        strcpy(alunos[i].escola_linha, "\0");
        for(int j=0; j<4; j++){
            alunos[i].tag[j] = 0xff;
        }
        alunos[i].embarque = false;
    }

    //Estrutura dos dados da EEPROM das Tags
    //Sector 0 - Bloco 0 - UID
    //Sector 0 - Bloco 1 - Nome do Aluno ou ADMIN
    //Sector 0 - Bloco 2 - Linha Onibus *Utilizado 4 bytes - primeiro fixo em 0xAA

    //Define TAG Invalida 
    uint8_t tagNull[] = {0x00, 0x00, 0x00, 0x00};
    //Define estrutura da Tag Admin
    uint8_t tagADM[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t NomeAdmin[10] = {'A', 'D', 'M', 'I', 'N', '*', '*', '*', '*', '*'}; //Validacao da TAG ADMIN
    uint8_t LinhaOnibus[4] = {}; //4 bytes reservados para a linha, o primeiro obrigatoriamente deve ser 0xAA

    //Inicialização do Módulo RFID
    MFRC522Ptr_t mfrc = MFRC522_Init();
    PCD_Init(mfrc, spi0);

     // Inicialização do i2c para Display OLED
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);           //Inicia I2C 1
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);          //Configura GPIO como I2c
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);          //Configura GPIO como I2c
    gpio_pull_up(I2C_SDA);                              //Ativa resistor de Pull up
    gpio_pull_up(I2C_SCL);                              //Ativa resistor de Pull up
    ssd1306_init();                                     // Processo de inicialização completo do OLED SSD1306
    calculate_render_area_buffer_length(&frame_area);
    zerar_display();

    Display_MSG_Cartao_Motorista();                     //Mensagem para aproximar cartao do motorista

    //******************************************************************/
    //Esperando pelo cartao do Motorista para fazer o cadastro da viagem
    while(1){
        printf("Aproxime o cartao Motorista no leitor \n\r");       //Debug serial
        Liga_LedRGB_Yellow();                                       //Ativa Led amarelo para indicar espera de leitura da TAG Motorista
        while(!PICC_IsNewCardPresent(mfrc));                        //Aguarda reconhecer cartao
        printf("Identificando cartao morotirsta...\n\r");           //Debug serial

        PICC_ReadCardSerial(mfrc);                                  //Faz a leitura da TAG
        if(memcmp(mfrc->uid.uidByte, tagNull, 4) != 0) {            //Verifica se a TAG valida
            printf("Uid Adm is: ");                                 //Debug serial
            for (int i = 0; i < 4; i++){                            //Se for copia para TAG ADM
                tagADM[i] = mfrc-> uid.uidByte[i];
                printf("%x ", tagADM[i]);                           //Debug serial TAG ADM
            }

            PICC_ReadBlock(mfrc, &(mfrc->uid), 1);                  //Realiza a leitura da EEPROM da TAG
            //verifica se  os dados da EEPROM correspondente ao Cartao do Motorista esta OK
            if ((memcmp(mfrc->uid.eeprom, NomeAdmin, 10) == 0) && mfrc->uid.eeprom[10] == 0xAA){ 
                for (int i=0; i < 4; i++){
                    LinhaOnibus[i] = mfrc->uid.eeprom[10+i];
                }
                StatusRead_OK();                                    //Status Ok
                printf("\n\r");
                break;                                              //Condicao de saida do loop é somente quando reconhece o cartao ADM
            }
            else{
                Display_ERROR_Cartao_Invalido();                    //Exibe Mensagem de Erro
                StatusRead_Error();                                 //Status de Erro
            }   
        }
        else{
            Display_ERROR_Leitura_Tag();                            //Exibe Mensagem de Erro
            StatusRead_Error();                                     //Status de Erro
        }  
    }
        Desliga_LedRGB();                                           //Desliga Led RGB para nao indicar operacao
        printf("Retire o cartao do leitor \n\r");                   //Debug serial
        //sleep_ms(1000);

    gpio_set_irq_enabled_with_callback(BOTAO_AVANCAR, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);  //Habilita Interrupcao para Pino Avancar

    //******************************************************************/
    Display_MSG_Etapa1();                                           //Mensagem para Avisar da Etapa de Embarque

    while(1){
        printf("\n Etapa %d", Etapa);                               //Debug Serial
        printf("\n Aproxime o proximo cartao no leitor \n\r");      //Debug Serial
        while(1){                                                   //Loop Infinito
            botao_liberado = false;                                 //Desabilita Funcao do Botao Avancar
            Liga_LedRGB_Blue();                                     //Liga Led RGB Azul para indicar leitura das Tags dos Alunos
            if(PICC_IsNewCardPresent(mfrc)){                        //Testa se tem tag presente
                break;                                              //Se tiver sai do loop infinito e vai para o Switch casa
            };
            botao_liberado = true;                                  //Habilita Funcao do Botao Avancar
            sleep_ms(50);                                           //Garante um tempo para proxima leitura do RFID 
            if (mudanca_etapa == true){                             //Se houve mudanca de Etapa
                break;                                              //Sai do Loop Infinito
            }
        }

        //Switch Case para determinar quais Etapas o sistema deve executar
        switch (Etapa){
            //******************************************************************/
            //Etapa de Embarque dos alunos no Onibus
            case 1:
                printf("Lendo cartao...\n\r");                      //Debug Serial
                PICC_ReadCardSerial(mfrc);                          //Faz a leitura do ID do cartao

                //verifica se  os dados da TAG sao validos e que nao eh a TAG correspondente ao Cartao do Motorista esta OK
                if(memcmp(mfrc->uid.uidByte, tagNull, 4) != 0 && memcmp(mfrc->uid.uidByte, tagADM, 4 != 0)) {        //compara o valor do ID lido
                    //Se for uma tag valida != 0x00 e != tag Admin, copia para a proxima posicao o ID da Tag Lida
                    printf("\n Id do Aluno: ");                     //Debug Serial
                    for (int i=0; i < 4; i++){                                                                          
                        alunos[n_alunos_embarc].tag[i] = mfrc->uid.uidByte[i];
                        printf("%x ", alunos[n_alunos_embarc].tag[i]);
                    }

                    PICC_ReadBlock(mfrc, &(mfrc->uid), 1);          //Faz a Leitura dos dados da EEPROM necessario para identificacao do Aluno

                    //Verifica se os dados da EEPROM Estao validos, se estiverem copia para o cadastro
                    if (mfrc->uid.eeprom[0] != '\0' && mfrc->uid.eeprom[10] == 0xAA){     
                        //Se estiver copia os dados do nome do Aluno e Linha/Escola
                        for (int i=0; i < 10; i++){
                            alunos[n_alunos_embarc].nome[i] = mfrc->uid.eeprom[i];
                            alunos[n_alunos_embarc].escola_linha[i] = mfrc->uid.eeprom[10+i];
                        }
                        //Compara se a Tag corresponde aquele linha de Onibus
                        if(memcmp(alunos[n_alunos_embarc].escola_linha, LinhaOnibus, 4) !=0){
    
                            Display_ERROR_Linha_Incorreta();                                                        //Mensagem de Linha Incorreta
                            StatusRead_Error();                                                                     //Indicacao de Erro (LED Vermelho)
                            printf("Linha Incorreta");                                                             //Debug Serial
                            break;
                        }
                    }
                    else{   //Casso contrario aborta a Leitura dessa Tag
                        Display_ERROR_Tag_Invalida();
                        StatusRead_Error(); 
                        printf("Tag vazia");
                        break;
                    }

                    printf("numero do Aluno %d", n_alunos_embarc);                                                 //Debug do numero de alunos que ja embarcaram

                    //Verificacao de Aluno Repetido
                    bool aluno_repetido = false;                                                                   //flag para  indicar aluno repetido 
                    if(n_alunos_embarc > 0 && n_alunos_embarc < max_alunos){
                        for(int i = 0; i < n_alunos_embarc; i++){                                                     //Checa todas as posicoes cadastradas
                            aluno_repetido = true;                                                                      //inicia flag como verdadeiro
                            for (int j = 0; j < 4; j++){                                                                //verificacao dos 4 bytes de ID
                                printf("\n %x cmp %x", alunos[n_alunos_embarc].tag[j], alunos[i].tag[j]);
                                if (alunos[n_alunos_embarc].tag[j] != alunos[i].tag[j]){
                                    aluno_repetido = false;                                                             //caso 1 byte ja seja diferente, considera que nao eh igual a posicao
                                    break;                                                                              //quebra o loop for dos 4 bytes de ID
                                }               
                            }
                            if(aluno_repetido == true){                                                                 //Caso verifique que o ID eh repetido
                                break;                                                                                  //quebra o loop de verificacao de todos os alunos
                            }
                        }
                    }

                    printf("\n Aluno repetido bool %d", aluno_repetido);                                                //Debug informacao se o aluno eh repetido ou nao

                    //Verifica Se o aluno nao for repetido e for menor que o limite maximo de alunos
                    if (aluno_repetido == false && n_alunos_embarc < max_alunos){
                        printf("\n Authentication Sucess\n\r");                                                         //Debug que a autenticacao foi concluida
                        alunos[n_alunos_embarc].embarque = true;                                                        //coloca uma flag no cadastro do aluno indicando que ele esta "embarcado"

                        StatusRead_OK();                                                                                //Indicacao de OK (LED Verde)
                        entrada_matriz_verm(n_alunos_embarc);                                                         //adiciona aluno na matriz neopixel de forma sequencial
                        n_alunos_embarc++;                                                                            //Incrementa o numero de alunos embarcados

                        //Debug Serial das informacoes da EEPROM daquele aluno
                        printf("\n EEPROM da Tag: ");
                        for(int j = 0; j < 30; j++){
                            printf("%x ", mfrc->uid.eeprom[j]);
                        }
                    }

                    else{                           //Se chegou aqui indica que o aluno ja esta cadastrado
                        Display_ERROR_Aluno_Repetido();
                        StatusRead_Error();                                                                             //Indicacao de Erro (LED Vermelho)
                        printf("\n Aluno repetido\n\r");                                                                //Debug serial
                    }

                } 
                else {                      //Se chegou aqui indica que houve algum na leitura da Tag         
                    Display_ERROR_Leitura_Tag();                            //Exibe Mensagem de Erro
                    StatusRead_Error();                                     //Status de Erro
                    printf("/n Authentication Failed\n\r");                                                             //Debug serial

                }  

            Desliga_LedRGB();                                               //Desliga Led RGB
            printf("Retire o cartao \n");                                   //Debug Serial
            sleep_ms(1000);
            n_alunos_desemb = n_alunos_embarc;                                                                  //copia o numero de alunos embarcados pra outra variavel de manipulacao

            break; 

        //******************************************************************/
        //Etapa de desembarque dos alunos na escola
        case 2:
            if (mudanca_etapa == true){                                     //Verifica se houve uma mudanca de Etapa
                botao_liberado = false;                                     //Desabilita mudancao de Etapa
                Display_MSG_Etapa2();                                       //Exibe Mensagem de Etapa 2 - Desembarque na escola
                mudanca_etapa = false;                                      
                break;
            }

            printf("Lendo cartao...\n\r");                                  //Debug Serial
            PICC_ReadCardSerial(mfrc);                                      //Realiza a Leitura do ID do Aluno

            uint8_t tagAtual[4] = {};                                       //Cria um buffer de 4 bytes para armazenar a Tag atual

            if(memcmp(mfrc->uid.uidByte, tagNull, 4) != 0){                 //Verifica se a tag lida nao eh nula == 0x00
                for (int i=0; i < 4; i++){                                  //Verificacao dos 4 bytes
                    tagAtual[i] = mfrc->uid.uidByte[i];
                }

                //Verificacao de aluno
                bool encontrar_aluno = false;                               //Flag para indicacao se a tag lida ja estava cadastrada
                for(int i = 0; i < n_alunos_embarc; i++){                   //procura a tagAtual em todos os alunos que embarcarao
                    printf("Aluno %d embarque %d", i, alunos[i].embarque);  //Debug serial
                    if (alunos[i].embarque == true){                        //so permite fazer a verificacao em alunos que nao desembarcarao ainda
                        encontrar_aluno = true;                             //configura a flag para verdadeiro
                        for (int j = 0; j < 4; j++){                        //Faz a verificacao dos 4 bytes    
                            printf("\n %x cmp %x", tagAtual[j], alunos[i].tag[j]);
                            if (tagAtual[j] != alunos[i].tag[j]){
                                encontrar_aluno = false;                                                            //se 1 byte for diferente, aquela posicao nao confere
                                break;
                            }               
                        }
                    }
                    if(encontrar_aluno == true){
                        alunos[i].embarque = false;                         //Se encontra aluno, marca como desembarque
                        break;
                    }
                }
                if (encontrar_aluno == true){                               //Se aluno esta desembarcando
                    StatusRead_OK();                                        //Status OK
                    printf("\n Aluno Encontrado\n\r Etapa %d", Etapa);      //Debug Serial
                    n_alunos_desemb--;                                      //Decrementa a contagem de alunos que falta para o Desembarque
                    entrada_matriz_amar(n_alunos_desemb);                   //Marca na matriz Neopixel o aluno que desembarcou
                }
                else{
                    Display_ERROR_Aluno_Repetido();                            //Exibe Mensagem de Erro
                    StatusRead_Error();                                     //Status de Erro
                }
            }
            else{
                Display_ERROR_Leitura_Tag();                            //Exibe Mensagem de Erro
                StatusRead_Error();                                     //Status de Erro
                break;
            }
            printf("\n Numero alunos %d", n_alunos_desemb);                 //Debug Serial
            if(n_alunos_desemb == 0){                                       //Se a quantidade de alunos que desembarcou eh igual ao numero que tinha embarcado
                Etapa++;                                                    //pula o case 3, que eh somente para verificacao quando nao tiver concluido
                Etapa++;                                                    //E salta direto para a Etapa 4
                mudanca_etapa = true;
            }

            //Liga_LedRGB_Red();
            printf("Retire o cartao \n");
            sleep_ms(1000);
            break;

        //******************************************************************/
        //case 3 apenas para verificacao dos alunos que nao desceram do onibus
        case 3:
            if (mudanca_etapa == true){
                botao_liberado = false;                                     //Desabilita mudancao de Etapa
                alarme_disparado = true;
                add_alarm_in_ms(10, disparar_alarme, NULL, false);          //Ativa Alarme
                mudanca_etapa = false;                                      //Desabilita mudancao de Etapa
                Display_MSG_Verificacao();                                       //Exibe Mensagem de Etapa 2 - Desembarque na escola
                sleep_ms(100);                                              //Aguarda 100ms
            }

            //procura o nome dos alunos que estao pendentes
            for(int i = 0; i < n_alunos_embarc; i++){
                if (alunos[i].embarque == true){                            //Somente para alunos que nao desembarcarao
                    printf("Alunos que faltam %d \n", n_alunos_desemb);     //Debug Serial

                    int x = 5;                                              //eixo x do display
                    int y = 24;                                             //eixo y do display
                    for (int j = 0; j < 10; j++){                           
                        printf("%x ", alunos[i].nome[j]);                   //Debug Serial do Nome do Aluno
                        ssd1306_draw_char(ssd, x, y, alunos[i].nome[j]);    //Imprime do Display o nome do Aluno
                        render_on_display(ssd, &frame_area);                
                        x = x+8;
                        y = y+0;
                    }

                    //Existem duas formas de desativar o alarme, a primeira eh com a propria TAG do aluno, a segunda eh atraves da TAG ADM
                    bool validou_aluno = false;                             //Flag para validar aluno pendente
                    while (validou_aluno == false){                         //Fica preso ate validar alunos pendente
                        printf("Aproxime um cartao");                       //Debug Serial
                        Liga_LedRGB_Blue();                                 //Liga Led RGB Azul
                        while(!PICC_IsNewCardPresent(mfrc));                //Fica preso ate reconhecer uma tag
                        printf("Identificando cartao...\n\r");              //Debug serial
                        PICC_ReadCardSerial(mfrc);                          //Faz a leitura TAG

                        //Verifica se a Tag corresponde ao Aluno ou eh a TAG ADMIN
                        if(memcmp(mfrc->uid.uidByte, tagADM, 4) == 0 || memcmp(mfrc->uid.uidByte, alunos[i].tag, 4) == 0){
                            n_alunos_desemb--;                              //Se o aluno  foi validado decrementa a quantidade de alunos
                            alunos[i].embarque = false;                     //Configura aluno como desembarque
                            entrada_matriz_amar(n_alunos_desemb);           //realiza a saida na matriz neopixel
                            validou_aluno = true;                           //altera Flag de Aluno validado
                        }
                    }
                }
                if (n_alunos_desemb <= 0){                                  //Se o numero de alunos chegou a 0
                    break;                                                  //quebra o loop infinito
                }
            }
            Etapa++;                                                        //Pula para proxima Etapa
            mudanca_etapa = true;                                           //Flag mudanca etapa ativada
            alarme_disparado = false;                                       
            desligar_alarme();                                              //Desabilita alarme
            sleep_ms(3000);
            break;
        //******************************************************************/
        //Etapa de Reembarque dos alunos no onibus
        case 4:
            if (mudanca_etapa == true){
                Display_MSG_Etapa3();                                       //Exibe Mensagem de Etapa 2 - Desembarque na escola
                mudanca_etapa = false;                                      //Desabilita mudancao de Etapa
                break;
            }

            printf("\n Lendo cartao...\n\r");
            PICC_ReadCardSerial(mfrc);     

            //Verifica se a tag eh valida
            if(memcmp(mfrc->uid.uidByte, tagNull, 4) != 0){
                for (int i=0; i < 4; i++){
                    tagAtual[i] = mfrc->uid.uidByte[i];                     //Se for copia para Tag Atual
                }

                //Verificacao do Reembarque
                bool reembarque = false;
                for(int i = 0; i < n_alunos_embarc; i++){                   //Busca dentro de todos os alunos
                    printf("Aluno %d status %d", i, alunos[i].embarque);    //Debug Serial
                    if(alunos[i].embarque == false){                        //Apenas para alunos que nao embarcarao ainda
                        reembarque = true;
                        for (int j = 0; j < 4; j++){                        //Testa se a Tag Atual pertence ha algum aluno que nao reembarcou
                            printf("\n %x cmp %x", tagAtual[j], alunos[i].tag[j]); //Debug Serial
                            if (tagAtual[j] != alunos[i].tag[j]){
                                reembarque = false;                         
                                break;
                            }               
                        }
                        if(reembarque == true){
                            alunos[i].embarque = true;                      //Se encontrou aluno, marca flag de embarcado
                            break;
                        }
                    }
                }
                if (reembarque == true){                                        //Verifica se aluno foi encontrado  para embarque
                    printf("\n Aluno Encontrado\n\r Etapa %d", Etapa);          //Debug Serial
                    StatusRead_OK();                                            //Status OK
                    entrada_matriz_verd(n_alunos_desemb);                       //Saida na Matriz Neopixel led verde
                    n_alunos_desemb++;                                          //aumenta registrador que conta alunos que tem que embarcar
                    printf("alunos des %d", n_alunos_desemb);
                }
                else{
                    Display_ERROR_Aluno_Repetido();                            //Exibe Mensagem de Erro
                    StatusRead_Error();                                     //Status de Erro
                }

                if(n_alunos_desemb == n_alunos_embarc){                         //Verifica se numero de alunos que embarcarao eh igual ao nmumero de alunos iniciais
                    Etapa++;                                                    //Pula Etapa 5, pois chegou na contagem correta\
                    Etapa++;                                                    //Vai Direto para Etapa 6, de Desembarque na Escola
                    mudanca_etapa = true;                           
                }
            }
            else{
                Display_ERROR_Leitura_Tag();                            //Exibe Mensagem de Erro
                StatusRead_Error();                                     //Status de Erro
            }
            printf("Retire o cartao \n");                                   //Debug Serial
            sleep_ms(1000); 

            break;
        //******************************************************************/
        //case 5 apenas para verificacao dos alunos que nao subiram do onibus
        case 5:
            if (mudanca_etapa == true){
                botao_liberado = false;                                     //Desabilita mudancao de Etapa
                alarme_disparado = true;
                add_alarm_in_ms(10, disparar_alarme, NULL, false);          //Ativa Alarme
                mudanca_etapa = false;                                      //Desabilita mudancao de Etapa
                Display_MSG_Verificacao();                                       //Exibe Mensagem de Etapa 2 - Desembarque na escola
                sleep_ms(100);                                             //Aguarda 100ms
            }

            //procura o nome dos alunos que estao pendentes
            for(int i = 0; i < n_alunos_embarc; i++){
                if (alunos[i].embarque == false){                            //Somente para alunos que nao embarcaram
                    printf("Alunos que faltam %d \n", n_alunos_desemb);     //Debug Serial

                    int x = 5;                                              //eixo x do display
                    int y = 24;                                             //eixo y do display
                    for (int j = 0; j < 10; j++){                           
                        printf("%x ", alunos[i].nome[j]);                   //Debug Serial do Nome do Aluno
                        ssd1306_draw_char(ssd, x, y, alunos[i].nome[j]);    //Imprime do Display o nome do Aluno
                        render_on_display(ssd, &frame_area);                
                        x = x+8;
                        y = y+0;
                    }

                    //Existem duas formas de desativar o alarme, a primeira eh com a propria TAG do aluno, a segunda eh atraves da TAG ADM
                    bool validou_aluno = false;                             //Flag para validar aluno pendente
                    while (validou_aluno == false){                         //Fica preso ate validar alunos pendente
                        printf("Aproxime um cartao");                       //Debug Serial
                        Liga_LedRGB_Blue();                                 //Liga Led RGB Azul
                        while(!PICC_IsNewCardPresent(mfrc));                //Fica preso ate reconhecer uma tag
                        printf("Identificando cartao...\n\r");              //Debug serial
                        PICC_ReadCardSerial(mfrc);                          //Faz a leitura TAG

                        //Verifica se a Tag corresponde ao Aluno ou eh a TAG ADMIN
                        if(memcmp(mfrc->uid.uidByte, tagADM, 4) == 0 || memcmp(mfrc->uid.uidByte, alunos[i].tag, 4) == 0){
                            alunos[i].embarque = true;                     //Configura aluno como desembarque
                            entrada_matriz_verd(n_alunos_desemb);           //realiza a saida na matriz neopixel
                            n_alunos_desemb++;                              //Se o aluno  foi validado decrementa a quantidade de alunos
                            validou_aluno = true;                           //altera Flag de Aluno validado
                        }
                    }
                }
                if (n_alunos_desemb == n_alunos_embarc){                                  //Se o numero de alunos chegou a 0
                    break;                                                  //quebra o loop infinito
                    }
            }
            Etapa++;                                                        //Pula para proxima Etapa
            mudanca_etapa = true;                                           //Flag mudanca etapa ativada
            alarme_disparado = false;                                       
            desligar_alarme();                                              //Desabilita alarme
            sleep_ms(3000);
            break;
        //******************************************************************/    
        case 6:
            if (mudanca_etapa == true){
                Display_MSG_Etapa4();                                       //Exibe Mensagem de Etapa 2 - Desembarque na escola
                mudanca_etapa = false;                                      //Desabilita mudancao de Etapa
                break;
            }

            printf("Lendo cartao...\n\r");
            PICC_ReadCardSerial(mfrc);     

            if(memcmp(mfrc->uid.uidByte, tagNull, 4) != 0){
            for (int i=0; i < 4; i++){
                    tagAtual[i] = mfrc->uid.uidByte[i];
                }
                bool encontrar_aluno = false;
                for(int i = 0; i < n_alunos_embarc; i++){
                    if(alunos[i].embarque == true){
                        encontrar_aluno = true;
                        for (int j = 0; j < 4; j++){
                            printf("\n %x cmp %x", tagAtual[j], alunos[i].tag[j]);
                            if (tagAtual[j] != alunos[i].tag[j]){
                                encontrar_aluno = false;
                                break;
                            }               
                        }
                    }
                    if(encontrar_aluno == true){
                        alunos[i].embarque = false;
                        break;
                    }
                }
                if (encontrar_aluno == true){
                    StatusRead_OK();
                    printf("\n Aluno Encontrado\n\r");           
                    n_alunos_desemb--;
                    entrada_matriz_desl(n_alunos_desemb);
                }
                if(n_alunos_desemb == 0){
                    Etapa++;
                    Etapa++;
                    mudanca_etapa = true;
                    printf("Colocando Etapa %d", Etapa);
                }
            }
                printf("Retire o cartao \n");
                sleep_ms(2000);
            break;

        //******************************************************************/ 
        //case 7 apenas para verificacao dos alunos que nao desceram do onibus
        case 7:
            if (mudanca_etapa == true){
                botao_liberado = false;                                     //Desabilita mudancao de Etapa
                alarme_disparado = true;
                add_alarm_in_ms(10, disparar_alarme, NULL, false);          //Ativa Alarme
                mudanca_etapa = false;                                      //Desabilita mudancao de Etapa
                Display_MSG_Verificacao();                                       //Exibe Mensagem de Etapa 2 - Desembarque na escola
                sleep_ms(100);                                              //Aguarda 100ms
            }

            //procura o nome dos alunos que estao pendentes
            for(int i = 0; i < n_alunos_embarc; i++){
                if (alunos[i].embarque == true){                            //Somente para alunos que nao desembarcarao
                    printf("Alunos que faltam %d \n", n_alunos_desemb);     //Debug Serial

                    int x = 5;                                              //eixo x do display
                    int y = 24;                                             //eixo y do display
                    for (int j = 0; j < 10; j++){                           
                        printf("%x ", alunos[i].nome[j]);                   //Debug Serial do Nome do Aluno
                        ssd1306_draw_char(ssd, x, y, alunos[i].nome[j]);    //Imprime do Display o nome do Aluno
                        render_on_display(ssd, &frame_area);                
                        x = x+8;
                        y = y+0;
                    }

                    //Existem duas formas de desativar o alarme, a primeira eh com a propria TAG do aluno, a segunda eh atraves da TAG ADM
                    bool validou_aluno = false;                             //Flag para validar aluno pendente
                    while (validou_aluno == false){                         //Fica preso ate validar alunos pendente
                        printf("Aproxime um cartao");                       //Debug Serial
                        Liga_LedRGB_Blue();                                 //Liga Led RGB Azul
                        while(!PICC_IsNewCardPresent(mfrc));                //Fica preso ate reconhecer uma tag
                        printf("Identificando cartao...\n\r");              //Debug serial
                        PICC_ReadCardSerial(mfrc);                          //Faz a leitura TAG

                        //Verifica se a Tag corresponde ao Aluno ou eh a TAG ADMIN
                        if(memcmp(mfrc->uid.uidByte, tagADM, 4) == 0 || memcmp(mfrc->uid.uidByte, alunos[i].tag, 4) == 0){
                            n_alunos_desemb--;                              //Se o aluno  foi validado decrementa a quantidade de alunos
                            alunos[i].embarque = false;                     //Configura aluno como desembarque
                            entrada_matriz_desl(n_alunos_desemb);           //realiza a saida na matriz neopixel
                            validou_aluno = true;                           //altera Flag de Aluno validado
                        }
                    }
                }
                if (n_alunos_desemb <= 0){                                  //Se o numero de alunos chegou a 0
                    break;                                                  //quebra o loop infinito
                }
            }
            Etapa++;                                                        //Pula para proxima Etapa
            mudanca_etapa = true;                                           //Flag mudanca etapa ativada
            alarme_disparado = false;                                       
            desligar_alarme();                                              //Desabilita alarme
            sleep_ms(3000);
            break;
        case 8:
            mudanca_etapa = false;
            botao_liberado = false;
            Display_MSG_Final();                                       //Exibe Mensagem de Etapa 2 - Desembarque na escola
            while(1);
            break;
        default:
            break;
        }
    }
}