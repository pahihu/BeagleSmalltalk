// interpret.c
//
// Beagle Smalltalk
// Copyright (c) 2025 Simberon Incorporated
// Released under the MIT License
// https://opensource.org/license/MIT

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include "object.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <signal.h>


// When tracing == 1, the VM will dump calls, returns and bytecodes executed to
// stdout.  This is useful for very low-level debugging but it slows down the
// VM considerably.
int tracing = 0;

// The currentContext is the current stack frame for the VM.  It's in an object format
// so the PC is represented as a SmallInteger with the index of the current bytecode
// and the stack pointer is a SmallInteger with the index into the body of the context
// object of the current stack pointer.
//
// Since this machanism can be slow, we include a fastContext which has direct pointers
// to the bytecodes and the stack.  We create a fastContext when we start a method
// and use it to access bytecodes and the stack.  When we advance bytecodes and push or
// pop the stack, we also update the currentContext.
oop currentContext, stopFrame;
fastContextStruct fastContext;
memorySpaceStruct *currentStackSpace;

// The VM has its own low-level debugger.  This is invoked by the primitiveHalt method.
// When the debugger is activated, breakpointHit is set to true.
int breakpointHit = 0;


int suspended = FALSE;
char errorString[1024];
int eventWaitingFlag = FALSE;

uint64_t stackThreshold = WARN_STACK_THRESHOLD;

int (*pollForEvents)(void);
#define DUMP_SIZE 8192
char walkbackDump[DUMP_SIZE];


void classNameOf(oop aClass, char *className)
{
	strcpy (className, "No class name");

	if (classOf(aClass) == ST_METACLASS_CLASS) {
		STStringToC(asClass(asMetaclass(aClass)->thisClass)->name, className);
		strcat(className, " class");
	}
	else if (classOf(asClass(aClass)->name) == ST_BYTE_SYMBOL_CLASS)
		STStringToC(asClass(aClass)->name, className);
	else {
		strcpy (className, "No class name");
	}
}

void logMethodSignature()
{
	uint32_t i;
	char className[256];
	char selectorString[256];
	oop methodOop = asContext(currentContext)->method;

	if (isImmediate(methodOop))
		LOGI("Immediate??");

	if (methodOop == ST_NIL)
		LOGI("Method is nil??");

	if (classOf(methodOop) == ST_COMPILED_BLOCK_CLASS) {
		LOGI ("Block");
		return;
	}

	oop mclassOop = asCompiledMethod(methodOop)->mclass;

	oop methodDictionaryOop = asBehavior(mclassOop)->methodDictionary;
	oop key = identityDictionaryKeyAtValue (methodDictionaryOop, methodOop);

	STStringToC(key, selectorString);
	classNameOf(mclassOop, className);

	int offset = stIntToC(asContext(currentContext)->pcOffset);
	LOGI("%s >> %s (%x)", className, selectorString, offset);
}



void reportWalkback(char *message)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd, s, n;

	/* Obtain address(es) matching host/port */

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;	/* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
	hints.ai_flags = AI_NUMERICSERV;
	hints.ai_protocol = 0;		  /* Any protocol */

	s = getaddrinfo("simberon-office.dyndns.org", "47298", &hints, &result);
	if (s != 0) {
		return;
	}

	/* getaddrinfo() returns a list of address structures.
	   Try each address until we successfully connect(2).
	   If socket(2) (or connect(2)) fails, we (close the socket
	   and) try the next address. */

	sfd = -1;
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,
					 rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;				  /* Success */

		close(sfd);
	}

	if (rp == NULL) {			   /* No address succeeded */
		LOGW("Could not connect\n");
		return;
	}

	freeaddrinfo(result);		   /* No longer needed */
	/* Send message to the server */
	if (sfd != -1) {
		n = (int) write(sfd, message, (int) strlen(message));

		if (n < 0) {
			LOGW("ERROR writing to socket");
			return;
		}

		close(sfd);
	}
}

void debugDumpWalkback()
{
	dumpWalkback("Walkback");
}

void dumpWalkback(char *message)
{
	char selectorString[256], className[256];

	oop frame;
	walkbackDump[0]='\0';
	int walkbackIndex = 0;

	LOGW ("About to dump walkback");
	LOGW ("Message: %s", message);

	if (message != NULL) {
		walkbackIndex += snprintf(&walkbackDump[walkbackIndex], DUMP_SIZE - walkbackIndex, "%s\r", message);
	}

	if (currentContext == ST_NIL) {
		walkbackIndex += snprintf(&walkbackDump[walkbackIndex], DUMP_SIZE - walkbackIndex, "No current context\r");
		return;
	}

	for (frame = currentContext; asContext(frame)->method != ST_NIL; frame = asContext(frame)->frame)
	{
		int j;
		char receiverClassName[256];

		oop receiverOop = getReceiverForFrame(frame);
		oop receiverClassOop = classOf(receiverOop);

		if (classOf(receiverClassOop) == ST_METACLASS_CLASS)
		{
			oop receiverBaseClassOop = asMetaclass(receiverClassOop)->thisClass;
			STStringToC(asClass(receiverBaseClassOop)->name, receiverClassName);
			strcat (receiverClassName, " class");
		}
		else {
			STStringToC(asClass(receiverClassOop)->name, receiverClassName);
		}

		oop methodOop = asContext(frame)->method;

		if (classOf(methodOop) == ST_COMPILED_BLOCK_CLASS) {
			int offset = stIntToC(asContext(frame)->pcOffset);
			WALKBACK("\t%s (%x)", "Block", offset);
			walkbackIndex += snprintf(&walkbackDump[walkbackIndex], DUMP_SIZE - walkbackIndex, "\t%s (%x)\r", "Block", offset);
		}
		else {
			oop mclassOop = asCompiledMethod(methodOop)->mclass;

			if (classOf(mclassOop) == ST_METACLASS_CLASS) {
				STStringToC(asClass(asMetaclass(mclassOop)->thisClass)->name, className);
				strcat (className, " class");
			}
			else {
				STStringToC(asClass(mclassOop)->name, className);
			}

			int offset = stIntToC(asContext(frame)->pcOffset);

			oop identityDictionaryOop = asBehavior(mclassOop)->methodDictionary;
			oop selectorOop = identityDictionaryKeyAtValue (identityDictionaryOop, methodOop);
			STStringToC(selectorOop, selectorString);
			if (mclassOop == receiverClassOop) {
				WALKBACK ("\t%s >> %s (%x)", className, selectorString, offset);
				walkbackIndex += snprintf(&walkbackDump[walkbackIndex],
					   +						   DUMP_SIZE - walkbackIndex, "\t%s >> %s (%x)\r",
												  className, selectorString, offset);
			}
			else {
				WALKBACK ("\t%s(%s) >> %s (%x)", receiverClassName, className, selectorString, offset);
				walkbackIndex += snprintf(&walkbackDump[walkbackIndex],
												  DUMP_SIZE - walkbackIndex,
												  "\t%s(%s) >> %s (%x)\r", receiverClassName,
												  className, selectorString, offset);
			}
		}
	}
}

