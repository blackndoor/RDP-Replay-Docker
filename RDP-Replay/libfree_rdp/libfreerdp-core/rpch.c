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

#include <openssl/rand.h>

#include "rpch.h"

#define HTTP_STREAM_SIZE 0xFFFF

rdpRpch* rpch_new(rdpSettings* settings)
{
	rdpRpch* rpch;
	rpch = (rdpRpch*) xzalloc(sizeof(rdpRpch));

	rpch->http_in = (rdpRpchHTTP*) xzalloc(sizeof(rdpRpchHTTP));
	rpch->http_out = (rdpRpchHTTP*) xzalloc(sizeof(rdpRpchHTTP));

	rpch->http_in->ntht = ntlmssp_client_new();
	rpch->http_out->ntht = ntlmssp_client_new();
	rpch->http_in->state = RPCH_HTTP_DISCONNECTED;
	rpch->http_out->state = RPCH_HTTP_DISCONNECTED;

	rpch->read_buffer = NULL;
	rpch->write_buffer = NULL;
	rpch->read_buffer_len = 0;
	rpch->write_buffer_len = 0;

	rpch->BytesReceived = 0;
	rpch->AwailableWindow = 0;
	rpch->BytesSent = 0;
	rpch->RecAwailableWindow = 0;

	rpch->settings = settings;

	rpch->ntlmssp = ntlmssp_client_new();

	rpch->call_id = 0;
	return rpch;
}

boolean rpch_attach(rdpRpch* rpch, rdpTcp* tcp_in, rdpTcp* tcp_out, rdpTls* tls_in, rdpTls* tls_out)
{
	rpch->tcp_in = tcp_in;
	rpch->tcp_out = tcp_out;
	rpch->tls_in = tls_in;
	rpch->tls_out = tls_out;

	return true;
}

boolean rpch_out_connect_http(rdpRpch* rpch)
{
	rdpTls* tls_out = rpch->tls_out;
	rdpSettings* settings = rpch->settings;
	rdpRpchHTTP* http_out = rpch->http_out;
	NTLMSSP* http_out_ntlmssp = http_out->ntht;

	STREAM* ntlmssp_stream;
	STREAM* http_stream;

	ntlmssp_stream = stream_new(0xFFFF);
	http_stream = stream_new(0xFFFF);

	uint8* decoded_ntht_data = NULL;
	int decoded_ntht_length;
	uint8* encoded_ntht_data = NULL;
	int encoded_ntht_length;

	ntlmssp_set_username(http_out_ntlmssp, settings->username);
	ntlmssp_set_password(http_out_ntlmssp, settings->password);
	ntlmssp_set_domain(http_out_ntlmssp, settings->domain);
	ntlmssp_set_workstation(http_out_ntlmssp, "WORKSTATION"); /* TODO insert proper w.name */

	ntlmssp_send(http_out_ntlmssp,ntlmssp_stream);


	decoded_ntht_length = ntlmssp_stream->p-ntlmssp_stream->data;
	decoded_ntht_data = xmalloc(decoded_ntht_length);

	ntlmssp_stream->p = ntlmssp_stream->data;
	stream_read(ntlmssp_stream, decoded_ntht_data, decoded_ntht_length);

	stream_clear(ntlmssp_stream);
	ntlmssp_stream->p = ntlmssp_stream->data;

	crypto_base64_encode(decoded_ntht_data, decoded_ntht_length, &encoded_ntht_data, &encoded_ntht_length);

	stream_write(http_stream, "RPC_OUT_DATA /rpc/rpcproxy.dll?localhost:3388 HTTP/1.1\n", 55);
	stream_write(http_stream, "Accept: application/rpc\n", 24);
	stream_write(http_stream, "Cache-Control: no-cache\n", 24);
	stream_write(http_stream, "Connection: Keep-Alive\n", 23);
	stream_write(http_stream, "Content-Length: 0\n", 18);
	stream_write(http_stream, "User-Agent: MSRPC\n", 18);
	stream_write(http_stream, "Host: ", 6);
		stream_write(http_stream, settings->tsg_server, strlen(settings->tsg_server));
			stream_write(http_stream, "\n", 1);
	stream_write(http_stream, "Pragma: ResourceTypeUuid=44e265dd-7daf-42cd-8560-3cdb6e7a2729, SessionId=33ad20ac-7469-4f63-946d-113eac21a23c\n", 110);
	stream_write(http_stream, "Authorization: NTLM ", 20);
		stream_write(http_stream, encoded_ntht_data, encoded_ntht_length);
			stream_write(http_stream, "\n\n", 2);

	DEBUG_RPCH("\nSend:\n%s\n", http_stream->data);
	tls_write(tls_out, http_stream->data, http_stream->p - http_stream->data);
	stream_clear(http_stream);
	http_stream->p = http_stream->data;

	xfree(decoded_ntht_data);

	encoded_ntht_length = -1;
	xfree(encoded_ntht_data);
	encoded_ntht_data = NULL;
	uint8 buffer_byte;
	http_out->contentLength = 0;

	/* Example for how not to do clear code, sorry for that */
	while(true) /* TODO make proper reading */
	{
		tls_read(tls_out, &buffer_byte, 1);
		stream_write(http_stream, &buffer_byte, 1);

		if(encoded_ntht_length == -1)
		{
			uint8* start_of_ntht_data = strstr(http_stream->data, "NTLM ");
			if(start_of_ntht_data != NULL)
			{
				start_of_ntht_data += 5;
				for(encoded_ntht_length=0;;encoded_ntht_length++)
				{
					tls_read(tls_out, &buffer_byte, 1);
					stream_write(http_stream, &buffer_byte, 1);

					if(start_of_ntht_data[encoded_ntht_length]=='\n')
					{
						encoded_ntht_length--; /* \r */
						break;
					}
				}
				encoded_ntht_data = xmalloc(encoded_ntht_length);
				memcpy(encoded_ntht_data, start_of_ntht_data, encoded_ntht_length);
			}
		}

		if(http_out->contentLength == 0)
		{
			if(strstr(http_stream->data, "Content-Length: ") != NULL)
			{
				int i=0;
				while(*(http_stream->p-1) != '\n'){
					tls_read(tls_out, &buffer_byte, 1);
					stream_write(http_stream, &buffer_byte, 1);
					i++;
				}
				http_out->contentLength = strtol((char*)(http_stream->p-i),NULL,10);
			}
		}

		if(*(http_stream->p-1) == '\n' && *(http_stream->p-3) == '\n')
		{
			int i=0;
			for(;i < http_out->contentLength;i++)
			{
				tls_read(tls_out, &buffer_byte, 1);
				stream_write(http_stream, &buffer_byte, 1);
			}
			break;
		}
	}
	/* Example end */
	DEBUG_RPCH("\nRecv:\n%s\n", http_stream->data);
	stream_clear(http_stream);
	http_stream->p = http_stream->data;

	if(encoded_ntht_length==0) /* No NTLM data was found */
		return false;

	crypto_base64_decode(encoded_ntht_data, encoded_ntht_length, &decoded_ntht_data, &decoded_ntht_length);

	stream_write(ntlmssp_stream, decoded_ntht_data, decoded_ntht_length);
	ntlmssp_stream->p = ntlmssp_stream->data;

	xfree(decoded_ntht_data);
	xfree(encoded_ntht_data);

	ntlmssp_recv(http_out_ntlmssp, ntlmssp_stream);
	stream_clear(ntlmssp_stream);
	ntlmssp_stream->p = ntlmssp_stream->data;

	ntlmssp_send(http_out_ntlmssp, ntlmssp_stream);

	decoded_ntht_length = ntlmssp_stream->p-ntlmssp_stream->data;
	decoded_ntht_data = xmalloc(decoded_ntht_length);
	ntlmssp_stream->p = ntlmssp_stream->data;
	stream_read(ntlmssp_stream, decoded_ntht_data, decoded_ntht_length);

	stream_clear(ntlmssp_stream);
	ntlmssp_stream->p = ntlmssp_stream->data;

	crypto_base64_encode(decoded_ntht_data, decoded_ntht_length, &encoded_ntht_data, &encoded_ntht_length);

	stream_write(http_stream, "RPC_OUT_DATA /rpc/rpcproxy.dll?localhost:3388 HTTP/1.1\n", 55);
	stream_write(http_stream, "Accept: application/rpc\n", 24);
	stream_write(http_stream, "Cache-Control: no-cache\n", 24);
	stream_write(http_stream, "Connection: Keep-Alive\n", 23);
	stream_write(http_stream, "Content-Length: 76\n", 19);
	stream_write(http_stream, "User-Agent: MSRPC\n", 18);
	stream_write(http_stream, "Host: ", 6);
		stream_write(http_stream, settings->tsg_server, strlen(settings->tsg_server));
			stream_write(http_stream, "\n", 1);
	stream_write(http_stream, "Pragma: ResourceTypeUuid=44e265dd-7daf-42cd-8560-3cdb6e7a2729, SessionId=33ad20ac-7469-4f63-946d-113eac21a23c\n", 110);
	stream_write(http_stream, "Authorization: NTLM ", 20);
		stream_write(http_stream, encoded_ntht_data, encoded_ntht_length);
			stream_write(http_stream, "\n\n", 2);

	http_out->contentLength = 76;
	http_out->remContentLength = 76;

	DEBUG_RPCH("\nSend:\n%s\n", http_stream->data);

	tls_write(tls_out, http_stream->data, http_stream->p - http_stream->data);

	stream_clear(http_stream);
	http_stream->p = http_stream->data;

	xfree(decoded_ntht_data);
	xfree(encoded_ntht_data);
	/* At this point OUT connection is ready to send CONN/A1 and start with recieving data */

	http_out->state = RPCH_HTTP_SENDING;

	return true;
}

