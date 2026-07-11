// socket_primitives.c
//
// Beagle Smalltalk
// Copyright (c) 2025 Simberon Incorporated
// Released under the MIT License
// https://opensource.org/license/MIT

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <signal.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "object.h"

#define PRIM_SOCKET_CREATE 200
#define PRIM_FAMILY_ADDRESS_PORT 201
#define PRIM_SOCKET_ACCEPT 202
#define PRIM_SOCKET_BIND 203
#define PRIM_SOCKET_CONNECT 204
#define PRIM_SOCKET_GET_PEER_NAME 205
#define PRIM_SOCKET_GET_SOCK_NAME 206
#define PRIM_SOCKET_GET_SOCK_OPT 207
#define PRIM_SOCKET_LISTEN 208
#define PRIM_SOCKET_RECV 209
#define PRIM_SOCKET_RECV_FROM 210
#define PRIM_SOCKET_RECV_MSG 211
#define PRIM_SOCKET_SEND 212
#define PRIM_SOCKET_SEND_MSG 213
#define PRIM_SOCKET_SEND_TO 214
#define PRIM_SOCKET_SET_SOCK_OPT 215
#define PRIM_SOCKET_SHUTDOWN 216
#define PRIM_SOCKET_SOCKET_PAIR 217
#define PRIM_SOCKET_CLOSE 218
#define PRIM_SOCKET_POLL 219
#define PRIM_WEBSOCKET_PORT_NUMBER 220
#define PRIM_IOCTL 221

void primSocketCreate()
{
	oop receiver = getReceiver();
	oop domain = getLocal( 0);
	oop type = getLocal( 1);
	oop protocol = getLocal( 2);
	
	if (!isSmallInteger(domain) || !isSmallInteger(type) || !isSmallInteger(protocol)) {
//		LOGI("Socket create failed 1");
		PRIMITIVE_FAIL(1);
	}
	
	int socketNumber = socket(stIntToC(domain), stIntToC(type), stIntToC(protocol));
	
	if (socketNumber == -1) {
//		LOGI("Socket create failed 2");
		PRIMITIVE_FAIL(2);
	}

	oop stSocketOop = newInstanceOfClass (ST_UNINTERPRETED_BYTES_CLASS, sizeof(socketStruct), EdenSpace);
	asSocket(stSocketOop)->socketHandle = socketNumber;

//	LOGI("Socket created");

	push (cIntToST(0));
	push (stSocketOop);
}

void primSocketFamilyAddressPort()
{
	oop receiver = getReceiver();
	oop family = getLocal( 0);
	oop address = getLocal( 1);
	oop port = getLocal( 2);

	oop handle = instVarAtInt(receiver, 0);

	if (!isUninterpretedBytes(handle)
		|| !isSmallInteger(family)
		|| !isSmallInteger(address)
		|| !isSmallInteger(port)) {

//		LOGI("Socket family address port failed");
		PRIMITIVE_FAIL(2);
		}

	memset(&asSocket(handle) -> address, '0', sizeof(struct sockaddr_in));

	asSocket(handle) -> address.sin_family = (short) stIntToC(family);
	asSocket(handle) -> address.sin_port = htons(stIntToC(port));
	asSocket(handle) -> address.sin_addr.s_addr = htonl((unsigned long) stIntToC(address));
	asSocket(handle) -> addressLength = sizeof(struct sockaddr_in);

	push (cIntToST(0));
	push (cIntToST(0));
}

void primSocketAccept()
{
	oop receiver = getReceiver();
	oop handle = instVarAtInt(receiver, 0);

	if (!isUninterpretedBytes(handle)) {
//		LOGI("Socket accept failed 2");
		PRIMITIVE_FAIL(2);
	}

	int result = accept(asSocket(handle) -> socketHandle,
		NULL,
		NULL);

	if (result == -1) {
		push(cIntToST(0));
		push(cIntToST(errno));
		return;
	}

	oop stSocketOop = newInstanceOfClass (ST_UNINTERPRETED_BYTES_CLASS, sizeof(socketStruct), EdenSpace);
	asSocket(stSocketOop)->socketHandle = result;

//	LOGI("Socket accept");

	push (cIntToST(0));
	push (stSocketOop);
}