void captureFastContext(oop context)
{
	contextStruct *contextBody = asContext(context);

	fastContext.stackSpaceFirstFreeBlock = &currentStackSpace->firstFreeBlock;
	fastContext.currentContextBody = contextBody;
	fastContext.stackPointer = oopPtr(&contextBody->stackBody[stIntToC(contextBody->stackOffset)]);
	fastContext.stackOffsetPointer = (oop *) &contextBody->stackOffset;
	if (contextBody->frame != ST_NIL)
		fastContext.localsPointer = oopPtr(&asContext(contextBody->frame)->stackBody[stIntToC(asContext(contextBody->frame)->stackOffset)]);
	
	if (contextBody->method != ST_NIL) {
		fastContext.currentPCPointer = &((uint8_t *)asObjectHeader(getBytecodes(contextBody->method))->bodyPointer)[stIntToC(contextBody->pcOffset)];
		fastContext.currentPCOffset = oopPtr(&contextBody->pcOffset);
	}
}

void setupInterpreter(memorySpaceStruct *space)
{
	currentContext = newInstanceOfClass(ST_CODE_CONTEXT_CLASS, 0, currentStackSpace);
	contextStruct *newContext = asContext(currentContext);

	stopFrame = ST_NIL;

	newContext->frame = ST_NIL; // frame
	newContext->stackOffset = cIntToST(0); // stack offset
	newContext->pcOffset = cIntToST(0);  // pc
	newContext->method = ST_NIL;  // method
	newContext->methodContext = ST_NIL;  // method context
	newContext->contextId = markAsContextPointer(currentContext);
	errorString[0] = '\0';

	captureFastContext(currentContext);
	scavenge();
	auditImage();
}

oop findCompiledMethod (oop selector, oop class)
{
	return identityDictionaryAt (asBehavior(class)->methodDictionary, selector);
}

int checkForOutOfStack()
{
	if (asOop(fastContext.stackPointer) + stackThreshold > (((uint64_t) StackSpace) + StackSpace->spaceSize)) {
		dumpWalkback("Out of stack space");
		LOGE("Out of stack space");
		exit(1);
	}

	return(0);
}

void invoke(oop method, uint64_t numArgs)
{

	int i;
	oop previousFrameStackOffset = cIntToST(stIntToC(asContext(currentContext)->stackOffset) -1 - numArgs);
	oop newContextOop;

	if (checkForOutOfStack())
		return;

//	currentStackSpace->lastFreeBlock -= sizeof(objectHeaderStruct) / sizeof(oop);
//	header = asOop(&currentStackSpace->space[space->lastFreeBlock]);

//	newContext.stack = context->stack;
//	context->stack = asOop(&oopPtr(context->stack)[-1 - numArgs]);

	for (i=0; i < stIntToC (asCompiledMethod(method)->numberOfTemporaries); i++)
		push (ST_NIL);

	newContextOop = newInstanceOfClass(ST_CODE_CONTEXT_CLASS, 0, currentStackSpace);
	contextStruct *newContextStruct = asContext(newContextOop);

	newContextStruct->frame = currentContext;  // frame
	newContextStruct->stackOffset = cIntToST(0);  // stack
	newContextStruct->pcOffset = cIntToST(0);  // pcOffset
	newContextStruct->method = method; // method
	newContextStruct->methodContext = ST_NIL; // method context
	newContextStruct->contextId = markAsContextPointer(newContextOop);

	currentContext = newContextOop;
	previousFrame(currentContext)->stackOffset = previousFrameStackOffset;
	captureFastContext(currentContext);
}

void invokeBlock(oop blockClosureOop, uint64_t numArgs)
{
	uint32_t i, numberOfCopiedValues, numberOfTemporaries;
	oop previousFrameStackOffset = cIntToST(stIntToC(asContext(currentContext)->stackOffset) -1 - numArgs);
	oop newContextOop;

	if (checkForOutOfStack())
		return;

	if (asBlockClosure(blockClosureOop)->copiedValues != ST_NIL) {
		numberOfCopiedValues = indexedObjectSize(asBlockClosure(blockClosureOop)->copiedValues);
		for (i=0; i < numberOfCopiedValues; i++) {
			push (instVarAtInt(asBlockClosure(blockClosureOop)->copiedValues, i));
		}
	}

	numberOfTemporaries = stIntToC (asCompiledMethod(asBlockClosure(blockClosureOop) -> method)->numberOfTemporaries);
	for (i=0; i < numberOfTemporaries; i++)
		push (ST_NIL);

	newContextOop = newInstanceOfClass(ST_CODE_CONTEXT_CLASS, 0, currentStackSpace);
	contextStruct *newContextStruct = asContext(newContextOop);

	newContextStruct->frame = currentContext;  // frame
	newContextStruct->stackOffset = cIntToST(0);  // stack
	newContextStruct->pcOffset = cIntToST(0);  // pcOffset
	newContextStruct->method = asBlockClosure(blockClosureOop) -> method; // method
	newContextStruct->contextId = markAsContextPointer(newContextOop);
	if (asBlockClosure(blockClosureOop)->methodContext == ST_NIL)
		newContextStruct->methodContext = ST_NIL;  // method context		
	else
		newContextStruct->methodContext = asBlockClosure(blockClosureOop)->methodContext;  // method context

	currentContext = newContextOop;

	previousFrame(currentContext)->stackOffset = previousFrameStackOffset;
	captureFastContext(currentContext);
}

int returnFromContext()
{
	oop returnValue;

	returnValue = pop ();
	currentContext = asContext(currentContext)->frame;
	currentStackSpace->lastFreeBlock = (currentContext - (uint64_t)&currentStackSpace->space[0])/ sizeof(oop) - 1;
	currentStackSpace->firstFreeBlock = (((uint64_t)&asContext(currentContext)->stackBody[stIntToC(asContext(currentContext)->stackOffset)] - ((uint64_t)&currentStackSpace->space[0]))/ sizeof(oop));
	asObjectHeader(currentContext)->size = sizeof(objectHeaderStruct) + sizeof(contextStruct) + (stIntToC(asContext(currentContext)->stackOffset) * sizeof(oop));
	captureFastContext(currentContext);
	push (returnValue);

	if (tracing)
		LOGI ("Return from context");

	if (currentContext == stopFrame) {
		eventWaitingFlag = TRUE;
		return FALSE;
		}
	else
		return TRUE;
}

