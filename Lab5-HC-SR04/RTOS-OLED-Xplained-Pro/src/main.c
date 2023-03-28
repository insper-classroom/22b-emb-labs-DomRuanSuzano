#include <asf.h>
#include "conf_board.h"

#include "gfx_mono_ug_2832hsweg04.h"
#include "gfx_mono_text.h"
#include "sysfont.h"

#define PIO_TRI     PIOD
#define PIO_TRI_ID  ID_PIOD
#define PIO_TRI_PIN 26
#define PIO_TRI_PIN_MASK (1 << PIO_TRI_PIN)

#define PIO_ECH     PIOD
#define PIO_ECH_ID  ID_PIOD
#define PIO_ECH_PIN 30
#define PIO_ECH_PIN_MASK (1 << PIO_ECH_PIN)

/** RTOS  */
#define TASK_SOMA_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_SOMA_STACK_PRIORITY            (tskIDLE_PRIORITY)
#define TASK_SENSOR_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_SENSOR_STACK_PRIORITY            (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);

/** prototypes */
static void BUT_init(void);

/************************************************************************/
/* RTOS application funcs                                               */
/************************************************************************/


extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}


/** Queue for msg log send data */
QueueHandle_t xQueueEcho;
QueueHandle_t xQueueSensor;

volatile float tempo;

/************************************************************************/
/* handlers / callbacks                                                 */
/************************************************************************/

void echo_callback(void) {
	if (pio_get(PIO_ECH, PIO_INPUT, PIO_ECH_PIN_MASK)){
		rtt_init(RTT, 1);
		
	} else{
		tempo = rtt_read_timer_value(RTT);
		BaseType_t xHigherPriorityTaskWoken = pdTRUE;
		xQueueSendFromISR(xQueueEcho, &tempo, &xHigherPriorityTaskWoken);
	}
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/

static void task_sensor(void *pvParameters) {
	float media;
	int n = 10;
	gfx_mono_ssd1306_init();
	while (1) { 
		if (xQueueReceive(xQueueSensor, &(media), 1000)) {			
			gfx_mono_draw_string("                   ", 10, 22, &sysfont);
			if (media > 400.0 || media < 2.0) {
				gfx_mono_draw_string("Fora do range", 10,22, &sysfont);
			} else{
				float media_norm = 1 + (20.0*(media-2)/398);
				char str[128];
				sprintf(str, "D: %.2f cm", media);
				gfx_mono_draw_string(str, 10,22, &sysfont);
				gfx_mono_draw_rect(n, 0, 2, (int) media_norm, 1);
				n += 4;
				if (n > 120){
					n = 10;
					for (int k = 0; k < 120; k++){
						gfx_mono_draw_rect(k, 0, 2, 20, GFX_PIXEL_CLR);
					}
				}
			}
		}
	}
}

static void task_soma(void *pvParameters){
	
  // variável para recever dados da fila
	float tempo;
	float soma = 0;
	int n = 0;
	float media = 0;
	float t;
	while (1){
		pio_set(PIO_TRI, PIO_TRI_PIN_MASK);
		delay_us(10);
		pio_clear(PIO_TRI, PIO_TRI_PIN_MASK);
		if (xQueueReceive(xQueueEcho, &(tempo), 1000)){
			float dist = 0;
			float t = (1.0/32768.0) * tempo; 
			dist = (34000.0*t)/2;
			soma += dist;
			n++;
			if (n >= 10){
				media = soma / 10;
				n = 0;
				soma = 0;
				xQueueSend(xQueueSensor, &media, 10);
				media = 0;
			} 	
		} else {
			printf("sem informações no momento! \n");
		}
		vTaskDelay(60);
	}
}

/************************************************************************/
/* funcoes                                                              */
/************************************************************************/

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = CONF_UART_BAUDRATE,
		.charlength = CONF_UART_CHAR_LENGTH,
		.paritytype = CONF_UART_PARITY,
		.stopbits = CONF_UART_STOP_BITS,
	};

	/* Configure console UART. */
	stdio_serial_init(CONF_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	setbuf(stdout, NULL);
}

static void BUT_init(void) {
	
	/* conf trig como saida */
	pmc_enable_periph_clk(PIO_TRI_ID);
	pio_configure(PIO_TRI, PIO_OUTPUT_0, PIO_TRI_PIN_MASK, PIO_DEFAULT);
	
	/* conf echo como entrada */
	pmc_enable_periph_clk(PIO_ECH_ID);
	pio_configure(PIO_ECH, PIO_INPUT, PIO_ECH_PIN_MASK, PIO_DEFAULT);
	
	pio_handler_set(PIO_ECH, PIO_ECH_ID, PIO_ECH_PIN_MASK, PIO_IT_EDGE , echo_callback);
	pio_enable_interrupt(PIO_ECH, PIO_ECH_PIN_MASK);
	pio_get_interrupt_status(PIO_ECH);
		
	/* configura prioridae */
	NVIC_EnableIRQ(PIO_ECH_ID);
	NVIC_SetPriority(PIO_ECH_ID, 4);
}

static void RTT_init(float freqPrescale, uint32_t IrqNPulses, uint32_t rttIRQSource) {

	uint16_t pllPreScale = (int) (((float) 32768) / freqPrescale);
	
	rtt_sel_source(RTT, false);
	rtt_init(RTT, pllPreScale);
	
	if (rttIRQSource & RTT_MR_ALMIEN) {
		uint32_t ul_previous_time;
		ul_previous_time = rtt_read_timer_value(RTT);
		while (ul_previous_time == rtt_read_timer_value(RTT));
		rtt_write_alarm_time(RTT, IrqNPulses+ul_previous_time);
	}

	/* config NVIC */
	NVIC_DisableIRQ(RTT_IRQn);
	NVIC_ClearPendingIRQ(RTT_IRQn);
	NVIC_SetPriority(RTT_IRQn, 4);
	NVIC_EnableIRQ(RTT_IRQn);

	/* Enable RTT interrupt */
	if (rttIRQSource & (RTT_MR_RTTINCIEN | RTT_MR_ALMIEN))
	rtt_enable_interrupt(RTT, rttIRQSource);
	else
	rtt_disable_interrupt(RTT, RTT_MR_RTTINCIEN | RTT_MR_ALMIEN);
	
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/

int main(void) {
	/* Initialize the SAM system */
	sysclk_init();
	board_init();
	BUT_init();
	/* Initialize the console uart */
	configure_console();

	/* Create task to control oled */
	
	xQueueSensor = xQueueCreate(100, sizeof(float));
	if (xQueueSensor == NULL)
	printf("falha em criar a queue xQueueSensor \n");
	xQueueEcho = xQueueCreate(100, sizeof(float));
	if (xQueueEcho == NULL)
	printf("falha em criar a queue xQueueEcho \n");

	if (xTaskCreate(task_soma, "ADC", TASK_SOMA_STACK_SIZE, NULL,
	TASK_SOMA_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create test ADC task\r\n");
	}
	if (xTaskCreate(task_sensor, "PROC", TASK_SENSOR_STACK_SIZE, NULL,
	TASK_SENSOR_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create test PROC task\r\n");
	}

	/* Start the scheduler. */
	vTaskStartScheduler();

  /* RTOS não deve chegar aqui !! */
	while(1){}

	/* Will only get here if there was insufficient memory to create the idle task. */
	return 0;
}
