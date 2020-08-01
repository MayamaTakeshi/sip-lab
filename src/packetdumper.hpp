#ifndef __TCPDUMPER_H__
#define __TCPDUMPER_H__

#include <pcap.h>

#include <string>
#include <pthread.h>
#include "boost/bind.hpp"

#include <set>
#include <map>

using namespace std;

class PacketDumper
{
public:
	PacketDumper();
	~PacketDumper();
	bool init(string dev, string file);
	bool add_endpoint(uint32_t ipaddr, uint16_t port);
	bool remove_endpoint(uint32_t ipaddr, uint16_t port);
	void process();
	pthread_mutex_t _mutex;
	bool _end_flag;
	string _dev;
	pcap_t *_handler;
	pcap_dumper_t *_dumper;	
private:
	string _filter;
	bpf_program _filter_program;
	pthread_t _thread;
	int datalink;
	typedef set<uint16_t> port_set;
	typedef map <uint32_t, port_set> ipaddr_map;
	ipaddr_map _awaited_endpoints; 
};

static void *thread_func(void *vptr_args);

struct dlt_linux_sll {
	u_short packet_type;
	u_short ARPHRD;
	u_short slink_length;
	u_char bytes[8];
	u_short ether_type;
};

struct ip_header {
	u_char ip_version_headerlength; // version << 4 | header length >> 2
#define IP_HL(ip)	(((ip)->ip_version_headerlength) & 0x0f)
#define IP_V(ip)	(((ip)->ip_version_headerlength) >> 4)
	u_char ip_tos; //Type of service
	u_short ip_len; // total length
	u_short ip_id; // identification
	u_short ip_off; //fragment offset field
	
	u_char ip_ttl;
	u_char ip_p;
	u_short ip_sum;
	u_int ip_src;
	u_int ip_dst;
};



#endif