// The Polymorphic Inline Cache (PIC) isn't currently implemented.  This is a
// placeholder for that functionality once it's implemented.
//
// picLookup locates a method in the PIC
// picRegister registers a method call in the PIC

oop picLookup(oop methodOop, oop pcOop, oop classOop)
{
	return ST_NIL;
}

void picRegister(oop methodOop, oop pcOop, oop classOop, oop locatedMethodOop)
{
}

void dispatch (oop selector, uint64_t numArgs)
{

	oop receiver = asContext(currentContext)->stackBody[stIntToC(asContext(currentContext)->stackOffset) - 1 - numArgs];
	oop class, receiverClassOop;
	oop method;
	oop picFound;
	receiverClassOop = classOf(receiver);

//	classNameOf(receiverClassOop, className);
//	STStringToC(selector, selectorString);
//	LOGI ("Dispatching %s to a %s",  selectorString, className);

	class = receiverClassOop;
	picFound = ST_NIL;
	
	if (((method = picFound = picLookup(asContext(currentContext)->method, stIntToC(asContext(currentContext)->pcOffset), class)) == ST_NIL)
			|| (picFound == ST_TRUE)) {
		while ((method = findCompiledMethod(selector, class)) == ST_NIL) {
			if (asBehavior(class)->superclass == ST_NIL) {
				char selectorString[256], className[256];
				oop classNameSymbol;
				int i;

				STStringToC(selector, selectorString);
				LOGI("Message not understood: %s", selectorString);

				receiverClassOop = classOf(receiver);
				if (classOf(receiverClassOop) == ST_METACLASS_CLASS) {
					STStringToC(asClass(asMetaclass(receiverClassOop)->thisClass)->name, className);
					strcat(className, " class");
				}
				else
					STStringToC(asClass(receiverClassOop)->name, className);

				LOGW ("%s does not understand \"%s\"", className, selectorString);
				snprintf(errorString, 1024, "%s does not understand \"%s\"", className,
							selectorString);

				dumpWalkback(errorString);
				raiseSTError (ST_MESSAGE_NOT_UNDERSTOOD_CLASS, walkbackDump);
				errorString[0]='\0';
				return;
			}
			class = asBehavior(class)->superclass;
		}
		if (picFound == ST_NIL)
			picRegister(asContext(currentContext)->method, stIntToC(asContext(currentContext)->pcOffset), receiverClassOop, method);
	}

	if (tracing)
	{
	char selectorString[256];
		STStringToC(selector, selectorString);
		LOGI ("Dispatch %s", selectorString);
		//dumpWalkback("dispatch");
	}

	invoke(method, numArgs);
}

void dispatchSuper (oop selector, uint64_t numArgs)
{
	oop receiver = asContext(currentContext)->stackBody[stIntToC(asContext(currentContext)->stackOffset) - 1 - numArgs];
	oop class, receiverClassOop;
	oop method;
	oop picFound = ST_NIL;

	receiverClassOop = class = asBehavior(pop ()) -> superclass;

	if (((method = picFound = picLookup(asContext(currentContext)->method, stIntToC(asContext(currentContext)->pcOffset), class)) == ST_NIL)
		|| (picFound == ST_TRUE)) {
		while ((method = findCompiledMethod(selector, class)) == ST_NIL) {
			if (asBehavior(class)->superclass == ST_NIL) {
					char selectorString[256], className[256];
					oop classNameSymbol;
					int i;

					STStringToC(selector, selectorString);

					receiverClassOop = classOf(receiver);
					if (classOf(receiverClassOop) == ST_METACLASS_CLASS) {
						STStringToC(asClass(asMetaclass(receiverClassOop)->thisClass)->name, className);
						strcat(className, " class");
					}
					else
						STStringToC(asClass(receiverClassOop)->name, className);

					LOGW ("%s does not understand \"%s\"", className, selectorString);
					snprintf(errorString, 1024, "%s does not understand \"%s\"", className,
							selectorString);

					dumpWalkback(errorString);
					raiseSTError (ST_MESSAGE_NOT_UNDERSTOOD_CLASS, walkbackDump);
					errorString[0]='\0';
					return;
			}
			class = asBehavior(class)->superclass;
		}
		if (picFound == ST_NIL)
			picRegister(asContext(currentContext)->method, stIntToC(asContext(currentContext)->pcOffset), receiverClassOop, method);
	}

	if (tracing)
	{
		char selectorString[256];
		STStringToC(selector, selectorString);
		LOGI ("Dispatch super %s", selectorString);
		dumpWalkback("dispatch super");
	}

	invoke(method, numArgs);
}

void dispatchSpecial0 (unsigned int selectorNumber, oop receiver)
{
	push (receiver);
	dispatch(specialSelectors(selectorNumber), specialSelectorArguments(selectorNumber));
}

void dispatchSpecial1 (unsigned int selectorNumber, oop receiver, oop arg1)
{
	push (receiver);
	push (arg1);
	dispatch(specialSelectors(selectorNumber), specialSelectorArguments(selectorNumber));
}

void dispatchSpecial2 (unsigned int selectorNumber, oop receiver, oop arg1, oop arg2)
{
	push (receiver);
	push (arg1);
	push (arg2);
	dispatch(specialSelectors(selectorNumber), specialSelectorArguments(selectorNumber));
}

