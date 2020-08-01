#include <stdio.h>
#include <pcap.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <netinet/udp.h>

#include <pthread.h>

#include "packetdumper.hpp"

#include "boost/mem_fn.hpp"


#define LOCK() pthread_mutex_lock(&_mutex);
#define UNLOCK() pthread_mutex_unlock(&_mutex);

PacketDumper::PacketDumper()
: _handler(0), _dumper(0), _end_flag(false)
{
}

PacketDumper::~PacketDumper()
{ 
	_end_flag = true;
	pthread_join(_thread, NULL);
	//printf("PacketDumper destroyed\n"); 
}

bool PacketDumper::init(string dev, string file) {
	if(_handler){ return false; } // already initialized

	char errbuf[PCAP_ERRBUF_SIZE];

	_dev = dev;

	_handler = pcap_open_live(_dev.c_str(), 2000, 0, 0, errbuf);
	if(!_handler){
		printf("pcap_open_live(): %s\n", errbuf);
		return false;
	}

	this->datalink = pcap_datalink(_handler);
	//printf("pcap_datalink: %d\n",this->datalink);
	if(this->datalink != DLT_EN10MB && this->datalink != DLT_LINUX_SLL) {
		printf("pcap_datalink: unsupported datalink %d\n",this->datalink);
		return false;
	}

	if( pcap_setnonblock(_handler, 1, errbuf) == -1){
		printf("pcap_setnonblock(): %s\n", errbuf);
		return false;
	}

	_dumper = pcap_dump_open(_handler, file.c_str());
	if(!_dumper){
		printf("pcap_dump_open(): %s\n", pcap_geterr(_handler));
		return false;
	}

	bpf_u_int32 mask;
	bpf_u_int32 net;

	//if(pcap_lookupnet(_dev.c_str(), &net, &mask, errbuf) == -1){
	if(pcap_lookupnet(_dev.c_str(), &net, &mask, errbuf) < 0){
		printf("Couldn't get netmask for device %s: %s\n",
				_dev.c_str(), errbuf);
		return false;	
	}

	if( pcap_compile(_handler, &_filter_program, "udp", 0, net) == -1 ) {
		printf("Failed to compile filter: %s\n", pcap_geterr(_handler));
		return false;
	}

	if(pcap_setfilter(_handler, &_filter_program) == -1) {
		printf("Couldn't install filter: %s\n", pcap_geterr(_handler));
		pcap_freecode(&_filter_program);
		return false;
	}

	pthread_mutex_init(&_mutex, NULL);

	if( pthread_create(&_thread, NULL, &thread_func, (void*)this) ){
		printf("pthread_create() failed\n");
		return false;
	}

	//printf("phread_create ok\n");
	return true;
}

bool PacketDumper::add_endpoint(uint32_t ipaddr, uint16_t port){
	LOCK();
	ipaddr_map::iterator iter = _awaited_endpoints.find(ipaddr);
	if( _awaited_endpoints.end() == iter ) {
		//printf("PacketDumper: adding IP MAP %X\n", ipaddr);
		port_set ps;
		ps.insert(port);
		_awaited_endpoints.insert( make_pair(ipaddr, ps) );
	} else {
		iter->second.insert(port);
	}
	//printf("PacketDumper: adding port %X:%d\n", ipaddr, port);
	UNLOCK();
	return true;
}

bool PacketDumper::remove_endpoint(uint32_t ipaddr, uint16_t port){
	bool res = false;
	LOCK();
	ipaddr_map::iterator im_iter = _awaited_endpoints.find(ipaddr);
	if( _awaited_endpoints.end() != im_iter ) {
		port_set::iterator ps_iter = im_iter->second.find(port);
		if( im_iter->second.end() != ps_iter ) {
			im_iter->second.erase(ps_iter);
			//printf("PacketDumper: Removing port %X:%d\n", ipaddr, port); 
			res = true;
		}
		if(0 == im_iter->second.size()) { 
			_awaited_endpoints.erase(im_iter);
			//printf("PacketDumper: Removing IP MAP %X\n", ipaddr);
		}
	}
	UNLOCK();
	return res;
}

