//#include "stdafx.h"
#if VISUALSTUDIO
#pragma warning(disable:4996)
#include <fstream>
#include <string>
#include <iostream>
#else
#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>

// TODO: insert other include files here
#include "FreeRTOS.h"
#include "task.h"
#include "ITM_write.h"

#include <mutex>
#include "Fmutex.h"
#include "user_vcom.h"


#include <cstring>

// TODO: insert other definitions and declarations here


/* the following is required if runtime statistics are to be collected */
extern "C" {

	void vConfigureTimerForRunTimeStats(void) {
		Chip_SCT_Init(LPC_SCTSMALL1);
		LPC_SCTSMALL1->CONFIG = SCT_CONFIG_32BIT_COUNTER;
		LPC_SCTSMALL1->CTRL_U = SCT_CTRL_PRE_L(255) | SCT_CTRL_CLRCTR_L; // set prescaler to 256 (255 + 1), and start timer
	}

}
/* end runtime statictics collection */

/* Sets up system hardware */
static void prvSetupHardware(void)
{
	SystemCoreClockUpdate();
	Board_Init();

	/* Initial LED0 state is off */
	Board_LED_Set(0, false);

}
#endif

bool UNITTEST1 = false;

struct Cord {
	float x;
	float y;
	float distance;
};

enum PCode { GC_OK, GC_EXIT, GC_ERROR };

PCode parser(const char * myChar);
Cord getCordFromChar(const char * myChar);

#if VISUALSTUDIO
void readFromVS();
#else
void readFromUart();
#endif

int main()
{
#if VISUALSTUDIO
	readFromVS();
#else
	prvSetupHardware();
	readFromUart();
#endif

	return 0;
}

#if VISUALSTUDIO
void readFromVS() {
	PCode pcode = GC_OK;
	std::string line;
	std::ifstream myFile("gcode01.txt");

	while (getline(myFile, line)) {
		const char *myChar = line.c_str();
		pcode = parser(myChar);

		//Break if ERROR
		if (pcode == GC_ERROR) {
			std::cout << "ERROR: INVALID GCODE" << std::endl;
			break;
		}

		//Break if EXIT
		if (pcode == GC_EXIT) {
			break;
		}
	}
	std::cout << "End!" << std::endl;
	system("pause");
}
#else
void readFromUart() {
	PCode pcode = GC_OK;
	int c = 0;
	char myChar[61] = { 0 };
	uint8_t index = 0;
	Board_UARTPutSTR("OK\r\n");
	while (1) {
		if (UNITTEST1) {
			Board_UARTPutSTR("UNIT TEST 1: Valid code\r\n");
			pcode = parser("G1 X225 Y20 A0");
			break;
		}
		c = Board_UARTGetChar();
		if (c != EOF) {
			if (index < 60 && c != '\r') {
				Board_UARTPutChar(c);
				myChar[index] = c;
				index++;
			}
			else {
				pcode = parser(myChar);
				index = 0;
			}
		}

		if (pcode == GC_ERROR) {
			Board_UARTPutSTR("ERROR: INVALID GCODE");
			break;
		}
	}
}
#endif



/** Parser gcode sent from Uart/file.
 *   gcode is stored in const char *, avoid using string so microcontroller can also
 *	use this function.
 *
 *	Parser return PCode to identity the result
 */
PCode parser(const char * myChar) {
	Cord cord = { 0, 0, 0 };
	char moveG[] = "G1 ";
	char m4[] = "M4 ";
	char m10[] = "M10";
	char g28[] = "G28";
	char m1[] = "M1 ";
	//TODO add more case (if we know what the gcode mean)

	//strstr return null if can't found G1 in the string (which mean false)
	//if it can read then it get the cord (x, y) from the data.
	if (strstr(myChar, moveG)) {
		cord = getCordFromChar(myChar);
		return GC_OK;
	}
	//else if we read the "M4 ' we stop the parser
	else if (strstr(myChar, m4)) {
		return GC_OK;
	}
	else if (strstr(myChar, m10)) {
		return GC_OK;
	}
	else if (strstr(myChar, g28)) {
		return GC_OK;
	}
	else if (strstr(myChar, m1)) {
		return GC_OK;
	}
	else {
		return GC_ERROR;
	}
}

/** Trying to get cordinate value from G-code
 *	Note that to get here, we already checked the correct G-code start with 'G1 '.
 *
 */
Cord getCordFromChar(const char * myChar) {
	Cord cord = { 0, 0, 0 };
	sscanf(myChar, "G1 X%f Y%f A0", &cord.x, &cord.y);

#if VISUALSTUDIO
	std::cout << cord.x << " " << cord.y << std::endl;
#else
	Board_UARTPutSTR("OK\r\n");
#endif

	return cord;
}