void primSocketBind()
{
	oop receiver = getReceiver();
	oop handle = instVarAtInt(receiver, 0);
    int sock_fd, opt;

	if (!isUninterpretedBytes(handle)) {
//		LOGI("Socket bind failed 1");
		PRIMITIVE_FAIL(2);
	}

    sock_fd = asSocket(handle) -> socketHandle;
    opt = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		PRIMITIVE_FAIL(errno);
    }

	int result = bind(sock_fd,
		(struct sockaddr *) &asSocket(handle) -> address,
		sizeof (struct sockaddr_in));

	if (result == -1) {
//		LOGI("Socket bind failed 2");
		PRIMITIVE_FAIL(errno);
	}
	
//	LOGI("Socket bind");

	push (cIntToST(0));
	push (cIntToST(result));
}

void primSocketConnect()
{
	oop receiver = getReceiver();
	oop handle = instVarAtInt(receiver, 0);

	if (!isUninterpretedBytes(handle))
		PRIMITIVE_FAIL(2);

	int result = connect(asSocket(handle) -> socketHandle,
		(struct sockaddr *) &asSocket(handle) -> address,
		(socklen_t) asSocket(handle) -> addressLength);

	if (result == -1)
		PRIMITIVE_FAIL(3);

//	LOGI("Socket connected");

	push (cIntToST(0));
	push (cIntToST(result));
}

void primSocketGetPeerName()
{
	oop receiver = getReceiver();

	push (cIntToST(1));
	push (receiver);
}

void primSocketGetSockName()
{
	oop receiver = getReceiver();

	push (cIntToST(1));
	push (receiver);
}

void primSocketGetSockOpt()
{
	oop receiver = getReceiver();

	push (cIntToST(1));
	push (receiver);
}

void primSocketListen()
{
	oop receiver = getReceiver();
	oop backlog = getLocal( 0);

	oop handle = instVarAtInt(receiver, 0);

	if (!isUninterpretedBytes(handle) || !isSmallInteger(backlog)) {
//		LOGI("Socket listen failed 1");
		PRIMITIVE_FAIL(2);
	}

	int result = listen(asSocket(handle) -> socketHandle, stIntToC(backlog));

	if (result == -1) {
//		LOGI("Socket listen failed 2");
		PRIMITIVE_FAIL(3);
	}

//	LOGI("Socket listen");

	push (cIntToST(0));
	push (cIntToST(result));
}

void primSocketRecv()
{
	oop receiver = getReceiver();
	oop buffer = getLocal( 0);
	oop length = getLocal( 1);
	oop handle = instVarAtInt(receiver, 0);

	if (!isUninterpretedBytes(handle) || !isSmallInteger(length)) {
//		LOGI("Socket read failed");
		PRIMITIVE_FAIL(2);
	}

	int result = read(asSocket(handle)->socketHandle,
		(char *) asObjectHeader(buffer)->bodyPointer,
		stIntToC(length));

/*
	{
	int i;
	LOGI("Socket receive - length: %"PRIx64"", result);
	for (i = 0; i < result; i++)
		{
		uint8_t byte = ((uint8_t *)((asObjectHeader(buffer))->bodyPointer))[i];
		LOGI ("%0x: %0x %c", i, byte, (isprint(byte)?byte:'.'));
		}
	}
*/

	push (cIntToST(0));
	push (cIntToST(result));
}

void primSocketRecvFrom()
{
	oop receiver = getReceiver();

	push (cIntToST(1));
	push (receiver);
}

