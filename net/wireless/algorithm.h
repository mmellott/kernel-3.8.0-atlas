
void atlas_offer(void);
void atlas_claim(void);

void cleanup_atlas_algorithm(void);
void packet_timer_callback( unsigned long data );
void cw_update(void);
int atlas_algorithm(struct atlas_header *atlas_header_point, int tx_rx, u8 * src, struct sk_buff *skb, int queue_depth, struct state_survey *current_survey);
