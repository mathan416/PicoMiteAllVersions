static void reset(void) {
    memset(&storage,0,sizeof(storage)); storage.telnet_pcb_no=99;
    Telnetpos=telnet_rx_state=0; telnet_lastchar=-1;
    writes=aborts=closes=force_close_failure=force_write_failure=0;
    ConsoleRxBufHead=ConsoleRxBufTail=MMAbort=0;
    poll_hook=NULL; queue_busy=0;
}
static void console(struct tcp_pcb *pcb) {
    memset(pcb,0,sizeof(*pcb)); pcb->local_port=23;
    assert(tcp_server_accept(TCPstate,pcb,ERR_OK)==ERR_OK);
}
static void receive(struct tcp_pcb *pcb,const uint8_t *bytes,unsigned length) {
    struct pbuf p={length,length,bytes,NULL};
    assert(tcp_telnet_recv(TCPstate,pcb,&p,ERR_OK)==ERR_OK);
}
static void output_copy(void) {
    reset(); struct tcp_pcb pcb; console(&pcb); writes=0;
    for(unsigned i=0;i<252;i++)TelnetPutC('A',0);
    for(unsigned i=0;i<252;i++)TelnetPutC('B',0);
    assert(writes==2 && write_lengths[0]==252 && write_lengths[1]==252);
    assert(write_flags[0]&TCP_WRITE_FLAG_COPY);
    for(unsigned i=0;i<252;i++)assert(write_data[0][i]=='A' && write_data[1][i]=='B');
    puts("PASS: transmit buffers retain data until lwIP owns a copy");
}
static void reconnect(void) {
    reset();
    for(unsigned i=0;i<40;i++) {
        struct tcp_pcb pcb; console(&pcb);
        assert(tcp_telnet_recv(TCPstate,&pcb,NULL,ERR_OK)==ERR_OK);
        assert(storage.telnet_pcb_no==99 && storage.client_pcb[0]==NULL);
    }
    puts("PASS: 40 orderly disconnect/reconnect cycles release slots");
}
static void capacity(void) {
    reset(); struct tcp_pcb occupied[MaxPcb],extra={.local_port=80};
    for(unsigned i=0;i<MaxPcb;i++)storage.client_pcb[i]=&occupied[i];
    TCP_SERVER_T before=storage;
    assert(tcp_server_accept(TCPstate,&extra,ERR_OK)==ERR_ABRT);
    assert(aborts==1 && !memcmp(&before,&storage,sizeof(storage)));
    puts("PASS: exhausted connection table is rejected without out-of-bounds access");
}
static void exclusive_console(void) {
    reset(); struct tcp_pcb first,second={.local_port=23}; console(&first);
    assert(tcp_server_accept(TCPstate,&second,ERR_OK)==ERR_ABRT);
    assert(storage.client_pcb[0]==&first && storage.telnet_pcb_no==0 && !first.dead);
    first.dead=1; /* lwIP frees the PCB before calling tcp_err. */
    first.error(first.arg,-14);
    assert(storage.client_pcb[0]==NULL && storage.telnet_pcb_no==99 && closes==0);
    puts("PASS: second console is rejected and error cleanup never touches freed PCB");
}
static void error_cleanup(void) {
    reset(); struct tcp_pcb pcb={.local_port=80,.dead=1}; storage.client_pcb[3]=&pcb;
    storage.buffer_recv[3]=malloc(8); storage.keepalive[3]=1;
    tcp_server_err3(TCPstate,-14);
    assert(storage.client_pcb[3]==NULL && storage.buffer_recv[3]==NULL && !storage.keepalive[3]);
    assert(closes==0 && aborts==0);
    struct tcp_pcb console_pcb; console(&console_pcb); force_close_failure=1;
    assert(tcp_telnet_recv(TCPstate,&console_pcb,NULL,0)==ERR_ABRT);
    assert(storage.telnet_pcb_no==99 && storage.client_pcb[0]==NULL && aborts==1);
    puts("PASS: HTTP error and close-failure paths release state safely");
}
static void packet_boundaries(void) {
    static const uint8_t input[]={255,251,3,'P','R','I','N','T',' ','1',13,0};
    for(unsigned split=0;split<=sizeof(input);split++) {
        reset(); struct tcp_pcb pcb; console(&pcb);
        receive(&pcb,input,split); receive(&pcb,input+split,sizeof(input)-split);
        assert(ConsoleRxBufHead==8 && !memcmp(ConsoleRxBuf,"PRINT 1\r",8));
    }
    reset(); struct tcp_pcb pcb; console(&pcb);
    struct pbuf tail={2,2,(const uint8_t *)"YZ",NULL};
    struct pbuf head={1,3,(const uint8_t *)"X",&tail};
    tcp_telnet_recv(TCPstate,&pcb,&head,0);
    assert(ConsoleRxBufHead==3 && !memcmp(ConsoleRxBuf,"XYZ",3));
    puts("PASS: negotiation plus input survives all packet splits and chained pbufs");
}
static void telnet_commands(void) {
    reset(); struct tcp_pcb pcb; console(&pcb);
    const uint8_t escaped[]={255,255,'A',255,250,34,1,255,255,2,255,240,'B'};
    receive(&pcb,escaped,sizeof(escaped));
    assert(ConsoleRxBufHead==3 && ConsoleRxBuf[0]==255 && ConsoleRxBuf[1]=='A' && ConsoleRxBuf[2]=='B');
    const uint8_t interrupt[]={255,244}; receive(&pcb,interrupt,sizeof(interrupt));
    assert(MMAbort && ConsoleRxBufHead==ConsoleRxBufTail);
    Telnetpos=0; TelnetPutCommand(251,3);
    assert(Telnetpos==3 && (uint8_t)Telnetbuff[0]==255 && (uint8_t)Telnetbuff[1]==251 && Telnetbuff[2]==3);
    puts("PASS: escaped IAC, subnegotiation, interrupt-process, and option encoding");
}
static struct tcp_pcb *disconnecting;
static void disconnect_during_poll(void) {
    disconnecting->dead=1;
    tcp_telnet_err(TCPstate,-14);
    poll_hook=NULL;
}
static void interrupted_send(void) {
    reset(); struct tcp_pcb pcb; console(&pcb);
    disconnecting=&pcb; queue_busy=1; poll_hook=disconnect_during_poll;
    unsigned before=writes;
    storage.buffer_sent[0]=(uint8_t *)"test"; storage.to_send[0]=4;
    assert(tcp_server_send_data(TCPstate,&pcb,0)==ERR_CONN);
    assert(writes==before && storage.client_pcb[0]==NULL);
    puts("PASS: disconnect during transmit polling does not dereference freed PCB");
}
static void command_boundaries(void) {
    for(int position=250;position<256;position++) {
        reset(); struct tcp_pcb pcb; console(&pcb);
        Telnetpos=position; memset(Telnetbuff,'A',sizeof(Telnetbuff));
        TelnetPutCommand(251,3); TelnetPutC('Z',0); TelnetPutC(0,-1);
        assert(Telnetpos==0);
    }
    puts("PASS: Telnet option writes stay within the transmit buffer at its boundary");
}
static void callback_abort_results(void) {
    reset(); struct tcp_pcb initial={.local_port=23};
    force_write_failure=force_close_failure=1;
    assert(tcp_server_accept(TCPstate,&initial,0)==ERR_ABRT);
    assert(storage.telnet_pcb_no==99 && initial.dead);
    reset(); struct tcp_pcb http={.local_port=80}; storage.client_pcb[0]=&http;
    force_close_failure=1;
    assert(tcp_server_recv(TCPstate,&http,NULL,0,0)==ERR_ABRT);
    reset(); http=(struct tcp_pcb){.local_port=80}; storage.client_pcb[0]=&http;
    force_write_failure=force_close_failure=1;
    struct pbuf request={1,1,(const uint8_t *)"x",NULL};
    assert(tcp_server_recv(TCPstate,&http,&request,0,0)==ERR_ABRT);
    puts("PASS: receive/accept callbacks propagate abort after close or initial-write failure");
}
int main(void) {
    output_copy(); reconnect(); capacity(); exclusive_console(); error_cleanup();
    packet_boundaries(); telnet_commands(); interrupted_send(); command_boundaries(); callback_abort_results();
    puts("All WebMite network regression tests passed."); return 0;
}
