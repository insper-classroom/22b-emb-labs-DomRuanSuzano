/************************************************************************/
/* includes                                                             */
/************************************************************************/

#include <asf.h>
#include <string.h>
#include "ili9341.h"
#include "lvgl.h"
#include "touch/touch.h"
LV_FONT_DECLARE(dseg70);
LV_FONT_DECLARE(dseg45);
LV_FONT_DECLARE(dseg35);
LV_IMG_DECLARE(clock);

#define AFEC_POT AFEC0
#define AFEC_POT_ID ID_AFEC0
#define AFEC_POT_CHANNEL 5 // Canal do pino PB2

typedef struct  {
	uint32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t week;
	uint32_t hour;
	uint32_t minute;
	uint32_t second;
} calendar;

/************************************************************************/
/* LCD / LVGL                                                           */
/************************************************************************/

#define LV_HOR_RES_MAX          (320)
#define LV_VER_RES_MAX          (240)

/*A static or global variable to store the buffers*/
static lv_disp_draw_buf_t disp_buf;

/*Static or global buffer(s). The second buffer is optional*/
static lv_color_t buf_1[LV_HOR_RES_MAX * LV_VER_RES_MAX];
static lv_disp_drv_t disp_drv;          /*A variable to hold the drivers. Must be static or global.*/
static lv_indev_drv_t indev_drv;

static lv_obj_t * labelBtn1;
static lv_obj_t * labelDes;
static lv_obj_t * labelMenu;
static lv_obj_t * labelClk;
static lv_obj_t * labelUp;
static lv_obj_t * labelDown;

static lv_obj_t * scrIni;  // screen 1
static lv_obj_t * scrDes;  // screen 2

lv_obj_t * labelFloor;
lv_obj_t * labelFloor1;
lv_obj_t * labelFloor2;
lv_obj_t * labelSetValue;
lv_obj_t * labelClock;
lv_obj_t * labelPontos;
lv_obj_t * labelPonto;

SemaphoreHandle_t xSemaphoreHora;
SemaphoreHandle_t xSemaphoreAjuste;
TimerHandle_t xTimer;

/************************************************************************/
/* RTOS                                                                 */
/************************************************************************/

#define TASK_LCD_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_LCD_STACK_PRIORITY            (tskIDLE_PRIORITY)

#define TASK_RTC_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_RTC_STACK_PRIORITY            (tskIDLE_PRIORITY)

#define TASK_PROC_STACK_SIZE (1024 * 6 / sizeof(portSTACK_TYPE))
#define TASK_PROC_STACK_PRIORITY (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}

void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type);
static void config_AFEC_pot(Afec *afec, uint32_t afec_id, uint32_t afec_channel,
afec_callback_t callback);

volatile char flag_clock = 0;
QueueHandle_t xQueuePROC;
/************************************************************************/
/* lvgl                                                                 */
/************************************************************************/
static void AFEC_pot_callback(void) {
	float adc;
	adc = afec_channel_get_value(AFEC_POT, AFEC_POT_CHANNEL);
	BaseType_t xHigherPriorityTaskWoken = pdTRUE;
	xQueueSendFromISR(xQueuePROC, &adc, &xHigherPriorityTaskWoken);
}

