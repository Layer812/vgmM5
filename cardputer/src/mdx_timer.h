#ifndef MDX_TIMER_H_
#define MDX_TIMER_H_

struct mdx_timer;

typedef void (*mdx_timer_tick_callback)(struct mdx_timer *, void *data_ptr);

struct mdx_timer {
	void *data_ptr;
	mdx_timer_tick_callback tick;
	int sample_rate;
	int numerator, denominator, remainder;
};

int mdx_timer_init(struct mdx_timer *driver, int sample_rate);
void mdx_timer_deinit(struct mdx_timer *driver);
int mdx_timer_set_tick_callback(struct mdx_timer *driver, mdx_timer_tick_callback tick, void *data_ptr);
void mdx_timer_set_opm_tempo(struct mdx_timer *driver, int opm_tempo);
int mdx_timer_estimate(struct mdx_timer *driver, int samples);
int mdx_timer_advance(struct mdx_timer *driver, int samples);

#endif /* MDX_TIMER_H_ */
