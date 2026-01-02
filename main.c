#include <msp430.h>
#include <stdio.h>
#include <stdint.h>

#include "grlib.h"
#include "Crystalfontz128x128_ST7735.h"
#include "HAL_MSP430G2_Crystalfontz128x128_ST7735.h"

int i=0;

// NOTAS
#define NOTE_DO   262
#define NOTE_MI   330
#define NOTE_SOL  392
#define NOTE_SI   494
#define NOTE_DO2  523

#define GRAPHICS_COLOR_DARK_YELLOW  0x008B5400

Graphics_Context g_sContext;

// ===== Flags de despertar
volatile unsigned char tick = 0;
volatile unsigned char start = 0;
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
    LPM0;
    return (ADC10MEM);
}

#pragma vector=ADC10_VECTOR
__interrupt void ConvertidorAD(void){
    LPM0_EXIT;
}

// ------------------------------------------------------------
// TIMER tick 25ms (tu estilo)
// ------------------------------------------------------------
void timer_tick(void){
    TA0CTL = TASSEL_2 | ID_3 | MC_1;  // SMCLK/8 = 2MHz
    TA0CCR0 = 49999;                 // 25ms
    TA0CCTL0 = CCIE;
}

#pragma vector=TIMER0_A0_VECTOR
__interrupt void Interrupcion_T0(void){
    tick = 1;
    contador_ticks++;
    LPM0_EXIT;
}

// ------------------------------------------------------------
// START (botón joystick) P2.5
// ------------------------------------------------------------
void boton_start(void){
    P2DIR &= ~BIT5;
    P2REN |= BIT5;
    P2OUT |= BIT5;
    P2IFG &= ~BIT5;
    P2IES |= BIT5;
    P2IE  |= BIT5;
}

#pragma vector=PORT2_VECTOR
__interrupt void Interrupcion_P2(void){
    if(!(P2IN & BIT5)){
        start = 1;
    }
    P2IFG &= ~BIT5;
    LPM0_EXIT;
}

// ------------------------------------------------------------
// BUZZER P2.6 (toggle con Timer1)
// ------------------------------------------------------------
volatile unsigned char buzzer_activo = 0;

void init_buzzer(void){
    P2DIR  |= BIT6;
    P2SEL  &= ~BIT6;
    P2SEL2 &= ~BIT6;
    P2OUT  &= ~BIT6;

    TA1CTL = MC_0;
    TA1CCTL0 = 0;
    TA1CCR0 = 1000;
}

void suena_hz(unsigned int hz){
    unsigned long cuenta_media = 1000000UL / (unsigned long)hz; // 2MHz/(2*hz)

    if(cuenta_media < 10) cuenta_media = 10;
    if(cuenta_media > 65535) cuenta_media = 65535;

    TA1CTL = TASSEL_2 | ID_3 | MC_1;   // SMCLK/8 => 2MHz, UP
    TA1CCR0 = (unsigned int)(cuenta_media - 1);
    TA1CCTL0 = CCIE;

    buzzer_activo = 1;
}

void apaga_sonido(void){
    buzzer_activo = 0;
    TA1CCTL0 &= ~CCIE;
    TA1CTL = MC_0;
    P2OUT &= ~BIT6;
}

#pragma vector=TIMER1_A0_VECTOR
__interrupt void Interrupcion_T1(void){
    if(buzzer_activo) P2OUT ^= BIT6;
    else             P2OUT &= ~BIT6;
}

void sonido_color(unsigned char color){
    // 1=arriba, 2=derecha, 3=abajo, 4=izquierda
    switch(color){
    case 1: suena_hz(NOTE_DO);  break;
    case 2: suena_hz(NOTE_MI);  break;
    case 3: suena_hz(NOTE_SOL); break;
    case 4: suena_hz(NOTE_SI);  break;
    default: apaga_sonido();    break;
    }
}

