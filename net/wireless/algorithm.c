#include <linux/kernel.h>
#include "../../include/linux/printk.h"
#include "../../include/linux/time.h"
#include "../../include/net/cfg80211.h"
#include <linux/slab.h>
#include <linux/etherdevice.h>
#include <linux/bitmap.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <net/cfg80211.h>
#include <linux/time.h>
#include <linux/mutex.h>

#include "../mac80211/ieee80211_i.h"
#include "core.h"

#include "rdev-ops.h"

#include "algorithm.h"

//all state of algorithm

struct state_header {
	u8 mac[ETH_ALEN];
	u8 closed;
	u8 finished;
	u8 offer;
	u8 claim;
	u8 w;
	u8 status_bidd;
	u8 status_auct;
	u16 reserved;
} state_header;

struct state_header state_header_vect[10];
u8 N = 1;
int start_up = 0;
int cont_frame_data = 0;
int cont_frame_management = 0;

struct timespec last_time, present_time, diff_time;

long long mtime = 10;
static struct timer_list wait_packet_timer;
int update_claim_offer = 0;
int yet_update_claim_offer = 0;

u32 last_claim = 0;

int debug = 1;
int debug_tx = 1;

//to set a CW
struct net_device *netdev = NULL;
struct cfg80211_registered_device *rdev = NULL;

#define UNIT 			252
#define INTERVAL_W_NO_UPDATE 	10000
#define DELTA_W 		30
#define UP_LEVEL 		3200	//us
#define DOWN_LEVEL 		987000	//us

void packet_timer_callback(unsigned long data)
{
	int ret;
	printk("atlas timer_callback called (%ld) - restore W value\n",
	       jiffies);

	int rate = 0;
	int i;
	if (abs(state_header_vect[0].w - rate) > DELTA_W) {
		//restart algorithm
		state_header_vect[0].finished = 0;
		state_header_vect[0].closed = 0;
		state_header_vect[0].offer = UNIT;
		state_header_vect[0].claim = 0;

		for (i = 1; i < N; i++) {
			state_header_vect[i].claim = 0;
		}

		update_claim_offer = 0;
		yet_update_claim_offer = 0;

		state_header_vect[0].w = rate;

		atlas_offer();
	}

	//restart timer for next
	ret =
	    mod_timer(&wait_packet_timer,
		      jiffies + msecs_to_jiffies(INTERVAL_W_NO_UPDATE));
	if (ret)
		printk("Error in mod_timer\n");

}

void cleanup_atlas_algorithm(void)
{
	int ret;
	//cleanup timer
	ret = del_timer(&wait_packet_timer);
	if (ret)
		printk("The timer is still in use...\n");

	printk("Cleanup atlas algorithm and timer\n");

	return;
}

