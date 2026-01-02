#include <msp430.h>
#include <stdio.h>
#include <stdint.h>

#include "grlib.h"
#include "Crystalfontz128x128_ST7735.h"
#include "HAL_MSP430G2_Crystalfontz128x128_ST7735.h"

// NOTAS (sí como define)
#define NOTE_DO   262
#define NOTE_MI   330
#define NOTE_SOL  392
#define NOTE_SI   494

#define GRAPHICS_COLOR_DARK_YELLOW					 0x008B5400

Graphics_Context g_sContext;

// ===== Flags de despertar (tu estilo)
volatile unsigned char tick = 0;        // flag de tick
volatile unsigned char start = 0;       // flag pulsación start
volatile unsigned long contador_ticks = 0;

// ===== Joystick
volatile unsigned int ejex = 512;
volatile unsigned int ejey = 512;

// ------------------------------------------------------------
// RELOJ (tu estilo)
// ------------------------------------------------------------
void Set_Clk(char VEL){
    BCSCTL2 = SELM_0 | DIVM_0 | DIVS_0;
    switch(VEL){
    case 16:
        if (CALBC1_16MHZ != 0xFF) {
            __delay_cycles(100000);
            DCOCTL = 0x00;
            BCSCTL1 = CALBC1_16MHZ;
            DCOCTL = CALDCO_16MHZ;
        }
        break;
    case 8:
        if (CALBC1_8MHZ != 0xFF) {
            __delay_cycles(100000);
            DCOCTL = 0x00;
            BCSCTL1 = CALBC1_8MHZ;
            DCOCTL = CALDCO_8MHZ;
        }
        break;
    default:
        if (CALBC1_1MHZ != 0xFF) {
            DCOCTL = 0x00;
            BCSCTL1 = CALBC1_1MHZ;
            DCOCTL = CALDCO_1MHZ;
        }
        break;
    }
    BCSCTL1 |= XT2OFF | DIVA_0;
    BCSCTL3 = XT2S_0 | LFXT1S_2 | XCAP_1;
}

// ------------------------------------------------------------
// ADC (tu estilo)
// ------------------------------------------------------------
void inicia_ADC(char canales){
    ADC10CTL0 &= ~ENC;
    ADC10CTL0 = ADC10ON | ADC10SHT_3 | SREF_0 | ADC10IE;
    ADC10CTL1 = CONSEQ_0 | ADC10SSEL_0 | ADC10DIV_0 | SHS_0 | INCH_0;
    ADC10AE0 = canales;
    ADC10CTL0 |= ENC;
}

int lee_ch(char canal){
    ADC10CTL0 &= ~ENC;
    ADC10CTL1 &= (0x0fff);
    ADC10CTL1 |= canal<<12;
    ADC10CTL0 |= ENC;
    ADC10CTL0 |= ADC10SC;
    LPM0;                 // duerme hasta ISR ADC
    return (ADC10MEM);
}

#pragma vector=ADC10_VECTOR
__interrupt void ConvertidorAD(void){
    LPM0_EXIT;
}

// ------------------------------------------------------------
// TIMER TICK 25ms (tu estilo: despierta y en main procesas)
// ------------------------------------------------------------
void timer_tick(void){
    // SMCLK 16MHz /8 = 2MHz -> 25ms = 50.000
    TA0CTL = TASSEL_2 | ID_3 | MC_1;
    TA0CCR0 = 49999;
    TA0CCTL0 = CCIE;
}

#pragma vector=TIMER0_A0_VECTOR
__interrupt void Interrupcion_T0(void){
    tick = 1;
    contador_ticks++;
    LPM0_EXIT;
}

// ------------------------------------------------------------
// START por interrupción (ejemplo P1.3)
// (cambia BIT3 si tú usas otro)
// ------------------------------------------------------------
void boton_start(void){
    P1DIR &= ~BIT3;
    P1REN |= BIT3;
    P1OUT |= BIT3;
    P1IFG &= ~BIT3;
    P1IES |= BIT3;
    P1IE  |= BIT3;
}

#pragma vector=PORT1_VECTOR
__interrupt void Interrupcion_P1(void){
    if(!(P1IN & BIT3)){
        start = 1;
    }
    P1IFG &= ~BIT3;
    LPM0_EXIT;
}

// ------------------------------------------------------------
// BUZZER (PWM con Timer1) - por defecto P2.1 / TA1CCR1
// (si tu buzzer está en otro pin, cambia BIT1 o CCR)
// ------------------------------------------------------------
void init_buzzer(void){
    P2DIR  |= BIT1;
    P2SEL  |= BIT1;
    P2SEL2 &= ~BIT1;

    TA1CTL = TASSEL_2 | ID_3 | MC_1;  // SMCLK/8 => 2MHz
    TA1CCTL1 = OUTMOD_7;
    TA1CCR0 = 1000;
    TA1CCR1 = 0;
}