void PacketDumper::process(){
	const u_char *packet;
	pcap_pkthdr hdr;
	ip_header* ip_h;	
	udphdr *udp_h;

	packet = pcap_next(_handler, &hdr);
	if(packet)
	{
		bool dumpit;
		if(DLT_EN10MB == this->datalink) {
			ip_h = (ip_header*)(packet + sizeof(ether_header));
			u_int size_ip = IP_HL(ip_h)*4;
			udp_h = (udphdr*)(packet + sizeof(ether_header) + size_ip);
		} else {
			//Linux SLL cooked
			ip_h = (ip_header*)(packet + sizeof(dlt_linux_sll));
			u_int size_ip = IP_HL(ip_h)*4;
			udp_h = (udphdr*)(packet + sizeof(dlt_linux_sll) + size_ip);
		}
		
		
		//if(ip_h->ip_src == 0xb905adca || ip_h->ip_dst == 0xb905adca) {
		//	printf("PacketDumper: packet received src=%X:%d dst=%X:%d\n", ip_h->ip_src, udp_h->source, ip_h->ip_dst, udp_h->dest);
		//}

		ipaddr_map::iterator im_iter;

		//Check packet source
		im_iter = _awaited_endpoints.find( ip_h->ip_src );
		if( im_iter != _awaited_endpoints.end() ) {
			port_set::iterator ps_iter = im_iter->second.find( udp_h->source);
			if( ps_iter != im_iter->second.end() ) {
				pcap_dump((u_char*)_dumper, &hdr, packet);
				if( pcap_dump_flush(_dumper) == -1){
					printf("pcap_dump_flush() error\n");
				}
				return;
			}
		}


		//Check packet dest
		im_iter = _awaited_endpoints.find( ip_h->ip_dst );
		if( im_iter != _awaited_endpoints.end() ) {
			port_set::iterator ps_iter = im_iter->second.find( udp_h->dest);
			if( ps_iter != im_iter->second.end() ) {
				pcap_dump((u_char*)_dumper, &hdr, packet);
				if( pcap_dump_flush(_dumper) == -1){
					printf("pcap_dump_flush() error\n");
				}
				return;
			}
		}


	}
}

static void *thread_func(void *vptr_args){
	//printf("entering thread_func\n");
	PacketDumper *t = (PacketDumper*)vptr_args;
	while(!t->_end_flag){
		usleep(10);
		pthread_mutex_lock(&t->_mutex);
		t->process();
		pthread_mutex_unlock(&t->_mutex);
	}

	if(t->_handler){
		pcap_close(t->_handler);
	}
	if(t->_dumper){
		pcap_dump_close(t->_dumper);
	}
	
	//printf("exiting thread_func\n");
}

int test() {
	PacketDumper pd;
	pd.init("bond1", "x.cap");

	uint32_t ip_host = 0xc0a8581c;
	uint32_t ip_net = htonl(ip_host);
	printf("net:%X\n",ip_net);
	
	uint16_t port_host = 5060;
	uint16_t port_net = htons(port_host);
	printf("net:%X\n",port_net);
	
	pd.add_endpoint( inet_addr("192.168.88.2"), htons(5060) );
	pd.add_endpoint( inet_addr("192.168.88.8"), htons(5060) );
	pd.add_endpoint( inet_addr("192.168.88.3"), htons(5060) );
	pd.add_endpoint( inet_addr("192.168.88.28"), htons(5060) );
	pd.add_endpoint( inet_addr("192.168.88.4"), htons(5060) );
	pd.add_endpoint( inet_addr("192.168.88.66"), htons(5060) );
	pd.add_endpoint( inet_addr("192.168.88.64"), htons(5060) );
	pd.add_endpoint( inet_addr("192.168.88.62"), htons(5060) );

	pd.remove_endpoint( inet_addr("192.168.88.4"), htons(5060) );
	pd.remove_endpoint( inet_addr("192.168.88.66"), htons(5060) );
	for(;;) {
		;
	}
}
