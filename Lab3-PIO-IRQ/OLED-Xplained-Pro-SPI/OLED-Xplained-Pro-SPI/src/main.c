#include <asf.h>

#include "gfx_mono_ug_2832hsweg04.h"
#include "gfx_mono_text.h"
#include "sysfont.h"

// LED
#define LED_PIO      PIOC
#define LED_PIO_ID   ID_PIOC
#define LED_IDX      8
#define LED_IDX_MASK (1 << LED_IDX)

// Botão

#define BUT_PIO1		  PIOD
#define BUT_PIO_ID1		  ID_PIOD
#define BUT_PIO_IDX1	  28
#define BUT_PIO_IDX_MASK1 (1u << BUT_PIO_IDX1)

#define BUT_PIO2		  PIOC
#define BUT_PIO_ID2		  ID_PIOC
#define BUT_PIO_IDX2	  31
#define BUT_PIO_IDX_MASK2 (1u << BUT_PIO_IDX2)

#define BUT_PIO3		  PIOA
#define BUT_PIO_ID3		  ID_PIOA
#define BUT_PIO_IDX3	  19
#define BUT_PIO_IDX_MASK3 (1u << BUT_PIO_IDX3)

/************************************************************************/
/* variaveis globais                                                    */
/************************************************************************/

volatile char soltou1;
volatile char apertou1;

volatile char apertou2;

volatile char apertou3;
/************************************************************************/
/* prototype                                                            */
/************************************************************************/
void io_init(void);
void pisca_led(int n, int t);


void but_callback1 (void) {
	
	if (pio_get(BUT_PIO1, PIO_INPUT, BUT_PIO_IDX_MASK1)) {
		// PINO == 1 --> Borda de subida
		soltou1 = 1;
		} else {
		// PINO == 0 --> Borda de descida
		apertou1 = 1;
		soltou1 = 0;
	}
}

void but_callback2 (void) {
	if (!pio_get(BUT_PIO2, PIO_INPUT, BUT_PIO_IDX_MASK2)) {
		apertou2 = 1;
	}
}

void but_callback3 (void) {
	if (!pio_get(BUT_PIO3, PIO_INPUT, BUT_PIO_IDX_MASK3)) {
		apertou3 = 1;
	}
}

// pisca led N vez no periodo T
void pisca_led(int n, int t){
	gfx_mono_draw_horizontal_line(0, 5, 89, GFX_PIXEL_SET);
	for (int i=0;i<n;i++){
		pio_clear(LED_PIO, LED_IDX_MASK);	
		delay_ms(t);
		pio_set(LED_PIO, LED_IDX_MASK);
		gfx_mono_draw_rect(90-3*i, 5, 2, 10, GFX_PIXEL_CLR);
		gfx_mono_draw_rect(89-3*i, 5, 2, 10, GFX_PIXEL_CLR);
		gfx_mono_draw_rect(88-3*i, 5, 2, 10, GFX_PIXEL_CLR);
		delay_ms(t);
	}
}