static void RTT_init(float freqPrescale, uint32_t IrqNPulses,
uint32_t rttIRQSource) {

	uint16_t pllPreScale = (int)(((float)32768) / freqPrescale);

	rtt_sel_source(RTT, false);
	rtt_init(RTT, pllPreScale);

	if (rttIRQSource & RTT_MR_ALMIEN) {
		uint32_t ul_previous_time;
		ul_previous_time = rtt_read_timer_value(RTT);
		while (ul_previous_time == rtt_read_timer_value(RTT))
		;
		rtt_write_alarm_time(RTT, IrqNPulses + ul_previous_time);
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

void RTT_Handler(void) {
	uint32_t ul_status;
	ul_status = rtt_get_status(RTT);

	/* IRQ due to Alarm */
	if ((ul_status & RTT_SR_ALMS) == RTT_SR_ALMS) {
		afec_channel_enable(AFEC_POT, AFEC_POT_CHANNEL);
		afec_start_software_conversion(AFEC_POT);
		RTT_init(1000, 100, RTT_MR_ALMIEN);
	}
}

void RTC_Handler(void) {
	uint32_t ul_status = rtc_get_status(RTC);
	
	/* seccond tick */
	if ((ul_status & RTC_SR_SEC) == RTC_SR_SEC) {
		// o código para irq de segundo vem aqui
		xSemaphoreGiveFromISR(xSemaphoreHora, 0);
	}
	
	/* Time or date alarm */
	if ((ul_status & RTC_SR_ALARM) == RTC_SR_ALARM) {
		// o código para irq de alame vem aqui
	}

	rtc_clear_status(RTC, RTC_SCCR_SECCLR);
	rtc_clear_status(RTC, RTC_SCCR_ALRCLR);
	rtc_clear_status(RTC, RTC_SCCR_ACKCLR);
	rtc_clear_status(RTC, RTC_SCCR_TIMCLR);
	rtc_clear_status(RTC, RTC_SCCR_CALCLR);
	rtc_clear_status(RTC, RTC_SCCR_TDERRCLR);
}

static void event_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		lv_scr_load(scrDes); // exibe tela Desligada
	}
}

static void liga_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);

	if(code == LV_EVENT_CLICKED) {
		lv_scr_load(scrIni); // exibe tela Ligada
	}
}

static void menu_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);
	
	if (code == LV_EVENT_CLICKED) {
		
	}
}

static void clk_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);
	if (code == LV_EVENT_CLICKED) {
		flag_clock = !flag_clock;
	}
}

static void up_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);
	char *c;
	int temp;
	if(flag_clock){
		if(code == LV_EVENT_CLICKED) {
			// Obter a hora atual do RTC
			printf("Entrei aqui\n");
			Rtc *rtc = RTC;
			calendar t;
			rtc_get_time(RTC, &t.hour, &t.minute, &t.second);

			// Atualizar a hora em um minuto para trás
			t.minute++;
			if (t.minute > 59) {
				t.minute = 0;
				t.hour++;
				if (t.hour > 23) {
					t.hour = 0;
				}
			}

			// Atualizar a hora no RTC
			rtc_set_time(RTC, t.hour, t.minute, t.second);
			lv_label_set_text_fmt(labelClock, "%02d:%02d", t.hour, t.minute);
		}
	}
	else {
		if(code == LV_EVENT_CLICKED){
			c = lv_label_get_text(labelSetValue);
			temp = atoi(c);
			lv_label_set_text_fmt(labelSetValue, "%02d", temp + 1);
		}
	}
}

static void down_handler(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);
	char *c;
	int temp;
	if(flag_clock){
		if(code == LV_EVENT_CLICKED) {
			// Obter a hora atual do RTC
			printf("Entrei aqui\n");
			Rtc *rtc = RTC;
			calendar t;
			rtc_get_time(RTC, &t.hour, &t.minute, &t.second);

			// Atualizar a hora em um minuto para trás
			t.minute--;
			if (t.minute < 0) {
				t.minute = 59;
				t.hour--;
				if (t.hour < 0) {
					t.hour = 23;
				}
			}

			// Atualizar a hora no RTC
			rtc_set_time(RTC, t.hour, t.minute, t.second);
			lv_label_set_text_fmt(labelClock, "%02d:%02d", t.hour, t.minute);
		}
	}
	else {
		if(code == LV_EVENT_CLICKED){
			c = lv_label_get_text(labelSetValue);
			temp = atoi(c);
			lv_label_set_text_fmt(labelSetValue, "%02d", temp - 1);
		}
	}
	
}

