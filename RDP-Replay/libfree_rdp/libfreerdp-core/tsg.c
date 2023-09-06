/**
 * FreeRDP: A Remote Desktop Protocol Client
 * RDP Security
 *
 * Copyright 2012 Fujitsu Technology Solutions GmbH - Dmitrij Jasnov <dmitrij.jasnov@ts.fujitsu.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freerdp/utils/sleep.h>
#include <freerdp/utils/stream.h>
#include <freerdp/utils/memory.h>
#include <freerdp/utils/hexdump.h>
#include <freerdp/utils/unicode.h>

/*#include <time.h>
#include <errno.h>
#include <fcntl.h>*/

#include "tsg.h"

boolean tsg_connect(rdpTsg* tsg, const char* hostname, uint16 port)
{
	int status = -1;

	rdpRpch* rpch = tsg->rpch;
	rdpTransport* transport = tsg->transport;

	uint32 length;
	uint8* data;

	if(!rpch_attach(rpch,  transport->tcp_in,  transport->tcp_out,  transport->tls_in,  transport->tls_out))
	{
		printf("rpch_attach failed!\n");
		return false;
	}
	if(!rpch_connect(rpch))
	{
		printf("rpch_connect failed!\n");
		return false;
	}
	uint8 p[108] =	{0x43, 0x56, 0x00, 0x00, 0x43, 0x56, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x52, 0x54, 0x43, 0x56,
					0x04, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00,
					0x8A, 0xE3, 0x13, 0x71, 0x02, 0xF4, 0x36, 0x71, 0x01, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
					0x02, 0x40, 0x28, 0x00, 0xDD, 0x65, 0xE2, 0x44, 0xAF, 0x7D, 0xCD, 0x42, 0x85, 0x60, 0x3C, 0xDB,
					0x6E, 0x7A, 0x27, 0x29, 0x01, 0x00, 0x03, 0x00, 0x04, 0x5D, 0x88, 0x8A, 0xEB, 0x1C, 0xC9, 0x11,
					0x9F, 0xE8, 0x08, 0x00, 0x2B, 0x10, 0x48, 0x60, 0x02, 0x00, 0x00, 0x00};
	status = rpch_write(rpch, p, 108, 1);
	if(status <= 0)
	{
		printf("rpch_write opnum=1 failed!\n");
		return false;
	}

	length = 0x8FFF;
	data = xmalloc(length);
	status = rpch_read(rpch, data, length);
	if(status <= 0)
	{
		printf("rpch_recv failed!\n");
		return false;
	}

	tsg->tunnelContext = xmalloc(16);
	memcpy(tsg->tunnelContext, data+0x91c, 16);

#ifdef WITH_DEBUG_TSG
	printf("TSG tunnelContext:\n");
	freerdp_hexdump(tsg->tunnelContext, 16);
	printf("\n");
#endif

	uint8 p2[112] = {0x00, 0x00, 0x00, 0x00, 0x6A, 0x78, 0xE9, 0xAB, 0x02, 0x90, 0x1C, 0x44, 0x8D, 0x99, 0x29, 0x30,
					0x53, 0x6C, 0x04, 0x33, 0x52, 0x51, 0x00, 0x00, 0x52, 0x51, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00, 0x15, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00,
					0x61, 0x00, 0x62, 0x00, 0x63, 0x00, 0x2D, 0x00, 0x4E, 0x00, 0x48, 0x00, 0x35, 0x00, 0x37, 0x00,
					0x30, 0x00, 0x2E, 0x00, 0x43, 0x00, 0x53, 0x00, 0x4F, 0x00, 0x44, 0x00, 0x2E, 0x00, 0x6C, 0x00,
					0x6F, 0x00, 0x63, 0x00, 0x61, 0x00, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	memcpy(p2+4, tsg->tunnelContext, 16);

	status = rpch_write(rpch, p2, 112, 2);
	if(status <= 0)
	{
		printf("rpch_write opnum=2 failed!\n");
		return false;
	}

	status = rpch_read(rpch, data, length);
	if(status <= 0)
	{
		printf("rpch_recv failed!\n");
		return false;
	}

	uint8 p3[40] = {0x00, 0x00, 0x00, 0x00, 0x6A, 0x78, 0xE9, 0xAB, 0x02, 0x90, 0x1C, 0x44, 0x8D, 0x99, 0x29, 0x30,
					0x53, 0x6C, 0x04, 0x33, 0x01, 0x00, 0x00, 0x00, 0x52, 0x47, 0x00, 0x00, 0x52, 0x47, 0x00, 0x00,
					0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00};
	memcpy(p3+4, tsg->tunnelContext, 16);
	status = rpch_write(rpch, p3, 40, 3);
	if(status <= 0)
	{
		printf("rpch_write opnum=3 failed!\n");
		return false;
	}
	status = -1;

	UNICONV* tsg_uniconv = freerdp_uniconv_new();
	uint32 dest_addr_unic_len;
	uint8* dest_addr_unic = (uint8*)freerdp_uniconv_out(tsg_uniconv, hostname, (size_t*)&dest_addr_unic_len);
	freerdp_uniconv_free(tsg_uniconv);

	uint8 p4[48] = {0x00, 0x00, 0x00, 0x00, 0x6A, 0x78, 0xE9, 0xAB, 0x02, 0x90, 0x1C, 0x44, 0x8D, 0x99, 0x29, 0x30,
					0x53, 0x6C, 0x04, 0x33, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00};

	memcpy(p4+4, tsg->tunnelContext, 16);
	memcpy(p4+38, &port, 2);

	STREAM* s_p4 = stream_new(60+dest_addr_unic_len+2);
	stream_write(s_p4, p4, 48);
	stream_write_uint32(s_p4,(dest_addr_unic_len/2)+1);/*MaximumCount*/
	stream_write_uint32(s_p4,0x00000000);/*Offset*/
	stream_write_uint32(s_p4,(dest_addr_unic_len/2)+1);/*ActualCount*/
	stream_write(s_p4, dest_addr_unic, dest_addr_unic_len);
	stream_write_uint16(s_p4,0x0000);/*unicode zero to terminate hostname string*/

	status = rpch_write(rpch, s_p4->data, s_p4->size, 4);
	if(status <= 0)
	{
		printf("rpch_write opnum=4 failed!\n");
		return false;
	}
	xfree(dest_addr_unic);

	status = rpch_read(rpch, data, length);
	if(status < 0)
	{
		printf("rpch_recv failed!\n");
		return false;
	}

	tsg->channelContext = xmalloc(16);
	memcpy(tsg->channelContext, data+4, 16);

#ifdef WITH_DEBUG_TSG
	printf("TSG channelContext:\n");
	freerdp_hexdump(tsg->channelContext, 16);
	printf("\n");
#endif

	uint8 p5[20] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x00, 0x00, 0x00, 0x00};
	memcpy(p5+4, tsg->channelContext, 16);
	status = rpch_write(rpch, p5, 20, 8);
	if(status <= 0)
	{
		printf("rpch_write opnum=8 failed!\n");
		return false;
	}
	return true;
}

