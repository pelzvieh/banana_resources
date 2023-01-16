// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * Based in gpiomon.c copyright (C) 2017-2018 Bartosz Golaszewski <bartekgola@gmail.com>
 * Uses dht11_decode copyright (c) Harald Geyer <harald@ccbib.org>
 * Compiled to poll dht11, copyright (c) Andreas Feldner <pelzi@flying-snail.de>
 */

#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <gpiod.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "tools-common.h"

static const struct option longopts[] = {
	{ "help",		no_argument,		NULL,	'h' },
	{ "version",		no_argument,		NULL,	'v' },
	{ "active-low",		no_argument,		NULL,	'l' },
	{ "bias",		required_argument,	NULL,	'B' },
	{ "num-events",		required_argument,	NULL,	'n' },
	{ "silent",		no_argument,		NULL,	's' },
	{ "line-buffered",	no_argument,		NULL,	'b' },
	{ "format",		required_argument,	NULL,	'F' },
	{ GETOPT_NULL_LONGOPT },
};

static const int DHT11_BITS_PER_READ = 40;

static const char *const shortopts = "+hvlB:n:srfbF:";

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] <chip name/number> <offset 1> <offset 2> ...\n",
	       get_progname());
	printf("\n");
	printf("Wait for events on GPIO lines and print them to standard output\n");
	printf("\n");
	printf("Options:\n");
	printf("  -h, --help:\t\tdisplay this message and exit\n");
	printf("  -v, --version:\tdisplay the version and exit\n");
	printf("  -l, --active-low:\tset the line active state to low\n");
	printf("  -B, --bias=[as-is|disable|pull-down|pull-up] (defaults to 'as-is'):\n");
	printf("		set the line bias\n");
	printf("  -n, --num-events=NUM:\texit after processing NUM events\n");
	printf("  -s, --silent:\t\tdon't print event info\n");
	printf("  -b, --line-buffered:\tset standard output as line buffered\n");
	printf("  -F, --format=FMT\tspecify custom output format\n");
	printf("\n");
	print_bias_help();
	printf("\n");
	printf("Format specifiers:\n");
	printf("  %%o:  GPIO line offset\n");
	printf("  %%e:  event type (0 - falling edge, 1 rising edge)\n");
	printf("  %%s:  seconds part of the event timestamp\n");
	printf("  %%n:  nanoseconds part of the event timestamp\n");
}

struct event {
        bool is_high;
        unsigned long duration_ns;
        unsigned int pin;
        unsigned int num_samples;
};

struct mon_ctx {
	unsigned int offset;
	unsigned int events_wanted;
	unsigned int events_done;

	bool silent;
	char *fmt;

	int sigfd;
        struct event *events;
};

static int ctxless_flags_to_line_request_flags(bool active_low, int flags)
{
        int req_flags = 0;

        if (active_low)
                req_flags |= GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW;
        if (flags & GPIOD_CTXLESS_FLAG_OPEN_DRAIN)
                req_flags |= GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN;
        if (flags & GPIOD_CTXLESS_FLAG_OPEN_SOURCE)
                req_flags |= GPIOD_LINE_REQUEST_FLAG_OPEN_SOURCE;
        if (flags & GPIOD_CTXLESS_FLAG_BIAS_DISABLE)
                req_flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE;
        if (flags & GPIOD_CTXLESS_FLAG_BIAS_PULL_UP)
                req_flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
        if (flags & GPIOD_CTXLESS_FLAG_BIAS_PULL_DOWN)
                req_flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;

        return req_flags;
}


static void event_print_custom(unsigned int offset, int value,
			       long duration, struct mon_ctx *ctx)
{
	char *prev, *curr, fmt;

	for (prev = curr = ctx->fmt;;) {
		curr = strchr(curr, '%');
		if (!curr) {
			fputs(prev, stdout);
			break;
		}

		if (prev != curr)
			fwrite(prev, curr - prev, 1, stdout);

		fmt = *(curr + 1);

		switch (fmt) {
		case 'o':
			printf("%u", offset);
			break;
		case 'e':
                        if (value)
                                fputc('1', stdout);
                        else 
				fputc('0', stdout);
			break;
		case 'n':
			printf("%ld", duration);
			break;
		case '%':
			fputc('%', stdout);
			break;
		case '\0':
			fputc('%', stdout);
			goto end;
		default:
			fwrite(curr, 2, 1, stdout);
			break;
		}

		curr += 2;
		prev = curr;
	}

end:
	fputc('\n', stdout);
}