void atlas_offer()
{

	int i;

	int enable_status_debug = 0;
	if (yet_update_claim_offer == 0)
		enable_status_debug = 1;

	//************************************
	//START ALGORITHM 2 AUCTIONER FOR NODE
	//************************************

	if (!(update_claim_offer % 2) && yet_update_claim_offer == 0) {

		//check if a message has been received from every bidder in B-active
		int num_recvd = 0;
		for (i = 0; i < N; i++) {
			num_recvd =
			    num_recvd + state_header_vect[i].status_bidd;
		}

		if (num_recvd == N) {
			//debug
			if (debug) {
				printk("num_recvd %d\n", num_recvd);
				printk
				    ("*********OFFER ALGORITHM************\n");
			}
			if (debug && enable_status_debug) {
				int i = 0;
				for (i = 0; i < N; i++) {
					//printk("mac address : %pM - \n", state_header_vect[i].mac);
					printk
					    ("before -> closed: %d - finished: %d - offer: %d - claim: %d\n",
					     state_header_vect[i].closed,
					     state_header_vect[i].finished,
					     state_header_vect[i].offer,
					     state_header_vect[i].claim);
					//printk("status_auct: %d - status_bidd: %d\n", state_header_vect[i].status_auct, state_header_vect[i].status_bidd);
				}
				printk("atlaslog");

				struct timeval t;
				struct tm broken;
				do_gettimeofday(&t);
				time_to_tm(t.tv_sec, 0, &broken);
				printk(",2013:12:%d:%d:%d:%d", broken.tm_mday,
				       broken.tm_hour, broken.tm_min,
				       broken.tm_sec);

				for (i = 0; i < N; i++) {
					printk(",%d,%d,%d,%d",
					       state_header_vect[i].closed,
					       state_header_vect[i].finished,
					       state_header_vect[i].offer,
					       state_header_vect[i].claim);
				}
				printk("\n");
				printk("*********************************\n");
			}

			//look if at least a node is active
			int active = 0;
			for (i = 0; i < N; i++) {
				if (state_header_vect[i].finished == 0) {
					active++;
				}
			}

			//if al least node active run algorithm (2)
			if (active > 0) {

				//da considerare il caso in cui non arrivo qui perchè active vale 0
				yet_update_claim_offer = 1;

				//we have received from every bidder
				//reset status bidder
				for (i = 1; i < N; i++) {
					state_header_vect[i].status_bidd = 0;
				}

				u8 X = 0;
				//find X, sum of all claim for inactive node
				for (i = 0; i < N; i++) {
					if (state_header_vect[i].finished == 1) {
						X = X +
						    state_header_vect[i].claim;
					}
				}

				//                              if(active)
				state_header_vect[0].offer =
				    (UNIT - X) / active;
				//                              else
				//                                      state_header_vect[0].offer = (UNIT-X);

				if (debug)
					printk
					    ("value of X: %d - my offer : %d\n",
					     X, state_header_vect[0].offer);

				u8 claimed = 0;
				for (i = 0; i < N; i++) {
					claimed =
					    claimed +
					    state_header_vect[i].claim;
				}

				if (debug)
					printk("the claim sum: %d\n", claimed);

				if (claimed == UNIT) {
					state_header_vect[0].closed = 1;
				} else
					state_header_vect[0].closed = 0;

			}

			if (debug && enable_status_debug) {
				int i = 0;
				for (i = 0; i < N; i++) {
					//printk("mac address : %pM - \n", state_header_vect[i].mac);
					printk
					    ("after -> closed: %d - finished: %d - offer: %d - claim: %d\n",
					     state_header_vect[i].closed,
					     state_header_vect[i].finished,
					     state_header_vect[i].offer,
					     state_header_vect[i].claim);
					//printk("status_auct: %d - status_bidd: %d\n", state_header_vect[i].status_auct, state_header_vect[i].status_bidd);
				}
				printk("atlaslog");

				struct timeval t;
				struct tm broken;
				do_gettimeofday(&t);
				time_to_tm(t.tv_sec, 0, &broken);

				printk(",2013:12:%d:%d:%d:%d", broken.tm_mday,
				       broken.tm_hour, broken.tm_min,
				       broken.tm_sec);

				for (i = 0; i < N; i++) {
					printk(",%d,%d,%d,%d",
					       state_header_vect[i].closed,
					       state_header_vect[i].finished,
					       state_header_vect[i].offer,
					       state_header_vect[i].claim);
				}
				printk("\n");
				printk("*********************************\n");
			}

		}

	}
	//************************************
	// END ALGORITHM 2 AUCTIONER FOR NODE
	//************************************

}

