#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#define NUM_FORMATS (WPAD_FMT_BTNS_ACC_IR + 1)
static void *xfb = NULL;
static GXRModeObj *rmode = NULL;
static u8 disconnection_reason[WPAD_MAX_DEVICES] = {0,};


static void mem_report(const char *prefix)
{
    fprintf(stderr, "%s MEM1: %p-%p (%d KB), MEM2: %p-%p (%d KB)\n", prefix,
	        SYS_GetArenaLo(), SYS_GetArenaHi(), (SYS_GetArenaHi() - SYS_GetArenaLo()) / 1024,
	        SYS_GetArena2Lo(), SYS_GetArena2Hi(), (SYS_GetArena2Hi() - SYS_GetArena2Lo()) / 1024);
}

static int format_buttons(char *buffer, const WPADData *data)
{
	u32 buttons = data->btns_h;
	char *ptr = buffer;
	if (data->exp.type == EXP_CLASSIC) {
		if (buttons & WPAD_CLASSIC_BUTTON_UP) ptr += sprintf(ptr, "Up ");
		if (buttons & WPAD_CLASSIC_BUTTON_DOWN) ptr += sprintf(ptr, "Dn ");
		if (buttons & WPAD_CLASSIC_BUTTON_LEFT) ptr += sprintf(ptr, "Lf ");
		if (buttons & WPAD_CLASSIC_BUTTON_RIGHT) ptr += sprintf(ptr, "Rt ");
		if (buttons & WPAD_CLASSIC_BUTTON_A) ptr += sprintf(ptr, "A ");
		if (buttons & WPAD_CLASSIC_BUTTON_B) ptr += sprintf(ptr, "B ");
		if (buttons & WPAD_CLASSIC_BUTTON_X) ptr += sprintf(ptr, "X ");
		if (buttons & WPAD_CLASSIC_BUTTON_Y) ptr += sprintf(ptr, "Y ");
		if (buttons & WPAD_CLASSIC_BUTTON_PLUS) ptr += sprintf(ptr, "+ ");
		if (buttons & WPAD_CLASSIC_BUTTON_MINUS) ptr += sprintf(ptr, "- ");
		if (buttons & WPAD_CLASSIC_BUTTON_HOME) ptr += sprintf(ptr, "Home ");
		if (buttons & WPAD_CLASSIC_BUTTON_ZL) ptr += sprintf(ptr, "ZL ");
		if (buttons & WPAD_CLASSIC_BUTTON_ZR) ptr += sprintf(ptr, "ZR ");
		if (buttons & WPAD_CLASSIC_BUTTON_FULL_L) ptr += sprintf(ptr, "L ");
		if (buttons & WPAD_CLASSIC_BUTTON_FULL_R) ptr += sprintf(ptr, "R ");
	} else {
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
	}
	return ptr - buffer;
}

static int generate_audio_s16(s16 *samples, int playback_frequency, int duration_ms)
{
	int num_samples = duration_ms * playback_frequency / 1000;
	float duration_secs = duration_ms / 1000.0f;
	static const float tunes[] = {
		261.63f, /* Do */
		293.66f, /* Re */
		329.63f, /* Mi */
		349.23f, /* Fa */
		392.00f, /* Sol */
		440.00f, /* La */
		493.88f, /* Si */
	};
	const int num_tunes = sizeof(tunes) / sizeof(tunes[0]);
	int tune_duration = num_samples / num_tunes;
	float tune = 0, last_sample = 0.0f;
	float tune_start_time = 0;
	for (int i = 0; i < num_samples; i++) {
		float t = duration_secs * i / num_samples;
		if (fabs(last_sample) < 0.1) {
			float prev_tune = tune;
			tune = tunes[(i / tune_duration) % num_tunes];
			if (tune != prev_tune) {
				tune_start_time = t;
			}
		}
		float sample = sinf((t - tune_start_time) * 2 * M_PI * tune);
		samples[i] = ((s16)(sample * INT16_MAX)) >> 0;
		last_sample = sample;
	}

	return num_samples;
}

static int generate_audio_adpcm(void *audio_data, int playback_frequency, int duration_ms)
{
	int num_samples = generate_audio_s16(audio_data, playback_frequency, duration_ms);

	/* We reuse the same buffer, since the generated data is smaller than the
	 * original one */
	WPADEncStatus info = { 0, };
	WPAD_EncodeData(&info, 0, audio_data, num_samples, audio_data);
	return num_samples / 2;
}