void suena_hz(unsigned int hz){
    unsigned long periodo = 2000000UL / (unsigned long)hz; // 2MHz
    if(periodo < 10) periodo = 10;
    if(periodo > 65535) periodo = 65535;
    TA1CCR0 = (unsigned int)(periodo - 1);
    TA1CCR1 = (unsigned int)((periodo - 1) / 2);
}

void apaga_sonido(void){
    TA1CCR1 = 0;
}

void sonido_color(unsigned char color){
    switch(color){
    case 1: suena_hz(NOTE_DO);  break;
    case 2: suena_hz(NOTE_MI);  break;
    case 3: suena_hz(NOTE_SOL); break;
    case 4: suena_hz(NOTE_SI);  break;
    default: apaga_sonido();    break;
    }
}

// ------------------------------------------------------------
// LFSR (aleatorio sin rand)
// ------------------------------------------------------------
static uint16_t lfsr = 0xACE1u;

void semilla(uint16_t s){
    if(s == 0) s = 0xACE1u;
    lfsr = s;
}
uint16_t lfsr_siguiente(void){
    uint16_t bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1;
    lfsr = (lfsr >> 1) | (bit << 15);
    return lfsr;
}
unsigned char aleatorio_1_4(void){
    return (unsigned char)((lfsr_siguiente() & 0x03) + 1);
}