void atlas_claim()
{

	int i;
	int min = UNIT;

	int enable_status_debug = 0;
	if (yet_update_claim_offer == 0)
		enable_status_debug = 1;

	//************************************
	//START ALGORITHM 3 BIDDER FOR NODE
	//************************************

	if ((update_claim_offer % 2) && (yet_update_claim_offer == 0)) {

		//Update bidders, search min offer
		//check if a message has been been received from all auctioneer in
		int num_rec = 0;
		int num_finished = 0;
		for (i = 0; i < N; i++) {
			num_rec = num_rec + state_header_vect[i].status_bidd;
		}

		// it time to respond with a claim
		if (num_rec == N) {

			if (debug) {
				printk("num_rec %d\n", num_rec);
				printk
				    ("*********CLAIM ALGORITHM************\n");
			}

			if (debug && enable_status_debug) {
				int i = 0;
				for (i = 0; i < N; i++) {
					//printk("mac address : %pM - \n", state_header_vect[i].mac);
					printk
					    ("before -> closed: %d - finished: %d - offer: %d - claim: %d\n",
					     state_header_vect[i].closed,
					     state_header_vect[i].finished,
					     state_header_vect[i].offer,
					     state_header_vect[i].claim);
					//printk("status_auct: %d - status_bidd: %d\n", state_header_vect[i].status_auct, state_header_vect[i].status_bidd);
				}
				printk("atlaslog");

				struct timeval t;
				struct tm broken;
				do_gettimeofday(&t);
				time_to_tm(t.tv_sec, 0, &broken);
				printk(",2013:12:%d:%d:%d:%d", broken.tm_mday,
				       broken.tm_hour, broken.tm_min,
				       broken.tm_sec);

				for (i = 0; i < N; i++) {
					printk(",%d,%d,%d,%d",
					       state_header_vect[i].closed,
					       state_header_vect[i].finished,
					       state_header_vect[i].offer,
					       state_header_vect[i].claim);
				}
				printk("\n");
				printk("*********************************\n");
			}

			//da considerare il caso in cui non arrivo qui perchè active vale 0
			yet_update_claim_offer = 1;

			//reset status_auct for other node
			for (i = 1; i < N; i++) {
				state_header_vect[i].status_bidd = 0;
			}

			for (i = 0; i < N; i++) {
				if (state_header_vect[i].offer < min) {
					min = state_header_vect[i].offer;
				}
			}

			if (debug)
				printk("min of the offers : %d\n", min);

			if (state_header_vect[0].w < min) {
				state_header_vect[0].claim =
				    state_header_vect[0].w;
			} else {
				state_header_vect[0].claim = min;
			}

			//check if someone have closed
			int bottlenecked = 0;
			for (i = 0; i < N; i++) {
				if (state_header_vect[i].closed == 1) {
					bottlenecked = 1;
					break;
				}
			}

			//check if I have closed
			if (state_header_vect[0].claim == state_header_vect[0].w
			    || bottlenecked)
				state_header_vect[0].finished = 1;
			else
				state_header_vect[0].finished = 0;

			if (debug)
				printk("my finished : %d - my claim : %d -- \n",
				       state_header_vect[0].finished,
				       state_header_vect[0].claim);

			if (debug && enable_status_debug) {
				int i = 0;
				for (i = 0; i < N; i++) {
					//printk("mac address : %pM - \n", state_header_vect[i].mac);
					printk
					    ("after -> closed: %d - finished: %d - offer: %d - claim: %d\n",
					     state_header_vect[i].closed,
					     state_header_vect[i].finished,
					     state_header_vect[i].offer,
					     state_header_vect[i].claim);
					//printk("status_auct: %d - status_bidd: %d\n", state_header_vect[i].status_auct, state_header_vect[i].status_bidd);
				}
				printk("atlaslog");

				struct timeval t;
				struct tm broken;
				do_gettimeofday(&t);
				time_to_tm(t.tv_sec, 0, &broken);
				printk(",2013:12:%d:%d:%d:%d", broken.tm_mday,
				       broken.tm_hour, broken.tm_min,
				       broken.tm_sec);

				for (i = 0; i < N; i++) {
					printk(",%d,%d,%d,%d",
					       state_header_vect[i].closed,
					       state_header_vect[i].finished,
					       state_header_vect[i].offer,
					       state_header_vect[i].claim);
				}
				printk("\n");
				printk("*********************************\n");
			}

		}

	}
	//************************************
	// END ALGORITHM 3 BIDDER FOR NODE
	//************************************

}

