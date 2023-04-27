#include <asf.h>
#include "conf_board.h"

#include "gfx_mono_ug_2832hsweg04.h"
#include "gfx_mono_text.h"
#include "sysfont.h"

#define BUT_PLACA_PIO     PIOA
#define BUT_PLACA_PIO_ID  ID_PIOA
#define BUT_PLACA_PIO_PIN 11
#define BUT_PLACA_PIO_PIN_MASK (1 << BUT_PLACA_PIO_PIN)


/** RTOS  */
#define TASK_IMU_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_IMU_STACK_PRIORITY            (tskIDLE_PRIORITY)
#define TASK_DOWN_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_DOWN_STACK_PRIORITY            (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);
int8_t mcu6050_i2c_bus_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint8_t cnt);
int8_t mcu6050_i2c_bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint8_t cnt);

/** prototypes */
void but_callback(void);
static void BUT_init(void);
void pisca_led(int n, int delay);
void mcu6050_i2c_bus_init(void);

QueueHandle_t xQueueOrientacao;
SemaphoreHandle_t xSemaphoreQueda;

enum orientacao{ESQUERDA=1, FRENTE, DIREITA};
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

/************************************************************************/
/* handlers / callbacks                                                 */
/************************************************************************/

void but_callback(void) {
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/
static void task_house_down(void *pvParameters) {
	
	while(1){
		if (xSemaphoreTake(xSemaphoreQueda, 1000)) {
			pisca_led(5, 100);
		} else {
			pio_set(LED_PIO, LED_IDX_MASK);
		}
	}
}

static void task_orientacao(void *pvParameters) {
	int direcao;
	while(1){
		if (xQueueReceive(xQueueOrientacao, &(direcao), 100)){
			if (direcao == ESQUERDA){
				pio_clear(LED1_PIO, LED1_PIO_IDX_MASK);
				pio_set(LED2_PIO, LED2_PIO_IDX_MASK);
				pio_set(LED3_PIO, LED3_PIO_IDX_MASK);
			}
			if (direcao == FRENTE){
				pio_clear(LED2_PIO, LED2_PIO_IDX_MASK);
				pio_set(LED1_PIO, LED1_PIO_IDX_MASK);
				pio_set(LED3_PIO, LED3_PIO_IDX_MASK);
			}
			if (direcao == DIREITA){
				pio_clear(LED3_PIO, LED3_PIO_IDX_MASK);
				pio_set(LED1_PIO, LED1_PIO_IDX_MASK);
				pio_set(LED2_PIO, LED2_PIO_IDX_MASK);
			}
			if (direcao == 4){
				pio_set(LED3_PIO, LED3_PIO_IDX_MASK);
				pio_set(LED1_PIO, LED1_PIO_IDX_MASK);
				pio_set(LED2_PIO, LED2_PIO_IDX_MASK);
			}
		}
	}
}

static void task_imu(void *pvParameters) {
	int16_t  raw_acc_x, raw_acc_y, raw_acc_z;
	volatile uint8_t  raw_acc_xHigh, raw_acc_yHigh, raw_acc_zHigh;
	volatile uint8_t  raw_acc_xLow,  raw_acc_yLow,  raw_acc_zLow;
	float proc_acc_x, proc_acc_y, proc_acc_z;

	int16_t  raw_gyr_x, raw_gyr_y, raw_gyr_z;
	volatile uint8_t  raw_gyr_xHigh, raw_gyr_yHigh, raw_gyr_zHigh;
	volatile uint8_t  raw_gyr_xLow,  raw_gyr_yLow,  raw_gyr_zLow;
	float proc_gyr_x, proc_gyr_y, proc_gyr_z;
		
	mcu6050_i2c_bus_init();
	
	/* buffer para recebimento de dados */
	uint8_t bufferRX[10];
	uint8_t bufferTX[10];

	/* resultado da função */
	uint8_t rtn;
	
	rtn = twihs_probe(TWIHS2, MPU6050_DEFAULT_ADDRESS);
	if(rtn != TWIHS_SUCCESS){
		printf("[ERRO] [i2c] [probe] \n");
	} else {
		printf("[DADO] [i2c] probe OK\n" );
	}
	
	// Lê registrador WHO AM I
	rtn = mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_WHO_AM_I, bufferRX, 1);
	if(rtn != TWIHS_SUCCESS){
		printf("[ERRO] [i2c] [read] \n");
		} else {
		printf("[DADO] [i2c] %x:%x \n", MPU6050_RA_WHO_AM_I, bufferRX[0]);
	}
	if (bufferRX[0] == 104)
	{
		printf("buffer está recebendo certo \n");
	} else {
		printf("Erro no recebimento do buffer \n");
	}
	
	// Set Clock source
	bufferTX[0] = MPU6050_CLOCK_PLL_XGYRO;
	rtn = mcu6050_i2c_bus_write(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_PWR_MGMT_1, bufferTX, 1);
	if(rtn != TWIHS_SUCCESS)
	printf("[ERRO] [i2c] [write] \n");

	// Aceletromtro em 2G
	bufferTX[0] = MPU6050_ACCEL_FS_2 << MPU6050_ACONFIG_AFS_SEL_BIT;
	rtn = mcu6050_i2c_bus_write(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_ACCEL_CONFIG, bufferTX, 1);
	if(rtn != TWIHS_SUCCESS)
	printf("[ERRO] [i2c] [write] \n");

	// Configura range giroscopio para operar com 250 °/s
	bufferTX[0] = 0x00; // 250 °/s
	rtn = mcu6050_i2c_bus_write(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_GYRO_CONFIG, bufferTX, 1);
	if(rtn != TWIHS_SUCCESS)
	printf("[ERRO] [i2c] [write] \n");
	
	/* Inicializa Função de fusão */
	FusionAhrs ahrs;
	FusionAhrsInitialise(&ahrs);
	
	int d;
	
	while (1)  {
		// Le valor do acc X High e Low
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_ACCEL_XOUT_H, &raw_acc_xHigh, 1);
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_ACCEL_XOUT_L, &raw_acc_xLow,  1);

		// Le valor do acc y High e  Low
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_ACCEL_YOUT_H, &raw_acc_yHigh, 1);
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_ACCEL_ZOUT_L, &raw_acc_yLow,  1);

		// Le valor do acc z HIGH e Low
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_ACCEL_ZOUT_H, &raw_acc_zHigh, 1);
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_ACCEL_ZOUT_L, &raw_acc_zLow,  1);

		// Dados são do tipo complemento de dois
		raw_acc_x = (raw_acc_xHigh << 8) | (raw_acc_xLow << 0);
		raw_acc_y = (raw_acc_yHigh << 8) | (raw_acc_yLow << 0);
		raw_acc_z = (raw_acc_zHigh << 8) | (raw_acc_zLow << 0);

		// Le valor do gyr X High e Low
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_GYRO_XOUT_H, &raw_gyr_xHigh, 1);
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_GYRO_XOUT_L, &raw_gyr_xLow,  1);

		// Le valor do gyr y High e  Low
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_GYRO_YOUT_H, &raw_gyr_yHigh, 1);
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_GYRO_ZOUT_L, &raw_gyr_yLow,  1);

		// Le valor do gyr z HIGH e Low
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_GYRO_ZOUT_H, &raw_gyr_zHigh, 1);
		mcu6050_i2c_bus_read(MPU6050_DEFAULT_ADDRESS, MPU6050_RA_GYRO_ZOUT_L, &raw_gyr_zLow,  1);

		// Dados são do tipo complemento de dois
		raw_gyr_x = (raw_gyr_xHigh << 8) | (raw_gyr_xLow << 0);
		raw_gyr_y = (raw_gyr_yHigh << 8) | (raw_gyr_yLow << 0);
		raw_gyr_z = (raw_gyr_zHigh << 8) | (raw_gyr_zLow << 0);

		// Dados em escala real
		proc_acc_x = (float)raw_acc_x/16384;
		proc_acc_y = (float)raw_acc_y/16384;
		proc_acc_z = (float)raw_acc_z/16384;

		proc_gyr_x = (float)raw_gyr_x/131;
		proc_gyr_y = (float)raw_gyr_y/131;
		proc_gyr_z = (float)raw_gyr_z/131;
		
		if (sqrt(proc_acc_x*proc_acc_x + proc_acc_y*proc_acc_y + proc_acc_z*proc_acc_z) > 1.5){
			xSemaphoreGive(xSemaphoreQueda);
		}
		
		const FusionVector gyroscope = {proc_gyr_x, proc_gyr_y, proc_gyr_z};
		const FusionVector accelerometer = {proc_acc_x, proc_acc_y, proc_acc_z};
			
		// Tempo entre amostras
		float dT = 0.1;

		// aplica o algoritmo
		FusionAhrsUpdateNoMagnetometer(&ahrs, gyroscope, accelerometer, dT);

		// dados em pitch roll e yaw
		const FusionEuler euler = FusionQuaternionToEuler(FusionAhrsGetQuaternion(&ahrs));
		
		if (euler.angle.pitch < -25){
			d = 4;
		} else if (euler.angle.pitch > 25) {
			d = FRENTE;
		} else if (euler.angle.roll < -25){
			d = ESQUERDA;
		} else if (euler.angle.roll > 25){
			d = DIREITA;
		} else {
			d = 4;
		}
		xQueueSend(xQueueOrientacao, &d, 0);
		// uma amostra a cada 50ms
		vTaskDelay(100);
	}
}