boolean rpch_in_connect_http(rdpRpch* rpch)
{
	rdpTls* tls_in = rpch->tls_in;
	rdpSettings* settings = rpch->settings;
	rdpRpchHTTP* http_in = rpch->http_in;
	NTLMSSP* http_in_ntlmssp = http_in->ntht;

	STREAM* ntlmssp_stream;
	STREAM* http_stream;

	ntlmssp_stream = stream_new(0xFFFF);
	http_stream = stream_new(0xFFFF);

	uint8* decoded_ntht_data = NULL;
	int decoded_ntht_length;
	uint8* encoded_ntht_data = NULL;
	int encoded_ntht_length;

	ntlmssp_set_username(http_in_ntlmssp, settings->username);
	ntlmssp_set_password(http_in_ntlmssp, settings->password);
	ntlmssp_set_domain(http_in_ntlmssp, settings->domain);
	ntlmssp_set_workstation(http_in_ntlmssp, "WORKSTATION"); /* TODO insert proper w.name */

	ntlmssp_send(http_in_ntlmssp,ntlmssp_stream);


	decoded_ntht_length = ntlmssp_stream->p-ntlmssp_stream->data;
	decoded_ntht_data = xmalloc(decoded_ntht_length);

	ntlmssp_stream->p = ntlmssp_stream->data;
	stream_read(ntlmssp_stream, decoded_ntht_data, decoded_ntht_length);

	stream_clear(ntlmssp_stream);
	ntlmssp_stream->p = ntlmssp_stream->data;

	crypto_base64_encode(decoded_ntht_data, decoded_ntht_length, &encoded_ntht_data, &encoded_ntht_length);

	stream_write(http_stream, "RPC_IN_DATA /rpc/rpcproxy.dll?localhost:3388 HTTP/1.1\n", 54);
	stream_write(http_stream, "Accept: application/rpc\n", 24);
	stream_write(http_stream, "Cache-Control: no-cache\n", 24);
	stream_write(http_stream, "Connection: Keep-Alive\n", 23);
	stream_write(http_stream, "Content-Length: 0\n", 18);
	stream_write(http_stream, "User-Agent: MSRPC\n", 18);
	stream_write(http_stream, "Host: ", 6);
		stream_write(http_stream, settings->tsg_server, strlen(settings->tsg_server));
			stream_write(http_stream, "\n", 1);
	stream_write(http_stream, "Pragma: ResourceTypeUuid=44e265dd-7daf-42cd-8560-3cdb6e7a2729, SessionId=33ad20ac-7469-4f63-946d-113eac21a23c\n", 110);
	stream_write(http_stream, "Authorization: NTLM ", 20);
		stream_write(http_stream, encoded_ntht_data, encoded_ntht_length);
			stream_write(http_stream, "\n\n", 2);

	DEBUG_RPCH("\nSend:\n%s\n", http_stream->data);
	tls_write(tls_in, http_stream->data, http_stream->p - http_stream->data);
	stream_clear(http_stream);
	http_stream->p = http_stream->data;

	xfree(decoded_ntht_data);

	encoded_ntht_length = -1;
	xfree(encoded_ntht_data);
	encoded_ntht_data = NULL;
	uint8 buffer_byte;
	http_in->contentLength = 0;

	/* Example for how not to do clear code, sorry for that */
	while(true) /* TODO make proper reading */
	{
		tls_read(tls_in, &buffer_byte, 1);
		stream_write(http_stream, &buffer_byte, 1);

		if(encoded_ntht_length == -1)
		{
			uint8* start_of_ntht_data = strstr(http_stream->data, "NTLM ");
			if(start_of_ntht_data != NULL)
			{
				start_of_ntht_data += 5;
				for(encoded_ntht_length=0;;encoded_ntht_length++)
				{
					tls_read(tls_in, &buffer_byte, 1);
					stream_write(http_stream, &buffer_byte, 1);

					if(start_of_ntht_data[encoded_ntht_length]=='\n')
					{
						encoded_ntht_length--; /* \r */
						break;
					}
				}
				encoded_ntht_data = xmalloc(encoded_ntht_length);
				memcpy(encoded_ntht_data, start_of_ntht_data, encoded_ntht_length);
			}
		}

		if(http_in->contentLength == 0)
		{
			if(strstr(http_stream->data, "Content-Length: ") != NULL)
			{
				int i=0;
				while(*(http_stream->p-1) != '\n'){
					tls_read(tls_in, &buffer_byte, 1);
					stream_write(http_stream, &buffer_byte, 1);
					i++;
				}
				http_in->contentLength = strtol((char*)(http_stream->p-i),NULL,10);
			}
		}

		if(*(http_stream->p-1) == '\n' && *(http_stream->p-3) == '\n')
		{
			int i=0;
			for(;i < http_in->contentLength;i++)
			{
				tls_read(tls_in, &buffer_byte, 1);
				stream_write(http_stream, &buffer_byte, 1);
			}
			break;
		}
	}
	/* Example end */
	DEBUG_RPCH("\nRecv:\n%s\n", http_stream->data);
	stream_clear(http_stream);
	http_stream->p = http_stream->data;

	if(encoded_ntht_length==0) /* No NTLM data was found */
		return false;

	crypto_base64_decode(encoded_ntht_data, encoded_ntht_length, &decoded_ntht_data, &decoded_ntht_length);

	stream_write(ntlmssp_stream, decoded_ntht_data, decoded_ntht_length);
	ntlmssp_stream->p = ntlmssp_stream->data;

	xfree(decoded_ntht_data);
	xfree(encoded_ntht_data);

	ntlmssp_recv(http_in_ntlmssp, ntlmssp_stream);
	stream_clear(ntlmssp_stream);
	ntlmssp_stream->p = ntlmssp_stream->data;

	ntlmssp_send(http_in_ntlmssp, ntlmssp_stream);

	decoded_ntht_length = ntlmssp_stream->p-ntlmssp_stream->data;
	decoded_ntht_data = xmalloc(decoded_ntht_length);
	ntlmssp_stream->p = ntlmssp_stream->data;
	stream_read(ntlmssp_stream, decoded_ntht_data, decoded_ntht_length);

	stream_clear(ntlmssp_stream);
	ntlmssp_stream->p = ntlmssp_stream->data;

	crypto_base64_encode(decoded_ntht_data, decoded_ntht_length, &encoded_ntht_data, &encoded_ntht_length);

	stream_write(http_stream, "RPC_IN_DATA /rpc/rpcproxy.dll?localhost:3388 HTTP/1.1\n", 54);
	stream_write(http_stream, "Accept: application/rpc\n", 24);
	stream_write(http_stream, "Cache-Control: no-cache\n", 24);
	stream_write(http_stream, "Connection: Keep-Alive\n", 23);
	stream_write(http_stream, "Content-Length: 1073741824\n", 27);
	stream_write(http_stream, "User-Agent: MSRPC\n", 18);
	stream_write(http_stream, "Host: ", 6);
		stream_write(http_stream, settings->tsg_server, strlen(settings->tsg_server));
			stream_write(http_stream, "\n", 1);
	stream_write(http_stream, "Pragma: ResourceTypeUuid=44e265dd-7daf-42cd-8560-3cdb6e7a2729, SessionId=33ad20ac-7469-4f63-946d-113eac21a23c\n", 110);
	stream_write(http_stream, "Authorization: NTLM ", 20);
		stream_write(http_stream, encoded_ntht_data, encoded_ntht_length);
			stream_write(http_stream, "\n\n", 2);

	http_in->contentLength = 1073741824;
	http_in->remContentLength = 1073741824;

	DEBUG_RPCH("\nSend:\n%s\n", http_stream->data);

	tls_write(tls_in, http_stream->data, http_stream->p - http_stream->data);

	stream_clear(http_stream);
	http_stream->p = http_stream->data;

	xfree(decoded_ntht_data);
	xfree(encoded_ntht_data);
	/* At this point IN connection is ready to send CONN/B1 and start with sending data */

	http_in->state = RPCH_HTTP_SENDING;

	return true;
}

