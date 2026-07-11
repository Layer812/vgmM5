#ifndef ADPCM_H_
#define ADPCM_H_

struct adpcm_status {
	short last;
	short step_index;
};

void adpcm_init(struct adpcm_status *);
char adpcm_encode( short, struct adpcm_status *);
short adpcm_decode( char, struct adpcm_status *);

#endif /* ADPCM_H_ */
