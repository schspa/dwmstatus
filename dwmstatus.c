/*
 * Copy me if you can.
 * by 20h
 */

#define _BSD_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <X11/Xlib.h>

char *tzargentina = "Asia/Shanghai";
char *tzutc = "UTC";

static Display *dpy;

char *
smprintf(char *fmt, ...)
{
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

void
settz(char *tzname)
{
	setenv("TZ", tzname, 1);
}

char *
mktimes(char *fmt, char *tzname)
{
	char buf[129];
	time_t tim;
	struct tm *timtm;

	settz(tzname);
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL)
		return smprintf("");

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		return smprintf("");
	}

	return smprintf("%s", buf);
}

void
setstatus(char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void)
{
	double avgs[3];

	if (getloadavg(avgs, 3) < 0)
		return smprintf("");

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

char *
readfile(char *base, char *file)
{
	char *path, line[513];
	FILE *fd;

	memset(line, 0, sizeof(line));

	path = smprintf("%s/%s", base, file);
	fd = fopen(path, "r");
	free(path);
	if (fd == NULL)
		return NULL;

	if (fgets(line, sizeof(line)-1, fd) == NULL)
		return NULL;
	fclose(fd);

	return smprintf("%s", line);
}

char *
getbattery(char *base)
{
	char *co, status;
	int descap, remcap;

	descap = -1;
	remcap = -1;

	co = readfile(base, "present");
	if (co == NULL)
		return smprintf("");
	if (co[0] != '1') {
		free(co);
		return smprintf("not present");
	}
	free(co);

	co = readfile(base, "charge_full_design");
	if (co == NULL) {
		co = readfile(base, "energy_full_design");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &descap);
	free(co);

	co = readfile(base, "charge_now");
	if (co == NULL) {
		co = readfile(base, "energy_now");
		if (co == NULL)
			return smprintf("");
	}
	sscanf(co, "%d", &remcap);
	free(co);

	co = readfile(base, "status");
	if (!strncmp(co, "Discharging", 11)) {
		status = '-';
	} else if(!strncmp(co, "Charging", 8)) {
		status = '+';
	} else {
		status = '?';
	}

	if (remcap < 0 || descap < 0)
		return smprintf("invalid");

	return smprintf("%.0f%%%c", ((float)remcap / (float)descap) * 100, status);
}

char *
gettemperature(char *base, char *sensor)
{
	char *co;

	co = readfile(base, sensor);
	if (co == NULL)
		return smprintf("");
	return smprintf("%02.0f°C", atof(co) / 1000);
}

int
parse_netdev(unsigned long long int *receivedabs, unsigned long long int *sentabs)
{
	char buf[255];
	char *datastart;
	static int bufsize;
	int rval;
	FILE *devfd;
	unsigned long long int receivedacc, sentacc;

	bufsize = 255;
	devfd = fopen("/proc/net/dev", "r");
	rval = 1;

	// Ignore the first two lines of the file
	fgets(buf, bufsize, devfd);
	fgets(buf, bufsize, devfd);

	while (fgets(buf, bufsize, devfd)) {
		if ((datastart = strstr(buf, "lo:")) == NULL) {
			datastart = strstr(buf, ":");

			// With thanks to the conky project at http://conky.sourceforge.net/
			sscanf(datastart + 1, "%llu  %*d     %*d  %*d  %*d  %*d   %*d        %*d       %llu",\
				&receivedacc, &sentacc);
			*receivedabs += receivedacc;
			*sentabs += sentacc;
			rval = 0;
		}
	}

	fclose(devfd);
	return rval;
}

void
calculate_speed(char *speedstr, size_t len, unsigned long long int newval, unsigned long long int oldval)
{
	double speed;
	speed = (newval - oldval) / 1024.0;
	if (speed > 1024.0) {
		speed /= 1024.0;
		snprintf(speedstr, len, "%.3f MB/s", speed);
	} else {
		snprintf(speedstr, len, "%.2f KB/s", speed);
	}
}

char *
get_netusage(unsigned long long int *rec, unsigned long long int *sent)
{
	unsigned long long int newrec, newsent;
	newrec = newsent = 0;
	char downspeedstr[15], upspeedstr[15];
	static char retstr[48];
	int retval;

	retval = parse_netdev(&newrec, &newsent);
	if (retval) {
		fprintf(stdout, "Error when parsing /proc/net/dev file.\n");
		exit(1);
	}

	calculate_speed(downspeedstr, sizeof(downspeedstr), newrec, *rec);
	calculate_speed(upspeedstr, sizeof(upspeedstr), newsent, *sent);

	snprintf(retstr, sizeof(retstr), " %s  %s", downspeedstr, upspeedstr);

	*rec = newrec;
	*sent = newsent;
	return retstr;
}

#define SEPARATOR_STR "^v^^c#55cdfc^ | ^t^"

int
main(void)
{
	char *status;
	char *avgs;
	char *bat;
	char *bat1;
	char *databar;
	char *tmar;
	char *t0, *t1, *t2;
	char *netstats;
	static unsigned long long int rec = 0, sent = 0;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;sleep(1)) {
		avgs = loadavg();
		bat = getbattery("/sys/class/power_supply/BAT0");
		bat1 = getbattery("/sys/class/power_supply/BAT1");
		databar = mktimes("%F W:%W", tzargentina);
		tmar = mktimes("%H:%M", tzargentina);
		t0 = gettemperature("/sys/devices/virtual/hwmon/hwmon0", "temp1_input");
		t1 = gettemperature("/sys/devices/virtual/hwmon/hwmon2", "temp1_input");
		t2 = gettemperature("/sys/devices/virtual/hwmon/hwmon4", "temp1_input");
		netstats = get_netusage(&rec, &sent);

		status = smprintf("%s "SEPARATOR_STR"  %s "SEPARATOR_STR"  %s "SEPARATOR_STR"  %s",
				netstats, avgs, databar, tmar);
		setstatus(status);

		free(t0);
		free(t1);
		free(t2);
		free(avgs);
		free(bat);
		free(bat1);
		free(databar);
		free(tmar);
		free(status);
	}

	XCloseDisplay(dpy);

	return 0;
}