void callWellKnown(void)
{
	uint8_t selectorNumber = nextBytecode();
	switch (selectorNumber)
	{
		case SPECIAL_PLUS:	// +
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (isSmallInteger(receiver) && isSmallInteger(arg)) {
					int64_t result = stIntToC(receiver) + stIntToC(arg);
					int64_t carry = result & (int64_t)0xF000000000000000L;
					if ((carry == 0) || (carry == (int64_t)0xF000000000000000L))
						push (cIntToST(result));
					else
						push (asSumLargeInteger(result));
				}
				else
					if (isFloat(receiver) && isFloat(arg)) {
						double result = stFloatToC(receiver) + stFloatToC(arg);
						push (cFloatToST(result));
						}
					else
						dispatchSpecial1 (SPECIAL_PLUS, receiver, arg);
			}
			break;
		case SPECIAL_MINUS:	// -
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (isSmallInteger(receiver) && isSmallInteger(arg)) {
					int64_t result = stIntToC(receiver) - stIntToC(arg);
					int64_t carry = result & (int64_t)0xF000000000000000L;
					if ((carry == 0) || (carry == (int64_t)0xF000000000000000L))
						push (cIntToST(result));
					else
						push (asSumLargeInteger(result));
				}
				else
					if (isFloat(receiver) && isFloat(arg)) {
						double result = stFloatToC(receiver) - stFloatToC(arg);
						push (cFloatToST(result));
						}
					else
						dispatchSpecial1 (SPECIAL_MINUS, receiver, arg);
			}
			break;
		case SPECIAL_TIMES:	// *
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (isSmallInteger(receiver) && isSmallInteger(arg)) {
					int64_t x = stIntToC(receiver);
					int64_t y = stIntToC(arg);

					if ((ABS(x) < 0x40000000L) && (ABS(y) < 0x40000000L)) {
						push (cIntToST(x * y));
					}
					else {	
						dispatchSpecial1 (SPECIAL_TIMES, smallToLargeInteger(receiver), smallToLargeInteger(arg));
					}
				}
				else
					if (isFloat(receiver) && isFloat(arg)) {
						double result = stFloatToC(receiver) * stFloatToC(arg);
						push (cFloatToST(result));
						}
					else
						dispatchSpecial1 (SPECIAL_TIMES, receiver, arg);
			}
			break;
		case SPECIAL_NOT:	// not
			{
				oop receiver = pop ();
				if (receiver == ST_TRUE) {
					push (ST_FALSE);
				}
				else
					if (receiver == ST_FALSE)
						push (ST_TRUE);
					else
						dispatchSpecial0 (SPECIAL_NOT, receiver);
			}
			break;
		case SPECIAL_IDENTICAL:	// ==
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (arg == receiver)
						push (ST_TRUE);
				else
						push (ST_FALSE);
			}
			break;
		case SPECIAL_NOT_IDENTICAL:	// ~~
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (arg == receiver)
						push (ST_FALSE);
				else
						push (ST_TRUE);
			}
			break;
		case SPECIAL_EQUALS:	// =
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (isSmallInteger(receiver) && isSmallInteger(arg))
					if (stIntToC(receiver) == stIntToC(arg))
						push (ST_TRUE);
					else
						push (ST_FALSE);
				else
					if (isFloat(receiver) && isFloat(arg))
						if (stFloatToC(receiver) == stFloatToC(arg))
							push (ST_TRUE);
						else
							push (ST_FALSE);
					else
						dispatchSpecial1 (SPECIAL_EQUALS, receiver, arg);
			}
			break;
		case SPECIAL_NOT_EQUALS:	// ~=
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (isSmallInteger(receiver) && isSmallInteger(arg))
					if (stIntToC(receiver) == stIntToC(arg))
						push (ST_FALSE);
					else
						push (ST_TRUE);
				else
					if (isFloat(receiver) && isFloat(arg))
						if (stFloatToC(receiver) == stFloatToC(arg))
							push (ST_FALSE);
						else
							push (ST_TRUE);
					else
						dispatchSpecial1 (SPECIAL_NOT_EQUALS, receiver, arg);
			}
			break;
		case SPECIAL_IS_NIL:	// isNil
			{
				oop receiver = pop ();
				if (receiver == ST_NIL)
					push (ST_TRUE);
				else
					push (ST_FALSE);							
			}
			break;
		case SPECIAL_NOT_NIL:	// notNil
			{
				oop receiver = pop ();
				if (receiver == ST_NIL)
					push (ST_FALSE);							
				else
					push (ST_TRUE);
			}
			break;
		case SPECIAL_GREATER_THAN:	// >
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (isSmallInteger(receiver) && isSmallInteger(arg))
					if (stIntToC(receiver) > stIntToC(arg))
						push (ST_TRUE);
					else
						push (ST_FALSE);
				else
					if (isFloat(receiver) && isFloat(arg))
						if (stFloatToC(receiver) > stFloatToC(arg))
							push (ST_TRUE);
						else
							push (ST_FALSE);
					else
						dispatchSpecial1 (SPECIAL_GREATER_THAN, receiver, arg);
			}
			break;
		case SPECIAL_LESS_THAN:	// <
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (isSmallInteger(receiver) && isSmallInteger(arg))
					if (stIntToC(receiver) < stIntToC(arg))
						push (ST_TRUE);
					else
						push (ST_FALSE);
				else
					if (isFloat(receiver) && isFloat(arg))
						if (stFloatToC(receiver) < stFloatToC(arg))
							push (ST_TRUE);
						else
							push (ST_FALSE);
					else
						dispatchSpecial1 (SPECIAL_LESS_THAN, receiver, arg);
			}
			break;
		case SPECIAL_GREATER_THAN_OR_EQUAL:	// >=
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (isSmallInteger(receiver) && isSmallInteger(arg))
					if (stIntToC(receiver) >= stIntToC(arg))
						push (ST_TRUE);
					else
						push (ST_FALSE);
				else
					if (isFloat(receiver) && isFloat(arg))
						if (stFloatToC(receiver) >= stFloatToC(arg))
							push (ST_TRUE);
						else
							push (ST_FALSE);
					else
						dispatchSpecial1 (SPECIAL_GREATER_THAN_OR_EQUAL, receiver, arg);
			}
			break;
		case SPECIAL_LESS_THAN_OR_EQUAL:	// <=
			{
				oop arg = pop ();
				oop receiver = pop ();
				if (isSmallInteger(receiver) && isSmallInteger(arg))
					if (stIntToC(receiver) <= stIntToC(arg))
						push (ST_TRUE);
					else
						push (ST_FALSE);
				else
					if (isFloat(receiver) && isFloat(arg))
						if (stFloatToC(receiver) <= stFloatToC(arg))
							push (ST_TRUE);
						else
							push (ST_FALSE);
					else
						dispatchSpecial1 (SPECIAL_LESS_THAN_OR_EQUAL, receiver, arg);
			}
			break;
		case SPECIAL_EVALUATE:	// evaluate:
			{
				oop arg = pop ();
				oop receiver = pop ();
				dispatchSpecial1 (SPECIAL_EVALUATE, receiver, arg);
			}
		break;

		case SPECIAL_PRINT_STRING:	// printString
			{
				oop receiver = pop ();
				dispatchSpecial0 (SPECIAL_PRINT_STRING, receiver);
			}
			break;
		case SPECIAL_RAISE_SIGNAL:	// raiseSignal
			{
				oop receiver = pop ();
				dispatchSpecial0 (SPECIAL_RAISE_SIGNAL, receiver);
			}
			break;
		case SPECIAL_PERFORM_WITH_ARGUMENTS:	// perform:withArguments:
			{
				oop args = pop ();
				oop selector = pop ();
				oop receiver = pop ();

				int i, argCount = 0;
				for (i=1; i <= basicByteSize(selector); i++)
					if (basicByteAtInt(selector, i) == ':')
						argCount++;

				if (!isalnum(basicByteAtInt(selector, 1)))
					argCount++;

				if (argCount != indexedObjectSize(args)) {
					LOGE("perform:withArguments: called with %"PRId64" arguments when it expected %d", indexedObjectSize(args), argCount);
					break;
				}

				push (receiver);
				for (i=0; i<indexedObjectSize(args); i++) {
					push (instVarAtInt(args, i));
				}

				char selectorString[256];
				STStringToC(selector, selectorString);
					dispatch (selector, indexedObjectSize(args));
			}
			break;

		case SPECIAL_HALT:	//  primitive halt
			{
//				oop receiver = pop ();
				breakpointHit = TRUE;
				eventWaitingFlag = TRUE;
			}
			break;

		case SPECIAL_DEBUGIT:	// debugIt:
			{
				oop arg = pop ();
				oop receiver = pop ();
				dispatchSpecial1 (SPECIAL_DEBUGIT, receiver, arg);
			}
			break;

		case SPECIAL_EVALUATE_JSON:	// evaluateJsonString:
			{
				oop arg = pop ();
				oop receiver = pop ();
				dispatchSpecial1 (SPECIAL_EVALUATE_JSON, receiver, arg);
			}
			break;

		default:
			break;
	}
}