int rpch_out_write(rdpRpch* rpch, uint8* data, int length)
{
	rdpRpchHTTP* http_out = rpch->http_out;
	rdpTls* tls_out = rpch->tls_out;
	int status = -1;
	int sent = 0;

	if(http_out->state == RPCH_HTTP_DISCONNECTED)
		if(!rpch_out_connect_http(rpch))
			return false;

	if(http_out->remContentLength < length)
	{
		printf("RPCH Error: HTTP frame is over.\n");
		return -1;/* TODO ChannelRecycling */
	}

#ifdef WITH_DEBUG_RPCH
	printf("rpch_out_write(): length: %d\n", length);
	freerdp_hexdump(data, length);
	printf("\n");
#endif
	while(sent < length)
	{
		status = tls_write(tls_out, data+sent, length-sent);

		if(status <= 0)
			return status; /* TODO no idea how to handle errors */

		sent += status;
	}

	http_out->remContentLength -= sent;

	return sent;
}

int rpch_in_write(rdpRpch* rpch, uint8* data, int length)
{
	rdpRpchHTTP* http_in = rpch->http_in;
	rdpTls* tls_in = rpch->tls_in;
	int status = -1;
	int sent = 0;

	if(http_in->state == RPCH_HTTP_DISCONNECTED)
		if(!rpch_in_connect_http(rpch))
			return -1;

	if(http_in->remContentLength < length)
	{
		printf("RPCH Error: HTTP frame is over.\n");
		return -1;/* TODO ChannelRecycling */
	}
#ifdef WITH_DEBUG_RPCH
	printf("\nrpch_in_send(): length: %d, remaining content length: %d\n", length, http_in->remContentLength);
	freerdp_hexdump(data, length);
	printf("\n");
#endif
	while(sent < length)
	{
		status = tls_write(tls_in, data+sent, length-sent);

		if(status <= 0)
			return status;/* TODO no idea how to handle errors */

		sent += status;
	}

	rpch->BytesSent += sent;
	http_in->remContentLength -= sent;

	return sent;
}

uint8* rpch_create_cookie()
{
	uint8 *ret = xmalloc(16);
	RAND_pseudo_bytes(ret, 16);
	return ret;
}

