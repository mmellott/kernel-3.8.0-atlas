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
#include <linux/icmp.h>
#include <linux/ip.h>
#include "../mac80211/ieee80211_i.h"
#include "core.h"

#include "rdev-ops.h"

#include "algorithm.h"

#define MAX_PKT_ALL 1000

#define CLAIM_CAPACITY 80
// CHANNEL STATS
//uint64_t phy_busy_stat=0;
//EXPORT_SYMBOL(phy_busy_stat);

u64 busy=0;
EXPORT_SYMBOL(busy);

unsigned int completed_count=0;
unsigned int completed_count_=0;
EXPORT_SYMBOL(completed_count);
u64 data_count_=0;
u64 data_count=0;
EXPORT_SYMBOL(data_count);

unsigned int icmp_count=0;
unsigned int icmp_count_=0;

u64 rts_count=0;
EXPORT_SYMBOL(rts_count);
u64 rts_count_=0;
EXPORT_SYMBOL(rts_count_);

int64_t diff_data_count=0;
int64_t diff_icmp_count=0;
int64_t diff_rts_count=0;

//unsigned long vbusy_rts=0;
//EXPORT_SYMBOL(vbusy_rts);

unsigned long ath_psucc=0;
EXPORT_SYMBOL(ath_psucc);

unsigned int ath_tx_accessrate;
EXPORT_SYMBOL(ath_tx_accessrate);


extern unsigned long iperf_rate;
extern int is_cw_forced;
unsigned int psucc=100;
extern long dbg_psucc;


bool is_timer_started=false;
struct timespec t0, t1;
int D=0;
int D2=0;

u64 busy_perc_tot=0;

uint64_t delta_time=0;
int cw_=1023;

int64_t busytx2=0;
int64_t busytx2_data=0;
int64_t busytx2_rts=0;
int64_t freeze2=0;
int64_t avg_tx=0;
int64_t avg_tx_data=0;
int64_t avg_tx_rts=0;

u64 tx_goal=0;
u64 freeze_preditct=0;
u64 gross_rate=0;
u64 I=0;
u64 freeze_predict=0;


u64 busy_perc=0;

//lock-unlock the CW computation over busytime and psucc estimation
unsigned int channel_stats_enable=1;


struct survey_info survey0,survey1;
struct survey_info diff_survey;
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
    u16 cw;
} state_header;

struct state_header state_header_vect[10]; 
u8 			N = 1;
int 			start_up = 0;
int 			cont_frame_data = 0;
int 			cont_frame_management = 0;

struct 	timespec 	last_time, present_time, diff_time;

long long 		mtime = 10;
static 			struct timer_list wait_packet_timer;
int 			update_claim_offer = 0;
int 			yet_update_claim_offer = 0;

u32 last_claim = 0;

int debug = 1;
int debug_tx = 1;
  
unsigned int stop_compute_cw=0;  
uint64_t tic=0;
uint64_t toc=0;
  
//to set a CW
struct net_device *netdev = NULL;
struct cfg80211_registered_device *rdev = NULL;


#define UNIT			252
#define MAX_THR			5140 //kbps
#define tdata			2050 //transmissione packet time us @6Mbps
#define tack			39 // ACK us
#define tpack		 	(tdata+tack) //us	
#define SIFS			16	//us
#define tslot			9	//us
#define AIFS			2	//num
#define DIFS			(AIFS*tslot+SIFS) //us
#define IFS			(DIFS+SIFS) // DIFS+SIFIS us

#define txT			DIFS+tdata+SIFS+tack //TxTime+DIFS+ACK+SIFS us

#define trts			47 //us
#define tcts			39 //us
#define TxTime			(trts+tcts+tdata+tack+(3*SIFS)+DIFS)

#define INTERVAL_W_NO_UPDATE 	10000
#define DELTA_W 		0
#define UP_LEVEL 		3200 //us
#define DOWN_LEVEL 		987000 //us

int all_finished=0;
int is_tic_start=0;
int stop_print_react_offer=0;
void packet_timer_callback( unsigned long data )
{
  int ret, rate, i;
  rate=0;

  printk( "atlas timer_callback called (%ld) - restore W value\n", jiffies );

  if( abs(state_header_vect[0].w - rate) > DELTA_W) {
			    //restart algorithm
			state_header_vect[0].finished=0;
			state_header_vect[0].closed=0;
			 if (N>0)
				state_header_vect[0].offer =  UNIT/N;
			else
				state_header_vect[0].offer =  UNIT;

			//state_header_vect[0].offer = UNIT;
			state_header_vect[0].claim = 0;
			state_header_vect[0].cw = 32;

			for(i=1; i<N; i++){
				state_header_vect[i].claim = 0;
			}


			update_claim_offer = 0;
			yet_update_claim_offer=0;

			state_header_vect[0].w = rate;

			atlas_offer();
  }
   
  //restart timer for next 
  ret = mod_timer( &wait_packet_timer, jiffies + msecs_to_jiffies(INTERVAL_W_NO_UPDATE) );
  if (ret) printk("Error in mod_timer\n");
   
}