void primSocketRecvMsg()
{
	oop receiver = getReceiver();

	push (cIntToST(1));
	push (receiver);
}

void primSocketSend()
{
	oop receiver = getReceiver();
	oop buffer = getLocal( 0);
	oop length = getLocal( 1);
	oop handle = instVarAtInt(receiver, 0);

	if (!isUninterpretedBytes(handle) || !isSmallInteger(length))
		PRIMITIVE_FAIL(2);

	int result = write(asSocket(handle)->socketHandle,
		(uint8_t *)asObjectHeader(buffer)->bodyPointer,
		stIntToC(length));

/*
	{
	int i;
	LOGI("Socket send - length: %"PRIx64"", result);
	for (i = 0; i < result; i++)
		{
		uint8_t byte = ((uint8_t *)((asObjectHeader(buffer))->bodyPointer))[i];
		LOGI ("%0x: %0x %c", i, byte, (isprint(byte)?byte:'.'));
		}
	}
*/

	push (cIntToST(0));
	push (cIntToST(result));
}

void primSocketSendMsg()
{
	oop receiver = getReceiver();

	push (cIntToST(1));
	push (receiver);
}

void primSocketSendTo()
{
	oop receiver = getReceiver();

	push (cIntToST(1));
	push (receiver);
}

void primSocketSetOpt()
{
	oop receiver = getReceiver();

	push (cIntToST(1));
	push (receiver);
}

void primSocketShutdown()
{
	oop receiver = getReceiver();
	oop how = getLocal( 0);
	oop handle = instVarAtInt(receiver, 0);

	if (!isUninterpretedBytes(handle) || !isSmallInteger(how))
		PRIMITIVE_FAIL(2);

	int result = shutdown(asSocket(handle)->socketHandle, stIntToC(how));
//	LOGI("Socket shutdown");

	push (cIntToST(0));
	push (result);
}

void primSocketPair()
{
	oop receiver = getReceiver();

	push (cIntToST(1));
	push (receiver);
}

void primSocketClose()
{
	oop receiver = getReceiver();
	oop handle = instVarAtInt(receiver, 0);

	if (!isUninterpretedBytes(handle))
		PRIMITIVE_FAIL(1);

	int result = close(asSocket(handle)->socketHandle);
//	LOGI("Socket close");

	push (cIntToST(0));
	push (result);
}

struct pollfd fds[256];

// Receiver - doesn't matter
// Parameters:
// 1 - Array of socket handles
// 2 - Array of integers (events)
// 3 - Array of integers (revents)
// 4 - Integer (timeout)
// Return:
// 0: socket changes
// 1: timeout

#define ST_POLLIN   1
#define ST_POLLHUP  16
			   
void primSocketPoll()
{
	int i, pollResult;
	uint64_t cTimeout;
	oop receiver = getReceiver();
	oop handles = getLocal(0);
	oop events = getLocal(1);
	oop revents = getLocal(2);
	oop timeout = getLocal(3);
	sigset_t sigmask;
	struct timespec tmo_p;

	if (!isArray(handles) || !isArray(events) || !isArray(revents) || !isSmallInteger(timeout))
		PRIMITIVE_FAIL(1);

	if ((indexedObjectSize(handles) != indexedObjectSize(events))
		|| (indexedObjectSize(handles) != indexedObjectSize(revents)))
		PRIMITIVE_FAIL(1);

	for (i=0; i<indexedObjectSize(handles); i++) {
		oop handle = indexedVarAtInt (handles, i+1);

		if (!isUninterpretedBytes(handle))
			PRIMITIVE_FAIL(1);
		
		fds[i].fd = asSocket(handle)->socketHandle;
		fds[i].events = stIntToC(indexedVarAtInt(events, i+1));
		fds[i].revents = stIntToC(indexedVarAtInt(revents, i+1));
        if (ST_POLLIN == fds[i].events) fds[i].events = POLLIN;
	}

	cTimeout = stIntToC(timeout);
	tmo_p.tv_sec = cTimeout / 1000000;
	tmo_p.tv_nsec = (cTimeout % 1000000) * 1000;

	sigemptyset(&sigmask);

	pollResult = ppoll (fds, indexedObjectSize(handles), &tmo_p, &sigmask);

	for (i=0; i<indexedObjectSize(handles); i++) {
        if (POLLIN & fds[i].events) fds[i].events = ST_POLLIN;
        int revents = fds[i].revents; fds[i].revents = 0;
        if ( POLLIN & revents) fds[i].revents += ST_POLLIN;
        if (POLLHUP & revents) fds[i].revents += ST_POLLHUP;
        }

	for (i=0; i<indexedObjectSize(handles); i++) {
		indexedVarAtIntPut(events, i+1, cIntToST(fds[i].events));
		indexedVarAtIntPut(revents, i+1, cIntToST(fds[i].revents));
		}

	push (cIntToST(0));
	push (cIntToST(pollResult));
}