boolean rpch_out_send_CONN_A1(rdpRpch* rpch)
{
	STREAM* pdu = stream_new(76);

	uint8 rpc_vers = 0x05;
	uint8 rpc_vers_minor = 0x00;
	uint8 ptype = PTYPE_RTS;
	uint8 pfc_flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
	uint32 packet_drep = 0x00000010;
	uint16 frag_length = 76;
	uint16 auth_length = 0;
	uint32 call_id = 0x00000000;
	uint16 flags = 0x0000;
	uint16 num_commands = 0x0004;

	/*Version*/
	uint32 vCommandType = 0x00000006;
	uint32 Version = 0x00000001;

	/*VirtualConnectionCookie*/
	uint32 vccCommandType = 0x00000003;
	rpch->virtualConnectionCookie = rpch_create_cookie(); /*16bytes*/

	/*OUTChannelCookie*/
	uint32 occCommandType = 0x00000003;
	rpch->OUTChannelCookie = rpch_create_cookie(); /*16bytes*/

	/*ReceiveWindowSize*/
	uint32 rwsCommandType = 0x00000000;
	uint32 reseiveWindowSize = 0x00010000;
	rpch->AwailableWindow = reseiveWindowSize;

	stream_write_uint8(pdu, rpc_vers);
	stream_write_uint8(pdu, rpc_vers_minor);
	stream_write_uint8(pdu, ptype);
	stream_write_uint8(pdu, pfc_flags);
	stream_write_uint32(pdu, packet_drep);
	stream_write_uint16(pdu, frag_length);
	stream_write_uint16(pdu, auth_length);
	stream_write_uint32(pdu, call_id);
	stream_write_uint16(pdu, flags);
	stream_write_uint16(pdu, num_commands);
	stream_write_uint32(pdu, vCommandType);
	stream_write_uint32(pdu, Version);
	stream_write_uint32(pdu, vccCommandType);
	stream_write(pdu, rpch->virtualConnectionCookie, 16);
	stream_write_uint32(pdu, occCommandType);
	stream_write(pdu, rpch->OUTChannelCookie, 16);
	stream_write_uint32(pdu, rwsCommandType);
	stream_write_uint32(pdu, reseiveWindowSize);

	rpch_out_write(rpch, pdu->data, pdu->p - pdu->data);

	stream_free(pdu);

	return true;
}

boolean rpch_in_send_CONN_B1(rdpRpch* rpch)
{
	STREAM* pdu = stream_new(104);

	uint8 rpc_vers = 0x05;
	uint8 rpc_vers_minor = 0x00;
	uint8 ptype = PTYPE_RTS;
	uint8 pfc_flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
	uint32 packet_drep = 0x00000010;
	uint16 frag_length = 104;
	uint16 auth_length = 0;
	uint32 call_id = 0x00000000;
	uint16 flags = 0x0000;
	uint16 num_commands = 0x0006;

	/*Version*/
	uint32 vCommandType = 0x00000006;
	uint32 Version = 0x00000001;

	/*VirtualConnectionCookie*/
	uint32 vccCommandType = 0x00000003;

	/*INChannelCookie*/
	uint32 iccCommandType = 0x00000003;
	rpch->INChannelCookie = rpch_create_cookie(); /*16bytes*/

	/*ChannelLifetime*/
	uint32 clCommandType = 0x00000004;
	uint32 ChannelLifetime = 0x40000000;

	/*ClientKeepalive*/
	uint32 ckCommandType = 0x00000005;
	uint32 ClientKeepalive = 0x000493e0;

	/*AssociationGroupId*/
	uint32 agidCommandType = 0x0000000c;
	uint8* AssociationGroupId = rpch_create_cookie(); /*16bytes*/

	stream_write_uint8(pdu, rpc_vers);
	stream_write_uint8(pdu, rpc_vers_minor);
	stream_write_uint8(pdu, ptype);
	stream_write_uint8(pdu, pfc_flags);
	stream_write_uint32(pdu, packet_drep);
	stream_write_uint16(pdu, frag_length);
	stream_write_uint16(pdu, auth_length);
	stream_write_uint32(pdu, call_id);
	stream_write_uint16(pdu, flags);
	stream_write_uint16(pdu, num_commands);
	stream_write_uint32(pdu, vCommandType);
	stream_write_uint32(pdu, Version);
	stream_write_uint32(pdu, vccCommandType);
	stream_write(pdu, rpch->virtualConnectionCookie, 16);
	stream_write_uint32(pdu, iccCommandType);
	stream_write(pdu, rpch->INChannelCookie, 16);
	stream_write_uint32(pdu, clCommandType);
	stream_write_uint32(pdu, ChannelLifetime);
	stream_write_uint32(pdu, ckCommandType);
	stream_write_uint32(pdu, ClientKeepalive);
	stream_write_uint32(pdu, agidCommandType);
	stream_write(pdu, AssociationGroupId, 16);

	rpch_in_write(rpch, pdu->data, pdu->p - pdu->data);

	stream_free(pdu);

	return true;
}

boolean rpch_in_send_keep_alive(rdpRpch* rpch)
{
	STREAM* pdu = stream_new(28);

	uint8 rpc_vers = 0x05;
	uint8 rpc_vers_minor = 0x00;
	uint8 ptype = PTYPE_RTS;
	uint8 pfc_flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
	uint32 packet_drep = 0x00000010;
	uint16 frag_length = 28;
	uint16 auth_length = 0;
	uint32 call_id = 0x00000000;
	uint16 flags = 0x0002;
	uint16 num_commands = 0x0001;

	/*ClientKeepalive*/
	uint32 ckCommandType = 0x00000005;
	uint32 ClientKeepalive = 0x00007530;

	stream_write_uint8(pdu, rpc_vers);
	stream_write_uint8(pdu, rpc_vers_minor);
	stream_write_uint8(pdu, ptype);
	stream_write_uint8(pdu, pfc_flags);
	stream_write_uint32(pdu, packet_drep);
	stream_write_uint16(pdu, frag_length);
	stream_write_uint16(pdu, auth_length);
	stream_write_uint32(pdu, call_id);
	stream_write_uint16(pdu, flags);
	stream_write_uint16(pdu, num_commands);
	stream_write_uint32(pdu, ckCommandType);
	stream_write_uint32(pdu, ClientKeepalive);

	rpch_in_write(rpch, pdu->data, pdu->p - pdu->data);

	stream_free(pdu);

	return true;
}