static void event_print_human_readable(unsigned int offset, bool value, long duration, unsigned int samples)
{
	char *evname;

	if (value)
		evname = "HIGH";
	else
		evname = " LOW";

	printf("event: %s offset: %u duration: %12ld samples: %u\n",
	       evname, offset, duration, samples);
}

static void handle_event(
			  unsigned int line_offset, bool value,
                          long duration, unsigned int num_samples,
                          struct mon_ctx *ctx)
{
        if (ctx->events_done < ctx->events_wanted) {
            struct event *event = &ctx->events[ctx->events_done++];
            event -> is_high = !!value;
            event -> duration_ns = duration;
            event -> pin = line_offset;
            event -> num_samples = num_samples;
        }
}

static void print_events(struct mon_ctx *ctx) {
    unsigned int i;
    if (ctx->silent)
        return;

    for (i = 0; i < ctx->events_done; i++) {
            struct event *event;
            event = &ctx->events[i];
	    if (ctx->fmt)
		event_print_custom(event->pin, event->is_high, event->duration_ns, ctx);
            else
                event_print_human_readable(event->pin, event->is_high, event->duration_ns, event->num_samples);
    }
}

static void *timeout_timer(void *arg) {
    volatile bool *should_run = (bool*)arg;
    sleep(1);
    *should_run = 0;
    return NULL;
}

static unsigned char dht11_decode_byte(char *bits)
{
	unsigned char ret = 0;
	int i;

	for (i = 0; i < 8; ++i) {
		ret <<= 1;
		if (bits[i])
			++ret;
	}

	return ret;
}

static int dht11_decode(struct event *events, int offset)
{
	int i;
        unsigned long t;
	char bits[DHT11_BITS_PER_READ];
	unsigned char temp_int, temp_dec, hum_int, hum_dec, checksum;
        double temperature, humidity;
        struct timespec wallclock;

	for (i = 0; i < DHT11_BITS_PER_READ; ++i) {
		t = events[offset + 2 * i + 1].duration_ns;
		if (!events[offset + 2 * i + 1].is_high) {
			fprintf(stderr,
				"lost synchronisation at edge %d\n",
				offset + 2 * i + 1);
			return -EIO;
		}
		bits[i] = t > 50000l;
	}

	hum_int = dht11_decode_byte(bits);
	hum_dec = dht11_decode_byte(&bits[8]);
	temp_int = dht11_decode_byte(&bits[16]);
	temp_dec = dht11_decode_byte(&bits[24]);
	checksum = dht11_decode_byte(&bits[32]);

	if (((hum_int + hum_dec + temp_int + temp_dec) & 0xff) != checksum) {
		fprintf(stderr, "invalid checksum\n");
		return -EIO;
	}

	if (hum_int < 4) {  /* DHT22: 100000 = (3*256+232)*100 */
		temperature = (((temp_int & 0x7f) << 8) + temp_dec) *
					((temp_int & 0x80) ? -100 : 100) / 1000.0;
		humidity = ((hum_int << 8) + hum_dec) / 10.0;
	} else if (temp_dec == 0 && hum_dec == 0) {  /* DHT11 */
		temperature = temp_int;
		humidity = hum_int;
	} else {
		fprintf(stderr,
			"Don't know how to decode data: %d %d %d %d\n",
			hum_int, hum_dec, temp_int, temp_dec);
		return -EIO;
	}
        clock_gettime(CLOCK_REALTIME, &wallclock);

        printf (
                "{\n    \"temperature\": %lf,\n    \"humidity\": %lf,\n"
                "    \"timestamp\": \"%lf\"\n}\n",
                temperature, humidity, 
                (unsigned long)wallclock.tv_sec + ((unsigned long)wallclock.tv_nsec * 1.0e-9));

	return 0;
}


