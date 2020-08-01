#ifndef __CHAINLINK_H__
#define __CHAINLINK_H__

#include <pjmedia/port.h>

struct chainlink {
	pjmedia_port port;
	pjmedia_port *next;
};

#endif	/* __CHAINLINK_H__ */
