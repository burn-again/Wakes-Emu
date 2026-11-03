/*
 * wakes_headless -- run Wakes in the emulator from a script, no browser.
 *
 *   wakes_headless [options] [EVENT ...]
 *     --ms N            run for N ms of device time (default 6000)
 *     --wav FILE        write everything the I2S stream played (48 kHz stereo 16-bit)
 *     --drive DIR       on exit, write the WAKES drive's files under DIR
 *     --load DIR        before boot, load files from DIR onto the drive (path = relative)
 *     --resetreas HEX   reset reason at boot (default 0x10000: woken by "••")
 *     --quiet           do not print the console
 *   EVENT is T:what=value, at T ms:
 *     fnc=1|0  play=1|0  t1..t4=1|0  volup=1|0  voldown=1|0  ffwd=1|0  rwd=1|0
 *     f1..f4=0..1  usb=0|1|2  batt=MV  midi=90,3c,64 (hex bytes)
 *
 * The device boots once per run: a reset or SYSTEM_OFF ends the run and says why.
 * Exit status 3 if it ended in a watchdog reset, a fault or main() returning.
 */
#include "emu_api.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int  emu_drive_slots(void);
const char *emu_drive_path(int i);
int  emu_drive_is_dir(int i);
uint32_t emu_drive_size(int i);
const uint8_t *emu_drive_data(int i);
int  emu_drive_put(const char *path, const uint8_t *data, uint32_t n, int is_dir);

struct ev { double t; char what[16]; char val[128]; };
static struct ev evs[512];
static int nev;
static uint32_t buttons;

static const char *stop_name(int r)
{
	switch (r) {
	case 1: return "SYSTEM_OFF";
	case 2: return "soft reset";
	case 3: return "WATCHDOG";
	case 4: return "fault";
	case 5: return "main returned";
	default: return "running";
	}
}

static void apply(const struct ev *e)
{
	static const struct { const char *n; int bit; } btn[] = {
		{ "play", 0 }, { "t1", 1 }, { "t2", 2 }, { "t3", 3 }, { "t4", 4 },
		{ "volup", 5 }, { "voldown", 6 }, { "ffwd", 7 }, { "rwd", 8 },
	};
	if (strcmp(e->what, "fnc") == 0) { emu_set_fnc(atoi(e->val)); return; }
	if (strcmp(e->what, "usb") == 0) { emu_set_usb(atoi(e->val)); return; }
	if (strcmp(e->what, "batt") == 0) { emu_set_batt_mv(atoi(e->val)); return; }
	if (e->what[0] == 'f' && e->what[1] >= '1' && e->what[1] <= '4' && !e->what[2]) {
		emu_set_fader(e->what[1] - '1', (float)atof(e->val));
		return;
	}
	if (strcmp(e->what, "midi") == 0) {
		uint8_t *b = emu_midi_buf();
		uint32_t n = 0;
		for (char *s = (char *)e->val; *s && n < 256;) {
			b[n++] = (uint8_t)strtoul(s, &s, 16);
			if (*s == ',') { s++; }
		}
		emu_midi_send(n);
		return;
	}
	for (size_t i = 0; i < sizeof(btn) / sizeof(btn[0]); i++) {
		if (strcmp(e->what, btn[i].n) == 0) {
			if (atoi(e->val)) { buttons |= 1u << btn[i].bit; }
			else              { buttons &= ~(1u << btn[i].bit); }
			emu_set_buttons(buttons);
			return;
		}
	}
	fprintf(stderr, "unknown event %s\n", e->what);
}

static int cmp(const void *a, const void *b)
{
	const double d = ((const struct ev *)a)->t - ((const struct ev *)b)->t;
	return d < 0 ? -1 : d > 0;
}

static void wav_header(FILE *f, uint32_t frames)
{
	const uint32_t data = frames * 4u, rate = 48000u, byterate = rate * 4u;
	fwrite("RIFF", 1, 4, f);
	const uint32_t riff = 36u + data;
	fwrite(&riff, 4, 1, f);
	fwrite("WAVEfmt ", 1, 8, f);
	const uint32_t fmt_len = 16; const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
	fwrite(&fmt_len, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
	fwrite(&rate, 4, 1, f); fwrite(&byterate, 4, 1, f); fwrite(&align, 2, 1, f);
	fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f);
	fwrite(&data, 4, 1, f);
}

static void mkdirs(const char *path)
{
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
	}
	mkdir(tmp, 0755);
}

static void load_dir(const char *root, const char *rel)
{
	char full[1024];
	snprintf(full, sizeof(full), "%s%s", root, rel);
	DIR *d = opendir(full);
	if (!d) { return; }
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') { continue; }
		char r[1024], f[1100];
		snprintf(r, sizeof(r), "%s/%s", rel, e->d_name);
		snprintf(f, sizeof(f), "%s%s", root, r);
		struct stat st;
		if (stat(f, &st) != 0) { continue; }
		if (S_ISDIR(st.st_mode)) {
			emu_drive_put(r, NULL, 0, 1);
			load_dir(root, r);
		} else {
			FILE *fp = fopen(f, "rb");
			uint8_t *buf = malloc((size_t)st.st_size + 1);
			size_t n = fread(buf, 1, (size_t)st.st_size, fp);
			fclose(fp);
			emu_drive_put(r, buf, (uint32_t)n, 0);
			free(buf);
		}
	}
	closedir(d);
}

