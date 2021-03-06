/*
===============================================================================
 Name        : main.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : main definition
===============================================================================
 */

#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>
#include "FreeRTOS.h"
#include "task.h"
#include "DigitalIoPin.h"
#include "semphr.h"
#include <mutex>
#include <stdio.h>
#include "queue.h"
#include <string>
#include <cmath>
#include "Fmutex.h"
#include "user_vcom.h"
#include <cstring>

#define LEFT true			//counter clockwise
#define RIGHT false			//clockwise
#define CALIB 0
#define BRESENHAM 1

struct profile {
	bool motorX_dir;		//0: clockwidse, 1: counter-clockwise
	bool motorY_dir;
	int speed;				//0 - 100%
	int penUp;				//0 - 255
	int penDown;
	int penPos;
	bool limXmax;
	bool limXmin;
	bool limYmax;
	bool limYmin;
	int height;
	int width;
	double X;
	double Y;
	bool A;
};

volatile uint32_t XRIT_count;
volatile uint32_t YRIT_count;
bool calib_done = false;

DigitalIoPin swXmin(0, 29, DigitalIoPin::pullup, false);
DigitalIoPin swXmax(0, 9, DigitalIoPin::pullup, false);
DigitalIoPin swYmin(1, 3, DigitalIoPin::pullup, false);
DigitalIoPin swYmax(0, 0, DigitalIoPin::pullup, false);

xSemaphoreHandle sbRIT = xSemaphoreCreateBinary();
xSemaphoreHandle limXmax_sem = xSemaphoreCreateBinary();
xSemaphoreHandle limXmin_sem = xSemaphoreCreateBinary();
xSemaphoreHandle limYmax_sem = xSemaphoreCreateBinary();
xSemaphoreHandle limYmin_sem = xSemaphoreCreateBinary();
xSemaphoreHandle calib_donesem = xSemaphoreCreateBinary();
xSemaphoreHandle inten_donesem = xSemaphoreCreateBinary();

QueueHandle_t xQueue;

static void prvSetupHardware(void);
void RIT_start(int xcount, int ycount, int us);
void SetupInt(int port, int pin, int index);
void GotoPos(DigitalIoPin xdir, DigitalIoPin ydir, int &xcurrent_pulse, int &ycurrent_pulse, double x, double y, double pulseOnwidth, double pulseOnheight);
static void vTask1(void *pvParameters);
static void vTask2(void *pvParameters);