static const char *exp_name(const WPADData *data)
{
	if (!(data->data_present & WPAD_DATA_EXPANSION))
		return "";

	switch (data->exp.type) {
	case EXP_NONE: return "";
	case EXP_NUNCHUK: return "nunchuk ";
	case EXP_CLASSIC: return "classic ";
	case EXP_WII_BOARD: return "board ";
	case EXP_GUITAR_HERO_3: return "guitar";
	default: return "exp? ";
	}
}

#define BTN_COLOR "\x1b[1;33m"
#define STATUS_COLOR "\x1b[1;36m"

static void print_help(bool search_active, int data_format, bool motion_plus, bool speaker_enabled)
{
	const char *search_text = search_active ? "on" : "off";

	const char *data_format_text;
	switch (data_format) {
	case WPAD_FMT_BTNS: data_format_text = "BTNS"; break;
	case WPAD_FMT_BTNS_ACC: data_format_text = "BTNS+ACC"; break;
	case WPAD_FMT_BTNS_ACC_IR: data_format_text = "BTNS+ACC+IR"; break;
	default: data_format_text = "?"; break;
	}

	const char *motion_plus_text = motion_plus ? "on" : "off";
	const char *speaker_text = speaker_enabled ? "on" : "off";

	printf(BTN_COLOR "+" CONSOLE_RESET ": toggle search (" STATUS_COLOR "%s" CONSOLE_RESET ")  "
		   BTN_COLOR "1" CONSOLE_RESET ": data format (" STATUS_COLOR "%s" CONSOLE_RESET ")  "
		   BTN_COLOR "Up" CONSOLE_RESET ": toggle M+ (" STATUS_COLOR "%s" CONSOLE_RESET ")  "
		   "\n"
		   BTN_COLOR "Down" CONSOLE_RESET ": rumble  "
		   BTN_COLOR "Right" CONSOLE_RESET ": speaker (" STATUS_COLOR "%s" CONSOLE_RESET ")  "
		   BTN_COLOR "Left" CONSOLE_RESET ": Play sound  "
		   "\n"
		   BTN_COLOR "-" CONSOLE_RESET ": disconnect controller  "
		   BTN_COLOR "Home" CONSOLE_RESET ": quit"
		   "\n\n",
		   search_text,
		   data_format_text,
		   motion_plus_text,
		   speaker_text
		   );
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
	//WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS);

	// Listen for disconnections, in order to catch the reason
	WPAD_SetDisconnectCallback(disconnected_cb);

	//set console starting position to the second row, to make space for tv's offsets
	printf("\x1b[2;0H");

	printf("Hello World!\n");
	printf("Connect a wiimote by pressing a button\n");
	printf("Or press the red sync button on the wii together with the wiimote!\n");
	printf("         Other combinations: "BTN_COLOR "B" CONSOLE_RESET" plus the following:\n");
	
	//WPAD_WipeSavedControllers();
	WPAD_Search();
	bool search_active = true;
	//WPAD_StartPairing();
	//WPAD_SetIdleTimeout(4);
	bool mp_enabled = false;
	bool speaker_enabled = false;

	int sample_size = sizeof(s16);
	int duration_ms = 3000;
	int frequency = 6000;
	int audio_data_size = sample_size * frequency * duration_ms / 1000;
	u8 *audio_data = malloc(audio_data_size);
	audio_data_size = generate_audio_adpcm(audio_data, frequency, duration_ms);

	s32 format = WPAD_FMT_BTNS_ACC_IR;
	int num_lines_prev = 0;
	bool done = false;
	while(!done && SYS_MainLoop()) 
	{
		//reset console location
		printf("\x1b[6;0H");
		print_help(search_active, format, mp_enabled, speaker_enabled);
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

				if (data->data_present & WPAD_DATA_EXPANSION)
				{
					if (data->exp.type == EXP_MOTION_PLUS)
					{
						dst += sprintf(dst, "gyro (%d, %d, %d)  ",
									   data->exp.mp.rx, data->exp.mp.ry, data->exp.mp.rz);
					}
					else if (data->exp.type == EXP_CLASSIC)
					{
						dst += sprintf(dst, "LJS %03d,%03d RJS %03d,%03d  ",
									   data->exp.classic.ljs.pos.x, data->exp.classic.ljs.pos.y,
									   data->exp.classic.rjs.pos.x, data->exp.classic.rjs.pos.y);
					}
					else if (data->exp.type == EXP_NUNCHUK)
					{
						dst += sprintf(dst, "JS %03d,%03d Accel: %.2f, %.2f, %.2f  ",
									   data->exp.nunchuk.js.pos.x, data->exp.nunchuk.js.pos.y,
									   data->exp.nunchuk.gforce.x, data->exp.nunchuk.gforce.y, data->exp.nunchuk.gforce.z);
					}
				}

				printf("wiimote %d %s(%.1f%%)\n", i, exp_name(data), battery_level);
				if (dst != line) {
					printf("\33[2K\r");
					printf("        %-60s\n", line);
					num_lines++;
				}

				/* Second line */
				dst = line;
				if (data->btns_h) {
					dst += format_buttons(dst, data);
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
			u32 btn_classic_combo = (data->exp.type == EXP_CLASSIC && data->exp.classic.btns) ?
				(data->exp.classic.btns | data->exp.classic.btns_held) : 0;
			u32 btn_classic_released = data->exp.type == EXP_CLASSIC ? data->exp.classic.btns_released : 0;

			if (btn_combo & WPAD_BUTTON_B || btn_classic_combo & CLASSIC_CTRL_BUTTON_B)
			{
				if (btn_combo & WPAD_BUTTON_UP)
				{
					mp_enabled = !mp_enabled;
					WPAD_SetMotionPlus(WPAD_CHAN_ALL, mp_enabled);
				}

				if (btn_combo & WPAD_BUTTON_DOWN || btn_classic_combo & CLASSIC_CTRL_BUTTON_DOWN)
				{
					WPAD_Rumble(i, 1);
				}

				if (btn_combo & WPAD_BUTTON_RIGHT)
				{
					speaker_enabled = !speaker_enabled;
					WPAD_ControlSpeaker(WPAD_CHAN_ALL, speaker_enabled);
					if (speaker_enabled) {
						WPAD_StopSearch();
						search_active = false;
					}
				}
				if (btn_combo & WPAD_BUTTON_LEFT)
				{
					if (speaker_enabled) {
						WPAD_SendStreamData(i, audio_data, audio_data_size);
					}
				}
				if (btn_combo & WPAD_BUTTON_1 || btn_classic_combo & CLASSIC_CTRL_BUTTON_X)
				{
					format = (format + 1) % NUM_FORMATS;
					WPAD_SetDataFormat(WPAD_CHAN_ALL, format);
				}

				if (btn_combo & WPAD_BUTTON_PLUS || btn_classic_combo & CLASSIC_CTRL_BUTTON_PLUS)
				{
					// toggle searching for guest wiimotes
					// these stay active and valid on the wii untill the wiimote subsystem is shutdown
					// when searching is started, all wiimotes will disconnect and the system will start searching for new wiimotes
					// the searching lasts for 60 seconds or so.
					if(search_active)
						WPAD_StopSearch();
					else
						WPAD_Search();

					search_active = !search_active;
				}

				if (btn_combo & WPAD_BUTTON_MINUS || btn_classic_combo & CLASSIC_CTRL_BUTTON_MINUS)
				{
					WPAD_Disconnect(i);
				}

				if (btn_combo & WPAD_BUTTON_HOME || btn_classic_combo & CLASSIC_CTRL_BUTTON_HOME)
				{
					done = true;
				}
			}

			if (btn_released & WPAD_BUTTON_DOWN || btn_classic_released & CLASSIC_CTRL_BUTTON_DOWN)
			{
				WPAD_Rumble(i, 0);
			}
		}

		for (int i = num_lines; i <= num_lines_prev; i++) {
			printf("\33[2K\r");
		}
		num_lines_prev = num_lines;

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

	mem_report("Before shutdown");
	// Shutdown the WPAD system
	// Any wiimotes that are connected will be force disconnected
	// this results in any connected wiimotes to be left in a seeking state
	// in a seeking state the wiimotes will automatically reconnect when the subsystem is reinitialized
	WPAD_Shutdown();
	mem_report("After shutdown");

	return 0;
}
