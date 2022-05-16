#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"


static u_char out_buf[NR_BUFS][PKT_LEN];
static u_char in_buf[NR_BUFS][PKT_LEN];
static u_char nbuffered = 0;
static u_char ack_expected = 0;
static u_char next_frame_to_send = 0;

static u_char frame_expected = 0;
static u_char too_far = NR_BUFS;
static boolean arrived[NR_BUFS];

static boolean no_nak = true;

static int phl_ready = 0;

static int between(u_char a, u_char b, u_char c){
    return ((a <= b && b < c) || (c < a && a <= b) || (b < c && c < a));
}

static void put_frame(u_char* frame, int len){
    *(unsigned int*)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

static void send_data_frame(FRAME_KIND fk, u_char frame_seq,u_char ack_seq){
    FRAME frame;
    frame.kind = fk;
    frame.seq = frame_seq;
    frame.ack = ack_seq;
    //(frame_expected + MAX_SEQ) % (MAX_SEQ + 1)
    if(fk == DATA){
        memcpy(frame.data, out_buf[frame_seq % NR_BUFS], PKT_LEN);
        dbg_frame("Send DATA %d with ACK %d, ID %d""\n", frame.seq, frame.ack, *(short*)frame.data);
        put_frame((u_char*)&frame, 3 + PKT_LEN);
        start_timer(frame_seq, DATA_TIMER);
    }
    else if(fk == ACK){
        dbg_frame(L_BLUE"Send ACK %d"NONE"\n", frame.seq);
        put_frame((u_char*)&frame, 2);
    }
    else if(fk == NAK){
        no_nak = false;
        dbg_frame(L_BLUE"Send NAK %d"NONE"\n", frame.seq);
        put_frame((u_char*)&frame, 2);
    }
    stop_ack_timer();
}

static void inc(u_char* seq){
    *seq = (*seq + 1) % (MAX_SEQ + 1); 
}


int main(int argc, char **argv)
{
	int event, arg;
	FRAME f;
	int len = 0;
    memset(arrived,0,sizeof(arrived));

	protocol_init(argc, argv);
	lprintf("Designed by Jianxff, build: " __DATE__ "  "__TIME__"\n");

	enable_network_layer();

    for(;;){
        event = wait_for_event(&arg);

        switch(event){
            case NETWORK_LAYER_READY:
                //dbg_event("Nerwork layer ready:\n");
                get_packet(out_buf[next_frame_to_send % NR_BUFS]);
                ++nbuffered;
                send_data_frame(DATA,next_frame_to_send,(frame_expected + MAX_SEQ )% (MAX_SEQ + 1));
                inc(&next_frame_to_send);
                break;
            case PHYSICAL_LAYER_READY:
                //dbg_event("Physical layer ready:\n");
                phl_ready = 1;
                break;
            case FRAME_RECEIVED:
                len = recv_frame((u_char *)&f, sizeof f);
			    if (len < 5 || crc32((u_char *)&f, len) != 0){
				    dbg_event(L_RED"**** Receiver Error, Bad CRC Checksum"NONE"\n");
                    if(no_nak){
                        send_data_frame(NAK,frame_expected,0);
                    }
                    break;
			    }
                if(f.kind == DATA){
                    dbg_frame(WHITE"Recv DATA %d with "L_GREEN"ACK %d"WHITE", ID %d"NONE"\n", f.seq, f.ack, *(short *)f.data);
                    if(f.seq != frame_expected && no_nak){
                        send_data_frame(NAK,frame_expected,0);
                    }else{
                        start_ack_timer(ACK_TIMER);
                    }
                    if(between(frame_expected,f.seq,too_far) && !arrived[f.seq % NR_BUFS]){
                        arrived[f.seq % NR_BUFS] = true;
                        memcpy(in_buf[f.seq % NR_BUFS],f.data,len - 7);
                        while(arrived[frame_expected % NR_BUFS]){
                            put_packet(in_buf[frame_expected % NR_BUFS],len - 7);
                            no_nak = true;
                            arrived[frame_expected % NR_BUFS] = false;
                            inc(&frame_expected);
                            inc(&too_far);
                            start_ack_timer(ACK_TIMER);
                        }
                    }
                }
                if(f.kind == NAK){
                    dbg_frame(L_PURPLE"Recv NAK %d"NONE"\n", f.seq);
                    if(between(ack_expected,f.seq,next_frame_to_send)){
                        send_data_frame(DATA,f.seq,(frame_expected + MAX_SEQ )% (MAX_SEQ + 1));
                    }
                    break;
                }
                if(f.kind == ACK){
                    dbg_frame(L_GREEN"Recv ACK %d"NONE"\n", f.seq);
                    f.ack = f.seq;
                }
                while(between(ack_expected,f.ack,next_frame_to_send)){
                    --nbuffered;
                    stop_timer(ack_expected);
                    inc(&ack_expected);
                }
                break;
            case DATA_TIMEOUT:
                dbg_event(YELLOW"---- DATA %d timeout"NONE"\n", arg);
                send_data_frame(DATA,arg,(frame_expected + MAX_SEQ )% (MAX_SEQ + 1));
                break;
            case ACK_TIMEOUT:
                dbg_event(YELLOW"---- ACK timeout"NONE"\n");
                send_data_frame(ACK,(frame_expected + MAX_SEQ )% (MAX_SEQ + 1),0);
                break;
        }

        if (nbuffered < NR_BUFS && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
    }
}