int main(void)
{
	xQueue = xQueueCreate(1, sizeof(profile));
	if(xQueue != NULL) {
		prvSetupHardware();
		xTaskCreate(vTask1, "serial",
				configMINIMAL_STACK_SIZE + 450, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

		xTaskCreate(vTask2, "stepper",
				configMINIMAL_STACK_SIZE + 300, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);

		xTaskCreate(cdc_task, "CDC",
				configMINIMAL_STACK_SIZE + 150, NULL, (tskIDLE_PRIORITY + 1UL),
				(TaskHandle_t *) NULL);
		vTaskStartScheduler();
	}
	else
		return 1;
}

static void vTask1(void *pvParameters) {		//serial
	char command[26] = {0};
	unsigned char reply[60];
	const unsigned char OK_reply[] = "OK\r\n";
	int j, pos, num = 0, cmd_len;
	double coordinate;
	std::string str;
	profile plotter;
	plotter.motorX_dir = 0;
	plotter.motorY_dir = 0;
	plotter.penDown = 180;
	plotter.penUp = 0;
	plotter.penPos = 0;
	plotter.speed = 50;
	plotter.height = 310;
	plotter.width = 380;
	plotter.X = 0.0;
	plotter.Y = 0.0;
	plotter.A = 0;

	while(xSemaphoreTake(calib_donesem, 0) != pdTRUE) {				//wait until calibration is done
	}

	while(1) {
		cmd_len = USB_receive((uint8_t *) command, 25);

		if(command[cmd_len-1] == '\n') {
			str = command;
			str[str.length()-1] = '\0';

			if((pos = str.find("M10")) != -1) {
				snprintf((char*) reply, 60, "M10 XY 310 340 0.00 0.00 A%d B%d H0 S%d U%d D%d\r\nOK\r\n",
						plotter.motorX_dir, plotter.motorY_dir, plotter.speed, plotter.penUp, plotter.penDown);

				USB_send(reply, strlen((char*) reply));
				str = "";
			}
			else if((pos = str.find("M11")) != -1) {
				snprintf((char*)reply, 60, "M11 %d %d %d %d\r\nOK\r\n",
						plotter.limXmin, plotter.limXmax, plotter.limYmin, plotter.limYmax);
				USB_send(reply, strlen((char*) reply));
				str = "";
			}
			else if((pos = str.find("M2 U")) != -1) {					//send to task 4
				pos += 4;											//first digit after "M2 U"
				for(j = pos; str[j] >= '0' && str[j] <= '9'; j++) {
				}
				num = 0;
				for(; pos < j; pos++ ) {
					num += (str[pos] - '0') * pow(10, j - pos - 1);
				}
				plotter.penUp = num;
				pos += 2;
				for(j = pos; str[j] >= '0' && str[j] <= '9'; j++) {
				}
				num = 0;
				for(; pos < j; pos++ ) {
					num += (str[pos] - '0') * pow(10, j - pos - 1);
				}
				plotter.penDown = num;
				USB_send((uint8_t*) OK_reply, 4);
				str = "";
			}
			else if((pos = str.find("M1 ")) != -1) {						//servo pen position
				pos += 3;											//first digit after "M1 "
				for(j = pos; str[j] >= '0' && str[j] <= '9'; j++) {
				}
				num = 0;
				for(; pos < j; pos++ ) {
					num += (str[pos] - '0') * pow(10, j - pos - 1);
				}
				plotter.penPos = num;

				USB_send((uint8_t*) OK_reply, 4);
				str = "";
			}
			else if((pos = str.find("M5 A")) != -1) {					//task 3
				pos += 4;											//first digit after "M5 A"
				num = str[pos] - '0';
				plotter.motorX_dir = (bool) num;

				pos += 3;
				num = str[pos] - '0';
				plotter.motorY_dir = (bool) num;

				pos += 3;
				for(j = pos; str[j] >= '0' && str[j] <= '9'; j++) {
				}
				num = 0;
				for(; pos < j; pos++ ) {
					num += (str[pos] - '0') * pow(10, j - pos - 1);
				}
				plotter.height = num;
				pos += 2;
				for(j = pos; str[j] >= '0' && str[j] <= '9'; j++) {
				}
				num = 0;
				for(; pos < j; pos++ ) {
					num += (str[pos] - '0') * pow(10, j - pos - 1);
				}
				plotter.width = num;
				pos += 2;
				for(j = pos; str[j] >= '0' && str[j] <= '9'; j++) {
				}
				num = 0;
				for(; pos < j; pos++ ) {
					num += (str[pos] - '0') * pow(10, j - pos - 1);
				}
				plotter.speed = num;
				USB_send((uint8_t*) OK_reply, 4);
				str = "";
				xQueueSend(xQueue, &plotter, 0);

			}
			else if((pos = str.find("G28")) != -1) {								//task 3
				plotter.X = 0.0;
				plotter.Y = 0.0;
				str = "";
				xQueueSend(xQueue, &plotter, 0);
			}
			else if((pos = str.find("G1 X")) != -1) {							//task 3
				pos += 4;						//first digit after "M2 U"
				for(j = pos; str[j] >= '0' && str[j] <= '9'; j++) {			//get final digit pos of xx. part
				}
				coordinate = 0;
				for(; pos < j; pos++ ) {									//collect the xx.
					coordinate += (str[pos] - '0') * pow(10, j - pos - 1);
				}
				for(j = pos + 2; str[j] >= '0' && str[j] <= '9'; j++) {		//get final digit pos of .xx part
				}
				for(pos++; pos < j; pos++) {								//collect the .xx
					coordinate += (str[pos] - '0') * pow(10, j - pos - 3);
				}
				plotter.X = coordinate;

				pos += 2;													//first digit after "Y"
				for(j = pos; str[j] >= '0' && str[j] <= '9'; j++) {			//get final digit pod of xx. part
				}
				coordinate = 0;
				for(; pos < j; pos++ ) {									//collect the xx.
					coordinate += (str[pos] - '0') * pow(10, j - pos - 1);
				}
				for(j = pos + 2; str[j] >= '0' && str[j] <= '9'; j++) {		//get final digit pos of .xx part
				}
				for(pos++; pos < j; pos++) {								//collect the .xx
					coordinate += (str[pos] - '0') * pow(10, j - pos - 3);
				}
				plotter.Y = coordinate;

				pos += 2;
				num = str[pos] - '0';
				plotter.A = (bool) num;
				str = "";
				USB_send((uint8_t*) OK_reply, 4);
				xQueueSend(xQueue, &plotter, portMAX_DELAY);
			}

		}
	}
}


static void vTask2(void *pvParameters) {					//motors
	DigitalIoPin YDIR(0, 28, DigitalIoPin::output, false);
	DigitalIoPin XDIR(1, 0, DigitalIoPin::output, false);

	int Xpulse_count = 0, Ypulse_count = 0, Xcurrent_pulse = 0, Ycurrent_pulse = 0;
	double XpulseOverwidth, YpulseOverheight;
	profile plotter2;

	int current_pos;
	uint32_t  ticks, penup, pendown;
	/* Initialize the SCT as PWM and set frequency */
	Chip_SCTPWM_Init(LPC_SCT0);
	Chip_SCTPWM_SetRate(LPC_SCT0, 50);					//50 Hz
	Chip_Clock_EnablePeriphClock(SYSCTL_CLOCK_SWM);		// Enable SWM clock before altering SWM

	Chip_SWM_MovablePinAssign(SWM_SCT0_OUT1_O, 10);		//Set PIO0_10 as PWM out
	Chip_Clock_DisablePeriphClock(SYSCTL_CLOCK_SWM);
	Chip_SCTPWM_SetOutPin(LPC_SCT0, 1, 1);				//index: 1, pinout: 1

	penup = Chip_SCTPWM_GetTicksPerCycle(LPC_SCT0)*5/100;
	pendown = Chip_SCTPWM_GetTicksPerCycle(LPC_SCT0)*8/100;
	Chip_SCTPWM_SetDutyCycle(LPC_SCT0, 1, penup);
	Chip_SCTPWM_Start(LPC_SCT0);

	XDIR.write(RIGHT);
	while(swXmin.read()) {		//go to X = 0
		RIT_start(10, 0, 200);
	}

	YDIR.write(RIGHT);
	while(swYmin.read()) {		//go to Y = 0
		RIT_start(0, 10, 200);
	}

#if CALIB
	XDIR.write(LEFT);
	while(swXmax.read()) {		//go to X = max
		Xpulse_count++;
		RIT_start(1, 0, 200);
	}

	YDIR.write(LEFT);
	while(swYmax.read()) {		//go to Y  = max
		Ypulse_count++;
		RIT_start(0, 1, 200);
	}
	XDIR.write(RIGHT);
	YDIR.write(RIGHT);
#endif
#if !CALIB
	Xpulse_count = 26985;		//27216	27218 27219	plotter1: 26985
	Ypulse_count = 30427;		//30188 30190 30181			  30427
	XDIR.write(LEFT);
	YDIR.write(LEFT);
#endif

	Xcurrent_pulse = Xpulse_count/2;
	Ycurrent_pulse = Ypulse_count/2;
	XpulseOverwidth = (double) Xpulse_count/310;
	YpulseOverheight = (double) Ypulse_count/340;

	RIT_start(Xcurrent_pulse, Ycurrent_pulse, 200);

	calib_done = true;
	xSemaphoreGive(calib_donesem);								//give semaphore to task 1

	while(1) {
		if(xQueueReceive(xQueue, &plotter2, 10) == pdTRUE) {
			if(plotter2.penPos != current_pos) {				//only move servo when needed
				ticks = plotter2.penPos*(pendown - penup)/248 + penup;
				Chip_SCTPWM_SetDutyCycle(LPC_SCT0, 1, ticks);
				vTaskDelay(configTICK_RATE_HZ/5);				//wait for servo to drive to desired position
			}
			GotoPos(XDIR, YDIR, Xcurrent_pulse, Ycurrent_pulse, plotter2.X, plotter2.Y, XpulseOverwidth, YpulseOverheight);
			current_pos = plotter2.penPos;
		}
	}
}

void plotLineLow(DigitalIoPin ydir, int x0, int y0, int x1, int y1) {
	int dx, dy, D;
	dx = x1 - x0;
	dy = y1 - y0;

	if(dy < 0) {
		ydir.write(RIGHT);		//decrease
		dy = -dy;
	}
	else ydir.write(LEFT);		//increase
	D = 2*dy - dx;

	if(dx < 0) {
		dx = -dx;
		for (int x = x1; x <= x0; x++) {
			if(D > 0) {
				RIT_start(1, 1, 200);
				D = D - 2*dx;
			}
			else RIT_start(1, 0, 200);
			D = D + 2*dy;
		}
	}
	else {
		for (int x = x0; x <= x1; x++) {
			if(D > 0) {
				RIT_start(1, 1, 200);
				D = D - 2*dx;
			}
			else RIT_start(1, 0, 200);
			D = D + 2*dy;
		}
	}
}

void plotLineHigh(DigitalIoPin xdir, int x0, int y0, int x1, int y1) {
	int dx, dy, D;
	dx = x1 - x0;
	dy = y1 - y0;

	if(dx < 0) {
		xdir.write(RIGHT);
		dx = -dx;
	}
	else xdir.write(LEFT);
	D = 2*dx - dy;

	if(dy < 0) {
		dy = - dy;
		for(int y = y1; y <= y0; y++) {
			if(D > 0) {
				RIT_start(1, 1, 200);
				D = D - 2*dy;
			}
			else RIT_start(0, 1, 200);
			D = D + 2*dx;
		}

	}
	else {
		for(int y = y0; y <= y1; y++) {
			if(D > 0) {
				RIT_start(1, 1, 200);
				D = D - 2*dy;
			}
			else RIT_start(0, 1, 200);
			D = D + 2*dx;
		}
	}
}
#if BRESENHAM
void GotoPos(DigitalIoPin xdir, DigitalIoPin ydir, int &xcurrent_pulse, int &ycurrent_pulse, double x, double y, double pulseOnwidth, double pulseOnheight) {
	int Xpulse, Ypulse, Xpulse_relative, Ypulse_relative;
	Xpulse = (int) round(x*pulseOnwidth);
	Ypulse = (int) round(y*pulseOnheight);
	Xpulse_relative = abs(Xpulse - xcurrent_pulse);
	Ypulse_relative = abs(Ypulse - ycurrent_pulse);

	if(Ypulse_relative < Xpulse_relative) {
		if(xcurrent_pulse < Xpulse) {
			xdir.write(LEFT);
		}
		else {
			xdir.write(RIGHT);
		}
		plotLineLow(ydir, xcurrent_pulse, ycurrent_pulse, Xpulse, Ypulse);
	}
	else {
		if(ycurrent_pulse < Ypulse) {
			ydir.write(LEFT);
		}
		else {
			ydir.write(RIGHT);
		}
		plotLineHigh(xdir, xcurrent_pulse, ycurrent_pulse, Xpulse, Ypulse);

	}

	xcurrent_pulse = Xpulse;
	ycurrent_pulse = Ypulse;
}
#endif

#if !BRESENHAM
void GotoPos(DigitalIoPin xdir, DigitalIoPin ydir, int &xcurrent_pulse, int &ycurrent_pulse, double x, double y, double pulseOnwidth, double pulseOnheight) {
	int Xpulse, Ypulse, Xpulse_relative, Ypulse_relative;
	Xpulse = (int) round(x*pulseOnwidth);
	Ypulse = (int) round(y*pulseOnheight);
	if(Xpulse >= xcurrent_pulse) {
		xdir.write(LEFT);
		Xpulse_relative = Xpulse - xcurrent_pulse;
	}
	else {
		xdir.write(RIGHT);
		Xpulse_relative = xcurrent_pulse - Xpulse;
	}

	if(Ypulse >= ycurrent_pulse) {
		ydir.write(LEFT);
		Ypulse_relative = Ypulse - ycurrent_pulse;
	}
	else {
		ydir.write(RIGHT);
		Ypulse_relative = ycurrent_pulse - Ypulse;
	}
	RIT_start(Xpulse_relative, Ypulse_relative, 200);
	xcurrent_pulse = Xpulse;
	ycurrent_pulse = Ypulse;
}
#endif

void RIT_start(int xcount, int ycount, int us)
{
	uint64_t cmp_value;
	// Determine approximate compare value based on clock rate and passed interval
	cmp_value = (uint64_t) Chip_Clock_GetSystemClockRate() * (uint64_t) us / 1000000;
	// disable timer during configuration
	Chip_RIT_Disable(LPC_RITIMER);
	XRIT_count = xcount;
	YRIT_count = ycount;
	// enable automatic clear on when compare value==timer value
	// this makes interrupts trigger periodically
	Chip_RIT_EnableCompClear(LPC_RITIMER);
	// reset the counter
	Chip_RIT_SetCounter(LPC_RITIMER, 0);
	Chip_RIT_SetCompareValue(LPC_RITIMER, cmp_value);
	// start counting
	Chip_RIT_Enable(LPC_RITIMER);
	// Enable the interrupt signal in NVIC (the interrupt controller)
	NVIC_EnableIRQ(RITIMER_IRQn);
	// wait for ISR to tell that we're done
	if(xSemaphoreTake(sbRIT, portMAX_DELAY) == pdTRUE) {
		// Disable the interrupt signal in NVIC (the interrupt controller)
		NVIC_DisableIRQ(RITIMER_IRQn);
	}
	else {
		// unexpected error
	}
}

static void prvSetupHardware(void)
{
	SystemCoreClockUpdate();
	Board_Init();
	// initialize RIT (= enable clocking etc.)
	Chip_RIT_Init(LPC_RITIMER);
	// set the priority level of the interrupt
	// The level must be equal or lower than the maximum priority specified in FreeRTOS config
	// Note that in a Cortex-M3 a higher number indicates lower interrupt priority
	NVIC_SetPriority( RITIMER_IRQn, 5 );
}

/* the following is required if runtime statistics are to be collected */
extern "C" {

void vConfigureTimerForRunTimeStats( void ) {
	Chip_SCT_Init(LPC_SCTSMALL1);
	LPC_SCTSMALL1->CONFIG = SCT_CONFIG_32BIT_COUNTER;
	LPC_SCTSMALL1->CTRL_U = SCT_CTRL_PRE_L(255) | SCT_CTRL_CLRCTR_L; // set prescaler to 256 (255 + 1), and start timer
}

void RIT_IRQHandler(void)
{
	static bool decrementX = false;
	static bool decrementY = false;
	static bool Xstate = true, Ystate = true;
	static DigitalIoPin XSTEP(0, 24, DigitalIoPin::output, false);
	static DigitalIoPin YSTEP(0, 27, DigitalIoPin::output, false);
	// This used to check if a context switch is required
	portBASE_TYPE xHigherPriorityWoken = pdFALSE;
	// Tell timer that we have processed the interrupt.
	// Timer then removes the IRQ until next match occurs
	Chip_RIT_ClearIntStatus(LPC_RITIMER); // clear IRQ flag
	if(calib_done == false || (swXmin.read() && swXmax.read() && swYmin.read() && swYmax.read())) {
		if(XRIT_count > 0) {
			XSTEP.write(Xstate);
			if(decrementX == true) {
				XRIT_count--;
			}
			decrementX = !decrementX;
			Xstate = !Xstate;
		}
		if(YRIT_count > 0) {
			YSTEP.write(Ystate);
			if(decrementY == true) {
				YRIT_count--;
			}
			decrementY = !decrementY;
			Ystate = !Ystate;
		}
	}

	if((XRIT_count == 0) && (YRIT_count == 0)) {
		Chip_RIT_Disable(LPC_RITIMER); // disable timer
		// Give semaphore and set context switch flag if a higher priority task was woken up
		xSemaphoreGiveFromISR(sbRIT, &xHigherPriorityWoken);
	}
	// End the ISR and (possibly) do a context switch
	portEND_SWITCHING_ISR(xHigherPriorityWoken);
}
}


















/*
 * void GotoPos(DigitalIoPin xdir, DigitalIoPin ydir, int &xcurrent_pulse, int &ycurrent_pulse, double x, double y, double pulseOnwidth, double pulseOnheight) {
	int Xpulse, Ypulse, Xpulse_relative, Ypulse_relative;
	int ratio, ratio_mod, reduce_sawtooth;
	bool plus;
	Xpulse = (int) round(x*pulseOnwidth);
	Ypulse = (int) round(y*pulseOnheight);

	if(Xpulse >= xcurrent_pulse) {
		xdir.write(LEFT);
		Xpulse_relative = Xpulse - xcurrent_pulse;
	}
	else {
		xdir.write(RIGHT);
		Xpulse_relative = xcurrent_pulse - Xpulse;
	}

	if(Ypulse >= ycurrent_pulse) {
		ydir.write(LEFT);
		Ypulse_relative = Ypulse - ycurrent_pulse;
	}
	else {
		ydir.write(RIGHT);
		Ypulse_relative = ycurrent_pulse - Ypulse;
	}

	if(Xpulse_relative > Ypulse_relative) {
		ratio = Xpulse_relative / Ypulse_relative;
		ratio_mod = Xpulse_relative % Ypulse_relative;
		reduce_sawtooth = Ypulse_relative / ratio_mod;

		for(int i = 0; i < Ypulse_relative; i++) {		//this runs Ypulse_relative times
			if(i % reduce_sawtooth == 0) {				//this happens every reduce_sawtooth times
				plus = true;
			}
			else plus = false;
			RIT_start(ratio + plus, 1, 200);
			xcurrent_pulse += (ratio + plus);
		}
		if(ratio_mod != 0) {
			RIT_start(abs(Xpulse - xcurrent_pulse), 0, 200);
		}
	}
	else {
		ratio = Ypulse_relative / Xpulse_relative;
		ratio_mod = Ypulse_relative % Xpulse_relative;
		reduce_sawtooth = Xpulse_relative / ratio_mod;

		for(int i = 0; i < Xpulse_relative; i++) {
			if(i % reduce_sawtooth == 0) {
				plus = true;
			}
			else plus = false;
			RIT_start(1, ratio + plus, 200);
			ycurrent_pulse += (ratio + plus);
		}
		if(ratio_mod != 0) {
			RIT_start(0, abs(Ypulse - ycurrent_pulse), 200);
		}
	}

	xcurrent_pulse = Xpulse;
	ycurrent_pulse = Ypulse;
}
 */