boolean rpch_in_send_bind(rdpRpch* rpch)
{
	STREAM* ntlm_stream = stream_new(0xFFFF);
	rpch->ntlmssp = ntlmssp_client_new();
	rpch->ntlmssp->ntlm_v2 = true;
	rpch->ntlmssp->do_not_seal = true;
	ntlmssp_set_username(rpch->ntlmssp, "PanoGatewayUser49");
	ntlmssp_set_password(rpch->ntlmssp, rpch->settings->tsg_password);
	ntlmssp_set_domain(rpch->ntlmssp, "PANOGATEWAY");
	ntlmssp_set_workstation(rpch->ntlmssp, "WORKSTATION"); /* TODO insert proper w.name */


	ntlmssp_send(rpch->ntlmssp,ntlm_stream);

	rpcconn_bind_hdr_t* bind_pdu = xmalloc(sizeof(rpcconn_bind_hdr_t));
	bind_pdu->rpc_vers = 5;
	bind_pdu->rpc_vers_minor = 0;
	bind_pdu->PTYPE = PTYPE_BIND;
	bind_pdu->pfc_flags = PFC_FIRST_FRAG | PFC_LAST_FRAG | PFC_PENDING_CANCEL | PFC_CONC_MPX;
	bind_pdu->packed_drep[0] = 0x10;bind_pdu->packed_drep[1] = 0x00;bind_pdu->packed_drep[2] = 0x00;bind_pdu->packed_drep[3] = 0x00;
	bind_pdu->frag_length = 124 + (ntlm_stream->p - ntlm_stream->data);
	bind_pdu->auth_length = ntlm_stream->p - ntlm_stream->data;
	bind_pdu->call_id = 2;
	bind_pdu->max_xmit_frag = 0x0FF8;
	bind_pdu->max_recv_frag = 0x0FF8;
	bind_pdu->assoc_group_id = 0;
	bind_pdu->p_context_elem.n_context_elem = 2;
	bind_pdu->p_context_elem.reserved = 0;
	bind_pdu->p_context_elem.reserved2 = 0;
	bind_pdu->p_context_elem.p_cont_elem = xmalloc(sizeof(p_cont_elem_t) * bind_pdu->p_context_elem.n_context_elem);
	bind_pdu->p_context_elem.p_cont_elem[0].p_cont_id = 0;
	bind_pdu->p_context_elem.p_cont_elem[0].n_transfer_syn = 1;
	bind_pdu->p_context_elem.p_cont_elem[0].reserved = 0;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.time_low = 0x44e265dd;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.time_mid = 0x7daf;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.time_hi_and_version = 0x42cd;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.clock_seq_hi_and_reserved = 0x85;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.clock_seq_low = 0x60;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.node[0] = 0x3c;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.node[1] = 0xdb;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.node[2] = 0x6e;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.node[3] = 0x7a;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.node[4] = 0x27;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_uuid.node[5] = 0x29;
	bind_pdu->p_context_elem.p_cont_elem[0].abstract_syntax.if_version = 0x00030001;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes = xmalloc(sizeof(p_syntax_id_t));
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.time_low = 0x8a885d04;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.time_mid = 0x1ceb;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.time_hi_and_version = 0x11c9;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.clock_seq_hi_and_reserved = 0x9f;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.clock_seq_low = 0xe8;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.node[0] = 0x08;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.node[1] = 0x00;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.node[2] = 0x2b;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.node[3] = 0x10;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.node[4] = 0x48;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_uuid.node[5] = 0x60;
	bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes[0].if_version = 0x00000002;
	bind_pdu->p_context_elem.p_cont_elem[1].p_cont_id = 1;
	bind_pdu->p_context_elem.p_cont_elem[1].n_transfer_syn = 1;
	bind_pdu->p_context_elem.p_cont_elem[1].reserved = 0;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.time_low = 0x44e265dd;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.time_mid = 0x7daf;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.time_hi_and_version = 0x42cd;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.clock_seq_hi_and_reserved = 0x85;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.clock_seq_low = 0x60;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.node[0] = 0x3c;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.node[1] = 0xdb;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.node[2] = 0x6e;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.node[3] = 0x7a;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.node[4] = 0x27;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_uuid.node[5] = 0x29;
	bind_pdu->p_context_elem.p_cont_elem[1].abstract_syntax.if_version = 0x00030001;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes = xmalloc(sizeof(p_syntax_id_t));
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.time_low = 0x6cb71c2c;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.time_mid = 0x9812;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.time_hi_and_version = 0x4540;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.clock_seq_hi_and_reserved = 0x03;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.clock_seq_low = 0x00;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.node[0] = 0x00;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.node[1] = 0x00;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.node[2] = 0x00;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.node[3] = 0x00;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.node[4] = 0x00;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_uuid.node[5] = 0x00;
	bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes[0].if_version = 0x00000001;
	bind_pdu->auth_verifier.auth_pad = NULL; /* align(4); size_is(auth_pad_length) p*/
	bind_pdu->auth_verifier.auth_type = 0x0a;       /* :01  which authent service */
	bind_pdu->auth_verifier.auth_level = 0x05;      /* :01  which level within service */
	bind_pdu->auth_verifier.auth_pad_length = 0x00; /* :01 */
	bind_pdu->auth_verifier.auth_reserved = 0x00;   /* :01 reserved, m.b.z. */
	bind_pdu->auth_verifier.auth_context_id = 0x00000000; /* :04 */
	bind_pdu->auth_verifier.auth_value = xmalloc(bind_pdu->auth_length); /* credentials; size_is(auth_length) p*/;
	memcpy(bind_pdu->auth_verifier.auth_value, ntlm_stream->data, bind_pdu->auth_length);

	stream_free(ntlm_stream);

	STREAM* pdu = stream_new(bind_pdu->frag_length);

	stream_write(pdu, bind_pdu, 24);
	stream_write(pdu, &bind_pdu->p_context_elem, 4);
	stream_write(pdu, bind_pdu->p_context_elem.p_cont_elem, 24);
	stream_write(pdu, bind_pdu->p_context_elem.p_cont_elem[0].transfer_syntaxes, 20);
	stream_write(pdu, bind_pdu->p_context_elem.p_cont_elem + 1, 24);
	stream_write(pdu, bind_pdu->p_context_elem.p_cont_elem[1].transfer_syntaxes, 20);
	if(bind_pdu->auth_verifier.auth_pad_length > 0)
		stream_write(pdu, bind_pdu->auth_verifier.auth_pad, bind_pdu->auth_verifier.auth_pad_length);
	stream_write(pdu, &bind_pdu->auth_verifier.auth_type, 8); /* assumed that uint8 pointer is 32bit long (4 bytes) */
	stream_write(pdu, bind_pdu->auth_verifier.auth_value, bind_pdu->auth_length);

	rpch_in_write(rpch, pdu->data, pdu->p - pdu->data);
	/*TODO there are some alocatad memory*/
	xfree(bind_pdu);
	return true;
}