void interpret(void)
{
	suspended = 0;
	basicInterpret (0);
}

int basicInterpret (int maxBytecodes)
{
	int count = 0;
	suspended = 0;
	breakpointHit = FALSE;
	errorString[0] = '\0';
	oop junk;

	eventWaitingFlag = maxBytecodes > 0;

	while (1) {
		uint8_t bytecode;

		if (tracing) {
			logString[0]='\0';
			logPtr = logString;
			logBytecode();
			printf ("\n=== %s\n\n", logString);
		}

		if (asContext(currentContext)->method == ST_NIL) {
			simlog ("Exitted\n");
			return TRUE;
		}

		bytecode = nextBytecode();

		switch (bytecode)
		{
		case 0x00: //  push inst var 0
		case 0x01: //  push inst var 1
		case 0x02: //  push inst var 2
		case 0x03: //  push inst var 3
		case 0x04: //  push inst var 4
		case 0x05: //  push inst var 5
		case 0x06: //  push inst var 6
		case 0x07: //  push inst var 7
		case 0x08: //  push inst var 8
		case 0x09: //  push inst var 9
		case 0x0a: //  push inst var 10
		case 0x0b: //  push inst var 11
		case 0x0c: //  push inst var 12
		case 0x0d: //  push inst var 13
		case 0x0e: //  push inst var 14
		case 0x0f:  //  push inst var 15
			push (instVarAtInt(getReceiver(),bytecode));
			break;

		case 0x10:  //  store inst var 0
		case 0x11:  //  store inst var 1
		case 0x12:  //  store inst var 2
		case 0x13:  //  store inst var 3
		case 0x14:  //  store inst var 4
		case 0x15:  //  store inst var 5
		case 0x16:  //  store inst var 6
		case 0x17:  //  store inst var 7
		case 0x18:  //  store inst var 8
		case 0x19:  //  store inst var 9
		case 0x1a:  //  store inst var 10
		case 0x1b:  //  store inst var 11
		case 0x1c:  //  store inst var 12
		case 0x1d:  //  store inst var 13
		case 0x1e:  //  store inst var 14
		case 0x1f:  //  store inst var 15
		{
			oop value = top ();
			instVarAtIntPut(getReceiver(), bytecode & 0x0f, value);
			break;
		}

		case 0x20:  //  push local 1
		case 0x21:  //  push local 2
		case 0x22:  //  push local 3
		case 0x23:  //  push local 4
		case 0x24:  //  push local 5
		case 0x25:  //  push local 6
		case 0x26:  //  push local 7
		case 0x27:  //  push local 8
		case 0x28:  //  push local 9
		case 0x29:  //  push local 10
		case 0x2a:  //  push local 11
		case 0x2b:  //  push local 12
		case 0x2c:  //  push local 13
		case 0x2d:  //  push local 14
		case 0x2e:  //  push local 15
		case 0x2f:  //  push local 16
			push (getLocal( bytecode & 0x0f));
			break;

		case 0x30:  //  store local 1
		case 0x31:  //  store local 2
		case 0x32:  //  store local 3
		case 0x33:  //  store local 4
		case 0x34:  //  store local 5
		case 0x35:  //  store local 6
		case 0x36:  //  store local 7
		case 0x37:  //  store local 8
		case 0x38:  //  store local 9
		case 0x39:  //  store local 10
		case 0x3a:  //  store local 11
		case 0x3b:  //  store local 12
		case 0x3c:  //  store local 13
		case 0x3d:  //  store local 14
		case 0x3e:  //  store local 15
		case 0x3f:  //  store local 16
			setLocal( bytecode & 0x0f, top ());
			break;

		case 0x40: //  push static 1
		case 0x41: //  push static 2
		case 0x42: //  push static 3
		case 0x43: //  push static 4
		case 0x44: //  push static 5
		case 0x45: //  push static 6
		case 0x46: //  push static 7
		case 0x47: //  push static 8
		case 0x48: //  push static 9
		case 0x49: //  push static 10
		case 0x4a: //  push static 11
		case 0x4b: //  push static 12
		case 0x4c: //  push static 13
		case 0x4d: //  push static 14
		case 0x4e: //  push static 15
		case 0x4f: //  push static 16
			push (asAssociation(getLiteral(asContext(currentContext)->method, bytecode & 0x0f))->value);
			break;


		case 0x50: //  store static 1
		case 0x51: //  store static 2
		case 0x52: //  store static 3
		case 0x53: //  store static 4
		case 0x54: //  store static 5
		case 0x55: //  store static 6
		case 0x56: //  store static 7
		case 0x57: //  store static 8
		case 0x58: //  store static 9
		case 0x59: //  store static 10
		case 0x5a: //  store static 11
		case 0x5b: //  store static 12
		case 0x5c: //  store static 13
		case 0x5d: //  store static 14
		case 0x5e: //  store static 15
		case 0x5f: //  store static 16
		{
			oop association = getLiteral(asContext(currentContext)->method, bytecode & 0x0f);
			asAssociation(association)->value = top ();
			registerIfNeeded (association, asAssociation(association)->value);
			break;
		}

		case 0x60:  //  push 1
		case 0x61:  //  push 2
		case 0x62:  //  push 3
		case 0x63:  //  push 4
		case 0x64:  //  push 5
		case 0x65:  //  push 6
		case 0x66:  //  push 7
		case 0x67:  //  push 8
		case 0x68:  //  push 9
		case 0x69:  //  push 10
		case 0x6a:  //  push 11
		case 0x6b:  //  push 12
		case 0x6c:  //  push 13
		case 0x6d:  //  push 14
		case 0x6e:  //  push 15
		case 0x6f:  //  push 16
			push (cIntToST((bytecode & 0x0f) + 1));
			break;

		case 0x70:  //  push 0
		case 0x71:  //  push -1
		case 0x72:  //  push -2
		case 0x73:  //  push -3
		case 0x74:  //  push -4
		case 0x75:  //  push -5
		case 0x76:  //  push -6
		case 0x77:  //  push -7
		case 0x78:  //  push -8
		case 0x79:  //  push -9
		case 0x7a:  //  push -10
		case 0x7b:  //  push -11
		case 0x7c:  //  push -12
		case 0x7d:  //  push -13
		case 0x7e:  //  push -14
		case 0x7f:  //  push -15
			push (cIntToST(-(bytecode & 0x0f)));
			break;

		case 0x80: //  push literal 1
		case 0x81: //  push literal 2
		case 0x82: //  push literal 3
		case 0x83: //  push literal 4
		case 0x84: //  push literal 5
		case 0x85: //  push literal 6
		case 0x86: //  push literal 7
		case 0x87: //  push literal 8
		case 0x88: //  push literal 9
		case 0x89: //  push literal 10
		case 0x8a: //  push literal 11
		case 0x8b: //  push literal 12
		case 0x8c: //  push literal 13
		case 0x8d: //  push literal 14
		case 0x8e: //  push literal 15
		case 0x8f: //  push literal 16
			push (getLiteral(asContext(currentContext)->method, bytecode & 0x0f));
			break;

		case 0x90:  //  push true
			push (ST_TRUE);
			break;

		case 0x91:  //  push false
			push (ST_FALSE);
			break;

		case 0x92:  //  push nil
			push (ST_NIL);
			break;

		case 0x93:  //  push self
			push (getReceiver());
			break;

		case 0x94: //  push inst var
		{
			uint8_t instVarNumber = nextBytecode();
			push (instVarAtInt(getReceiver(),instVarNumber));
		}
		break;

		case 0x95: //  push inst var extended
		{
			uint16_t instVarNumber = nextBytecode() * 256 + nextBytecode();
			push (instVarAtInt(getReceiver(),instVarNumber));
		}
		break;

		case 0x96: //  push local
		{
			uint8_t localNumber = nextBytecode();
			push (getLocal( localNumber));
		}
		break;

		case 0x97: //  push local extended
		{
			uint16_t localNumber = nextBytecode() * 256 + nextBytecode();
			push (getLocal( localNumber));
		}
		break;

		case 0x98: //  push local indirect
		case 0xb7: //  push self instvar indirect
		{
			uint8_t localNumber = nextBytecode();
			uint8_t varNumber = nextBytecode();
			oop copiedVariables = getLocal( localNumber);
			push (instVarAtInt (copiedVariables, varNumber));
		}
		break;

		case 0x99: //  push local indirect extended
		case 0xb8: //  push self instvar indirect extended
		{
			uint16_t localNumber = nextBytecode() * 256 + nextBytecode();
			uint16_t varNumber = nextBytecode() * 256 + nextBytecode();
			oop copiedVariables = getLocal( localNumber);
			push (instVarAtInt (copiedVariables, varNumber));
		}
		break;

		case 0x9a: //  push global
		{
			uint8_t literalNumber = nextBytecode();			
			push (asAssociation(getLiteral(asContext(currentContext)->method, literalNumber))->value);
		}
		break;

		case 0x9b: //  push global extended
		{
			uint16_t literalNumber = nextBytecode() * 256 + nextBytecode();
			push (asAssociation(getLiteral(asContext(currentContext)->method, literalNumber))->value);
		}
		break;

		case 0x9c: //  push literal
		{
			uint8_t literalNumber = nextBytecode();			
			push (getLiteral(asContext(currentContext)->method, literalNumber));
		}
		break;

		case 0x9d: //  push literal extended
		{
			uint16_t literalNumber = nextBytecode() * 256 + nextBytecode();
			push (getLiteral(asContext(currentContext)->method, literalNumber));
		}
		break;

		case 0x9e: //  push one byte integer
		{
			int8_t integer = (int8_t) nextBytecode();
			push (cIntToST(integer));
		}
		break;

		case 0x9f: //  push two byte integer
		{
			int16_t integer = (int16_t) (nextBytecode() * 256 + nextBytecode());
			push (cIntToST(integer));
		}
		break;

		case 0xa0: //  push four byte integer
		{
			int32_t integer = (int32_t) (
				nextBytecode() * 16777216
				+ nextBytecode() * 65536
				+ nextBytecode() * 256
				+ nextBytecode());
			push (cIntToST(integer));
		}
		break;

		case 0xa1: //  push copying block
		{
			uint8_t literalNumber = nextBytecode();
			uint8_t numberOfCopiedVariables = nextBytecode();
			oop compiledBlock;
			oop blockClosureOop;
			oop array;
			int i;

			blockClosureOop = newInstanceOfClass (ST_BLOCK_CLOSURE_CLASS, 0, EdenSpace);

			compiledBlock = getLiteral(asContext(currentContext)->method, literalNumber);
			asBlockClosure(blockClosureOop) -> method = compiledBlock;
			registerIfNeeded(blockClosureOop, compiledBlock);

			push (blockClosureOop);
			array = newInstanceOfClass (ST_ARRAY_CLASS, numberOfCopiedVariables, EdenSpace);
			blockClosureOop = pop ();

			asBlockClosure(blockClosureOop) -> copiedValues = array;
			registerIfNeeded(blockClosureOop, array);

			for (i=numberOfCopiedVariables ; i > 0 ; i--) {
				oop object = pop ();
				indexedVarAtIntPut (array, i, object);
			}

			push (blockClosureOop);
		}
		break;

		case 0xa2: //  push full block
		{
			uint8_t literalNumber = nextBytecode();
			uint8_t numberOfCopiedVariables = nextBytecode();
			oop compiledBlock;
			oop blockClosureOop;
			oop array;
			int i;

			blockClosureOop = newInstanceOfClass (ST_BLOCK_CLOSURE_CLASS, 0, EdenSpace);
			compiledBlock = getLiteral(asContext(currentContext)->method, literalNumber);
			asBlockClosure(blockClosureOop) -> method = compiledBlock;
			registerIfNeeded(blockClosureOop, compiledBlock);

			if (stripTags(asContext(currentContext) -> methodContext) == ST_NIL)
				asBlockClosure(blockClosureOop) -> methodContext = markAsContextPointer(currentContext);
			else
				asBlockClosure(blockClosureOop) -> methodContext = asContext(currentContext)->methodContext;

			push (blockClosureOop);
			array = newInstanceOfClass (ST_ARRAY_CLASS, numberOfCopiedVariables, EdenSpace);
			blockClosureOop = pop ();
			
			asBlockClosure(blockClosureOop) -> copiedValues = array;
			registerIfNeeded(blockClosureOop, array);

			for (i=numberOfCopiedVariables ; i > 0 ; i--) {
				oop object = pop ();
				indexedVarAtIntPut (array, i, object);
				}
			push (blockClosureOop);
		}
		break;

		case 0xa3: //  store inst var
			{
			uint8_t instVarNumber = nextBytecode();		
			oop value = top ();
			instVarAtIntPut(getReceiver(), instVarNumber, value);
			}			
			break;

		case 0xa4: //  store inst var extended
			{
			uint16_t instVarNumber = nextBytecode() * 256 + nextBytecode();
			oop value = top ();
			instVarAtIntPut(getReceiver(), instVarNumber, value);
			}			
			break;

		case 0xa5: //  store local
			{
			uint8_t instVarNumber = nextBytecode();		
			setLocal( instVarNumber, top ());
			}			
			break;

		case 0xa6: //  store local extended
			{
			uint16_t instVarNumber = nextBytecode() * 256 + nextBytecode();
			setLocal( instVarNumber, top ());
			}			
			break;

		case 0xa7: //  store local indirect
		case 0xb9: //  store self instvar indirect
			{
			uint8_t localNumber = nextBytecode();
			uint8_t varNumber = nextBytecode();
			oop copiedVariables = getLocal( localNumber);
			if (isImmediate(copiedVariables)) {
				LOGE("Indirect store into immediate object");
				dumpWalkback("Indirect store into immediate object");
				exit(1);
			}
			if (totalObjectSize(copiedVariables) < varNumber) {
				LOGE("Indirect store out of bounds");
				dumpWalkback("Indirect store out of bounds");
				exit(1);
			}
			oop value = top ();
			instVarAtIntPut (copiedVariables, varNumber, value);
			}
			break;

		case 0xa8: //  store local indirect extended
		case 0xba: //  store self instvar indirect extended
			{
			uint16_t localNumber = nextBytecode() * 256 + nextBytecode();
			uint16_t varNumber = nextBytecode() * 256 + nextBytecode();
			oop copiedVariables = getLocal( localNumber);
			oop value = top ();
			instVarAtIntPut (copiedVariables, varNumber, value);
			}
			break;


		case 0xa9: //  store global
			{
			uint8_t literalNumber = nextBytecode();
			oop association = getLiteral(asContext(currentContext)->method, literalNumber);
			asAssociation(association)->value = top ();
			registerIfNeeded(association, asAssociation(association)->value);
			}
			break;

		case 0xaa: //  store global extended
			{
			uint16_t literalNumber = nextBytecode() * 256 + nextBytecode();
			oop association = getLiteral(asContext(currentContext)->method, literalNumber);
			asAssociation(association)->value = top ();
			registerIfNeeded(association, asAssociation(association)->value);
			}
			break;

		case 0xab:  //  store new array
		{
			uint8_t numberOfCopiedVariables = nextBytecode();
			uint8_t localNumber = nextBytecode();
			oop array = newInstanceOfClass (ST_ARRAY_CLASS, numberOfCopiedVariables, EdenSpace);
			setLocal( localNumber, array);
		}
		break;

		case 0xac: //  pop
			pop ();
			break;

		case 0xad: //  dup
			push (top ());
			break;

		case 0xae: //  dropCascadeReceiver
		{
			oop value = pop ();
			pop ();
			push (value);
		}
		break;

		case 0xb0: //  jump
		{
			int8_t offset = nextBytecode();
			jump( offset);
		}
		break;

		case 0xb1: //  jump extended
		{
			int16_t offset = nextBytecode() * 256 + nextBytecode();
			jump( offset);
		}
		break;

		case 0xb2: //  jump if true
		{
			int8_t offset = nextBytecode();
			if (pop () == ST_TRUE)
				jump( offset);
			break;
		}	
		case 0xb3: //  jump if true extended
		{
			int16_t offset = nextBytecode() * 256 + nextBytecode();
			if (pop () == ST_TRUE)
				jump( offset);
			break;
		}
		break;

		case 0xb4: //  jump if false
		{
			int8_t offset = nextBytecode();
			if (pop () == ST_FALSE)
				jump( offset);
			break;
		}	
		case 0xb5: //  jump if false extended
		{
			int16_t offset = nextBytecode() * 256 + nextBytecode();
			if (pop () == ST_FALSE)
				jump( offset);
			break;
		}
		break;

		case 0xb6: // thisContext
			push(contextCopy(currentContext));
			break;

		case 0xc0: //  send literal 1
		case 0xc1: //  send literal 2
		case 0xc2: //  send literal 3
		case 0xc3: //  send literal 4
		case 0xc4: //  send literal 5
		case 0xc5: //  send literal 6
		case 0xc6: //  send literal 7
		case 0xc7: //  send literal 8
		case 0xc8: //  send literal 9
		case 0xc9: //  send literal 10
		case 0xca: //  send literal 11
		case 0xcb: //  send literal 12
		case 0xcc: //  send literal 13
		case 0xcd: //  send literal 14
		case 0xce: //  send literal 15
		case 0xcf: //  send literal 16
			{
			int numberOfArguments = nextBytecode();
			dispatch (getLiteral(asContext(currentContext)->method, bytecode & 0x0f), numberOfArguments);
			break;
			}

		case 0xd0: //  super call literal 1
		case 0xd1: //  super call literal 2
		case 0xd2: //  super call literal 3
		case 0xd3: //  super call literal 4
		case 0xd4: //  super call literal 5
		case 0xd5: //  super call literal 6
		{
			uint32_t nextByte = nextBytecode();
			dispatchSuper (getLiteral(asContext(currentContext)->method, bytecode & 0x0f), (uint64_t) nextByte);
			break;
		}

		case 0xd6: //  call well known
			callWellKnown();
		break;

		case 0xd7: //  call literal
			{
			uint8_t literalNumber = nextBytecode();
			uint8_t numberOfArguments = nextBytecode();
			dispatch (getLiteral(asContext(currentContext)->method, literalNumber), numberOfArguments);
			}
			break;

		case 0xd8:  //  call literal extended
			{
			uint16_t literalNumber = nextBytecode() * 256 +  nextBytecode();
			uint8_t numberOfArguments = nextBytecode();
			dispatch (getLiteral(asContext(currentContext)->method, literalNumber), numberOfArguments);
			}
			break;

		case 0xd9:  //  super call literal
			{
			uint8_t literalNumber = nextBytecode();
			uint8_t numberOfArguments = nextBytecode();
 			dispatchSuper (getLiteral(asContext(currentContext)->method, literalNumber), numberOfArguments);
			}
			break;

		case 0xda:  //  super call literal extended
			{
			uint16_t literalNumber = nextBytecode() * 256 +  nextBytecode();
			uint8_t numberOfArguments = nextBytecode();
			dispatchSuper (getLiteral(asContext(currentContext)->method, literalNumber), numberOfArguments);
			}
			break;

		case 0xdb:  //  primitive call
		{
			uint16_t primitiveNumber = ((uint16_t) nextBytecode()) * 256;
			primitiveNumber += ((uint16_t) nextBytecode());
			invokePrimitive(primitiveNumber);
		}
		break;

		case 0xdc:  //  return
		case 0xdd:  //  block return
			if (returnFromContext())
				break;
			return TRUE;

		case 0xde:  //  non local return
			{
			oop returnValue;
			oop closureOop = getReceiver();

			returnValue = pop ();

			closureOop = stripTags(asBlockClosure(closureOop)->methodContext);

			currentContext = closureOop;
			push (returnValue);

			if (returnFromContext())
				break;
			return TRUE;
			}

		case 0xdf:  //  primitive return
		{
			oop tos = pop ();
			oop result = pop ();

			if (result == cIntToST(0)) {
				push (tos);
				if (returnFromContext())
					break;
				return TRUE;
			}
			else {
				push (result);
			}
		}
		break;

		case 0xe0: //  send literal 1
		case 0xe1: //  send literal 2
		case 0xe2: //  send literal 3
		case 0xe3: //  send literal 4
		case 0xe4: //  send literal 5
		case 0xe5: //  send literal 6
		case 0xe6: //  send literal 7
		case 0xe7: //  send literal 8
		case 0xe8: //  send literal 9
		case 0xe9: //  send literal 10
		case 0xea: //  send literal 11
		case 0xeb: //  send literal 12
		case 0xec: //  send literal 13
		case 0xed: //  send literal 14
		case 0xee: //  send literal 15
		case 0xef: //  send literal 16
			{
			int numberOfArguments = nextBytecode();
			dispatch (getLiteral(asContext(currentContext)->method, bytecode & 0x0f), numberOfArguments);
			break;
			}

		case 0xf0: //  super call literal 1
		case 0xf1: //  super call literal 2
		case 0xf2: //  super call literal 3
		case 0xf3: //  super call literal 4
		case 0xf4: //  super call literal 5
		case 0xf5: //  super call literal 6
		{
			uint32_t nextByte = nextBytecode();
			dispatchSuper (getLiteral(asContext(currentContext)->method, bytecode & 0x0f), (uint64_t) nextByte);
			break;
		}

		case 0xf6: //  call well known
			callWellKnown();
		break;

		case 0xf7: //  call literal
			{
			uint8_t literalNumber = nextBytecode();
			uint8_t numberOfArguments = nextBytecode();
			dispatch (getLiteral(asContext(currentContext)->method, literalNumber), numberOfArguments);
			}
			break;

		case 0xf8:  //  call literal extended
			{
			uint16_t literalNumber = nextBytecode() * 256 +  nextBytecode();
			uint8_t numberOfArguments = nextBytecode();
			dispatch (getLiteral(asContext(currentContext)->method, literalNumber), numberOfArguments);
			}
			break;

		case 0xf9:  //  super call literal
			{
			uint8_t literalNumber = nextBytecode();
			uint8_t numberOfArguments = nextBytecode();
 			dispatchSuper (getLiteral(asContext(currentContext)->method, literalNumber), numberOfArguments);
			}
			break;

		case 0xfa:  //  super call literal extended
			{
			uint16_t literalNumber = nextBytecode() * 256 +  nextBytecode();
			uint8_t numberOfArguments = nextBytecode();
			dispatchSuper (getLiteral(asContext(currentContext)->method, literalNumber), numberOfArguments);
			}
			break;

		case 0xfb:  //  primitive call
		{
			uint16_t primitiveNumber = ((uint16_t) nextBytecode()) * 256;
			primitiveNumber += ((uint16_t) nextBytecode());
			invokePrimitive(primitiveNumber);
		}
		break;

		case 0xfc:  //  return
		case 0xfd:  //  block return
			if (returnFromContext())
				break;
			return TRUE;

		case 0xfe:  //  non local return
			{
			oop returnValue;
			oop closureOop = getReceiver();

			returnValue = pop ();

			closureOop = stripTags(asBlockClosure(closureOop)->methodContext);

			currentContext = closureOop;
			push (returnValue);

			if (returnFromContext())
				break;
			return TRUE;
			}

		case 0xff:  //  primitive return
		{
			oop tos = pop ();
			oop result = pop ();

			if (result == cIntToST(0)) {
				push (tos);
				if (returnFromContext())
					break;
				return TRUE;
			}
			else {
				push (result);
			}
		}
		break;

		default:
			LOGE ("Bad bytecode: %x\n", bytecode);
			return FALSE;
		}

		if (currentContext == stopFrame){
			return TRUE;
		}

	   if (eventWaitingFlag) {
			if (breakpointHit) {
				LOGI ("breakpoint hit");
				startupDebugger();
				return TRUE;
			}

			if (maxBytecodes > 0) {
				count++;
				if (count == maxBytecodes) {
					return TRUE;
				}
			}

			if (suspended != 0) {
			return TRUE;
			}

			if (errorString[0] != '\0')
			   return TRUE;
			eventWaitingFlag = maxBytecodes > 0;
		}
	}
}