// ------------------------------------------------------------
// LFSR (aleatorio)
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
    inicia_ADC(BIT0 | BIT3); // joystick A0 y A3

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

    // ==========================================================
    // DIBUJO SIMÉTRICO COMO TU IMAGEN
    // MISMO grosor t, MISMO hueco central c
    // PERO con separación acortando SOLO el largo (g)
    //  - ARRIBA: AMARILLO
    //  - DERECHA: AZUL
    //  - ABAJO: VERDE
    //  - IZQUIERDA: ROJO
    // ==========================================================
    Graphics_Rectangle boton[4];

    unsigned int xC = 64;
    unsigned int yC = 70;
    unsigned int c  = 26;     // hueco central
    unsigned int t  = 30;     // grosor (NO se toca)

    unsigned int g  = 3;      // separación visual (solo recorta el largo). Prueba 2..4

    unsigned int x0 = xC - c/2;
    unsigned int x1 = xC + c/2;
    unsigned int y0 = yC - c/2;
    unsigned int y1 = yC + c/2;

    unsigned int xL = xC - (c/2 + t);
    unsigned int xR = xC + (c/2 + t);
    unsigned int yT = yC - (c/2 + t);
    unsigned int yB = yC + (c/2 + t);

    unsigned int gJ = 3;   // gap SOLO en juntas internas (2..4)
    unsigned int gE = 0;   // gap en extremos exteriores (lo quieres 0)

    // ARRIBA (AMARILLO): gap SOLO en las esquinas que juntan con ROJO y AZUL
    //  - junta con ROJO en x0
    //  - junta con AZUL en x1 (no en xR)
    boton[0] = (Graphics_Rectangle){ (int)(x0 + gJ), (int)yT, (int)(xR - gE), (int)y0 };

    // DERECHA (AZUL): gap SOLO donde junta con AMARILLO (y0) y VERDE (y1)
    //  (exterior derecha queda a ras)
    boton[1] = (Graphics_Rectangle){ (int)x1, (int)(y0 + gJ), (int)xR, (int)yB };

    // ABAJO (VERDE): gap SOLO en las esquinas que juntan con ROJO y AZUL
    //  - junta con ROJO en x0 (no en xL)
    //  - junta con AZUL en x1
    boton[2] = (Graphics_Rectangle){ (int)(xL + gE), (int)y1, (int)(x1 - gJ), (int)yB };

    // IZQUIERDA (ROJO): gap SOLO donde junta con AMARILLO (y0) y VERDE (y1)
    //  (exterior izquierda queda a ras)
    // ROJO: sin gap en el borde exterior SUPERIOR (yT). Gap solo en la junta interior con VERDE (y1)
    boton[3] = (Graphics_Rectangle){ (int)xL, (int)yT, (int)x0, (int)(y1 - gJ) };



    // Marco exterior
    Graphics_Rectangle marco = (Graphics_Rectangle){ 2, 2, 125, 125 };

    // Barra de porcentaje arriba
    Graphics_Rectangle barra = (Graphics_Rectangle){ 12, 8, 110, 18 };
    Graphics_Rectangle caja_pct = (Graphics_Rectangle){ 112, 8, 120, 18 };

    // Ecualizador victoria (dibujado)
    Graphics_Rectangle barra1 = (Graphics_Rectangle){ 44, 90, 52, 114 };
    Graphics_Rectangle barra2 = (Graphics_Rectangle){ 58, 90, 66, 114 };
    Graphics_Rectangle barra3 = (Graphics_Rectangle){ 72, 90, 80, 114 };
    Graphics_Rectangle marco_eq = (Graphics_Rectangle){ 36, 86, 88, 118 };

    // Colores (UP, RIGHT, DOWN, LEFT)
    uint32_t color_alto[4], color_bajo[4];

    color_alto[0] = GRAPHICS_COLOR_YELLOW;
    color_alto[1] = GRAPHICS_COLOR_BLUE;
    color_alto[2] = GRAPHICS_COLOR_GREEN;
    color_alto[3] = GRAPHICS_COLOR_RED;

    color_bajo[0] = GRAPHICS_COLOR_DARK_YELLOW;
    color_bajo[1] = GRAPHICS_COLOR_DARK_BLUE;
    color_bajo[2] = GRAPHICS_COLOR_DARK_GREEN;
    color_bajo[3] = GRAPHICS_COLOR_DARK_RED;

    // ----- Juego
    unsigned char secuencia[32];

    unsigned int ronda = 1;
    unsigned int puntuacion = 0;

    unsigned int T = 1000; // ms
    unsigned int T_ticks = (unsigned int)(T / 25); if(T_ticks < 2) T_ticks = 2;
    unsigned int T4_ticks = (unsigned int)(T_ticks / 4); if(T4_ticks < 1) T4_ticks = 1;
    unsigned int T2_ticks = (unsigned int)(2 * T_ticks);

    typedef enum { BIENVENIDA=0, MENSAJE_RONDA, MAQUINA, TURNO_JUGADOR, VICTORIA, FIN } estados_t;
    estados_t estado = BIENVENIDA;

    unsigned char sub_maquina = 0;
    unsigned int paso_maquina = 0;

    unsigned int paso_jugador = 0;

    unsigned int tms = 0;

    unsigned char estado_joystick = 0;

    unsigned char seleccion = 0;
    unsigned int tflash = 0;

    // victoria
    unsigned int t_vict = 0;
    unsigned int paso_vict = 0;
    unsigned char anim = 0;

    while(1){
        LPM0;

        if(tick){
            tick = 0;
            tms++;

            switch(estado){

            case BIENVENIDA:
                if(tms == 1){
                    apaga_sonido();
                    Graphics_clearDisplay(&g_sContext);

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawStringCentered(&g_sContext, "SIMON DICE", -1, 64, 50, TRANSPARENT_TEXT);
                    Graphics_drawStringCentered(&g_sContext, "PULSA START", -1, 64, 75, TRANSPARENT_TEXT);
                }

                if(start){
                    start = 0;

                    semilla((uint16_t)(contador_ticks ^ 0xBEEF));
                    for(i=0;i<32;i++){
                        secuencia[i] = aleatorio_1_4();
                    }

                    ronda = 1;
                    puntuacion = 0;
                    paso_maquina = 0;
                    paso_jugador = 0;
                    sub_maquina = 0;

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
                    apaga_sonido();
                    Graphics_clearDisplay(&g_sContext);

                    sprintf(cad, "RONDA %d", ronda);
                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawStringCentered(&g_sContext, (int8_t*)cad, -1, 64, 55, TRANSPARENT_TEXT);
                    Graphics_drawStringCentered(&g_sContext, "MIRA...", -1, 64, 80, TRANSPARENT_TEXT);

                    paso_maquina = 0;
                    sub_maquina = 0;
                }

                if(tms >= (800/25)){
                    Graphics_clearDisplay(&g_sContext);

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawRectangle(&g_sContext, &marco);

                    Graphics_drawRectangle(&g_sContext, &barra);
                    Graphics_drawRectangle(&g_sContext, &caja_pct);
                    Graphics_drawString(&g_sContext, "%", -1, 122, 7, TRANSPARENT_TEXT);

                    for(i=0;i<4;i++){
                        Graphics_setForegroundColor(&g_sContext, color_bajo[i]);
                        Graphics_fillRectangle(&g_sContext, &boton[i]);
                    }

                    estado = MAQUINA;
                    tms = 0;
                }
                break;

            case MAQUINA:
                if(paso_maquina >= ronda){
                    apaga_sonido();

                    for(i=0;i<4;i++){
                        Graphics_setForegroundColor(&g_sContext, color_bajo[i]);
                        Graphics_fillRectangle(&g_sContext, &boton[i]);
                    }

                    paso_jugador = 0;
                    estado_joystick = 0;

                    estado = TURNO_JUGADOR;
                    tms = 0;
                    break;
                }

                if(sub_maquina == 0){
                    if(tms == 1){
                        unsigned char color = secuencia[paso_maquina];
                        unsigned char k = color - 1;

                        Graphics_setForegroundColor(&g_sContext, color_alto[k]);
                        Graphics_fillRectangle(&g_sContext, &boton[k]);

                        sonido_color(color);

                        // barra progreso máquina (relleno rojo)
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
                            Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_RED);
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
                if(tms >= T2_ticks){
                    estado = FIN;
                    tms = 0;
                    apaga_sonido();
                    break;
                }

                ejex = (unsigned int)lee_ch(0);
                ejey = (unsigned int)lee_ch(3);

                int dx = (int)ejex - 512;
                int dy = (int)ejey - 512;

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
                            if(dx > 0) elegido = 2;  // derecha
                            else       elegido = 4;  // izquierda
                        }else{
                            if(dy > 0) elegido = 1;  // arriba
                            else       elegido = 3;  // abajo
                        }
                    }
                }

                if(elegido != 0){
                    unsigned char esperado = secuencia[paso_jugador];

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

                        // barra progreso jugador (relleno blanco)
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
                            estado = VICTORIA;
                            tms = 0;

                            t_vict = 0;
                            paso_vict = 0;
                            anim = 0;

                            apaga_sonido();
                        }else{
                            tms = 0;
                        }
                    }else{
                        estado = FIN;
                        tms = 0;
                    }
                }

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

            case VICTORIA:
                if(tms == 1){
                    Graphics_clearDisplay(&g_sContext);

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawStringCentered(&g_sContext, "RONDA", -1, 64, 45, TRANSPARENT_TEXT);
                    Graphics_drawStringCentered(&g_sContext, "SUPERADA", -1, 64, 65, TRANSPARENT_TEXT);

                    Graphics_drawRectangle(&g_sContext, &marco_eq);

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_BLACK);
                    Graphics_fillRectangle(&g_sContext, &barra1);
                    Graphics_fillRectangle(&g_sContext, &barra2);
                    Graphics_fillRectangle(&g_sContext, &barra3);

                    paso_vict = 0;
                    t_vict = 0;
                    anim = 0;
                }

                t_vict++;

                if(paso_vict == 0){  suena_hz(NOTE_DO);  }
                if(paso_vict == 1){  apaga_sonido();     }
                if(paso_vict == 2){  suena_hz(NOTE_MI);  }
                if(paso_vict == 3){  apaga_sonido();     }
                if(paso_vict == 4){  suena_hz(NOTE_SOL); }
                if(paso_vict == 5){  apaga_sonido();     }
                if(paso_vict == 6){  suena_hz(NOTE_DO2); }
                if(paso_vict == 7){  apaga_sonido();     }
                if(paso_vict == 8){  suena_hz(NOTE_SOL); }
                if(paso_vict == 9){  apaga_sonido();     }
                if(paso_vict == 10){ suena_hz(NOTE_DO2); }
                if(paso_vict == 11){ apaga_sonido();     }

                if((paso_vict % 2) == 0){
                    if(t_vict >= 6){ t_vict = 0; paso_vict++; }
                }else{
                    if(t_vict >= 2){ t_vict = 0; paso_vict++; }
                }

                if((tms % 2) == 0){
                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_BLACK);
                    Graphics_fillRectangle(&g_sContext, &barra1);
                    Graphics_fillRectangle(&g_sContext, &barra2);
                    Graphics_fillRectangle(&g_sContext, &barra3);

                    unsigned char h1=8, h2=18, h3=12;
                    if(anim==0){ h1=8;  h2=18; h3=12; }
                    if(anim==1){ h1=18; h2=10; h3=16; }
                    if(anim==2){ h1=12; h2=16; h3=8;  }
                    if(anim==3){ h1=16; h2=8;  h3=18; }

                    if((paso_vict % 2) == 1){
                        h1 = 6; h2 = 6; h3 = 6;
                    }

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);

                    Graphics_Rectangle b1 = barra1; b1.yMin = (unsigned int)(barra1.yMax - h1);
                    Graphics_Rectangle b2 = barra2; b2.yMin = (unsigned int)(barra2.yMax - h2);
                    Graphics_Rectangle b3 = barra3; b3.yMin = (unsigned int)(barra3.yMax - h3);

                    Graphics_fillRectangle(&g_sContext, &b1);
                    Graphics_fillRectangle(&g_sContext, &b2);
                    Graphics_fillRectangle(&g_sContext, &b3);

                    anim++;
                    if(anim >= 4) anim = 0;
                }

                if(paso_vict >= 12){
                    apaga_sonido();

                    ronda++;
                    if(ronda > 32) ronda = 32;

                    if(ronda == 32){
                        if(T > 200) T /= 2;
                        T_ticks = (unsigned int)(T / 25); if(T_ticks < 2) T_ticks = 2;
                        T4_ticks = (unsigned int)(T_ticks / 4); if(T4_ticks < 1) T4_ticks = 1;
                        T2_ticks = (unsigned int)(2 * T_ticks);
                    }

                    paso_maquina = 0;
                    sub_maquina = 0;
                    paso_jugador = 0;
                    seleccion = 0;

                    estado = MENSAJE_RONDA;
                    tms = 0;
                }
                break;

            case FIN:
                if(tms == 1){
                    char cad[24];
                    apaga_sonido();
                    Graphics_clearDisplay(&g_sContext);

                    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
                    Graphics_drawStringCentered(&g_sContext, "GAME OVER", -1, 64, 45, TRANSPARENT_TEXT);

                    sprintf(cad, "PUNTOS: %d", puntuacion);
                    Graphics_drawStringCentered(&g_sContext, (int8_t*)cad, -1, 64, 70, TRANSPARENT_TEXT);

                    Graphics_drawStringCentered(&g_sContext, "PULSA START", -1, 64, 95, TRANSPARENT_TEXT);

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