boolean rpch_in_send_rpc_auth_3(rdpRpch* rpch)
{
	STREAM* ntlm_stream = stream_new(0xFFFF);

	ntlmssp_send(rpch->ntlmssp,ntlm_stream);

	rpcconn_rpc_auth_3_hdr_t* rpc_auth_3_pdu = xmalloc(sizeof(rpcconn_rpc_auth_3_hdr_t));
	rpc_auth_3_pdu->rpc_vers = 5;
	rpc_auth_3_pdu->rpc_vers_minor = 0;
	rpc_auth_3_pdu->PTYPE = PTYPE_RPC_AUTH_3;
	rpc_auth_3_pdu->pfc_flags = PFC_FIRST_FRAG | PFC_LAST_FRAG | PFC_CONC_MPX;
	rpc_auth_3_pdu->packed_drep[0] = 0x10;rpc_auth_3_pdu->packed_drep[1] = 0x00;rpc_auth_3_pdu->packed_drep[2] = 0x00;rpc_auth_3_pdu->packed_drep[3] = 0x00;
	rpc_auth_3_pdu->frag_length = 28 + (ntlm_stream->p - ntlm_stream->data);
	rpc_auth_3_pdu->auth_length = ntlm_stream->p - ntlm_stream->data;
	rpc_auth_3_pdu->call_id = 2;
	rpc_auth_3_pdu->max_xmit_frag = 0x0FF8;
	rpc_auth_3_pdu->max_recv_frag = 0x0FF8;
	rpc_auth_3_pdu->auth_verifier.auth_pad = NULL; /* align(4); size_is(auth_pad_length) p*/
	rpc_auth_3_pdu->auth_verifier.auth_type = 0x0a;       /* :01  which authent service */
	rpc_auth_3_pdu->auth_verifier.auth_level = 0x05;      /* :01  which level within service */
	rpc_auth_3_pdu->auth_verifier.auth_pad_length = 0x00; /* :01 */
	rpc_auth_3_pdu->auth_verifier.auth_reserved = 0x00;   /* :01 reserved, m.b.z. */
	rpc_auth_3_pdu->auth_verifier.auth_context_id = 0x00000000; /* :04 */
	rpc_auth_3_pdu->auth_verifier.auth_value = xmalloc(rpc_auth_3_pdu->auth_length); /* credentials; size_is(auth_length) p*/;
	memcpy(rpc_auth_3_pdu->auth_verifier.auth_value, ntlm_stream->data, rpc_auth_3_pdu->auth_length);

	stream_free(ntlm_stream);

	STREAM* pdu = stream_new(rpc_auth_3_pdu->frag_length);

	stream_write(pdu, rpc_auth_3_pdu, 20);
	if(rpc_auth_3_pdu->auth_verifier.auth_pad_length > 0)
		stream_write(pdu, rpc_auth_3_pdu->auth_verifier.auth_pad, rpc_auth_3_pdu->auth_verifier.auth_pad_length);
	stream_write(pdu, &rpc_auth_3_pdu->auth_verifier.auth_type, 8);
	stream_write(pdu, rpc_auth_3_pdu->auth_verifier.auth_value, rpc_auth_3_pdu->auth_length);

	rpch_in_write(rpch, pdu->data, pdu->p - pdu->data);
	xfree(rpc_auth_3_pdu);
	return true;
}

boolean rpch_in_send_flow_control(rdpRpch* rpch)
{
	STREAM* pdu = stream_new(56);

	uint8 rpc_vers = 0x05;
	uint8 rpc_vers_minor = 0x00;
	uint8 ptype = PTYPE_RTS;
	uint8 pfc_flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
	uint32 packet_drep = 0x00000010;
	uint16 frag_length = 56;
	uint16 auth_length = 0;
	uint32 call_id = 0x00000000;
	uint16 flags = 0x0002;
	uint16 num_commands = 0x0002;

	/*ClientKeepalive*/
	uint32 ckCommandType = 0x0000000d;
	uint32 ClientKeepalive = 0x00000003;

	/**/
	uint32 a = 0x00000001;
	uint32 aa = rpch->BytesReceived;
	uint32 aaa = 0x00010000;
	rpch->AwailableWindow = aaa;

	uint8* b = rpch->OUTChannelCookie;

	stream_write_uint8(pdu, rpc_vers);
	stream_write_uint8(pdu, rpc_vers_minor);
	stream_write_uint8(pdu, ptype);
	stream_write_uint8(pdu, pfc_flags);
	stream_write_uint32(pdu, packet_drep);
	stream_write_uint16(pdu, frag_length);
	stream_write_uint16(pdu, auth_length);
	stream_write_uint32(pdu, call_id);
	stream_write_uint16(pdu, flags);
	stream_write_uint16(pdu, num_commands);
	stream_write_uint32(pdu, ckCommandType);
	stream_write_uint32(pdu, ClientKeepalive);
	stream_write_uint32(pdu, a);
	stream_write_uint32(pdu, aa);
	stream_write_uint32(pdu, aaa);
	stream_write(pdu, b, 16);

	rpch_in_write(rpch, pdu->data, pdu->p - pdu->data);

	stream_free(pdu);

	return true;
}

boolean rpch_in_send_ping(rdpRpch* rpch)
{
	STREAM* pdu = stream_new(20);

	uint8 rpc_vers = 0x05;
	uint8 rpc_vers_minor = 0x00;
	uint8 ptype = PTYPE_RTS;
	uint8 pfc_flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
	uint32 packet_drep = 0x00000010;
	uint16 frag_length = 56;
	uint16 auth_length = 0;
	uint32 call_id = 0x00000000;
	uint16 flags = 0x0001;
	uint16 num_commands = 0x0000;

	stream_write_uint8(pdu, rpc_vers);
	stream_write_uint8(pdu, rpc_vers_minor);
	stream_write_uint8(pdu, ptype);
	stream_write_uint8(pdu, pfc_flags);
	stream_write_uint32(pdu, packet_drep);
	stream_write_uint16(pdu, frag_length);
	stream_write_uint16(pdu, auth_length);
	stream_write_uint32(pdu, call_id);
	stream_write_uint16(pdu, flags);
	stream_write_uint16(pdu, num_commands);

	rpch_in_write(rpch, pdu->data, pdu->p - pdu->data);

	stream_free(pdu);

	return true;
}

