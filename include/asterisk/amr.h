#ifndef _AST_FORMAT_AMR_H_
#define _AST_FORMAT_AMR_H_

struct amr_attr {
	unsigned int octet_align;
	unsigned int mode_set:9;
	unsigned int mode_change_period;
	unsigned int mode_change_capability;
	unsigned int mode_change_neighbor;
	unsigned int crc;
	unsigned int robust_sorting;
	unsigned int interleaving;
	int max_red;
	/* internal variables for transcoding module */
	unsigned char mode_current; /* see amr_clone for default */
	int vad;                    /* see amr_clone for default */
};

#endif /* _AST_FORMAT_AMR_H */