static void quiet_sink(const char *s, size_t n) { (void)s; (void)n; }

int main(int argc, char **argv)
{
	double ms = 6000;
	const char *wav = NULL, *drive = NULL, *load = NULL;
	uint32_t resetreas = 0x10000u;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--ms") && i + 1 < argc) { ms = atof(argv[++i]); }
		else if (!strcmp(argv[i], "--wav") && i + 1 < argc) { wav = argv[++i]; }
		else if (!strcmp(argv[i], "--drive") && i + 1 < argc) { drive = argv[++i]; }
		else if (!strcmp(argv[i], "--load") && i + 1 < argc) { load = argv[++i]; }
		else if (!strcmp(argv[i], "--resetreas") && i + 1 < argc) { resetreas = (uint32_t)strtoul(argv[++i], NULL, 16); }
		else if (!strcmp(argv[i], "--quiet")) { emu_set_console_sink(quiet_sink); }
		else if (nev < 512) {
			struct ev *e = &evs[nev];
			char what[160];
			if (sscanf(argv[i], "%lf:%159s", &e->t, what) != 2 || !strchr(what, '=')) {
				fprintf(stderr, "bad event: %s\n", argv[i]);
				return 2;
			}
			*strchr(what, '=') = 0;
			snprintf(e->what, sizeof(e->what), "%s", what);
			snprintf(e->val, sizeof(e->val), "%s", what + strlen(what) + 1);
			nev++;
		}
	}
	qsort(evs, (size_t)nev, sizeof(evs[0]), cmp);

	if (load) {
		load_dir(load, "");
	}
	/* Events at t=0 are the state at power-up (a held "••", a plugged cable). */
	int next = 0;
	while (next < nev && evs[next].t <= 0) { apply(&evs[next++]); }
	emu_boot(resetreas);

	FILE *wf = NULL;
	if (wav) {
		wf = fopen(wav, "wb");
		wav_header(wf, 0);
	}
	uint32_t read = 0, frames = 0;
	const uint32_t ring = emu_audio_ring_frames();
	double t = 0;
	double peak = 0;
	while (t < ms && !emu_stopped()) {
		double until = t + 5.0;
		if (next < nev && evs[next].t < until) { until = evs[next].t; }
		if (until > ms) { until = ms; }
		t = emu_run_until_ms(until);
		while (next < nev && evs[next].t <= t) { apply(&evs[next++]); }
		const uint32_t wr = emu_audio_written();
		const int16_t *r = emu_audio_ring_ptr();
		while (read != wr) {
			const uint32_t at = read % ring;
			const int16_t lr[2] = { r[2 * at], r[2 * at + 1] };
			const double a = lr[0] < 0 ? -lr[0] : lr[0];
			if (a > peak) { peak = a; }
			if (wf) { fwrite(lr, 2, 2, wf); }
			read++;
			frames++;
		}
	}
	if (wf) {
		fseek(wf, 0, SEEK_SET);
		wav_header(wf, frames);
		fclose(wf);
	}
	float *led = emu_leds();
	fprintf(stderr, "\n[emu] %.0f ms, stop: %s, audio %u frames (%.2f s), peak %.0f (%.1f dBFS), underruns %u\n",
		t, stop_name(emu_stopped()), frames, frames / 48000.0, peak,
		peak > 0 ? 20.0 * __builtin_log10(peak / 32767.0) : -999.0, emu_audio_underrun_count());
	fprintf(stderr, "[emu] LEDs track %.2f %.2f %.2f %.2f   play %.2f %.2f %.2f %.2f\n",
		led[0], led[1], led[2], led[3], led[4], led[5], led[6], led[7]);

	if (drive) {
		for (int i = 0; i < emu_drive_slots(); i++) {
			const char *p = emu_drive_path(i);
			if (!p) { continue; }
			char out[1024];
			snprintf(out, sizeof(out), "%s%s", drive, p);
			if (emu_drive_is_dir(i)) {
				mkdirs(out);
			} else {
				char dir[1024];
				snprintf(dir, sizeof(dir), "%s", out);
				*strrchr(dir, '/') = 0;
				mkdirs(dir);
				FILE *fp = fopen(out, "wb");
				if (fp) {
					fwrite(emu_drive_data(i), 1, emu_drive_size(i), fp);
					fclose(fp);
				}
			}
		}
		fprintf(stderr, "[emu] drive written to %s\n", drive);
	}
	/* A watchdog reset, a fault or main() returning is a failure; SYSTEM_OFF and a soft
	 * reset are things Wakes does on purpose. */
	const int why = emu_stopped();
	return (why == 3 || why == 4 || why == 5) ? 3 : 0;
}