int rpch_out_read_http_header(rdpRpch* rpch)
{
	int status;
	rdpTls* tls_out = rpch->tls_out;
	rdpRpchHTTP* http_out = rpch->http_out;
	STREAM* http_stream;

	http_stream = stream_new(1024); /*hope the header will not be larger*/
	http_out->contentLength = 0;

	while(true) /* TODO make proper reading */
	{
		status = tls_read(tls_out, stream_get_tail(http_stream), 1);
		if(status < 0)
			return status;
		stream_seek(http_stream, 1);

		if(http_out->contentLength == 0)
		{
			if(strstr(stream_get_head(http_stream), "Content-Length:") != NULL)
			{
				int i=0;
				while(*(stream_get_tail(http_stream)-1) != '\n'){
					status = tls_read(tls_out, stream_get_tail(http_stream), 1);
					if(status < 0)
						return status;
					stream_seek(http_stream,1);
					i++;
					if(*(stream_get_tail(http_stream)-1) == ' ')i--;
				}
				http_out->contentLength = strtol((char*)(stream_get_tail(http_stream)-i),NULL,10);
				http_out->remContentLength = http_out->contentLength;
			}
		}

		if(*(stream_get_tail(http_stream)-1) == '\n' && *(stream_get_tail(http_stream)-3) == '\n')
		{
			break;
		}
	}
	DEBUG_RPCH("\nRecv HTTP header:\n%s",stream_get_head(http_stream));
	stream_free(http_stream);

	return status;
}

int rpch_proceed_RTS(rdpRpch* rpch, uint8* pdu, int length)
{
	uint16 flags = *(uint16*)(pdu + 16);
	uint16 num_commands = *(uint16*)(pdu + 18);
	uint8* iterator = pdu + 20;
	uint32 CommandType;
	int i;

	if(flags & RTS_FLAG_PING)
	{
		rpch_in_send_keep_alive(rpch);
		return 0;
	}

	for(i=0;i<num_commands;i++)
	{
		CommandType = *(uint32*)iterator;
		switch(CommandType)
		{
		case 0x00000000:/*ReceiveWindowSize*/
			iterator+=8;
			break;
		case 0x00000001:/*FlowControlAck*/
			iterator+=28;
			break;
		case 0x00000002:/*ConnectionTimeout*/
			iterator+=8;
			break;
		case 0x00000003:/*Cookie*/
			iterator+=20;
			break;
		case 0x00000004:/*ChannelLifetime*/
			iterator+=8;
			break;
		case 0x00000005:/*ClientKeepalive*/
			iterator+=8;
			break;
		case 0x00000006:/*Version*/
			iterator+=8;
			break;
		case 0x00000007:/*Empty*/
			iterator+=4;
			break;
		case 0x00000008:/*Padding*/
			iterator+= 8 + *(uint32*)(iterator+4);
			break;
		case 0x00000009:/*NegativeANCE*/
			iterator+=4;
			break;
		case 0x0000000a:/*ANCE*/
			iterator+=4;
			break;
		case 0x0000000b:/*ClientAddress*/
			iterator+= 4 + 4 + (12 * (*(uint32*)(iterator+4))) + 12 ;
			break;
		case 0x0000000c:/*AssociationGroupId*/
			iterator+=20;
			break;
		case 0x0000000d:/*Destination*/
			iterator+=8;
			break;
		case 0x0000000e:/*PingTrafficSentNotify*/
			iterator+=8;
			break;
		default:
			printf(" Error: Unknown RTS CommandType: 0x%x\n", CommandType);
			return -1;
		}
	}
	return 0;
}

int rpch_out_read(rdpRpch* rpch, uint8* data, int length)
{
	int status;
	rdpTls* tls_out = rpch->tls_out;
	rdpRpchHTTP* http_out = rpch->http_out;
	uint8* pdu;
	uint8 ptype;
	uint16 frag_length;

	if(rpch->AwailableWindow < 0x00008FFF)/*Just a simple workaround*/
	//if(rpch->AwailableWindow < 0x00010000)/*Just a simple workaround*/
		rpch_in_send_flow_control(rpch);  /*send FlowControlAck every time AW reaches the half*/

	if(http_out->remContentLength <= 0xFFFF)/*TODO make ChannelRecycling*/
		if(rpch_out_read_http_header(rpch) < 0)
			return -1;

	pdu = xmalloc(0xFFFF);


	status = tls_read(tls_out, pdu, 10);
	if(status <= 0) /*read first 10 bytes to get the frag_length value*/
	{
		xfree(pdu);
		return status;
	}

	ptype = *(pdu + 2);
	frag_length = *((uint16*)(pdu + 8));

	status = tls_read(tls_out, pdu+10, frag_length-10);
	if(status < 0)
	{
		xfree(pdu);
		return status;
	}

	if(ptype == 0x14) /*RTS PDU*/
	{
		//printf("RTS PDU received...\n");
		rpch_proceed_RTS(rpch, pdu, frag_length);
		xfree(pdu);
		return 0;
	}
	else
	{
		rpch->BytesReceived += frag_length; /*RTS PDUs are not subjects for FlowControl*/
		rpch->AwailableWindow -= frag_length;
	}

	http_out->remContentLength -= frag_length;

	if(length < frag_length)
	{
		printf("rcph_out_read(): Error! Given buffer is to small. Recieved data fits not in.\n");
		xfree(pdu);
		return -1; /*TODO add buffer for storing remaining data for the next read
											 in case destination buffer is to small*/
	}

	memcpy(data, pdu, frag_length);

#ifdef WITH_DEBUG_RPCH
	printf("\nrpch_out_recv(): length: %d, remaining content length: %d\n", frag_length, http_out->remContentLength);
	freerdp_hexdump(data, frag_length);
	printf("\n");
#endif

	xfree(pdu);

	return frag_length;
}

int rpch_out_recv_bind_ack(rdpRpch* rpch)
{
	int pdu_length = 0x8FFF; /*32KB buffer*/
	uint8* pdu = xmalloc(pdu_length);
	int status = rpch_out_read(rpch, pdu, pdu_length);
	if(status > 0)
	{
		uint16 frag_length = *((uint16*)(pdu+8));
		uint16 auth_length = *((uint16*)(pdu+10));

		STREAM* ntlmssp_stream = stream_new(0xFFFF);
		stream_write(ntlmssp_stream, (pdu+(frag_length-auth_length)), auth_length);
		ntlmssp_stream->p = ntlmssp_stream->data;

		ntlmssp_recv(rpch->ntlmssp, ntlmssp_stream);

		stream_free(ntlmssp_stream);
	}
	xfree(pdu);
	return status;
}