/************************************************************************/
/* funcoes                                                              */
/************************************************************************/

void led_placa_init(void){
	// Ativa PIO
	pmc_enable_periph_clk(LED_PIO_ID);
	
	pmc_enable_periph_clk(LED1_PIO_ID);
	pmc_enable_periph_clk(LED2_PIO_ID);
	pmc_enable_periph_clk(LED3_PIO_ID);
	
	// Configura como OUTPUT
	pio_configure(LED_PIO, PIO_OUTPUT_0, LED_IDX_MASK, PIO_DEFAULT);
	pio_configure(LED1_PIO, PIO_OUTPUT_0, LED1_PIO_IDX_MASK, PIO_DEFAULT);
	pio_configure(LED2_PIO, PIO_OUTPUT_0, LED2_PIO_IDX_MASK, PIO_DEFAULT);
	pio_configure(LED3_PIO, PIO_OUTPUT_0, LED3_PIO_IDX_MASK, PIO_DEFAULT);	
}
// pisca led N vez no periodo T
void pisca_led(int n, int t){
	for (int i=0;i<n;i++){
		pio_clear(LED_PIO, LED_IDX_MASK);
		delay_ms(t);
		pio_set(LED_PIO, LED_IDX_MASK);
		delay_ms(t);
	}
}

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