int main(int argc, char **argv)
{
	unsigned int offset, i, num_lines, num_samples;
        int value, value_prev;
	bool active_low = false;
	int flags = 0;
	int optc, opti, rv;
	struct mon_ctx ctx;
	char *end;
        struct gpiod_line_bulk bulk;
        struct gpiod_chip *chip;
        struct gpiod_line *line;
        struct timespec start, prev_tsp;
        pthread_t timer_thread;
        volatile bool should_run = !0;

	memset(&ctx, 0, sizeof(ctx));
        num_lines = 0;
        ctx.events_wanted = 100l;

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'h':
			print_help();
			return EXIT_SUCCESS;
		case 'v':
			print_version();
			return EXIT_SUCCESS;
		case 'l':
			active_low = true;
			break;
		case 'B':
			flags = bias_flags(optarg);
			break;
		case 'n':
			ctx.events_wanted = strtoul(optarg, &end, 10);
			if (*end != '\0')
				die("invalid number: %s", optarg);
			break;
		case 's':
			ctx.silent = true;
			break;
		case 'b':
			setlinebuf(stdout);
			break;
		case 'F':
			ctx.fmt = optarg;
			break;
		case '?':
			die("try %s --help", get_progname());
		default:
			abort();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		die("gpiochip must be specified");

	if (argc < 2)
		die("at least one GPIO line offset must be specified");

	for (i = 1; i < (unsigned int)argc; i++) {
		offset = strtoul(argv[i], &end, 10);
		if (*end != '\0' || offset > INT_MAX)
			die("invalid GPIO offset: %s", argv[i]);

		num_lines++;
	}
        if (num_lines != 1) {
            fputs ("No support for more than 1 line implemented\n", stderr);
            exit(1);
        }

        ctx.events = calloc(ctx.events_wanted, sizeof(*ctx.events));
        if (!ctx.events) {
            fputs ("Could not allocate memory for expected number of events\n", stderr);
            exit(3);
        }
	ctx.sigfd = make_signalfd();
        value = 0;
        rv = pthread_create(&timer_thread, 0, timeout_timer, &should_run);
        if (rv < 0)
                die_perror("error spawning a timekeeper thread");
 
	rv = gpiod_ctxless_set_value_multiple_ext(
				argv[0], &offset, &value,
				num_lines, active_low, "gpiosetpoll",
				0, 0, flags);
	if (rv < 0)
		die_perror("error setting the GPIO line values");
        usleep(500);

        chip = gpiod_chip_open_lookup(argv[0]);
        if (!chip)
                return -1;

        gpiod_line_bulk_init(&bulk);

        line = gpiod_chip_get_line(chip, offset);
        if (!line) {
                gpiod_chip_close(chip);
                return -1;
        }

        gpiod_line_bulk_add(&bulk, line);

        int req_flags = ctxless_flags_to_line_request_flags(active_low, flags);
        rv = gpiod_line_request_bulk_input_flags(&bulk, "gpiosetpoll", req_flags);
        if (rv < 0) {
                gpiod_chip_close(chip);
                return -1;
        }

        value_prev = 0;
        num_samples = 0;
        clock_gettime(CLOCK_MONOTONIC, &start);
        memcpy(&prev_tsp, &start, sizeof(prev_tsp));
        while (should_run && rv == 0 && ctx.events_done < ctx.events_wanted) {
            value = 0;
            rv = gpiod_line_get_value_bulk(&bulk, &value);

            if (rv != 0 || value != value_prev || !should_run) {
               struct timespec tsp;
               clock_gettime(CLOCK_MONOTONIC, &tsp);
               handle_event(offset, value_prev, 
                            (tsp.tv_sec - prev_tsp.tv_sec)*1000000000l + tsp.tv_nsec - prev_tsp.tv_nsec,
                            num_samples, &ctx);
               value_prev = value;
               num_samples = 1;
               memcpy(&prev_tsp, &tsp, sizeof(prev_tsp));
            } else {
               num_samples++;
            }
        }
        gpiod_chip_close(chip);
        

        print_events(&ctx);

        if (ctx.events_done > 2 * DHT11_BITS_PER_READ) {
            unsigned int offset = ctx.events_done - 2 * DHT11_BITS_PER_READ - 1;
            if (!ctx.events[offset].is_high) {
                rv = dht11_decode(ctx.events, offset);
            } else if (offset > 0) {
                rv = dht11_decode(ctx.events, --offset);
            } else {
                fprintf(stderr, "Missed start event: %ud\n", ctx.events_done);
                exit(5);
            }
        } else {
            fprintf(stderr, "Too few events detected: %ud\n", ctx.events_done);
            exit(6);
        }

	if (rv)
		die_perror("error interpreting read signals");

	return EXIT_SUCCESS;
}