void cleanup_atlas_algorithm( void )
{
	int ret;
	//cleanup timer
	ret = del_timer( &wait_packet_timer );
	if (ret) printk("The timer is still in use...\n");
	 
	printk("Cleanup atlas algorithm and timer\n");

	return;
}




void atlas_offer() {
  
	int i;

	struct timeval t;
	struct tm broken;
	int enable_status_debug = 0;
	int active = 0;
	u8 X = 0;
	u8 claimed=0;
	

	if(yet_update_claim_offer==0)
		  enable_status_debug=1;

  
  
	//************************************
	//START ALGORITHM 2 AUCTIONER FOR NODE
	//************************************
	 
	if( !(update_claim_offer%2) && yet_update_claim_offer==0){
		//check if a message has been received from every bidder in B-active
		int num_recvd = 0;
		for(i=0; i<N; i++){
			num_recvd = num_recvd + state_header_vect[i].status_bidd;
		}
 
		do_gettimeofday(&t);
		time_to_tm(t.tv_sec, 0, &broken);
		if(num_recvd == N){
			//look if at least a node is active
			for (i=0; i<N; i++) {
				if (state_header_vect[i].finished == 0) {
					active++;
					channel_stats_enable=1;
				}
			}
			
			
			
			//if al least node active run algorithm (2)
			if(active > 0) {
					//da considerare il caso in cui non arrivo qui perchè active vale 0
					yet_update_claim_offer=1;

			  
					//we have received from every bidder
					//reset status bidder
					for(i=1; i<N; i++){
						state_header_vect[i].status_bidd = 0;
					}
					
					
					X = 0;
					//find X, sum of all claim for inactive node
					for (i=0; i<N; i++) {
						  if (state_header_vect[i].finished == 1) {
							  X = X + state_header_vect[i].claim;
						  }
					}
					
					
					state_header_vect[0].offer = (UNIT-X)/active;
				
					claimed=0;
					for (i=0; i<N; i++) {
						claimed = claimed + state_header_vect[i].claim;
					}
					
					if (claimed == UNIT) {
						state_header_vect[0].closed=1;
					}
					else
						state_header_vect[0].closed=0;
					
			
			  
					
					
					
			}
			
/*	
			if(debug && enable_status_debug){
				int i=0;
				for (i=0; i<N; i++){
					  //printk("mac address : %pM - \n", state_header_vect[i].mac);
					  printk("after -> closed: %d - finished: %d - offer: %d - claim: %d\n", 
						state_header_vect[i].closed, 
						state_header_vect[i].finished, 
						state_header_vect[i].offer*100/UNIT, 
						state_header_vect[i].claim*100/UNIT);
					  //printk("status_auct: %d - status_bidd: %d\n", state_header_vect[i].status_auct, state_header_vect[i].status_bidd);
				}
				printk("atlaslog");
				
										    
				do_gettimeofday(&t);
				time_to_tm(t.tv_sec, 0, &broken);
				printk(",%ld:%d:%d:%d:%d:%d", broken.tm_year+1900, broken.tm_mon+1, broken.tm_mday, broken.tm_hour, broken.tm_min, broken.tm_sec);


				for (i=0; i<N; i++){
					  printk(",%d,%d,%d,%d", 
						state_header_vect[i].closed, 
						state_header_vect[i].finished, 
						state_header_vect[i].offer*100/UNIT, 
						state_header_vect[i].claim*100/UNIT);
				}
				printk("\n");
				printk("*********************************\n");	
			}	

*/
				
		}


	}
	do_gettimeofday(&t);
	time_to_tm(t.tv_sec, 0, &broken);

	if (stop_print_react_offer==0){	
		for (i=0; i<N; i++){
			//printk ("react_offer,%lu.%lu,%d,%02X:%02X:%02X:%02X:%02X:%02X, claim=%u, finished=%u, closed=%u, offer=%u\n",

			printk ("react_table,%llu,%d,%02X:%02X:%02X:%02X:%02X:%02X,%u,%u,%u,%u\n",
				(uint64_t)(t.tv_sec*1000*1000+t.tv_usec),
				i,
				state_header_vect[i].mac[0],state_header_vect[i].mac[1],state_header_vect[i].mac[2],state_header_vect[i].mac[3],state_header_vect[i].mac[4],state_header_vect[i].mac[5],
				state_header_vect[i].claim*100/UNIT,
				state_header_vect[i].finished,
				state_header_vect[i].closed,
				state_header_vect[i].offer*100/UNIT
			);

		}
	}

	all_finished=0;
	
	for (i=0; i<N; i++){
		if (state_header_vect[i].finished==1)
			all_finished++;
	}
	if (all_finished==0 && is_tic_start==0){
		is_tic_start=1;
		tic=t.tv_sec*1000*1000+t.tv_usec;
	}

	if (all_finished==N && stop_print_react_offer==0){
		toc=t.tv_sec*1000*1000+t.tv_usec;
		printk("react_offer,%llu,%llu,%llu\n",toc-tic,tic,toc);
	}
	if (all_finished==N){
		stop_print_react_offer=1;
		is_tic_start=0;
	}
	else{
		stop_print_react_offer=0;
	}
		
	
		//************************************
		// END ALGORITHM 2 AUCTIONER FOR NODE
		//************************************
	
  
  
}