void lv_termostato(void) {
	static lv_style_t style;
	lv_style_init(&style);
	lv_style_set_bg_color(&style, lv_color_black());
	lv_style_set_border_color(&style, lv_color_black());
	lv_style_set_border_width(&style, 5);
	
	lv_obj_t * labelDes;
	lv_obj_t * btnDes = lv_btn_create(scrDes);
	lv_obj_add_event_cb(btnDes, liga_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btnDes, LV_ALIGN_CENTER, 0, 0);

	labelDes = lv_label_create(btnDes);
	lv_label_set_text(labelDes, LV_SYMBOL_POWER);
	lv_obj_center(labelDes);
	lv_obj_add_style(btnDes, &style, 0);
	lv_obj_set_width(btnDes, 55);
	lv_obj_set_height(btnDes, 55);
	
	lv_obj_t * labelBtn1;
	lv_obj_t * btn1 = lv_btn_create(scrIni);
	lv_obj_add_event_cb(btn1, event_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btn1, LV_ALIGN_BOTTOM_LEFT, 5, -10);

	labelBtn1 = lv_label_create(btn1);
	lv_label_set_text(labelBtn1, "[ " LV_SYMBOL_POWER);
	lv_obj_center(labelBtn1);
	lv_obj_add_style(btn1, &style, 0);
	lv_obj_set_width(btn1, 55);
	lv_obj_set_height(btn1, 55);
	
	
	lv_obj_t * labelMenu;
	lv_obj_t * btnMenu = lv_btn_create(scrIni);
	lv_obj_add_event_cb(btnMenu, menu_handler, LV_EVENT_ALL, NULL);
	lv_obj_align_to(btnMenu, btn1, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
	
	labelMenu = lv_label_create(btnMenu);
	lv_label_set_text(labelMenu, "| M |");
	lv_obj_center(labelMenu);
	lv_obj_add_style(btnMenu, &style, 0);
	lv_obj_set_width(btnMenu, 55);
	lv_obj_set_height(btnMenu, 55);
	

	lv_obj_t * clockBtn = lv_imgbtn_create(scrIni);
	lv_imgbtn_set_src(clockBtn, LV_IMGBTN_STATE_RELEASED, 0, &clock, 0);
	lv_imgbtn_set_src(clockBtn, LV_IMGBTN_STATE_PRESSED, 0, &clock, 0);
	lv_imgbtn_set_src(clockBtn, LV_IMGBTN_STATE_CHECKED_RELEASED, 0, &clock, 0);
	lv_imgbtn_set_src(clockBtn, LV_IMGBTN_STATE_CHECKED_PRESSED, 0, &clock, 0);
	lv_obj_add_flag(clockBtn, LV_OBJ_FLAG_CHECKABLE);
	lv_obj_add_event_cb(clockBtn, clk_handler, LV_EVENT_ALL, NULL);
	lv_obj_set_height(clockBtn, 25);
	lv_obj_set_width(clockBtn, 25);
	lv_obj_align_to(clockBtn, btnMenu, LV_ALIGN_OUT_RIGHT_MID, 15, 2);
	
	labelClk = lv_label_create(scrIni);
	lv_obj_align_to(labelClk, clockBtn, LV_ALIGN_OUT_RIGHT_MID, 0, -3);
	lv_obj_set_style_text_color(labelClk, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelClk, "  ]");
	
	
	lv_obj_t * labelUp;
	lv_obj_t * btnUp = lv_btn_create(scrIni);
	lv_obj_add_event_cb(btnUp, up_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btnUp, LV_ALIGN_BOTTOM_MID, 60, -10);

	labelUp = lv_label_create(btnUp);
	lv_label_set_text(labelUp, "[ " LV_SYMBOL_UP);
	lv_obj_center(labelUp);
	lv_obj_add_style(btnUp, &style, 0);
	lv_obj_set_width(btnUp, 55);
	lv_obj_set_height(btnUp, 55);
	
	
	lv_obj_t * labelDown;
	lv_obj_t * btnDown = lv_btn_create(scrIni);
	lv_obj_add_event_cb(btnDown, down_handler, LV_EVENT_ALL, NULL);
	lv_obj_align_to(btnDown, btnUp, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
	
	labelDown = lv_label_create(btnDown);
	lv_label_set_text(labelDown, "| " LV_SYMBOL_DOWN " ]");
	lv_obj_center(labelDown);
	lv_obj_add_style(btnDown, &style, 0);
	lv_obj_set_width(btnDown, 55);
	lv_obj_set_height(btnDown, 55);
	
	labelFloor = lv_label_create(scrIni);
	lv_obj_align(labelFloor, LV_ALIGN_LEFT_MID, 45 , -25);
	lv_obj_set_style_text_font(labelFloor, &dseg70, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelFloor, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelFloor, "%02d", 23);
	labelFloor1 = lv_label_create(scrIni);
	lv_obj_align_to(labelFloor1, labelFloor, LV_ALIGN_LEFT_MID, -35, -2);
	lv_obj_set_style_text_font(labelFloor1, &dseg35, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelFloor1, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelFloor1, "Floor");
	labelFloor2 = lv_label_create(scrIni);
	lv_obj_align_to(labelFloor2, labelFloor1, LV_ALIGN_TOP_MID, 10,15);
	lv_obj_set_style_text_font(labelFloor2, &dseg35, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelFloor2, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelFloor2, "Temp");
	
	labelPonto = lv_label_create(scrIni);
	lv_obj_align_to(labelPonto, labelFloor, LV_ALIGN_OUT_RIGHT_MID, 0 , 0);
	lv_obj_set_style_text_font(labelPonto, &dseg45, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelPonto, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelPonto, ".%d", 4);
	
	labelSetValue = lv_label_create(scrIni);
	lv_obj_align(labelSetValue, LV_ALIGN_RIGHT_MID, -20 , -35);
	lv_obj_set_style_text_font(labelSetValue, &dseg45, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelSetValue, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelSetValue, "%02d", 22);
	
	labelClock = lv_label_create(scrIni);
	lv_obj_align(labelClock, LV_ALIGN_TOP_RIGHT, -5, 5);
	lv_obj_set_style_text_font(labelClock, &dseg35, LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(labelClock, lv_color_white(), LV_STATE_DEFAULT);
	lv_label_set_text_fmt(labelClock, "%02d:%02d", 14, 20);
	
}

void RTC_init(Rtc *rtc, uint32_t id_rtc, calendar t, uint32_t irq_type) {
	/* Configura o PMC */
	pmc_enable_periph_clk(ID_RTC);

	/* Default RTC configuration, 24-hour mode */
	rtc_set_hour_mode(rtc, 0);

	/* Configura data e hora manualmente */
	rtc_set_date(rtc, t.year, t.month, t.day, t.week);
	rtc_set_time(rtc, t.hour, t.minute, t.second);

	/* Configure RTC interrupts */
	NVIC_DisableIRQ(id_rtc);
	NVIC_ClearPendingIRQ(id_rtc);
	NVIC_SetPriority(id_rtc, 4);
	NVIC_EnableIRQ(id_rtc);

	/* Ativa interrupcao via alarme */
	rtc_enable_interrupt(rtc,  irq_type);
}

/*void lv_ex_btn_1(void) {
	lv_obj_t * label;

	lv_obj_t * btn1 = lv_btn_create(lv_scr_act());
	lv_obj_add_event_cb(btn1, event_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btn1, LV_ALIGN_CENTER, 0, -40);

	label = lv_label_create(btn1);
	lv_label_set_text(label, "Corsi");
	lv_obj_center(label);

	lv_obj_t * btn2 = lv_btn_create(lv_scr_act());
	lv_obj_add_event_cb(btn2, event_handler, LV_EVENT_ALL, NULL);
	lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 40);
	lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
	lv_obj_set_height(btn2, LV_SIZE_CONTENT);

	label = lv_label_create(btn2);
	lv_label_set_text(label, "Toggle");
	lv_obj_center(label);
}*/

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/
void vTimerCallback(TimerHandle_t xTimer) {
	/* Selecina canal e inicializa conversão */
	afec_channel_enable(AFEC_POT, AFEC_POT_CHANNEL);
	afec_start_software_conversion(AFEC_POT);
}

static void task_lcd(void *pvParameters) {
	int px, py;
	scrIni  = lv_obj_create(NULL);
	scrDes  = lv_obj_create(NULL);
	//lv_ex_btn_1();
	
	
	lv_termostato();
	lv_scr_load(scrIni); // exibe tela Inicial
	for (;;)  {
		lv_tick_inc(50);
		lv_task_handler();
		vTaskDelay(50);
	}
}

static void task_rtc(void *pvParameters) {
	calendar rtc_initial = {2023, 04, 22, 4, 14, 20 ,0};

	/** Configura RTC */
	RTC_init(RTC, ID_RTC, rtc_initial, RTC_IER_SECEN);
	
	/* Leitura do valor atual do RTC */
	uint32_t current_hour, current_min, current_sec;
	uint32_t current_year, current_month, current_day, current_week;
	rtc_get_time(RTC, &current_hour, &current_min, &current_sec);

	for (;;)  {
		
		if (xSemaphoreTake(xSemaphoreHora, 0) == pdTRUE){
			rtc_get_time(RTC, &current_hour, &current_min, &current_sec);
			lv_label_set_text_fmt(labelClock, "%02d %02d", current_hour, current_min);
			vTaskDelay(1000);
			lv_label_set_text_fmt(labelClock, "%02d:%02d", current_hour, current_min);
			
		}
	}
}

static void task_proc(void *pvParameters){
	// configura ADC e TC para controlar a leitura
	config_AFEC_pot(AFEC_POT, AFEC_POT_ID, AFEC_POT_CHANNEL, AFEC_pot_callback);
	RTT_init(1000, 100, RTT_MR_ALMIEN);
  // variável para recever dados da fila
	float adc;
	int soma = 0;
	int n = 0;
	int media = 0;
	while (1){
		if (xQueueReceive(xQueuePROC, &(adc), 1000)){
			soma += adc;
			n++;
			if (n >= 10){
				media = soma / 10;
				n = 0;
				lv_label_set_text_fmt(labelFloor, "%02d", ((int) media)*100/4092);
				soma = 0;				
				
			} 	
		}
		vTaskDelay(100);
	}
}

/************************************************************************/
/* configs                                                              */
/************************************************************************/

static void configure_lcd(void) {
	/**LCD pin configure on SPI*/
	pio_configure_pin(LCD_SPI_MISO_PIO, LCD_SPI_MISO_FLAGS);  //
	pio_configure_pin(LCD_SPI_MOSI_PIO, LCD_SPI_MOSI_FLAGS);
	pio_configure_pin(LCD_SPI_SPCK_PIO, LCD_SPI_SPCK_FLAGS);
	pio_configure_pin(LCD_SPI_NPCS_PIO, LCD_SPI_NPCS_FLAGS);
	pio_configure_pin(LCD_SPI_RESET_PIO, LCD_SPI_RESET_FLAGS);
	pio_configure_pin(LCD_SPI_CDS_PIO, LCD_SPI_CDS_FLAGS);
	
	ili9341_init();
	ili9341_backlight_on();
}

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = USART_SERIAL_EXAMPLE_BAUDRATE,
		.charlength = USART_SERIAL_CHAR_LENGTH,
		.paritytype = USART_SERIAL_PARITY,
		.stopbits = USART_SERIAL_STOP_BIT,
	};

	/* Configure console UART. */
	stdio_serial_init(CONSOLE_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	setbuf(stdout, NULL);
}

static void config_AFEC_pot(Afec *afec, uint32_t afec_id, uint32_t afec_channel,
                            afec_callback_t callback) {
  /*************************************
   * Ativa e configura AFEC
   *************************************/
  /* Ativa AFEC - 0 */
  afec_enable(afec);

  /* struct de configuracao do AFEC */
  struct afec_config afec_cfg;

  /* Carrega parametros padrao */
  afec_get_config_defaults(&afec_cfg);

  /* Configura AFEC */
  afec_init(afec, &afec_cfg);

  /* Configura trigger por software */
  afec_set_trigger(afec, AFEC_TRIG_SW);

  /*** Configuracao específica do canal AFEC ***/
  struct afec_ch_config afec_ch_cfg;
  afec_ch_get_config_defaults(&afec_ch_cfg);
  afec_ch_cfg.gain = AFEC_GAINVALUE_0;
  afec_ch_set_config(afec, afec_channel, &afec_ch_cfg);

  /*
  * Calibracao:
  * Because the internal ADC offset is 0x200, it should cancel it and shift
  down to 0.
  */
  afec_channel_set_analog_offset(afec, afec_channel, 0x200);

  /***  Configura sensor de temperatura ***/
  struct afec_temp_sensor_config afec_temp_sensor_cfg;

  afec_temp_sensor_get_config_defaults(&afec_temp_sensor_cfg);
  afec_temp_sensor_set_config(afec, &afec_temp_sensor_cfg);

  /* configura IRQ */
  afec_set_callback(afec, afec_channel, callback, 1);
  NVIC_SetPriority(afec_id, 4);
  NVIC_EnableIRQ(afec_id);
}

/************************************************************************/
/* port lvgl                                                            */
/************************************************************************/

void my_flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p) {
	ili9341_set_top_left_limit(area->x1, area->y1);   ili9341_set_bottom_right_limit(area->x2, area->y2);
	ili9341_copy_pixels_to_screen(color_p,  (area->x2 + 1 - area->x1) * (area->y2 + 1 - area->y1));
	
	/* IMPORTANT!!!
	* Inform the graphics library that you are ready with the flushing*/
	lv_disp_flush_ready(disp_drv);
}

void my_input_read(lv_indev_drv_t * drv, lv_indev_data_t*data) {
	int px, py, pressed;
	
	if (readPoint(&px, &py))
		data->state = LV_INDEV_STATE_PRESSED;
	else
		data->state = LV_INDEV_STATE_RELEASED; 
	
	data->point.x = px;
	data->point.y = py;
}

void configure_lvgl(void) {
	lv_init();
	lv_disp_draw_buf_init(&disp_buf, buf_1, NULL, LV_HOR_RES_MAX * LV_VER_RES_MAX);
	
	lv_disp_drv_init(&disp_drv);            /*Basic initialization*/
	disp_drv.draw_buf = &disp_buf;          /*Set an initialized buffer*/
	disp_drv.flush_cb = my_flush_cb;        /*Set a flush callback to draw to the display*/
	disp_drv.hor_res = LV_HOR_RES_MAX;      /*Set the horizontal resolution in pixels*/
	disp_drv.ver_res = LV_VER_RES_MAX;      /*Set the vertical resolution in pixels*/

	lv_disp_t * disp;
	disp = lv_disp_drv_register(&disp_drv); /*Register the driver and save the created display objects*/
	
	/* Init input on LVGL */
	lv_indev_drv_init(&indev_drv);
	indev_drv.type = LV_INDEV_TYPE_POINTER;
	indev_drv.read_cb = my_input_read;
	lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/
int main(void) {
	/* board and sys init */
	board_init();
	sysclk_init();
	configure_console();

	/* LCd, touch and lvgl init*/
	configure_lcd();
	configure_touch();
	configure_lvgl();
	
	xSemaphoreHora = xSemaphoreCreateBinary();
	if (xSemaphoreHora == NULL)
	printf("falha em criar o semaforo \n");
	
	xSemaphoreAjuste = xSemaphoreCreateBinary();
	if (xSemaphoreAjuste == NULL)
	printf("falha em criar o semaforo \n");
	
	xQueuePROC = xQueueCreate(100, sizeof(int));
	if (xQueuePROC == NULL)
	printf("falha em criar a queue xQueuePROC \n");

	/* Create task to control oled */
	if (xTaskCreate(task_lcd, "LCD", TASK_LCD_STACK_SIZE, NULL, TASK_LCD_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create lcd task\r\n");
	}
	
	if (xTaskCreate(task_rtc, "RTC", TASK_RTC_STACK_SIZE, NULL, TASK_RTC_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create rtc task\r\n");
	}
	if (xTaskCreate(task_proc, "PROC", TASK_PROC_STACK_SIZE, NULL, TASK_PROC_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create test PROC task\r\n");
	}
	/* Start the scheduler. */
	vTaskStartScheduler();

	while(1){ }
}