// Inicializa botao SW0 do kit com interrupcao
void io_init(void)
{

	// Configura led
	pmc_enable_periph_clk(LED_PIO_ID);
	pio_configure(LED_PIO, PIO_OUTPUT_0, LED_IDX_MASK, PIO_DEFAULT);

	// Inicializa clock do periférico PIO responsavel pelo botao
	pmc_enable_periph_clk(BUT_PIO_ID1);
	pmc_enable_periph_clk(BUT_PIO_ID2);
	pmc_enable_periph_clk(BUT_PIO_ID3);

	// Configura PIO para lidar com o pino do botão como entrada
	// com pull-up
	// pio_configure(BUT_PIO, PIO_INPUT, BUT_IDX_MASK, PIO_PULLUP);
	
	pio_configure(BUT_PIO1, PIO_INPUT, BUT_PIO_IDX_MASK1, PIO_PULLUP | PIO_DEBOUNCE);
	pio_set_debounce_filter(BUT_PIO1, BUT_PIO_IDX_MASK1, 60);
	
	pio_configure(BUT_PIO2, PIO_INPUT, BUT_PIO_IDX_MASK2, PIO_PULLUP | PIO_DEBOUNCE);
	pio_set_debounce_filter(BUT_PIO2, BUT_PIO_IDX_MASK2, 60);
	
	pio_configure(BUT_PIO3, PIO_INPUT, BUT_PIO_IDX_MASK3, PIO_PULLUP | PIO_DEBOUNCE);
	pio_set_debounce_filter(BUT_PIO3, BUT_PIO_IDX_MASK3, 60);

	// Configura interrupção no pino referente ao botao e associa
	// função de callback caso uma interrupção for gerada
	// a função de callback é a: but_callback()
	
	pio_handler_set(BUT_PIO1, BUT_PIO_ID1, BUT_PIO_IDX_MASK1, PIO_IT_EDGE, but_callback1);
	
	pio_handler_set(BUT_PIO2, BUT_PIO_ID2, BUT_PIO_IDX_MASK2, PIO_IT_EDGE, but_callback2);
	
	pio_handler_set(BUT_PIO3, BUT_PIO_ID3, BUT_PIO_IDX_MASK3, PIO_IT_EDGE, but_callback3);
	

	// Ativa interrupção e limpa primeira IRQ gerada na ativacao	
	pio_enable_interrupt(BUT_PIO1, BUT_PIO_IDX_MASK1);
	pio_get_interrupt_status(BUT_PIO1);
	
	pio_enable_interrupt(BUT_PIO2, BUT_PIO_IDX_MASK2);
	pio_get_interrupt_status(BUT_PIO2);
	
	pio_enable_interrupt(BUT_PIO3, BUT_PIO_IDX_MASK3);
	pio_get_interrupt_status(BUT_PIO3);
	
	// Configura NVIC para receber interrupcoes do PIO do botao
	// com prioridade 4 (quanto mais próximo de 0 maior)
	
	NVIC_EnableIRQ(BUT_PIO_ID1);
	NVIC_SetPriority(BUT_PIO_ID1, 5); // Prioridade 4
	
	NVIC_EnableIRQ(BUT_PIO_ID2);
	NVIC_SetPriority(BUT_PIO_ID2, 4); // Prioridade 4
	
	NVIC_EnableIRQ(BUT_PIO_ID3);
	NVIC_SetPriority(BUT_PIO_ID3, 6); // Prioridade 4
}

int main (void){
	board_init();
	sysclk_init();
	delay_init();
	io_init();
	int velocidade = 200;
	double freq = 1000.0/velocidade;

  // Init OLED
	gfx_mono_ssd1306_init();
  
	char str[128];
	sprintf(str, "F: %.2f Hz", freq);
	gfx_mono_draw_string(str, 10,16, &sysfont);
	
  /* Insert application code here, after the board has been initialized. */
	while(1) {
				
				if (apertou1){
					apertou1 = 0;
					velocidade -= 100;
					while (soltou1 != 1){
						velocidade += 100;
						delay_ms(200);
					}
				}
				if (apertou2){
					apertou2 = 0;
					int travado = 1;
					while (travado){
						if (apertou2 != 0){
							travado = 0;
							apertou2 = 0;
						}
						delay_ms(200);
					}
				}
				if (apertou3){
					apertou3 = 0;
					velocidade += 100;
				}
			if (velocidade < 100){
				velocidade = 100;
			}
			freq = 1000.0/velocidade;
			sprintf(str, "F: %.2f Hz", freq);
			gfx_mono_draw_string(str, 10,16, &sysfont);
			pisca_led(30, velocidade);
						
			// Entra em sleep mode
			// pmc_sleep(SAM_PM_SMODE_SLEEP_WFI); // (1)

			// Escreve na tela um circulo e um texto			
	}
}