void atlas_claim() {
		int i;
		int min = UNIT;
		  
		//struct timeval t;
		//struct tm broken;
		int enable_status_debug = 0;
		int bottlenecked=0;
		if(yet_update_claim_offer==0)
			  enable_status_debug=1;

  
  
      		//************************************
		//START ALGORITHM 3 BIDDER FOR NODE
		//************************************

			
		if( (update_claim_offer%2) && (yet_update_claim_offer==0) ){	
			
			//Update bidders, search min offer
			//check if a message has been been received from all auctioneer in
			int num_rec = 0;
			//int num_finished = 0;
			for(i=0; i<N; i++){
				num_rec = num_rec + state_header_vect[i].status_bidd;
			}
			
			// it time to respond with a claim
			if(num_rec == N ){	
			  	
				yet_update_claim_offer=1; 

				//reset status_auct for other node
				for(i=1; i<N; i++){
					state_header_vect[i].status_bidd=0;
				}	
				
				for (i=0; i<N; i++) {
					if (state_header_vect[i].offer < min) {
						min = state_header_vect[i].offer;
					}
				}
				
				
				if(debug)
				   printk("min of the offers : %d\n", min);
				
				if(state_header_vect[0].w < min) {
					state_header_vect[0].claim = state_header_vect[0].w;
				}
				else {
					state_header_vect[0].claim = min;
				}

				//check if someone have closed
				bottlenecked=0;
				for (i=0; i<N; i++) {
					if (state_header_vect[i].closed==1) {
						bottlenecked=1;
						break;
					}
				}
		
				//check if I have closed
				if(state_header_vect[0].claim == state_header_vect[0].w || bottlenecked)
					state_header_vect[0].finished=1;
				else
					state_header_vect[0].finished=0;
				for (i=0; i<N; i++){
					printk ("CLAIM:MAC_ADDR[%d]=%02X:%02X:%02X:%02X:%02X:%02X, claim=%u, w=%u, finished=%u, closed=%u, offer=%u \n",
						i,
						state_header_vect[i].mac[0],
						state_header_vect[i].mac[1],
						state_header_vect[i].mac[2],
						state_header_vect[i].mac[3],
						state_header_vect[i].mac[4],
						state_header_vect[i].mac[5],
						state_header_vect[i].claim*100/UNIT,
						state_header_vect[0].w*100/UNIT,
						state_header_vect[i].finished,
						state_header_vect[i].closed,
						state_header_vect[i].offer*100/UNIT
					);

				}
			
				
			
/*
				if(debug && enable_status_debug){	
					    int i=0;
					    for (i=0; i<N; i++){
						    //printk("mac address : %pM - \n", state_header_vect[i].mac);
						    printk("after -> closed: %d - finished: %d - offer: %d - claim: %d\n", 
								state_header_vect[i].closed, 
								state_header_vect[i].finished, 
								state_header_vect[i].offer*100/UNIT, 
								state_header_vect[i].claim*100/UNIT);
						    //printk("status_auct: %d - status_bidd: %d\n", state_header_vect[i].status_auct, state_header_vect[i].status_bidd);
					    }
					    printk("atlaslog");
					    
					    
					    do_gettimeofday(&t);
					    time_to_tm(t.tv_sec, 0, &broken);
					    //printk(",%d:12:%d:%d:%d:%d", broken.tm_year+1900, broken.tm_mday, broken.tm_hour, broken.tm_min, broken.tm_sec);
					    printk(",%ld:%d:%d:%d:%d:%d", broken.tm_year+1900, broken.tm_mon+1, broken.tm_mday, broken.tm_hour, broken.tm_min, broken.tm_sec);

		    
					    for (i=0; i<N; i++){
						  printk(",%d,%d,%d,%d", state_header_vect[i].closed, state_header_vect[i].finished, state_header_vect[i].offer*100/UNIT, state_header_vect[i].claim*100/UNIT);
					    }
					    printk("\n");
					    printk("*********************************\n");	

				}
				
*/
				
			}
		
		}
			//************************************
			// END ALGORITHM 3 BIDDER FOR NODE
			//************************************
	
  
}
void set_ac(int aCWmin, int aCWmax){
	int ac,result;
	//int aCWmin=15;
	//int aCWmax=1023;
	struct ieee80211_txq_params txq_params;
	printk("atlaslog_func: %s \n",__func__);

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {

		if (ac==IEEE80211_AC_BE){
			txq_params.ac = ac;
			txq_params.txop = 0;
			txq_params.cwmin = aCWmin;
			txq_params.cwmax = aCWmax;
			txq_params.aifs = 3;
			result = rdev_set_txq_params(rdev, netdev, &txq_params);
		}
		if (ac==IEEE80211_AC_VO){
			txq_params.ac = ac;
			txq_params.txop = 0;
			txq_params.cwmin = aCWmin;
			txq_params.cwmax = aCWmax;
			txq_params.aifs = 2;
			result = rdev_set_txq_params(rdev, netdev, &txq_params);
		}
	}

/*
	//WARNING: if use this code module crashes!
	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		//aCWmax = 1023;
		//aCWmin = 15;
	  
		switch (ac) {
		case IEEE80211_AC_BK:
			txq_params.cwmax = aCWmax;
			txq_params.cwmin = aCWmin;
			txq_params.txop = 0;
			txq_params.aifs = 7;
			break;
		default:
		case IEEE80211_AC_BE:
			txq_params.cwmax = aCWmax;
			txq_params.cwmin = aCWmin;
			txq_params.txop = 0;
			txq_params.aifs = 3;
			break;
		case IEEE80211_AC_VI:
			//txq_params.cwmax = aCWmin;
			//txq_params.cwmin = (aCWmin + 1) / 2 - 1;
			txq_params.cwmax = aCWmax;
			txq_params.cwmin = aCWmin;
			//txq_params.txop = 3008/32;
			txq_params.txop = 0;
			txq_params.aifs = 2;
			break;
		case IEEE80211_AC_VO:
			//txq_params.cwmax = (aCWmin + 1) / 2 - 1;
			//txq_params.cwmin = (aCWmin + 1) / 4 - 1;
			txq_params.cwmax = aCWmax;
			txq_params.cwmin = aCWmin;
			//txq_params.txop = 1504/32;
			txq_params.txop = 0;
			txq_params.aifs = 2;
			break;
		}
		result = rdev_set_txq_params(rdev, netdev, &txq_params);
	}
*/
}
void cw_update2(int cw){
	//int ac,result;
	int aCWmin=15;
	int aCWmax=1023;
	//struct ieee80211_txq_params txq_params;
	int alpha=0; //PERCENT!!	
	
	if (iperf_rate==0 && (is_cw_forced<0) && (cw < 0)){
		cw_=1023;
		set_ac(aCWmin,aCWmax);
		return;
	}
	if (cw < 0){
		cw_=1023;
		set_ac(aCWmin,aCWmax);
		return;
	}
	if (cw_ != cw){
				
		cw_ = (alpha * cw_ + (100-alpha) * cw ) / 100;

		if (cw_ > 1023){
			cw_=1023;
		}

		if (cw_ < 15){
			cw_=15;
		}

		aCWmin=cw_;
		aCWmax=cw_;
			
		//printk("updatecw cw=%d,cw_=%d\n",cw,cw_);
		//printk("update_cw is_cw_forced=%d\n",is_cw_forced);
		//printk("update_cw aCWmin=%u, aCWmax=%u\n",aCWmin,aCWmax);

		set_ac(aCWmin,aCWmax);
/*
		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
			//if (ac==2){
				txq_params.ac = ac;
				txq_params.txop = 0;
				txq_params.cwmin = aCWmin;
				txq_params.cwmax = aCWmax;
				txq_params.aifs = 2;
				result = rdev_set_txq_params(rdev, netdev, &txq_params);
			//}else{
			//	continue;
			//}
		}
		//cw_=cw;	
*/			
	}

}
EXPORT_SYMBOL(cw_update2);
//UNUSED!!
void cw_update(void){

  
#define MAX_CW	1023
#define MIN_CW	15			
#define SATURATION_CW 128
#define Tslot 9 //us
#define Payload 1470 //Byte
	int j;
	struct ieee80211_txq_params txq_params;
	int ac, result;
	int active = 0;
	u32 cw=MIN_CW;
	int claim_sum=0;
	uint64_t num;
	uint64_t den;
   	//rwlock_t xxx_lock = __RW_LOCK_UNLOCKED(xxx_lock);

	for (j=0; j<N; j++) {
		if (state_header_vect[j].finished == 1) {
			active++;
		}
	}
	
	if( active == N ){
		printk("================= UPDATE CW ================\n");
		for (j=0; j<N; j++){
			claim_sum+=state_header_vect[j].claim;
		}
		
		printk("claim_sum=%d\n",claim_sum);
		printk("state_header_vect[0].claim=%d\n",state_header_vect[0].claim);
/*
		if (state_header_vect[0].claim == 0)
			return;

		if (claim_sum==UNIT){ //saturation
			cw=128;
		}else{
			cw=2*txT/state_header_vect[0].claim*(UNIT-claim_sum)/Tslot;
		}

		if (cw > MAX_CW)
			cw=MAX_CW;
		if (cw < MIN_CW)
			cw=MIN_CW;
*/
		//cw= 2*(Payload./(R/1e3.*claim)-TxTime)./Tslot;
		
		if (state_header_vect[0].claim == 0)
			return;
		
		num=(uint64_t)( 2ULL*( (uint64_t)(Payload)*8ULL*1000ULL*(uint64_t)(UNIT)-(uint64_t)(txT)*((uint64_t)(MAX_THR)*(uint64_t)(state_header_vect[0].claim)) ) );
//		num=2ULL*(1470ULL*8ULL*1000ULL*252ULL-2160ULL*5300ULL*95ULL);
		den=(long long) ( (long long)Tslot*(long long)MAX_THR*(long long)state_header_vect[0].claim );
		cw=num/den;
		cw=192;
		printk("cw=%u\n",cw);
		//printk("num=%llu\n",num);
		//printk("den=%llu\n",den);
/*
		if (cw > MAX_CW)
			cw=MAX_CW;
		if (cw < MIN_CW)
			cw=MIN_CW;
*/
		printk("============================================\n");
		if (state_header_vect[0].cw != cw){	
			state_header_vect[0].cw=cw;
			for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
				txq_params.ac = ac;
				txq_params.txop = 0;
				txq_params.cwmin = cw;
				txq_params.cwmax = cw;
				txq_params.aifs = 2;
		
			//write_lock_irqsave(&xxx_lock, netdev);
				//.. read and write exclusive access to the info ...
			result = rdev_set_txq_params(rdev, netdev, &txq_params);
			
			//write_unlock_irqrestore(&xxx_lock, netdev);
			}
		} else {
			printk("cw is the same, no update\n");
		}

// 			   if(( (last_claim != state_header_vect[0].claim)) || (current_survey->read==0) ){
// 				    
// 				 
// 				
// 				    //update contentin window
// 
// 				    last_claim = (u32) state_header_vect[0].claim;
// 				    current_survey->read = 1;
// 			  
// 				    u32 current_survey_diff_time = ((current_survey->diff_time.tv_sec * 1000000) + (current_survey->diff_time.tv_nsec / 1000)); 	//[us]
// 				    
// 				    u32 time_length = 3000;
// 				    u32 claim_time = (last_claim * current_survey_diff_time) / UNIT ;
// 				    u32 w_n = 0;
// 				    u32 claim_num_frame = 0;
// 				    u32 cw = 0;
// 				    u32 time_slot_length = 9;
// 
// 				    
// 				    if(claim_time != 0){
// 					  if ( (current_survey->ratio_busy_scale + claim_time) < current_survey_diff_time){
// 					      w_n = current_survey_diff_time - current_survey->ratio_busy_scale - claim_time;
// 					      claim_num_frame = ( claim_time / time_length );
// 					      cw = (w_n * 2) / (time_slot_length * claim_num_frame);
// 					      
// 					      if(cw>MAX_CW)
// 						cw=MAX_CW;
// 					      
// 					  }
// 					  else
// 					      cw = SATURATION_CW;
// 				    }
// 				    else
// 					cw = MAX_CW;
// 
// 				    				    
// 				    printk("ack_good_count : %u -- ack_bad_count %u \n", current_survey->frame_ack_good_count, current_survey->frame_ack_bad_count);
// 				    printk("claim %u - claim_time : %u -- busy %u -- diff_time_survey %u\n", last_claim, claim_time, current_survey->ratio_busy_scale, current_survey_diff_time);
// 				    printk("w_n : %u -- cw %u\n",  w_n, cw);
// 
// 				    
// 				    
// 				    /*u32 w_1=0;
// 				    u32 w_2=0;
// 				    u32 w_3=0;
// 				    *///u32 t_a = (2.2 * SCALE);
// 				    //t_a = t_a / current_survey_diff_time; 	//[1 : 1000000] //2,2 is length packet at 6Mbps [1470byte]
// 				    //u32 s_a = (last_claim * SCALE) / UNIT; 	//[1 : 1000000]
// 				    //u32 s_a = (last_claim * current_survey_diff_time) / UNIT; 	//time activiti
// 				    
// // 				    u32 cw;
// // 				    if (s_a == 0)
// // 					cw = MAX_CW;
// // 				    else{  
// // 				      
// // 					u32 framt_total_count = current_survey->frame_ack_good_count + current_survey->frame_ack_bad_count;
// // 					u32 p_success =
// // 				      
// // 					w_1 = ((current_survey->frame_ack_good_count * t_a) * SCALE) / s_a; 	//[1 : 10000]
// // 					
// // 					if(w_1 > SCALE)
// // 					  w_1 = SCALE;
// // 					
// // 					w_2 = ((current_survey->frame_ack_good_count + current_survey->frame_ack_bad_count) * t_a) ;
// // 					w_3 = current_survey->ratio_busy_scale;
// // 					
// // 					if( (w_2+w_3) > w_1){ 
// // 					  int i;
// // 					  int total_other_claim = 0;
// // 					  for(i=1; i<N; i++){
// // 						total_other_claim = total_other_claim + state_header_vect[i].claim;
// // 					  }
// // 					  
// // 					  if(total_other_claim < 40)
// // 					    cw=MIN_CW;
// // 					  else
// // 					    cw=MAX_CW;
// // 					}
// // 					else{
// // 					    cw = ( w_1 - w_2 - w_3); //2 * ( w_1 - w_2 - w_3)
// // 					    cw = (cw * MAX_CW) / SCALE;
// // 					}
// // 				    }
// 				    
// 				    	
//  				    struct ieee80211_txq_params txq_params;
// 				    int ac, result;
// 				    for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
// 					    txq_params.ac = ac;
// 					    txq_params.txop = 0;
// 					    txq_params.cwmin = cw;
// 					    txq_params.cwmax = cw;
// 					    txq_params.aifs = 2;
// 					    result = rdev_set_txq_params(rdev, netdev, &txq_params);
// 				    }
// // 				    printk("current_survey->frame_ack_good_count : %u -- current_survey->frame_ack_bad_count %u \n", current_survey->frame_ack_good_count, current_survey->frame_ack_bad_count);
// // 				    printk("claim : %u -- busy %u -- diff_time_survey %u -- t_a %u\n", s_a, current_survey->ratio_busy_scale, current_survey_diff_time, t_a);
// // 				    printk("w_1 : %u -- w_2 %u -- w_3 %u -- cw %u\n",  w_1, w_2, w_3, cw);
// 			  }
	}	
			
			
			
			
  
  
}