int tsg_write(rdpTsg* tsg, uint8* data, uint32 length)
{
	int status = -1;

	uint16 opnum = 9;
	uint32 tsg_length = length + 16 + 4 + 12 + 8;
	uint32 totalDataBytes = length + 4;

	STREAM* s = stream_new(12);
	stream_write_uint32_be(s,totalDataBytes);
	stream_write_uint32_be(s,0x01);
	stream_write_uint32_be(s,length);
	uint8* tsg_pkg = xmalloc(tsg_length);
	memset(tsg_pkg, 0, 4);
	memcpy(tsg_pkg+4, tsg->channelContext, 16);
	memcpy(tsg_pkg+20, s->data, 12);
	memcpy(tsg_pkg+32, data, length);
	uint8 pp[8] = {0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00};
	memcpy(tsg_pkg+32+length, pp, 8);

	status = rpch_write(tsg->rpch, tsg_pkg, tsg_length, opnum);

	xfree(tsg_pkg);
	stream_free(s);

	if(status <= 0)
	{
		printf("rpch_write failed!\n");
		return -1;
	}

	return length;
}

int tsg_read(rdpTsg* tsg, uint* data, uint32 length)
{
	int status;

	status = rpch_read(tsg->rpch, data, length);

	return status;
}

rdpTsg* tsg_new(rdpSettings* settings)
{
	rdpTsg* tsg;
	tsg = (rdpTsg*) xzalloc(sizeof(rdpTsg));

	tsg->settings = settings;
	tsg->rpch = rpch_new(settings);

	return tsg;
}

void tsg_free(rdpTsg* tsg)
{

}
