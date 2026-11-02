#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#define NUM_FORMATS (WPAD_FMT_BTNS_ACC_IR + 1)
static void *xfb = NULL;
static bool isSearching = false;
static GXRModeObj *rmode = NULL;
static u8 disconnection_reason[WPAD_MAX_DEVICES] = {0,};

static int format_buttons(char *buffer, u32 buttons)
{
	char *ptr = buffer;
	if (buttons & WPAD_BUTTON_UP) ptr += sprintf(ptr, "Up ");
	if (buttons & WPAD_BUTTON_DOWN) ptr += sprintf(ptr, "Dn ");
	if (buttons & WPAD_BUTTON_LEFT) ptr += sprintf(ptr, "Lf ");
	if (buttons & WPAD_BUTTON_RIGHT) ptr += sprintf(ptr, "Rt ");
	if (buttons & WPAD_BUTTON_A) ptr += sprintf(ptr, "A ");
	if (buttons & WPAD_BUTTON_B) ptr += sprintf(ptr, "B ");
	if (buttons & WPAD_BUTTON_PLUS) ptr += sprintf(ptr, "+ ");
	if (buttons & WPAD_BUTTON_MINUS) ptr += sprintf(ptr, "- ");
	if (buttons & WPAD_BUTTON_HOME) ptr += sprintf(ptr, "Home ");
	if (buttons & WPAD_BUTTON_1) ptr += sprintf(ptr, "1 ");
	if (buttons & WPAD_BUTTON_2) ptr += sprintf(ptr, "2 ");
	if (buttons & WPAD_NUNCHUK_BUTTON_C) ptr += sprintf(ptr, "C ");
	if (buttons & WPAD_NUNCHUK_BUTTON_Z) ptr += sprintf(ptr, "Z ");
	return ptr - buffer;
}

static void disconnected_cb(s32 chan, u8 reason)
{
	disconnection_reason[chan] = reason;
}

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------

	// Initialise the video system
	VIDEO_Init();

	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	rmode = VIDEO_GetPreferredMode(NULL);

	// Allocate memory for the display in the uncached region
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	// Initialise the console, required for printf
	CON_Init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth*VI_DISPLAY_PIX_SZ);
	SYS_STDIO_Report(false);

	// Set up the video registers with the chosen mode
	VIDEO_Configure(rmode);

	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);

	// Clear the framebuffer
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);

	// Make the display visible
	VIDEO_SetBlack(false);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	// This function initialises the attached controllers
	WPAD_Init();

	//tell wpad to output the IR data
	WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);

	// Listen for disconnections, in order to catch the reason
	WPAD_SetDisconnectCallback(disconnected_cb);

	//set console starting position to the second row, to make space for tv's offsets
	printf("\x1b[2;0H");