int atlas_algorithm(struct atlas_header *atlas_header_point, int tx_rx, u8 * src, struct sk_buff *skb, int queue_depth, struct state_survey *current_survey) {
	
	int ret;
  
	struct timeval t;
	struct tm broken;
 	 
    
//use ip header and icmp header to intercept specific frames, not useful yet...

	struct icmphdr *icmph;
	struct iphdr *iph;
	bool is_icmp=false;
	char source[16];
	char dest[16];
	icmph = icmp_hdr(skb);
	iph = ip_hdr(skb);
	if(iph->protocol == IPPROTO_ICMP){
		is_icmp=true;
		icmp_count++;
		snprintf(source, 16, "%pI4", &iph->saddr);
		snprintf(dest, 16, "%pI4", &iph->daddr);
	}

	is_icmp=true;
	//if(start_up == 0 && tx_rx == 0 && is_icmp){
	if(start_up == 0 && tx_rx == 0){
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
	  
	  
	  memcpy(state_header_vect[0].mac, src, ETH_ALEN);

	  //obtain dev device
	  netdev = skb->dev;
	  if (netdev) {
		rdev = wiphy_to_dev(netdev->ieee80211_ptr->wiphy);
	  }
	  
	  //init timer for wait packet
	  setup_timer( &wait_packet_timer, packet_timer_callback, 0 );
	  
	  printk( "Starting timer to fire in 1s (%ld)\n", jiffies );
	  ret = mod_timer( &wait_packet_timer, jiffies + msecs_to_jiffies(INTERVAL_W_NO_UPDATE) );
	  if (ret) printk("Error in mod_timer\n");
	  
	  start_up = 1;
	
	}
	else {




	  //TX frame
	  //if (tx_rx == 0 && start_up == 1 && is_icmp){
	  if (tx_rx == 0 && start_up == 1 ){

		long rate = 0;
		//long divider_data = 45;
		//long divider_management = 4;
		int pkt_max=1;
		//int threshold = DELTA_W;
		int i;
		
		//restart timer for next 
		ret = mod_timer_pinned( &wait_packet_timer, jiffies + msecs_to_jiffies(INTERVAL_W_NO_UPDATE) );

		rate = min((long)( UNIT ),(long)( (iperf_rate*UNIT)/MAX_THR) );

		//if ( cont_frame_management > pkt_max ) {
			cont_frame_management=0;
			if(state_header_vect[0].w != rate)  {
			    //restart algorithm
			    state_header_vect[0].finished=0;
			    state_header_vect[0].closed=0;
			    //state_header_vect[0].offer = UNIT;

			    if (N>0)
				state_header_vect[0].offer =  UNIT/N;
			    else
				state_header_vect[0].offer =  UNIT;

			    state_header_vect[0].claim = 0;
	  
			    for(i=1; i<N; i++){
				state_header_vect[i].claim = 0;
			    }
			    
			    update_claim_offer = 0;
			    yet_update_claim_offer=0;
			    state_header_vect[0].w = rate;
			}
		        atlas_offer();		
			atlas_claim();

		//}else{
		//if (cont_frame_management <= pkt_max)
		//	cont_frame_management++;
		//}


		do_gettimeofday(&t);
		time_to_tm(t.tv_sec, 0, &broken);
		
		//cw update
		atlas_rate_limiter();	
		atlas_header_point->offer = state_header_vect[0].offer;
		atlas_header_point->claim = state_header_vect[0].claim;
		atlas_header_point->closed = state_header_vect[0].closed;
		atlas_header_point->finished = state_header_vect[0].finished;
		
		//state_header_vect[0].reserved++;
		//atlas_header_point->reserved = state_header_vect[0].reserved;
		atlas_header_point->reserved = 0;
		
		if(yet_update_claim_offer==1){
			update_claim_offer++;
			yet_update_claim_offer=0;
		}
	
	}

	//RX frame	

	if (tx_rx == 1 && start_up == 1){
	 
		int i = 0;
		int j = 0; 
		int count;
	   
		// ***************************
		//search if the receive station is present
		//and update claim
		// **************************
		count=0;
		for (i=0; i<N; i++){
		      //found station, update value
		      if (!memcmp(state_header_vect[i].mac, src, ETH_ALEN)){
				  
			
			  if(  state_header_vect[i].finished && !atlas_header_point->finished){
			   
			    //if(  state_header_vect[i].closed && !atlas_header_point->closed){
			    	  
				  state_header_vect[0].finished=0;
				  //state_header_vect[0].closed=0;
				  //state_header_vect[0].offer = UNIT;
				  //state_header_vect[0].claim = 0;
	  
				  for(j=0; j<N; j++){
				      state_header_vect[j].claim = 0;
				  }
			    
				  update_claim_offer = 0;
				  yet_update_claim_offer=0;
			    //}
			  }
			  
			  state_header_vect[i].closed = atlas_header_point->closed;
			  state_header_vect[i].finished = atlas_header_point->finished;
			  state_header_vect[i].claim = atlas_header_point->claim;
			  state_header_vect[i].offer = atlas_header_point->offer;
			  
			  //state_header_vect[i].status_auct = 1;
			  state_header_vect[i].status_bidd = 1;
			  
			  break;
		      }
		      count++;
		}  
		
		
		
		
		//printk("count: %d\n", count);
		//if station don't found, we insert new station and set claim
		if (count == N){
			memcpy(state_header_vect[N].mac, src, ETH_ALEN);
			state_header_vect[N].closed = atlas_header_point->closed;
			state_header_vect[N].finished = atlas_header_point->finished;
			state_header_vect[N].offer = atlas_header_point->offer;
			state_header_vect[N].claim = atlas_header_point->claim;
			//state_header_vect[N].reserved = atlas_header_point->reserved;

			//state_header_vect[N].status_auct = 1;
			state_header_vect[N].status_bidd = 1;
			N++;

			// update my offer with new node number
			// int
				
			//printk("init my offer value: %d\n", state_header_vect[0].offer);
		}
	} 
	

  }
	
      return 0;
}
	