void cw_update(void)
{

#define MAX_CW	4096
#define MIN_CW	32
#define SATURATION_CW 128

	int j;
	int active = 0;
	for (j = 0; j < N; j++) {
		if (state_header_vect[j].finished == 1) {
			active++;
		}
	}

//                      if( active == N ){
//                         if(( (last_claim != state_header_vect[0].claim)) || (current_survey->read==0) ){
//
//
//
//                                  //update contentin window
//
//                                  last_claim = (u32) state_header_vect[0].claim;
//                                  current_survey->read = 1;
//
//                                  u32 current_survey_diff_time = ((current_survey->diff_time.tv_sec * 1000000) + (current_survey->diff_time.tv_nsec / 1000));         //[us]
//
//                                  u32 time_length = 3000;
//                                  u32 claim_time = (last_claim * current_survey_diff_time) / UNIT ;
//                                  u32 w_n = 0;
//                                  u32 claim_num_frame = 0;
//                                  u32 cw = 0;
//                                  u32 time_slot_length = 9;
//
//
//                                  if(claim_time != 0){
//                                        if ( (current_survey->ratio_busy_scale + claim_time) < current_survey_diff_time){
//                                            w_n = current_survey_diff_time - current_survey->ratio_busy_scale - claim_time;
//                                            claim_num_frame = ( claim_time / time_length );
//                                            cw = (w_n * 2) / (time_slot_length * claim_num_frame);
//
//                                            if(cw>MAX_CW)
//                                              cw=MAX_CW;
//
//                                        }
//                                        else
//                                            cw = SATURATION_CW;
//                                  }
//                                  else
//                                      cw = MAX_CW;
//
//
//                                  printk("ack_good_count : %u -- ack_bad_count %u \n", current_survey->frame_ack_good_count, current_survey->frame_ack_bad_count);
//                                  printk("claim %u - claim_time : %u -- busy %u -- diff_time_survey %u\n", last_claim, claim_time, current_survey->ratio_busy_scale, current_survey_diff_time);
//                                  printk("w_n : %u -- cw %u\n",  w_n, cw);
//
//
//
//                                  /*u32 w_1=0;
//                                  u32 w_2=0;
//                                  u32 w_3=0;
//                                  *///u32 t_a = (2.2 * SCALE);
//                                  //t_a = t_a / current_survey_diff_time;     //[1 : 1000000] //2,2 is length packet at 6Mbps [1470byte]
//                                  //u32 s_a = (last_claim * SCALE) / UNIT;    //[1 : 1000000]
//                                  //u32 s_a = (last_claim * current_survey_diff_time) / UNIT;         //time activiti
//
// //                               u32 cw;
// //                               if (s_a == 0)
// //                                   cw = MAX_CW;
// //                               else{
// //
// //                                   u32 framt_total_count = current_survey->frame_ack_good_count + current_survey->frame_ack_bad_count;
// //                                   u32 p_success =
// //
// //                                   w_1 = ((current_survey->frame_ack_good_count * t_a) * SCALE) / s_a;     //[1 : 10000]
// //
// //                                   if(w_1 > SCALE)
// //                                     w_1 = SCALE;
// //
// //                                   w_2 = ((current_survey->frame_ack_good_count + current_survey->frame_ack_bad_count) * t_a) ;
// //                                   w_3 = current_survey->ratio_busy_scale;
// //
// //                                   if( (w_2+w_3) > w_1){
// //                                     int i;
// //                                     int total_other_claim = 0;
// //                                     for(i=1; i<N; i++){
// //                                           total_other_claim = total_other_claim + state_header_vect[i].claim;
// //                                     }
// //
// //                                     if(total_other_claim < 40)
// //                                       cw=MIN_CW;
// //                                     else
// //                                       cw=MAX_CW;
// //                                   }
// //                                   else{
// //                                       cw = ( w_1 - w_2 - w_3); //2 * ( w_1 - w_2 - w_3)
// //                                       cw = (cw * MAX_CW) / SCALE;
// //                                   }
// //                               }
//
//
//                                  struct ieee80211_txq_params txq_params;
//                                  int ac, result;
//                                  for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
//                                          txq_params.ac = ac;
//                                          txq_params.txop = 0;
//                                          txq_params.cwmin = cw;
//                                          txq_params.cwmax = cw;
//                                          txq_params.aifs = 2;
//                                          result = rdev_set_txq_params(rdev, netdev, &txq_params);
//                                  }
// //                               printk("current_survey->frame_ack_good_count : %u -- current_survey->frame_ack_bad_count %u \n", current_survey->frame_ack_good_count, current_survey->frame_ack_bad_count);
// //                               printk("claim : %u -- busy %u -- diff_time_survey %u -- t_a %u\n", s_a, current_survey->ratio_busy_scale, current_survey_diff_time, t_a);
// //                               printk("w_1 : %u -- w_2 %u -- w_3 %u -- cw %u\n",  w_1, w_2, w_3, cw);
//                        }
//                      }

}