void primWebSocketPortNumber()
{
	push (cIntToST(0));
	push (cIntToST(webSocketPortNumber));
}

void primIoctl()
{
	int dataInt, result;
	unsigned int cmdInt;

	oop receiver = getReceiver();
	oop cmd = getLocal(0);
	oop data = getLocal(1);
	oop handle = instVarAtInt(receiver, 0);

	if (!isSmallInteger(cmd) || !isSmallInteger(data) || !isUninterpretedBytes(handle))
		PRIMITIVE_FAIL(2);

	cmdInt = stIntToC(cmd);
	dataInt = stIntToC(data);
	result = ioctl(asSocket(handle)->socketHandle, cmdInt, &dataInt);
	
	push (cIntToST(0));
	push (cIntToST(result));
}

void initializeSocketPrimitives()
{
		primitiveTable[PRIM_SOCKET_CREATE] = primSocketCreate;
		primitiveTable[PRIM_FAMILY_ADDRESS_PORT] = primSocketFamilyAddressPort;
		primitiveTable[PRIM_SOCKET_ACCEPT] = primSocketAccept;
		primitiveTable[PRIM_SOCKET_BIND] = primSocketBind;
		primitiveTable[PRIM_SOCKET_CONNECT] = primSocketConnect;
		primitiveTable[PRIM_SOCKET_GET_PEER_NAME] = primSocketGetPeerName;
		primitiveTable[PRIM_SOCKET_GET_SOCK_NAME] = primSocketGetSockName;
		primitiveTable[PRIM_SOCKET_GET_SOCK_OPT] = primSocketGetSockOpt;
		primitiveTable[PRIM_SOCKET_LISTEN] = primSocketListen;
		primitiveTable[PRIM_SOCKET_RECV] = primSocketRecv;
		primitiveTable[PRIM_SOCKET_RECV_FROM] = primSocketRecvFrom;
		primitiveTable[PRIM_SOCKET_RECV_MSG] = primSocketRecvMsg;
		primitiveTable[PRIM_SOCKET_SEND] = primSocketSend;
		primitiveTable[PRIM_SOCKET_SEND_MSG] = primSocketSendMsg;
		primitiveTable[PRIM_SOCKET_SEND_TO] = primSocketSendTo;
		primitiveTable[PRIM_SOCKET_SET_SOCK_OPT] = primSocketSetOpt;
		primitiveTable[PRIM_SOCKET_SHUTDOWN] = primSocketShutdown;
		primitiveTable[PRIM_SOCKET_SOCKET_PAIR] = primSocketPair;
		primitiveTable[PRIM_SOCKET_CLOSE] = primSocketClose;
		primitiveTable[PRIM_SOCKET_POLL] = primSocketPoll;
		primitiveTable[PRIM_WEBSOCKET_PORT_NUMBER] = primWebSocketPortNumber;
		primitiveTable[PRIM_IOCTL] = primIoctl;
}