EXPORT_SYMBOL(atlas_algorithm);

int atlas_rate_limiter(void){
	uint8_t claim,finished;
	uint64_t dt=0;
	struct timespec t_diff;	
	uint64_t  T = 1*1000*1000LLU; //usec
	struct timeval t;
	struct tm broken;
	unsigned int E_T=0;
	int cw=cw_;
	int i=0;

	//atlas rate limiter
	if (!is_timer_started){
		is_timer_started=true;
		getnstimeofday(&t0);
		//psucc=ath_psucc;
	}

	getnstimeofday(&t1); 


	t_diff = timespec_sub(t1, t0);
	dt=(long long)t_diff.tv_sec*1000000+(long long)t_diff.tv_nsec/1000;

	if ( (dt >= T) ){ 
		
		delta_time=dt;
		busy_perc_tot=busy;
		//busy_perc_tot=(busy_perc_tot*(unsigned long)alpha + (100LLU-(unsigned long)alpha)*(vbusy_cts+ath_busy_perc))/100LLU;
		if ( busy_perc_tot >=100 )	
			busy_perc_tot=100LLU;
		diff_data_count = data_count - data_count_ ;
		diff_icmp_count = icmp_count - icmp_count_ ;
		diff_rts_count = rts_count - rts_count_;

		data_count_=data_count;
		icmp_count_=icmp_count;
		rts_count_=rts_count;
		
	}

	claim = state_header_vect[0].claim;
	finished = state_header_vect[0].finished;

	if (diff_rts_count !=0)
		psucc = (u64)(100 * diff_data_count/diff_rts_count);

	if (finished==1){	
// SOLUTION 2:
// BUSYTIME PREDICTOR SOLUTION

		//if (diff_data_count-diff_icmp_count>=0)
			//TODO: compute duration of ICMP and add this time...
		//	busytx2_data=(2198)*(diff_data_count-diff_icmp_count)+(323)*(diff_icmp_count);
		if (diff_data_count-diff_icmp_count >= 0)
			busytx2_data=(2198)*(diff_data_count-diff_icmp_count);
		if (diff_rts_count-diff_icmp_count >= 0)
			busytx2_rts=(81)*(diff_rts_count-diff_icmp_count);
		
		//busytx2_data=(tdata)*diff_data_count;
		//busytx2_rts=(trts)*diff_rts_count;
		busytx2 = busytx2_data + busytx2_rts;

		if (cw_<0)
			freeze2 = (dt - busytx2); //how long the backoff has been frozen;
		else
			freeze2 = (dt - busytx2 - cw_/2*tslot*diff_rts_count); //how long the backoff has been frozen;
					
		if (diff_rts_count !=0){
			avg_tx=busytx2/diff_rts_count;
		}else{
			avg_tx=0;
		}

		gross_rate = CLAIM_CAPACITY*state_header_vect[0].claim/UNIT;

		if (diff_rts_count !=0)
			tx_goal = dt*gross_rate*1e-2/(avg_tx);

		if (iperf_rate==0){
			cw=-1;
			freeze2 = (dt - busytx2); //how long the backoff has been frozen;
		}

		if ((int64_t)dt-(int64_t)avg_tx*(int64_t)diff_rts_count!=0)
			freeze_predict = (int64_t)freeze2 * ((int64_t)dt-(int64_t)tx_goal*(int64_t)avg_tx) /((int64_t)dt-(int64_t)avg_tx*(int64_t)diff_rts_count);
		if (gross_rate !=0)
			I = (int64_t)busytx2/(int64_t)gross_rate; //averge transmission cycle for getting gross_rate;
		if (tx_goal !=0){
			if (iperf_rate==0){
				cw=-1;
			}else{
				//cw= 2LLU/(u64)tslot* ((u64)dt-(u64)tx_goal*(u64)avg_tx-(u64)freeze_predict)/(u64)tx_goal;
				cw=2LLU*((int64_t)dt-(int64_t)tx_goal*(int64_t)avg_tx-(int64_t)freeze_predict)/((int64_t)tx_goal * (int64_t)tslot );
			}
		}
// END BUSYTIME PREDICTOR SOLUTION
		if (claim!=0){

// standard DCF, NO RTS/CTS
//			D = (100-perc_busy)*( txT )*UNIT/(100*claim);
//			cw = 2*( D - (txT) ) / tslot;

// COMPUTE cw

// SOLUTION 1:
// REAL BUSYTIME ESTIMATION SOLUTION
/*
			busy_perc=busy_perc_tot;
			psucc=ath_psucc;
			E_T= DIFS + trts + psucc*(tcts+tdata+tack+ (3*SIFS) )/100;



			claim = CLAIM_CAPACITY*claim/UNIT;


			cw = 2* (int)E_T * ( 100*(100-(int)busy_perc)/(claim) - 100  ) /((int)tslot*100);

			//printk(" algo_debug: E_T=%u, cw=%d,busy_perc=%llu,claim=%u,psucc=%lu\n",E_T,cw,busy_perc,claim,psucc);
			

			if (cw < 0){
				//cw=cw_ + cw;
				cw=cw_;
				
			}

			if (cw > 1023)
				cw=1023;
			if (cw < 15)
				cw=15;


*/
			channel_stats_enable=0;





		}else{
			D=-1;
			D2=-1;
			cw=-1;
			//channel_stats_enable=1;
				
		}
	}

	if( dt >= T ){
		do_gettimeofday(&t);
		time_to_tm(t.tv_sec, 0, &broken);

		for (i=0; i<N; i++) {
			if (state_header_vect[i].finished == 1) {
				channel_stats_enable=0;
			}else{
				cw=-1;
				channel_stats_enable=1;
				break;
			}


		}

		printk("atlaslog_predictor : %ld:%d:%d:%d:%d:%d,dt=%lld,busytx2=%lld,freeze2=%lld,avg_tx=%lld,avg_tx_data=%lld,avg_tx_rts=%lld,gross_rate=%lld,tx_goal=%lld,freeze_predict=%lld,I=%lld,cw=%d,cw_=%d,diff_rts_count=%lld,diff_data_count=%lld,delta_count=%lld,icmp_count=%lld \n",
			broken.tm_year+1900, broken.tm_mon+1, broken.tm_mday, broken.tm_hour, broken.tm_min, broken.tm_sec,
			dt,busytx2,freeze2,avg_tx,avg_tx_data,avg_tx_rts,gross_rate,tx_goal,freeze_predict,I,cw,cw_,diff_rts_count,diff_data_count,diff_rts_count-diff_data_count,diff_icmp_count);

		printk("atlaslog_cw,%ld:%d:%d:%d:%d:%d,%llu,%u,%u,%u,%d,%llu,%llu,%u,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%d,%d,%llu,%llu\n",
		broken.tm_year+1900, broken.tm_mon+1, broken.tm_mday, broken.tm_hour, broken.tm_min, broken.tm_sec,
		delta_time,
		100*state_header_vect[0].claim/252,     //u
		state_header_vect[0].finished,          //u
		state_header_vect[0].closed,            //u	
		cw_,
		busy_perc_tot,
		busy_perc,
		psucc,
		busytx2,freeze2,avg_tx,gross_rate,tx_goal,freeze_predict,I,cw,cw_,diff_rts_count,diff_data_count
		);
		printk("atlaslog_update, N=%d channel_stats_enable=%d\n",N,channel_stats_enable);


		if (D2 > 0)	
			udelay(D2);
		//if ( channel_stats_enable==1 ){
		//	cw=-1;
		//	cw_update2(cw);
		//}
		if ( (is_cw_forced < 0) && (channel_stats_enable==0) ){
			cw_update2(cw);
		}
		t0=t1;

	}
	return D;
}