int atlas_algorithm(struct atlas_header *atlas_header_point, int tx_rx,
		    u8 * src, struct sk_buff *skb, int queue_depth,
		    struct state_survey *current_survey)
{

	int ret;

	if (start_up == 0 && tx_rx == 0) {
		//initializzation

		atlas_header_point->claim = 0;
		atlas_header_point->offer = 0;
		atlas_header_point->closed = 0;
		atlas_header_point->finished = 0;
		atlas_header_point->reserved = 0;
		state_header_vect[0].offer = atlas_header_point->offer;
		state_header_vect[0].claim = atlas_header_point->claim;
		state_header_vect[0].closed = atlas_header_point->closed;
		state_header_vect[0].finished = atlas_header_point->finished;
		state_header_vect[0].reserved = atlas_header_point->reserved;
		state_header_vect[0].status_auct = 1;
		state_header_vect[0].status_bidd = 1;
		state_header_vect[0].w = 0;

		getnstimeofday(&last_time);
		getnstimeofday(&present_time);

		/*  u8 check_mac_1[6] = {0x00, 0x15, 0x6d, 0x85, 0x90, 0x2d};
		   u8 check_mac_2[6] = {0x00, 0x15, 0x6d, 0x85, 0x75, 0xb3};
		   u8 check_mac_3[6] = {0xa8, 0x54, 0xb2, 0x69, 0x3b, 0xe3};

		   if (!memcmp(check_mac_1, src, ETH_ALEN)){
		   state_header_vect[0].w = 200; //UNIT;
		   }
		   if (!memcmp(check_mac_2, src, ETH_ALEN)){
		   state_header_vect[0].w = 80; //UNIT;
		   }
		   if (!memcmp(check_mac_3, src, ETH_ALEN)){
		   state_header_vect[0].w = 160; //UNIT;
		   }
		 */

		memcpy(state_header_vect[0].mac, src, ETH_ALEN);

		//obtain dev device
		netdev = skb->dev;
		if (netdev) {
			rdev = wiphy_to_dev(netdev->ieee80211_ptr->wiphy);
		}

		printk
		    ("inizializzazione = claim: %x -- offer: %x -- auction: %d -- bidder: %d\n",
		     atlas_header_point->claim, state_header_vect[0].offer,
		     atlas_header_point->closed, atlas_header_point->finished);

		//init timer for wait packet
		printk("Timer wait packet starting\n");
		setup_timer(&wait_packet_timer, packet_timer_callback, 0);

		printk("Starting timer to fire in 1s (%ld)\n", jiffies);
		ret =
		    mod_timer(&wait_packet_timer,
			      jiffies + msecs_to_jiffies(INTERVAL_W_NO_UPDATE));
		if (ret)
			printk("Error in mod_timer\n");

		start_up = 1;

	} else {

		//TX frame
		if (tx_rx == 0 && start_up == 1) {

			long rate = 0;
			long divider_data = 45;
			long divider_management = 4;
			int threshold = DELTA_W;
			int i;

			//restart timer for next
			ret =
			    mod_timer_pinned(&wait_packet_timer,
					     jiffies +
					     msecs_to_jiffies
					     (INTERVAL_W_NO_UPDATE));

			//considerate only the iperf frame
			if (skb_get_queue_mapping(skb) == 2) {
				//if( ( (queue_depth>35) && (queue_depth<45) ) || ((queue_depth>75))  ){
				if (queue_depth > 35) {
					getnstimeofday(&last_time);
					cont_frame_data = 0;
					cont_frame_management = 0;

					rate = UNIT;
					//in questo momento aggiorniamo bidd a 0 (rifacciamo l'algoritmo) se la varizione di w è significativa,
					//la preoccupazione è che w si aggiorni più velocemente di quanto l'algoritmo possa convergere

					if (abs(state_header_vect[0].w - rate) >
					    DELTA_W) {
						//restart algorithm
						state_header_vect[0].finished =
						    0;
						state_header_vect[0].closed = 0;
						state_header_vect[0].offer =
						    UNIT;
						state_header_vect[0].claim = 0;

						for (i = 1; i < N; i++) {
							state_header_vect[i].
							    claim = 0;
						}

						update_claim_offer = 0;
						yet_update_claim_offer = 0;

						state_header_vect[0].w = rate;

						if (debug_tx)
							printk
							    ("depth queue over 35,  chang val - ");

						if (debug_tx)
							printk("=> w : %d\n",
							       state_header_vect
							       [0].w);
					}
				} else
					cont_frame_data++;
			} else
				cont_frame_management++;

			if (cont_frame_data == divider_data) {

				//get current time
				getnstimeofday(&present_time);
				//compute difference
				diff_time =
				    timespec_sub(present_time, last_time);

				//save current time and reset count packet
				getnstimeofday(&last_time);
				cont_frame_data = 0;
				cont_frame_management = 0;

				//convert to us
				mtime =
				    (((long long)diff_time.tv_sec * 1000000) +
				     (diff_time.tv_nsec / 1000));
				long mtime_2 = (long)mtime;
				long rem = mtime_2 / divider_data;
				long long time = (long long)rem;

				if (debug_tx)
					printk
					    ("#### ave. inter. time - %lldus - queue_depth %d - ",
					     time, queue_depth);

				//find rate
				if (time > DOWN_LEVEL) {	//us
					rate = 1;
				} else if (time < UP_LEVEL) {	//us
					rate = UNIT;
				} else {
					rate =
					    (((UNIT * 10000) / (long)time) *
					     UP_LEVEL) / 10000;
				}

				if (state_header_vect[0].w < 200)
					threshold = DELTA_W;
				else
					threshold = DELTA_W + 20;

				//in questo momento aggiorniamo bidd a 0 (rifacciamo l'algoritmo) se la varizione di w è significativa,
				//la preoccupazione è che w si aggiorni più velocemente di quanto l'algoritmo possa convergere
				if (abs(state_header_vect[0].w - rate) >
				    threshold) {
					//restart algorithm
					state_header_vect[0].finished = 0;
					state_header_vect[0].closed = 0;
					state_header_vect[0].offer = UNIT;
					state_header_vect[0].claim = 0;

					for (i = 1; i < N; i++) {
						state_header_vect[i].claim = 0;
					}

					update_claim_offer = 0;
					yet_update_claim_offer = 0;
					if (debug_tx)
						printk("chang val - ");

					state_header_vect[0].w = rate;

				}

				if (debug_tx)
					printk("=> w : %d\n",
					       state_header_vect[0].w);

			} else if (cont_frame_management > divider_management) {
				rate = 0;
				cont_frame_data = 0;
				cont_frame_management = 0;

				if (abs(state_header_vect[0].w - rate) >
				    threshold) {
					//restart algorithm
					state_header_vect[0].finished = 0;
					state_header_vect[0].closed = 0;
					state_header_vect[0].offer = UNIT;
					state_header_vect[0].claim = 0;

					for (i = 1; i < N; i++) {
						state_header_vect[i].claim = 0;
					}

					update_claim_offer = 0;
					yet_update_claim_offer = 0;

					if (debug_tx)
						printk
						    ("chang val from management frame - \n");

					state_header_vect[0].w = rate;
				}

			}

			atlas_offer();
			atlas_claim();

			//cw update
			//cw_update();

			atlas_header_point->offer = state_header_vect[0].offer;
			atlas_header_point->claim = state_header_vect[0].claim;
			atlas_header_point->closed =
			    state_header_vect[0].closed;
			atlas_header_point->finished =
			    state_header_vect[0].finished;

			//state_header_vect[0].reserved++;
			//atlas_header_point->reserved = state_header_vect[0].reserved;
			atlas_header_point->reserved = 0;

			if (yet_update_claim_offer == 1) {
				update_claim_offer++;
				yet_update_claim_offer = 0;
			}

		}

		//RX frame
		if (tx_rx == 1 && start_up == 1) {

			int i = 0;
			int j = 0;
			int count;

			// ***************************
			//search if the receive station is present
			//and update claim
			// **************************
			count = 0;
			for (i = 0; i < N; i++) {
				//found station, update value
				if (!memcmp
				    (state_header_vect[i].mac, src, ETH_ALEN)) {

					if (state_header_vect[i].finished
					    && !atlas_header_point->finished) {

						//if(  state_header_vect[i].closed && !atlas_header_point->closed){

						state_header_vect[0].finished =
						    0;
						//state_header_vect[0].closed=0;
						//state_header_vect[0].offer = UNIT;
						//state_header_vect[0].claim = 0;

						for (j = 0; j < N; j++) {
							state_header_vect[j].
							    claim = 0;
						}

						update_claim_offer = 0;
						yet_update_claim_offer = 0;
						//}
					}

					state_header_vect[i].closed =
					    atlas_header_point->closed;
					state_header_vect[i].finished =
					    atlas_header_point->finished;
					state_header_vect[i].claim =
					    atlas_header_point->claim;
					state_header_vect[i].offer =
					    atlas_header_point->offer;

					//state_header_vect[i].status_auct = 1;
					state_header_vect[i].status_bidd = 1;

//                        printk("receive frame from : %pM - reserved %d\n", src, atlas_header_point->reserved);
//                        printk("*********RECEIVE************\n");
//                                        int i=0;
//                                        for (i=0; i<N; i++){
//                                                        //printk("mac address : %pM - \n", state_header_vect[i].mac);
//                                                        printk("closed: %d - finished: %d - offer: %d - claim: %d - \n", state_header_vect[i].closed, state_header_vect[i].finished, state_header_vect[i].offer, state_header_vect[i].claim);
//                                                       // printk("reserved %d\n", state_header_vect[i].reserved);
//                                        }
//                                        printk("*********************************\n");
//

					break;
				}
				count++;
			}

			//printk("count: %d\n", count);
			//if station don't found, we insert new station and set claim
			if (count == N) {
				memcpy(state_header_vect[N].mac, src, ETH_ALEN);
				state_header_vect[N].closed =
				    atlas_header_point->closed;
				state_header_vect[N].finished =
				    atlas_header_point->finished;
				state_header_vect[N].offer =
				    atlas_header_point->offer;
				state_header_vect[N].claim =
				    atlas_header_point->claim;
				//state_header_vect[N].reserved = atlas_header_point->reserved;

				//state_header_vect[N].status_auct = 1;
				state_header_vect[N].status_bidd = 1;
				N++;

				// update my offer with new node number
				// int
				state_header_vect[0].offer = UNIT;
				//printk("init my offer value: %d\n", state_header_vect[0].offer);
			}

		}

	}

	return 0;
}

EXPORT_SYMBOL(atlas_algorithm);