void mcu6050_i2c_bus_init(void)
{
	twihs_options_t mcu6050_option;
	pmc_enable_periph_clk(ID_TWIHS2);

	/* Configure the options of TWI driver */
	mcu6050_option.master_clk = sysclk_get_cpu_hz();
	mcu6050_option.speed      = 40000;
	twihs_master_init(TWIHS2, &mcu6050_option);
	
	pmc_enable_periph_clk(ID_PIOD);
	pio_set_peripheral(PIOD, PIO_TYPE_PIO_PERIPH_C, 1 << 28);
	pio_set_peripheral(PIOD, PIO_TYPE_PIO_PERIPH_C, 1 << 27);
}

int8_t mcu6050_i2c_bus_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint8_t cnt)
{
	int32_t ierror = 0x00;

	twihs_packet_t p_packet;
	p_packet.chip         = dev_addr;
	p_packet.addr[0]      = reg_addr;
	p_packet.addr_length  = 1;
	p_packet.buffer       = reg_data;
	p_packet.length       = cnt;

	ierror = twihs_master_write(TWIHS2, &p_packet);

	return (int8_t)ierror;
}

int8_t mcu6050_i2c_bus_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint8_t cnt)
{
	int32_t ierror = 0x00;

	twihs_packet_t p_packet;
	p_packet.chip         = dev_addr;
	p_packet.addr[0]      = reg_addr;
	p_packet.addr_length  = 1;
	p_packet.buffer       = reg_data;
	p_packet.length       = cnt;

	// TODO: Algum problema no SPI faz com que devemos ler duas vezes o registrador para
	//       conseguirmos pegar o valor correto.
	ierror = twihs_master_read(TWIHS2, &p_packet);

	return (int8_t)ierror;
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/


int main(void) {
	/* Initialize the SAM system */
	sysclk_init();
	board_init();
	led_placa_init();

	/* Initialize the console uart */
	configure_console();
	
	xSemaphoreQueda = xSemaphoreCreateBinary();
	if (xSemaphoreQueda == NULL){
		printf("falha em criar o semaforo queda");
	}
	
	if (xTaskCreate(task_imu, "imu", TASK_IMU_STACK_SIZE, NULL, TASK_IMU_STACK_PRIORITY, NULL) != pdPASS) {
	  printf("Failed to create imu task\r\n");
	}
	if (xTaskCreate(task_house_down, "house_down", TASK_DOWN_STACK_SIZE, NULL, TASK_DOWN_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create house_down task\r\n");
	}
	if (xTaskCreate(task_orientacao, "orientacao", TASK_DOWN_STACK_SIZE, NULL, TASK_DOWN_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create house_down task\r\n");
	}
	
	xQueueOrientacao = xQueueCreate(32, sizeof(int));
	
	if (xQueueOrientacao == NULL)
	printf("falha em criar a queue \n");

	/* Start the scheduler. */
	vTaskStartScheduler();

  /* RTOS não deve chegar aqui !! */
	while(1){}

	/* Will only get here if there was insufficient memory to create the idle task. */
	return 0;
}
