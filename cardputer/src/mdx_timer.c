#include <string.h>
#include <stdio.h>
#include "mdx_timer.h"
#include "tools.h"

int mdx_timer_init(struct mdx_timer *driver, int sample_rate) {
	memset(driver, 0, sizeof(*driver));
	driver->sample_rate = sample_rate;
	return 0;
}

void mdx_timer_deinit(struct mdx_timer *driver) {
	memset(driver, 0, sizeof(*driver));
}

int mdx_timer_set_tick_callback(struct mdx_timer *driver, mdx_timer_tick_callback tick, void *data_ptr) {
	driver->data_ptr = data_ptr;
	driver->tick = tick;
	return 0;
}

void mdx_timer_set_opm_tempo(struct mdx_timer *driver, int opm_tempo) {
	int
		clock = 4000000,
		d = gcd(clock, 1024),
		c1 = clock / d,
		c2 = 1024 / d,
		s = driver->sample_rate * c2,
		d2 = gcd(c1, s);

	driver->numerator = s * (256 - opm_tempo) / d2;
	driver->denominator = c1 / d2;
}

int mdx_timer_estimate(struct mdx_timer *driver, int samples) {
	int denom = driver->remainder;
	for(int i = 1; i <= samples; i++) {
		denom += driver->denominator;
		if(denom >= driver->numerator) {
			return i;
		}
	}
	return samples;
}

int mdx_timer_advance(struct mdx_timer *driver, int samples) {
	int denom = driver->remainder;
	int ticks = 0;
	for(int i = 1; i <= samples; i++) {
		denom += driver->denominator;
		if(denom >= driver->numerator) {
			if(driver->tick) {
				driver->tick(driver, driver->data_ptr);
			}
			ticks++;
			denom -= driver->numerator;
		}
	}
	driver->remainder = denom;
	return ticks;
}