int rpch_write(rdpRpch* rpch, uint8* data, int length, uint16 opnum)
{
	int status = -1;

	uint8 auth_pad_length = 16 - ((24 + length + 8 + 16) % 16);
	if(auth_pad_length == 16)auth_pad_length=0;

	rpcconn_request_hdr_t* request_pdu = xmalloc(sizeof(rpcconn_request_hdr_t));
	request_pdu->rpc_vers = 5;
	request_pdu->rpc_vers_minor = 0;
	request_pdu->PTYPE = PTYPE_REQUEST;
	request_pdu->pfc_flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
	request_pdu->packed_drep[0] = 0x10;request_pdu->packed_drep[1] = 0x00;request_pdu->packed_drep[2] = 0x00;request_pdu->packed_drep[3] = 0x00;
	request_pdu->frag_length = 24 + length + auth_pad_length + 8 + 16;
	request_pdu->auth_length = 16;
	request_pdu->call_id = ++rpch->call_id;if(opnum==8)rpch->pipe_call_id=rpch->call_id; /* opnum=8 means [MS-TSGU]TsProxySetupRecievePipe, save call_id for checking pipe responces */
	request_pdu->alloc_hint = length;
	request_pdu->p_cont_id = 0x0000;
	request_pdu->opnum = opnum;
	request_pdu->stub_data = data;
	request_pdu->auth_verifier.auth_type = 0x0a;       /* :01  which authent service */
	request_pdu->auth_verifier.auth_level = 0x05;      /* :01  which level within service */
	request_pdu->auth_verifier.auth_pad_length = auth_pad_length; /* :01 */
	request_pdu->auth_verifier.auth_pad = xmalloc(auth_pad_length); /* align(4); size_is(auth_pad_length) p*/
	int i = 0;
	for(;i<auth_pad_length;i++)request_pdu->auth_verifier.auth_pad[i] = 0x00;
	request_pdu->auth_verifier.auth_reserved = 0x00;   /* :01 reserved, m.b.z. */
	request_pdu->auth_verifier.auth_context_id = 0x00000000; /* :04 */
	request_pdu->auth_verifier.auth_value = xmalloc(request_pdu->auth_length); /* credentials; size_is(auth_length) p*/;

	STREAM* pdu = stream_new(request_pdu->frag_length);

	stream_write(pdu, request_pdu, 24);
	stream_write(pdu, request_pdu->stub_data, request_pdu->alloc_hint);
	if(request_pdu->auth_verifier.auth_pad_length > 0)
		stream_write(pdu, request_pdu->auth_verifier.auth_pad, request_pdu->auth_verifier.auth_pad_length);
	stream_write(pdu, &request_pdu->auth_verifier.auth_type, 8);

	rdpBlob rdpMsg;
	rdpMsg.data = pdu->data;
	rdpMsg.length = pdu->p - pdu->data;
	ntlmssp_encrypt_message(rpch->ntlmssp, &rdpMsg, NULL, request_pdu->auth_verifier.auth_value);

	stream_write(pdu, request_pdu->auth_verifier.auth_value, request_pdu->auth_length);

	status = rpch_in_write(rpch, pdu->data, pdu->p - pdu->data);

	xfree(request_pdu->auth_verifier.auth_value);
	xfree(request_pdu->auth_verifier.auth_pad);
	xfree(request_pdu);

	if(status < 0)
	{
		printf("rpch_write(): Error! rcph_in_write returned negative value.\n");
		return status;
	}

	return length;
}

int rpch_read(rdpRpch* rpch, uint8* data, int length)
{/*TODO*/
	int rpch_length = length + 0xFF;
	int status;
	int readed = 0;
	int data_length;
	uint8* rpch_data = xmalloc(rpch_length);
	/*uint32 return_code;*/
	uint16 frag_length;
	uint16 auth_length;
	uint8 auth_pad_length;
	uint32 call_id = -1;

	if(rpch->read_buffer_len > 0)
	{
		if(rpch->read_buffer_len > length)
		{
			/*TODO fix read_buufer is too long problem*/
			printf("ERROR! RPCH Stores data in read_buffer fits not in data on rpch_read.\n");
			return -1;
		}
		memcpy(data, rpch->read_buffer, rpch->read_buffer_len);
		readed += rpch->read_buffer_len;
		xfree(rpch->read_buffer);
		rpch->read_buffer_len = 0;
	}

	while(true)
	{
		status = rpch_out_read(rpch, rpch_data, rpch_length);
		if(status == 0)
		{
			xfree(rpch_data);
			return readed;
		}
		else if(status < 0)
		{
			printf("\nError! rpch_out_read() returned negative value. BytesSent: %d, BytesReceived: %d\n", rpch->BytesSent, rpch->BytesReceived);

			xfree(rpch_data);
			return status;
		}

		frag_length = *(uint16*)(rpch_data+8);
		auth_length = *(uint16*)(rpch_data+10);
		call_id = *(uint32*)(rpch_data+12);
		status = *(uint32*)(rpch_data+16); /*alloc_hint*/
		auth_pad_length = *(rpch_data + frag_length - auth_length - 6); /*-6 = -8 + 2 (sec_trailer + 2)*/
		/*data_length must be calculated because alloc_hint carries size of more than one pdu*/
		data_length = frag_length - auth_length - 24 - 8 - auth_pad_length; /*24 is header; 8 is sec_trailer*/

		if(status == 4)
			continue;

		if(readed + data_length > length) /*if readed data is greater then given buffer*/
		{
			rpch->read_buffer_len = readed + data_length - length;
			rpch->read_buffer = xmalloc(rpch->read_buffer_len);

			data_length -= rpch->read_buffer_len;

			memcpy(rpch->read_buffer, rpch_data+24+data_length, rpch->read_buffer_len);
		}

		memcpy(data+readed, rpch_data+24, data_length);

		readed += data_length;

		if(status > data_length && readed < length)
			continue;

		break;
	}

	xfree(rpch_data);
	return readed;
}

boolean rpch_connect(rdpRpch* rpch)
{
	int pdu_length;
	uint8* pdu;
	int status;

	if(!rpch_out_send_CONN_A1(rpch))
	{
		printf("rpch_out_send_CONN_A1 fault!\n");
		return false;
	}

	pdu_length = 0xFFFF;
	pdu = xmalloc(pdu_length);

	status = rpch_out_read(rpch, pdu, pdu_length);

	if(!rpch_in_send_CONN_B1(rpch))
	{
		printf("rpch_out_send_CONN_A1 fault!\n");
		return false;
	}

	status = rpch_out_read(rpch, pdu, pdu_length);

	/* [MS-RPCH] 3.2.1.5.3.1 Connection Establishment
	 * at this point VirtualChannel is created
	 */
	if(!rpch_in_send_bind(rpch))
	{
		printf("rpch_out_send_bind fault!\n");
		return false;
	}

	if(!rpch_out_recv_bind_ack(rpch))
	{
		printf("rpch_out_recv_bind_ack fault!\n");
		return false;
	}

	if(!rpch_in_send_rpc_auth_3(rpch))
	{
		printf("rpch_out_send_rpc_auth_3 fault!\n");
		return false;
	}

	xfree(pdu);
	return true;
}

