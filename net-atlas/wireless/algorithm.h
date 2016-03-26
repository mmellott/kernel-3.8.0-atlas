struct channel_access_stats {
	unsigned int psucc;
	unsigned int busy;
	unsigned int virt_cs;
};

void atlas_offer(void);
void atlas_claim(void);

void cleanup_atlas_algorithm(void);
void packet_timer_callback( unsigned long data );
void cw_update(void);
void cw_update2(int);
int atlas_rate_limiter(void);
int atlas_algorithm(struct atlas_header *atlas_header_point, int tx_rx, u8 * src, struct sk_buff *skb, int queue_depth, struct state_survey *current_survey);


