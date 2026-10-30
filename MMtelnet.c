/***********************************************************************************************************************
PicoMite MMBasic

custom.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved. 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: 
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed 
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed 
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software 
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 

************************************************************************************************************************/
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
//#define DEBUG_printf printf
#define DEBUG_printf
static char Telnetbuff[256]={0};
static int Telnetpos=0;
/* Receive state must survive arbitrary TCP packet boundaries. */
static unsigned char telnet_rx_state;
static int telnet_lastchar = -1;
static const uint8_t telnet_init_options[] =
{
//    TELNET_CHAR_IAC, TELNET_CHAR_WILL, TELNET_OPT_SUPPRESS_GO_AHEAD,
255,251,3,
//    TELNET_CHAR_IAC, TELNET_CHAR_DO, TELNET_OPT_SUPPRESS_GO_AHEAD,
255,253,3,
//    TELNET_CHAR_IAC, TELNET_CHAR_WILL, TELNET_OPT_ECHO,
255,251,1,
//
255,253,34,
255,254,34,

0
};

static err_t tcp_telnet_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
//    DEBUG_printf("telnet_server_sent %u\n", len);
    if (state && state->telnet_pcb_no >= 0 && state->telnet_pcb_no < MaxPcb)
        state->sent_len[state->telnet_pcb_no] = len;
    return ERR_OK;
}
void TelnetPutCommand(int command, int option){
        if (Telnetpos > sizeof(Telnetbuff) - 3) TelnetPutC(0, -1);
        if (Telnetpos > sizeof(Telnetbuff) - 3) return;
        Telnetbuff[Telnetpos]=255;
        Telnetpos++;
        Telnetbuff[Telnetpos]=command;
        Telnetpos++;
        if(option){
                Telnetbuff[Telnetpos]=option;
                Telnetpos++;
        }
        if (Telnetpos >= sizeof(Telnetbuff) - 4) TelnetPutC(0, -1);
}
void __not_in_flash_func(TelnetPutC)(int c,int flush){
        TCP_SERVER_T *state = (TCP_SERVER_T*)TCPstate;
        if (!state || state->telnet_pcb_no < 0 || state->telnet_pcb_no >= MaxPcb || !WIFIconnected) return;
        if(!(flush==-1)){
                Telnetbuff[Telnetpos]=c;
                Telnetpos++;
                if(c==255){
                        Telnetbuff[Telnetpos]=c;
                        Telnetpos++;
                }
                if(c==13){
                        Telnetbuff[Telnetpos]=0;
                        Telnetpos++;
                }
        }
        if(Telnetpos>=sizeof(Telnetbuff)-4 || (flush==-1 && Telnetpos)){
                int pcb=state->telnet_pcb_no;
                state->to_send[pcb]=Telnetpos;
                state->buffer_sent[state->telnet_pcb_no]=(uint8_t *)Telnetbuff;
                if(state->client_pcb[pcb]){
//                        cyw43_arch_lwip_check();
                        tcp_server_send_data(state, state->client_pcb[pcb], pcb);
                }
                Telnetpos=0;
        }
}
static void telnet_receive_char(unsigned char c) {
    if (telnet_lastchar == 13 && c == 0) {
        telnet_lastchar = -1;
        return;
    }
    if (BreakKey && c == BreakKey) {
        MMAbort = true;
        ConsoleRxBufHead = ConsoleRxBufTail;
    } else if (c == keyselect && KeyInterrupt != NULL) {
        Keycomplete = 1;
    } else {
        ConsoleRxBuf[ConsoleRxBufHead] = c;
        ConsoleRxBufHead = (ConsoleRxBufHead + 1) % CONSOLE_RX_BUF_SIZE;
        if (ConsoleRxBufHead == ConsoleRxBufTail)
            ConsoleRxBufTail = (ConsoleRxBufTail + 1) % CONSOLE_RX_BUF_SIZE;
    }
    telnet_lastchar = c;
}

err_t tcp_telnet_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T *)arg;
    (void)err;
    if (!p) {
        int pcb = state->telnet_pcb_no;
        if (pcb >= 0 && pcb < MaxPcb && state->client_pcb[pcb] == tpcb)
            return tcp_server_close(state, pcb);
        return ERR_OK;
    }
    tcp_recved(tpcb, p->tot_len);
    for (unsigned j = 0; j < p->tot_len; j++) {
        unsigned char c = pbuf_get_at(p, j);
        switch (telnet_rx_state) {
            case 0: /* data */
                if (c == 255) telnet_rx_state = 1;
                else telnet_receive_char(c);
                break;
            case 1: /* IAC */
                telnet_rx_state = 0;
                if (c == 255) telnet_receive_char(c);
                else if (c >= 251 && c <= 254) telnet_rx_state = 2;
                else if (c == 250) telnet_rx_state = 3;
                else if ((c == 244 || c == 243) && BreakKey) telnet_receive_char(BreakKey);
                break;
            case 2: /* WILL/WONT/DO/DONT option */
                telnet_rx_state = 0;
                break;
            case 3: /* subnegotiation */
                if (c == 255) telnet_rx_state = 4;
                break;
            case 4: /* IAC inside subnegotiation */
                telnet_rx_state = (c == 240) ? 0 : 3;
                break;
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

void tcp_telnet_err(void *arg, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T *)arg;
    (void)err;
    if (state) tcp_server_forget(state, state->telnet_pcb_no);
}

/*static err_t tcp_telnet_poll(void *arg, struct tcp_pcb *tpcb) {
        TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
//        cyw43_arch_lwip_check();
        int i=0;
        while(state->client_pcb[i]!=tpcb && i<=MaxPcb)i++;
        if(i==MaxPcb)error("Internal TCP receive error");
        state->write_pcb=i;
        DEBUG_printf("tcp_server_poll_fn\r\n");
        return tcp_server_close(argstate->telnet_pcb_no); // no activity so close the connection
}*/

err_t starttelnet(struct tcp_pcb *client_pcb, int pcb, void *arg){
        TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
//        DEBUG_printf("Telnet Client connected %x on pcb %d\r\n",(uint32_t)client_pcb,pcb);
        tcp_arg(client_pcb, state);
        tcp_sent(client_pcb, tcp_telnet_sent);
        tcp_recv(client_pcb, tcp_telnet_recv);
        tcp_err(client_pcb, tcp_telnet_err);
        state->telnet_pcb_no=pcb;
        Telnetpos = 0;
        telnet_rx_state = 0;
        telnet_lastchar = -1;
        state->keepalive[pcb]=1;
        err_t err = tcp_write(client_pcb, telnet_init_options, sizeof(telnet_init_options) - 1, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
                return tcp_server_close(state,pcb) == ERR_ABRT ? ERR_ABRT : ERR_OK;
        }
        return ERR_OK;
}