int main(void){
    WDTCTL = WDTPW | WDTHOLD;

    Set_Clk(16);
    inicia_ADC(BIT0 | BIT3);

    Crystalfontz128x128_Init();
    Crystalfontz128x128_SetOrientation(LCD_ORIENTATION_UP);
    Graphics_initContext(&g_sContext, &g_sCrystalfontz128x128);
    Graphics_setFont(&g_sContext, &g_sFontCm16b);
    Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_BLACK);
    Graphics_clearDisplay(&g_sContext);

    init_buzzer();
    timer_tick();
    boton_start();

    __bis_SR_register(GIE);

    // ----- Rectángulos “botones”
    Graphics_Rectangle boton[4];
    boton[0] = (Graphics_Rectangle){  8, 20, 60, 72 };
    boton[1] = (Graphics_Rectangle){ 68, 20,120, 72 };
    boton[2] = (Graphics_Rectangle){  8, 78, 60,120 };
    boton[3] = (Graphics_Rectangle){ 68, 78,120,120 };

    // barra progreso
    Graphics_Rectangle barra = (Graphics_Rectangle){ 8, 6, 120, 14 };

    // Colores: VERDE, AMARILLO, ROJO, AZUL
    uint32_t color_alto[4], color_bajo[4];
    color_alto[0] = GRAPHICS_COLOR_GREEN;
    color_alto[1] = GRAPHICS_COLOR_YELLOW;
    color_alto[2] = GRAPHICS_COLOR_RED;
    color_alto[3] = GRAPHICS_COLOR_BLUE;

    color_bajo[0] = GRAPHICS_COLOR_DARK_GREEN;
    color_bajo[1] = GRAPHICS_COLOR_DARK_YELLOW;
    color_bajo[2] = GRAPHICS_COLOR_DARK_RED;
    color_bajo[3] = GRAPHICS_COLOR_DARK_BLUE;

    // dibujar base
    Graphics_clearDisplay(&g_sContext);
    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
    Graphics_drawRectangle(&g_sContext, &barra);
    for(int i=0;i<4;i++){
        Graphics_setForegroundColor(&g_sContext, color_bajo[i]);
        Graphics_fillRectangle(&g_sContext, &boton[i]);
    }

    // ----- Juego (variables “tipo tus prácticas”)
    unsigned char secuencia[32];

    unsigned int ronda = 1;
    unsigned int puntuacion = 0;

    unsigned int T = 800; // ms
    unsigned int T_ticks = (unsigned int)(T / 25); if(T_ticks < 2) T_ticks = 2;
    unsigned int T4_ticks = (unsigned int)(T_ticks / 4); if(T4_ticks < 1) T4_ticks = 1;
    unsigned int T2_ticks = (unsigned int)(2 * T_ticks);

    // Estados (en español)
    typedef enum { BIENVENIDA=0, MENSAJE_RONDA, MAQUINA, TURNO_JUGADOR, FIN } estados_t;
    estados_t estado = BIENVENIDA;

    // auxiliares reproducción máquina
    unsigned char sub_maquina = 0;     // 0=mostrar, 1=pausa
    unsigned int paso_maquina = 0;

    // jugador
    unsigned int paso_jugador = 0;

    // temporización estado (ticks)
    unsigned int tms = 0;

    // joystick gating
    unsigned char estado_joystick = 0; // 0 neutral, 1 pulsado

    // feedback del jugador
    unsigned char seleccion = 0;
    unsigned int tflash = 0;

    while(1){
        LPM0;

        if(tick){
            tick = 0;
            tms++;

            switch(estado){

            case BIENVENIDA:
                if(tms == 1){
                    Graphics_clearDisplay(&g_sContext);

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawRectangle(&g_sContext, &barra);
                    for(int i=0;i<4;i++){
                        Graphics_setForegroundColor(&g_sContext, color_bajo[i]);
                        Graphics_fillRectangle(&g_sContext, &boton[i]);
                    }

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawString(&g_sContext, "SIMON DICE", -1, 20, 40, TRANSPARENT_TEXT);
                    Graphics_drawString(&g_sContext, "PULSA START", -1, 15, 60, TRANSPARENT_TEXT);
                }

                if(start){
                    start = 0;

                    semilla((uint16_t)(contador_ticks ^ 0xBEEF));
                    for(int i=0;i<32;i++){
                        secuencia[i] = aleatorio_1_4();
                    }

                    ronda = 1;
                    puntuacion = 0;
                    paso_maquina = 0;
                    paso_jugador = 0;
                    sub_maquina = 0;

                    // recalcular tiempos (por si cambias T)
                    T_ticks = (unsigned int)(T / 25); if(T_ticks < 2) T_ticks = 2;
                    T4_ticks = (unsigned int)(T_ticks / 4); if(T4_ticks < 1) T4_ticks = 1;
                    T2_ticks = (unsigned int)(2 * T_ticks);

                    estado = MENSAJE_RONDA;
                    tms = 0;
                }
                break;

            case MENSAJE_RONDA:
                if(tms == 1){
                    char cad[20];
                    Graphics_clearDisplay(&g_sContext);

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawRectangle(&g_sContext, &barra);
                    for(int i=0;i<4;i++){
                        Graphics_setForegroundColor(&g_sContext, color_bajo[i]);
                        Graphics_fillRectangle(&g_sContext, &boton[i]);
                    }

                    sprintf(cad, "RONDA %u", ronda);
                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawString(&g_sContext, (int8_t*)cad, -1, 25, 40, TRANSPARENT_TEXT);
                    Graphics_drawString(&g_sContext, "MIRA...", -1, 35, 60, TRANSPARENT_TEXT);

                    paso_maquina = 0;
                    sub_maquina = 0;
                }

                // 1s
                if(tms >= (1000/25)){
                    estado = MAQUINA;
                    tms = 0;
                }
                break;

            case MAQUINA:
                if(paso_maquina >= ronda){
                    apaga_sonido();
                    for(int i=0;i<4;i++){
                        Graphics_setForegroundColor(&g_sContext, color_bajo[i]);
                        Graphics_fillRectangle(&g_sContext, &boton[i]);
                    }

                    paso_jugador = 0;
                    estado_joystick = 0;

                    Graphics_Rectangle banda = (Graphics_Rectangle){0, 34, 127, 76};
                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_BLACK);
                    Graphics_fillRectangle(&g_sContext, &banda);
                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawString(&g_sContext, "TU TURNO", -1, 30, 50, TRANSPARENT_TEXT);

                    estado = TURNO_JUGADOR;
                    tms = 0;
                    break;
                }

                if(sub_maquina == 0){
                    if(tms == 1){
                        unsigned char color = secuencia[paso_maquina];   // 1..4
                        unsigned char k = color - 1;                    // 0..3

                        Graphics_setForegroundColor(&g_sContext, color_alto[k]);
                        Graphics_fillRectangle(&g_sContext, &boton[k]);

                        sonido_color(color);

                        // barra progreso
                        Graphics_Rectangle interior = barra;
                        interior.xMin += 1; interior.yMin += 1;
                        interior.xMax -= 1; interior.yMax -= 1;

                        Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_BLACK);
                        Graphics_fillRectangle(&g_sContext, &interior);

                        unsigned int W = interior.xMax - interior.xMin;
                        unsigned int relleno = (unsigned int)((unsigned long)W * (paso_maquina+1) / ronda);
                        if(relleno > 0){
                            Graphics_Rectangle f = interior;
                            f.xMax = f.xMin + relleno;
                            Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                            Graphics_fillRectangle(&g_sContext, &f);
                        }
                    }

                    if(tms >= T_ticks){
                        unsigned char color = secuencia[paso_maquina];
                        unsigned char k = color - 1;

                        Graphics_setForegroundColor(&g_sContext, color_bajo[k]);
                        Graphics_fillRectangle(&g_sContext, &boton[k]);

                        apaga_sonido();
                        sub_maquina = 1;
                        tms = 0;
                    }
                }else{
                    if(tms >= T4_ticks){
                        paso_maquina++;
                        sub_maquina = 0;
                        tms = 0;
                    }
                }
                break;

            case TURNO_JUGADOR: {
                // timeout por paso
                if(tms >= T2_ticks){
                    estado = FIN;
                    tms = 0;
                    apaga_sonido();
                    break;
                }

                // leer joystick (tu estilo)
                ejex = (unsigned int)lee_ch(0);
                ejey = (unsigned int)lee_ch(3);

                int dx = (int)ejex - 512;
                int dy = (int)ejey - 512;

                // zona muerta
                if(dx < 100 && dx > -100 && dy < 100 && dy > -100){
                    estado_joystick = 0;
                }

                unsigned char elegido = 0;
                if(estado_joystick == 0){
                    if(! (dx < 100 && dx > -100 && dy < 100 && dy > -100) ){
                        estado_joystick = 1;

                        int adx = (dx<0)?-dx:dx;
                        int ady = (dy<0)?-dy:dy;

                        if(adx > ady){
                            if(dx > 0) elegido = 2;
                            else       elegido = 4;
                        }else{
                            if(dy > 0) elegido = 1;
                            else       elegido = 3;
                        }
                    }
                }

                if(elegido != 0){
                    unsigned char esperado = secuencia[paso_jugador];

                    // feedback visual + sonido
                    {
                        unsigned char k = elegido - 1;
                        Graphics_setForegroundColor(&g_sContext, color_alto[k]);
                        Graphics_fillRectangle(&g_sContext, &boton[k]);
                        sonido_color(elegido);

                        seleccion = elegido;
                        tflash = 0;
                    }

                    if(elegido == esperado){
                        puntuacion++;
                        paso_jugador++;

                        // barra progreso jugador
                        Graphics_Rectangle interior = barra;
                        interior.xMin += 1; interior.yMin += 1;
                        interior.xMax -= 1; interior.yMax -= 1;

                        Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_BLACK);
                        Graphics_fillRectangle(&g_sContext, &interior);

                        unsigned int W = interior.xMax - interior.xMin;
                        unsigned int relleno = (unsigned int)((unsigned long)W * (paso_jugador) / ronda);
                        if(relleno > 0){
                            Graphics_Rectangle f = interior;
                            f.xMax = f.xMin + relleno;
                            Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                            Graphics_fillRectangle(&g_sContext, &f);
                        }

                        if(paso_jugador >= ronda){
                            ronda++;
                            if(ronda > 32) ronda = 32;

                            // en ronda 32, T/2 (mínimo 200ms)
                            if(ronda == 32){
                                if(T > 200) T /= 2;
                                T_ticks = (unsigned int)(T / 25); if(T_ticks < 2) T_ticks = 2;
                                T4_ticks = (unsigned int)(T_ticks / 4); if(T4_ticks < 1) T4_ticks = 1;
                                T2_ticks = (unsigned int)(2 * T_ticks);
                            }

                            estado = MENSAJE_RONDA;
                            tms = 0;
                        }else{
                            tms = 0; // reinicia timeout del próximo paso
                        }
                    }else{
                        estado = FIN;
                        tms = 0;
                    }
                }

                // apagar feedback tras T/2
                if(seleccion != 0){
                    tflash++;
                    if(tflash >= (T_ticks/2)){
                        unsigned char k = seleccion - 1;
                        Graphics_setForegroundColor(&g_sContext, color_bajo[k]);
                        Graphics_fillRectangle(&g_sContext, &boton[k]);
                        apaga_sonido();
                        seleccion = 0;
                    }
                }

                break;
            }

            case FIN:
                if(tms == 1){
                    char cad[24];
                    Graphics_clearDisplay(&g_sContext);

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawRectangle(&g_sContext, &barra);
                    for(int i=0;i<4;i++){
                        Graphics_setForegroundColor(&g_sContext, color_bajo[i]);
                        Graphics_fillRectangle(&g_sContext, &boton[i]);
                    }

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawString(&g_sContext, "GAME OVER", -1, 25, 35, TRANSPARENT_TEXT);
                    sprintf(cad, "PUNTOS: %u", puntuacion);
                    Graphics_drawString(&g_sContext, (int8_t*)cad, -1, 20, 55, TRANSPARENT_TEXT);
                    Graphics_drawString(&g_sContext, "PULSA START", -1, 20, 75, TRANSPARENT_TEXT);

                    suena_hz(150);
                }

                if(tms >= (600/25)){
                    apaga_sonido();
                }

                if(start){
                    start = 0;
                    estado = BIENVENIDA;
                    tms = 0;
                }
                break;
            }
        }
    }
}