#define BTN_COLOR "\x1b[1;33m"
	printf("Hello World!\n");
	printf("Connect a wiimote by pressing a button\n");
	printf("Or press the red sync button on the wii together with the wiimote!\n");
	printf("         Other combinations: "BTN_COLOR "B" CONSOLE_RESET" plus the following:\n");
	printf(BTN_COLOR "+" CONSOLE_RESET ": toggle guest search  "
		   BTN_COLOR "1" CONSOLE_RESET ": data format  "
		   BTN_COLOR "Up" CONSOLE_RESET ": toggle M+  "
		   "\n");
	printf(BTN_COLOR "Home" CONSOLE_RESET ": quit\n");
	
	bool mp_enabled = false;

	s32 format = WPAD_FMT_BTNS_ACC_IR;
	int num_lines_prev = 0;
	while(SYS_MainLoop()) 
	{
		//reset console location to 9th row
		printf("\x1b[9;0H");
		int num_lines = 0;

		// Call WPAD_ScanPads each loop, this reads the latest controller states
		WPAD_ScanPads();
		
		for (int i = 0; i <= 3; i++)
		{
			//clear line and print the wiimote's data
			printf("\33[2K\r");
			WPADData* data = WPAD_Data(i);
			if(data->data_present)
			{
				char line[128] = {0};
				char *dst = line;
				float battery_level = WPAD_BatteryLevel(i) * 100.0 / 200;
				if (data->data_present & WPAD_DATA_IR)
				{
					dst += sprintf(dst, "IR: %5.1fx%5.1f, %5.1fdeg  ",
					               data->ir.x, data->ir.y, data->ir.angle);
				}

				if (data->data_present & WPAD_DATA_ACCEL)
				{
					dst += sprintf(dst, "Accel: %.2f, %.2f, %.2f  ",
					               data->gforce.x, data->gforce.y, data->gforce.z);
				}

				if (data->data_present & WPAD_DATA_EXPANSION && data->exp.type == EXP_MOTION_PLUS)
				{
					dst += sprintf(dst, "gyro (%d, %d, %d)  ",
					               data->exp.mp.rx, data->exp.mp.ry, data->exp.mp.rz);
				}
				printf("wiimote %d (%.1f%%) %-50s\n", i, battery_level, line);

				/* Second line */
				dst = line;
				if (data->btns_h) {
					dst += format_buttons(dst, data->btns_h);
				}
				if (dst != line) {
					printf("\33[2K\r");
					printf("        %-60s\n", line);
					num_lines++;
				}
			}
			else {
				const char *reason_text = "";
				switch (disconnection_reason[i]) {
				case WPAD_DISCON_AUTH_FAILURE: reason_text = " (Auth failure)"; break;
				case WPAD_DISCON_REPEATED_ATTEMPTS: reason_text = " (Repeated attempts)"; break;
				case WPAD_DISCON_IDLE_TIMEOUT: reason_text = " (Idle timeout)"; break;
				case WPAD_DISCON_POWER_OFF: reason_text = " (Poweroff)"; break;
				case WPAD_DISCON_BATTERY_DIED: reason_text = " (Battery died)"; break;
				case WPAD_DISCON_SYNC_PRESSED: reason_text = " (Sync pressed)"; break;
				case WPAD_DISCON_TIMEOUT: reason_text = " (Connection timeout)"; break;
				}
				printf("wiimote %d: Disconnected %-40s\n", i, reason_text);
			}
			num_lines++;

			u32 btn_down = WPAD_ButtonsDown(i);
			u32 btn_combo = btn_down ? (btn_down | WPAD_ButtonsHeld(i)) : 0;
			u32 btn_released = WPAD_ButtonsUp(i);
			if (WPAD_ButtonsDown(i) & WPAD_BUTTON_MINUS)
			{
				WPAD_Disconnect(i);
			}

			if (btn_combo & WPAD_BUTTON_B)
			{
				if (btn_combo & WPAD_BUTTON_UP)
				{
					mp_enabled = !mp_enabled;
					WPAD_SetMotionPlus(i, mp_enabled);
				}

				if (btn_combo & WPAD_BUTTON_DOWN)
				{
					WPAD_Rumble(i, 1);
				}

				if (btn_combo & WPAD_BUTTON_1)
				{
					format = (format + 1) % NUM_FORMATS;
					WPAD_SetDataFormat(i, format);
				}
			}

			if (btn_released & WPAD_BUTTON_DOWN)
			{
				WPAD_Rumble(i, 0);
			}
		}

		for (int i = num_lines; i <= num_lines_prev; i++) {
			printf("\33[2K\r");
		}
		num_lines_prev = num_lines;
		// WPAD_ButtonsDown tells us which buttons were pressed in this loop
		// this is a "one shot" state which will not fire again until the button has been released
		u32 pressed = WPAD_ButtonsDown(0) | WPAD_ButtonsDown(1) | WPAD_ButtonsDown(2) | WPAD_ButtonsDown(3);

		// We return to the launcher application via exit
		if ( pressed & WPAD_BUTTON_HOME ) break;

		if ( pressed & WPAD_BUTTON_PLUS )
		{
			// toggle searching for guest wiimotes
			// these stay active and valid on the wii untill the wiimote subsystem is shutdown
			// when searching is started, all wiimotes will disconnect and the system will start searching for new wiimotes
			// the searching lasts for 60 seconds or so.
			if(isSearching)
				WPAD_StopSearch();
			else
				WPAD_Search();

			isSearching = !isSearching;
		}

		// Wait for the next frame
		VIDEO_WaitVSync();
	}

	// loop over all wiimotes and disconnect them
	// this would shutdown the wiimotes and they would only respond after have pressed a button again
	// under normal circumstances, this is not wanted and is why it's disabled here
	// its more user friendly if the wiimote is left in a seeking state so that the launcher can pick it back up again
#if 0
	for (int i = 0;i < WPAD_MAX_WIIMOTES ;i++)
	{
		if(WPAD_Probe(i,0) < 0)
			continue;
		WPAD_Flush(i);
		WPAD_Disconnect(i);
	}
#endif

	// Shutdown the WPAD system
	// Any wiimotes that are connected will be force disconnected
	// this results in any connected wiimotes to be left in a seeking state
	// in a seeking state the wiimotes will automatically reconnect when the subsystem is reinitialized
	WPAD_Shutdown();

	return 0;
}
