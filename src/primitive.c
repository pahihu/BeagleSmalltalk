// primitive.c
//
// Beagle Smalltalk
// Copyright (c) 2025 Simberon Incorporated
// Released under the MIT License
// https://opensource.org/license/MIT

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include "object.h"
#include <time.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#define PRIMITIVE_TABLE_SIZE 2048


#define PRIM_BASIC_AT 60
#define PRIM_BASIC_AT_PUT 61
#define PRIM_BASIC_SIZE 62
#define PRIM_BYTESTRING_BASIC_AT 63
#define PRIM_BYTESTRING_BASIC_AT_PUT 64
#define PRIM_NEW 70
#define PRIM_NEW_COLON 71
#define PRIM_IDENTITY_HASH 75
#define PRIM_IDENTICAL 110
#define PRIM_CLASS 111
#define PRIM_CHARACTER_AS_INTEGER 410
#define PRIM_CHARACTER_NEW_COLON 411
#define PRIM_VALUE 501
#define PRIM_VALUE_COLON 502
#define PRIM_VALUE_VALUE 503
#define PRIM_SUSPEND 405
#define PRIM_LOG 406
#define PRIM_HALT 407
#define PRIM_MILLISECONDS 408
#define PRIM_MICROSECONDS 412
#define PRIM_FINISH 409
#define PRIM_INST_VAR_AT 420
#define PRIM_INST_VAR_AT_PUT 421
#define PRIM_FLOAT_AT 548
#define PRIM_FLOAT_AT_PUT 549
#define PRIM_UNINTERPRETED_BYTES_COPY 550
#define PRIM_SYMBOL_TABLE 551
#define PRIM_PERFORM_WITH_ARGS 552
#define PRIM_SYSTEM_DICTIONARY 553
#define PRIM_EXCEPTION_HANDLERS 554
#define PRIM_BECOME 555
#define PRIM_ALL_INSTANCES 556
#define PRIM_WALKBACK 557
#define PRIM_SAVE_IMAGE 558
#define PRIM_GLOBAL_GC 559

#define PRIM_MARK_VM_MIGRATION_NEW 701
#define PRIM_UNMARK_VM_MIGRATION_NEW 702
#define PRIM_IS_VM_MIGRATION_NEW 703

#define PRIM_IS_EMSCRIPTEN 600

#ifdef __EMSCRIPTEN__
#define PRIM_EMSCRIPTEN_RUN_SCRIPT 601
#define PRIM_EMSCRIPTEN_RUN_SCRIPT_INT 602
#define PRIM_EMSCRIPTEN_RUN_SCRIPT_STRING 603
#endif

#define PRIM_WELL_KNOWN_AT 610
#define PRIM_WELL_KNOWN_AT_PUT 611
#define PRIM_WELL_KNOWN_LAST 612


// perhaps should be replaced with a server call for date and time in the future to avoid potential benefit hacking by changing system clocks
#define PRIM_SYSTEM_CURRENT_DATE_AND_TIME 700
#define PRIM_SET_CLASS 702

#define PRIM_PLATFORM 2000

#ifdef SIMTALK_DUMP_FRAME_RATE
uint64_t lastFrameTime = 0;
uint64_t numberOfSuspends = 0;
#endif

uint64_t wakeupTime;

primitiveFunction primitiveTable[PRIMITIVE_TABLE_SIZE];


void primIdentityHash()
{
	oop receiverOop = getReceiver();

	if (isImmediate(receiverOop)) {
		push (cIntToST(0));
		push (cIntToST(stIntToC(receiverOop)));
		return;
	}

	push (cIntToST(0));
	push (cIntToST(asObjectHeader(receiverOop)->identityHash));
}

void primClass()
{
	oop receiverOop = getReceiver();

	push (cIntToST(0));
	push (classOf(receiverOop));
}


void primIdentical()
{
	oop receiver = getReceiver();
	oop arg = getLocal( 0);

	push (cIntToST(0));
	if (receiver == arg)
		push (ST_TRUE);
	else
		push (ST_FALSE);
}


void primNew()
{
	oop receiverOop = getReceiver();
	oop newObjectOop;

	newObjectOop = newInstanceOfClass (receiverOop, 0, EdenSpace);
	if (asObjectHeader(newObjectOop) == NULL) {
		push (cIntToST(1));
		push (getReceiver());
		return;
	}

	push (cIntToST(0));
	push (newObjectOop);
}

void primNewColon()
{
	oop arg0Oop = getLocal( 0);
	oop receiverOop = getReceiver();
	oop newObjectOop;


	if (!isSmallInteger(arg0Oop)) {
		LOGE ("Argument isn't an integer: %" PRIx64, arg0Oop);
		push (cIntToST(1));
		push (getReceiver());
		return;
	}

	newObjectOop = newInstanceOfClass (receiverOop, stIntToC(arg0Oop), EdenSpace);
	if (asObjectHeader(newObjectOop) == NULL) {
		push (cIntToST(1));
		push (getReceiver());
		return;
	}

	push (cIntToST(0));
	push (newObjectOop);
}

void primBlockValue()
{
	oop closureOop = getReceiver();
	push (cIntToST(0));
	push (closureOop);
	invokeBlock (closureOop, 0);
}

void primBlockValueColon()
{
	oop closureOop = getReceiver();
	push (cIntToST(0));
	push (closureOop);
	push (getLocal( 0));
	invokeBlock (closureOop, 1);
}

void primBlockValueValue()
{
	oop closureOop = getReceiver();
	push (cIntToST(0));
	push (closureOop);
	push (getLocal( 0));
	push (getLocal( 1));
	invokeBlock (closureOop, 2);
}

void primBasicAt()
{
	oop index = getLocal( 0);
	oop receiver = getReceiver();
	int64_t indexInt;

	if (!isSmallInteger(index)) {
		push (cIntToST(1));
		push (receiver);
		return;
	}

	indexInt = stIntToC(index);
	if (indexInt <= 0) {
		push (cIntToST(1));
		push (receiver);
		return;
	}

	if (isBytes(receiver))
		if (indexInt > basicByteSize(receiver)) {
			push (cIntToST(1));
			push (receiver);
		}
		else {
			push (cIntToST(0));
			push (cIntToST((uint64_t)basicByteAtInt(receiver,indexInt)));
		}
	else
		if (indexInt > indexedObjectSize(receiver)) {
			push (cIntToST(1));
			push (receiver);
		}
		else {
			push (cIntToST(0));
			push (indexedVarAtInt(receiver, indexInt));
		}

}

void primBasicAtPut()
{
	oop value = getLocal( 1);
	oop index = getLocal( 0);
	oop receiver = getReceiver();
	int64_t indexInt;

 //   startSymbolLog (asContext(currentContext->frame)->method);

    if (!isSmallInteger(index)) {
		push (cIntToST(1));
		push (receiver);
		return;
	}

	indexInt = stIntToC(index);

	if (indexInt <= 0) {
		push (cIntToST(1));
		push (receiver);
		return;
	}

	if (isBytes(receiver))
		if (indexInt > basicByteSize(receiver)) {
			push (cIntToST(1));
			push (receiver);
		}
		else {
			basicByteAtIntPut(receiver, indexInt, stIntToC(value));
			push (cIntToST(0));
			push (value);
		}
	else
		if (indexInt > indexedObjectSize(receiver)) {
			push (cIntToST(1));
			push (receiver);
		}
		else {
			indexedVarAtIntPut(receiver, indexInt, value);
			push (cIntToST(0));
			push (value);
		}
}

void primBasicSize()
{
	oop receiver = getReceiver();

	if (isBytes(receiver)) {
		push (cIntToST(0));
		push (cIntToST(basicByteSize(receiver)));
	}
	else {
		push (cIntToST(0));
		push (cIntToST(indexedObjectSize(receiver)));
	}
}

void primByteStringBasicAt()
{
	oop index = getLocal( 0);
	oop receiver = getReceiver();
	int64_t indexInt;

	if (!isSmallInteger(index)) {
        LOGE("The index provided is not a valid index");
		push (cIntToST(1));
		push (receiver);
		return;
	}

	indexInt = stIntToC(index);
	if (indexInt <= 0) {
        LOGE("The index is below the reasonable bounds of a byte indexed string (< 1)");
		push (cIntToST(1));
		push (receiver);
		return;
	}

	if (!isBytes(receiver)) {
        LOGE("The receiver is a string not indexed in bytes");
		push (cIntToST(1));
		push (receiver);
		return;
	}

	if (indexInt > basicByteSize(receiver)) {
        LOGE("attempting to retrieve an index larger than the size of the string");
		push (cIntToST(1));
		push (receiver);
		return;
	}

	push (cIntToST(0));
	push (cCharToST((uint64_t) basicByteAtInt(receiver,indexInt)));
}

void primByteStringBasicAtPut()
{
	oop receiverOop = getReceiver();
	oop index = getLocal( 0);
	oop valueOop = getLocal( 1);

	basicByteAtIntPut(receiverOop, stIntToC(index), stIntToC(valueOop));
	push (cIntToST(0));
	push (receiverOop);
}

void primCharacterAsInteger()
{
	oop receiver = getReceiver();

	if (!isCharacter(receiver)) {
		push (cIntToST(1));
		push (receiver);
		return;
	}

	push (cIntToST(0));
	push (cIntToST(stIntToC(receiver)));
}

void primCharacterNewColon()
{
	oop receiver = getReceiver();
	oop value = getLocal( 0);

	uint64_t cValue;

	if (!isSmallInteger(value)) {
		push (cIntToST(1));
		push (receiver);
		return;
	}

	cValue = stIntToC(value);

	push (cIntToST(0));
	push (cCharToST(cValue));
}


void primSuspend()
{
	oop receiver = getReceiver();

	wakeupTime = stIntToC(asSystem(receiver)->wakeupTime);
	// LOGW("System suspended on event processing");
    
	suspended = 1;
	eventWaitingFlag = TRUE;
	push (cIntToST(0));
	push (cIntToST(1));
}

void primLog()
{
	oop value = getLocal( 0);

    char* message = malloc((unsigned long) basicByteSize(value) + 1);
    if(STStringToC(value, message) ){
        LOGE("primLog");
        free(message);
    }

	LOGI ("%s", message);
    free(message);
	push (cIntToST(0));
	push (cIntToST(1));
}

void primHalt()
{
	LOGI ("VM Halt");
	push (cIntToST(0));
	startupDebugger();
	push (cIntToST(1));
}

void primMilliseconds()
{
	struct timespec ts_current;
	uint64_t result;

	clock_gettime(CLOCK_MONOTONIC, &ts_current);
	result = ts_current.tv_sec * 1000ull + ts_current.tv_nsec / 1000000ull;
	push (cIntToST(0));
	push (cIntToST(result));
}

void primMicroseconds()
{
	struct timespec ts_current;
	uint64_t result;

	clock_gettime(CLOCK_MONOTONIC, &ts_current);
	result = ts_current.tv_sec * 1000000ull + ts_current.tv_nsec / 1000ull;
	push (cIntToST(0));
	push (cIntToST(result));
}

void primFinish()
{
	oop receiver = getReceiver();

	finish();

	push (cIntToST(0));
	push (receiver);
}


void primInstVarAt()
{
	oop receiver = getReceiver();
	oop i = getLocal( 0);
	oop value;
	uint64_t index;

	if (!isSmallInteger(i)) {
		push (cIntToST(1));
		push (receiver);
		return;
	}

	if (stIntToC(i) > totalObjectSize(receiver)) {
		push (cIntToST(1));
		push (receiver);
		return;
	}

	index = stIntToC(i) - 1;
	value = instVarAtInt(receiver, index);

	push (cIntToST(0));
	push (value);
}

void primInstVarAtPut()
{
	oop receiver = getReceiver();
	oop i = getLocal( 0);
	oop value = getLocal( 1);
	uint64_t index;

	if (!isSmallInteger(i)) {
		push (cIntToST(1));
		push (receiver);
		return;
	}

	if (stIntToC(i) > totalObjectSize(receiver)) {
		push (cIntToST(1));
		push (receiver);
		return;
	}

	index = stIntToC(i) - 1;
	instVarAtIntPut(receiver, index, value);

	push (cIntToST(0));
	push (value);
}

void primFloatAt() {
    push (getReceiver());
    uint64_t index = stIntToC(getLocal( 0));

    double floatValue;
	unsigned char *c = (unsigned char *) &floatValue;

    oop receiverOop = pop ();

    int i;
    for (i=0; i<4; i++)
        *c++ = basicByteAtInt(receiverOop, index*4 + i);

    push (cIntToST(0));
    push (cFloatToST(floatValue));
}

void primFloatAtPut() {
    oop receiverOop = getReceiver();
    uint64_t index = stIntToC(getLocal( 0));
    double floatValue = stFloatToC(getLocal( 1));
	unsigned char *c = (unsigned char *) &floatValue;

    int i;
    for (i=0; i<4; i++)
        basicByteAtIntPut(receiverOop, index*4 + i, *c++);

    push (cIntToST(0));
    push (cFloatToST(floatValue));
}

void primUninterpretedBytesCopy() {
	oop receiverOop = getReceiver();
	oop copyOop = newInstanceOfClass(asObjectHeader(receiverOop)->stClass, basicByteSize(receiverOop), EdenSpace);

	int i;
	for (i=0; i<basicByteSize(receiverOop); i++)
		basicByteAtIntPut(copyOop,i,basicByteAtInt(receiverOop,i));

	push (cIntToST(0));
	push (copyOop);
}

void primPlatform(){
#ifdef __ANDROID__
    push (cIntToST(0));
    push (cIntToST(0));
#else
    push (cIntToST(0));
    push (cIntToST(1));
#endif
}

void primSystemCurrentDateAndTime(){
    time_t now = time(&now);
    char* timeString = malloc(9 * sizeof(char));
    char* timeString2 = malloc(7 * sizeof(char));
    timeString2[0] = '\0';
    char* token;
    uint64_t timeInt;
    
    strftime(timeString, 9, "%d%m%Y", localtime(&now));
    
    timeInt = atoi(timeString);
    timeInt *= 100000000;

    strftime(timeString, 9, "%T", localtime(&now));
    token = strtok(timeString, ":");
    int multiplier = 60 * 60 * 1000;
    while(token != NULL){
        timeInt = timeInt + (atoi(token)*multiplier);
        multiplier = multiplier / 60;
        token = strtok(NULL, ":");
    }
    
    free(timeString);
    free(timeString2);
    
    push (cIntToST(0));
    push (cIntToST(timeInt));
}

void primSymbolTable(){
	push (cIntToST(0));
	push (ST_SYMBOL_TABLE);
}

void primSystemDictionary(){
	push (cIntToST(0));
	push (ST_SYSTEM_DICTIONARY);
}

void primExceptionHandlers(){
	push (cIntToST(0));
	push (ST_EXCEPTION_HANDLERS);
}

void primPerformWithArgs(){
    oop receiver = getReceiver();
    oop selector = getLocal( 0);
    oop args = getLocal( 1);

    int i, argCount = 0;
    for (i=1; i <= basicByteSize(selector); i++)
        if (basicByteAtInt(selector, i) == ':')
            argCount++;

    if (!isalnum(basicByteAtInt(selector, 1)))
        argCount++;

    if (argCount != indexedObjectSize(args)) {
        push (cIntToST(1));
        push (cIntToST(0));
        return;
    }

    push (receiver);
    for (i=0; i<indexedObjectSize(args); i++)
        push (instVarAtInt(args, i));

//    contextStruct *oldStopFrame = stopFrame;
//    stopFrame = currentContext;
	dispatch(selector, indexedObjectSize(args));
	basicInterpret(0);
//    stopFrame = oldStopFrame;

    oop result = pop ();
    push (cIntToST(0));
    push (result);
}

void primAllInstances(){
	scavenge();

	oop array = (newInstanceOfClass (ST_ARRAY_CLASS, 1, EdenSpace));
	asObjectHeader(array)->size -= sizeof(oop);
	asObjectHeader(array)->bodyPointer += sizeof(oop);

	oop receiver = getReceiver();

	// Collect instances in survivor space
	oop object;
	uint64_t instances = 0;

	for (object = (oop) &ActiveSurvivorSpace->space[0];
			object < (oop) &ActiveSurvivorSpace->space[ActiveSurvivorSpace->firstFreeBlock];
			object += nextObjectIncrement(object) * sizeof(oop))
	{
		if (asObjectHeader(object)->stClass == receiver)
		{
			asObjectHeader(array)->size += sizeof(oop);
			asObjectHeader(array)->bodyPointer -= sizeof(oop);
			indexedVarAtIntPut(array, 1, object);
			EdenSpace->lastFreeBlock--;
			instances++;
		}
	}

	// Collect instances in old space
	for (object = (oop) &OldSpace->space[0];
			object < (oop) &OldSpace->space[OldSpace->firstFreeBlock];
			object += nextObjectIncrement(object) * sizeof(oop))
	{
		if (asObjectHeader(object)->stClass == receiver)
		{
			asObjectHeader(array)->size += sizeof(oop);
			asObjectHeader(array)->bodyPointer -= sizeof(oop);
			indexedVarAtIntPut(array, 1, object);
			EdenSpace->lastFreeBlock--;
			instances++;
		}
	}

	EdenSpace->space[EdenSpace->lastFreeBlock] = 0;
	if (instances == 0)
		asObjectHeader(array)->bodyPointer = 0;

	push (cIntToST(0));
	push (array);
}

void primWalkback(){
	dumpWalkback("Walkback primitive");
	push (cIntToST(0));
	push (CStringToST(walkbackDump));
}

void swapHeaders(oop object1, oop object2)
{ 
	int isObject1Registered = unregisterRememberedSetObject(object1);
	int isObject2Registered = unregisterRememberedSetObject(object2);

	uint64_t tempSize = asObjectHeader(object1)->size;
	asObjectHeader(object1)->size = asObjectHeader(object2)->size;
	asObjectHeader(object2)->size = tempSize;

	uint16_t tempFlags = asObjectHeader(object1)->flags;
	asObjectHeader(object1)->flags = asObjectHeader(object2)->flags;
	asObjectHeader(object2)->flags = tempFlags;
	
	uint16_t tempFlips = asObjectHeader(object1)->flips;
	asObjectHeader(object1)->flips = asObjectHeader(object2)->flips;
	asObjectHeader(object2)->flips = tempFlips;
	
	uint32_t tempNumberOfNamedInstanceVariables = asObjectHeader(object1)->numberOfNamedInstanceVariables;
	asObjectHeader(object1)->numberOfNamedInstanceVariables = asObjectHeader(object2)->numberOfNamedInstanceVariables;
	asObjectHeader(object2)->numberOfNamedInstanceVariables = tempNumberOfNamedInstanceVariables;
	
	oop tempClass = asObjectHeader(object1)->stClass;
	asObjectHeader(object1)->stClass = asObjectHeader(object2)->stClass;
	asObjectHeader(object2)->stClass = tempClass;
	
	oop tempIdentityHash = asObjectHeader(object1)->identityHash;
	asObjectHeader(object1)->identityHash = asObjectHeader(object2)->identityHash;
	asObjectHeader(object2)->identityHash = tempIdentityHash;
	
	oop tempBodyPointer = asObjectHeader(object1)->bodyPointer;
	asObjectHeader(object1)->bodyPointer = asObjectHeader(object2)->bodyPointer;
	asObjectHeader(object2)->bodyPointer = tempBodyPointer;

	setBodyHeaderPointer(object1);
	setBodyHeaderPointer(object2);

	if (isObject1Registered)
		registerRememberedSetObject(object2);

	if (isObject2Registered)
		registerRememberedSetObject(object1);
}

void primBecome(){
	scavenge();


    oop receiver = getReceiver();
    oop objectToBecome = getLocal( 0);

	if (isImmediate(receiver) || isImmediate(objectToBecome)) {
		push (cIntToST(1));
		push (cIntToST(1));
		return;
	}
	
	if (isObjectInActiveSurvivorSpace(receiver) && isObjectInActiveSurvivorSpace(objectToBecome)) {
		swapHeaders(receiver, objectToBecome);
		push (cIntToST(0));
		push (cIntToST(1));
		return;
	}

	if (isObjectInOldSpace(receiver) && isObjectInOldSpace(objectToBecome)) {
		swapHeaders(receiver, objectToBecome);
		push (cIntToST(0));
		push (cIntToST(1));
		return;
	}
		
	if (isObjectInActiveSurvivorSpace(receiver)) {
		asObjectHeader(receiver)->flips = 65535;
	}

	if (isObjectInActiveSurvivorSpace(objectToBecome)) {
		asObjectHeader(objectToBecome)->flips = 65535;
	}

	scavenge();

    receiver = getReceiver();
    objectToBecome = getLocal( 0);
	
	if (isObjectInOldSpace(receiver) && isObjectInOldSpace(objectToBecome)) {
		swapHeaders(receiver, objectToBecome);
		push (cIntToST(0));
		push (cIntToST(1));
		return;
	}
		

	push (cIntToST(1));
	push (cIntToST(1));
}


void primSaveImage()
{
    oop filePath = getLocal( 0);

	char filePathString[1024];
	STStringToC (filePath, filePathString);
	strcat (filePathString, ".im");

	FILE *file = fopen(filePathString, "wb");
	saveImage (file);
	fclose(file);

	STStringToC (filePath, filePathString);
	strcat (filePathString, ".cha");

	push (cIntToST(0));
	push (cIntToST(0));
}

void primGlobalGarbageCollect()
{
	globalGarbageCollect();
	push (cIntToST(0));
	push (cIntToST(0));
}

void primWellKnownAt()
{
	oop arg0Oop = getLocal( 0);
	uint64_t index;

	if (!isSmallInteger(arg0Oop)) {
		LOGE ("Argument isn't an integer: %" PRIx64, arg0Oop);
		push (cIntToST(1));
		push (arg0Oop);
		return;
	}

	index = stIntToC(arg0Oop);
	if (index > O_LAST_WELL_KNOWN_OBJECT) {
		LOGE ("Well known index out of range: %" PRIx64, index);
		push (cIntToST(1));
		push (arg0Oop);
		return;
	}

	push (cIntToST(0));
	push (WellKnownObjects->space[index]);
}

void primWellKnownAtPut()
{
	oop arg0Oop = getLocal( 0);
	oop value = getLocal( 1);

	uint64_t index;

	if (!isSmallInteger(arg0Oop)) {
		LOGE ("Argument isn't an integer: %" PRIx64, arg0Oop);
		push (cIntToST(1));
		push (arg0Oop);
		return;
	}

	index = stIntToC(arg0Oop);
	if (index > O_LAST_WELL_KNOWN_OBJECT) {
		LOGE ("Well known index out of range: %" PRIx64, index);
		push (cIntToST(1));
		push (arg0Oop);
		return;
	}

	if (index > WellKnownObjects->spaceSize / sizeof(oop)) {
		LOGE ("Space too small: %" PRIx64, index);
		push (cIntToST(1));
		push (arg0Oop);
		return;
	}

	if (WellKnownObjects->firstFreeBlock <= index)
		WellKnownObjects->firstFreeBlock = index + 1;

	WellKnownObjects->space[index] = value;

	push (cIntToST(0));
	push (WellKnownObjects->space[index]);
}

void primWellKnownLast()
{
	push (cIntToST(0));
	push (cIntToST(O_LAST_WELL_KNOWN_OBJECT));
}

void primIsEmscripten()
{
	push (cIntToST(0));
#ifdef __EMSCRIPTEN__
	push (ST_TRUE);
#else
	push (ST_FALSE);
#endif
}

#ifdef __EMSCRIPTEN__
void primEmscriptenRunScript()
{
    oop jsCommand = getLocal( 0);
	uint64_t size = basicByteSize(jsCommand);

	char *jsCommandString = (char *) malloc(size + 1);
	STStringToC (jsCommand, jsCommandString);
	emscripten_run_script (jsCommandString);
	
	free (jsCommandString);
	
	push (cIntToST(0));
	push (cIntToST(0));	
}

void primEmscriptenRunScriptInt()
{
    oop jsCommand = getLocal( 0);
	uint64_t size = basicByteSize(jsCommand);
	int result;

	char *jsCommandString = (char *) malloc(size + 1);
	STStringToC (jsCommand, jsCommandString);
	result = emscripten_run_script_int (jsCommandString);
	
	free (jsCommandString);

	push (cIntToST(0));
	push (cIntToST(result));	
}

void primEmscriptenRunScriptString()
{
}
#endif

void primSetClass()
{
    oop receiver = getReceiver();
    oop newClass = getLocal( 0);

	asObjectHeader(receiver)->stClass = newClass;

	push (cIntToST(0));
	push (cIntToST(0));
}


void primMarkVMMigrationNew()
{
    oop receiver = getReceiver();
    
	markVMMigrationNew(receiver);

	push (cIntToST(0));
	push (cIntToST(0));
}

void primUnmarkVMMigrationNew()
{
    oop receiver = getReceiver();
    
	unmarkVMMigrationNew(receiver);

	push (cIntToST(0));
	push (cIntToST(0));
}

void primIsVMMigrationNew()
{
    oop receiver = getReceiver();
    
	push (cIntToST(0));
	if (isVMMigrationNew(receiver))
		push (ST_TRUE);
	else
		push (ST_FALSE);
}

void initializePrimitiveTable()
{
	int i;
	for (i=0; i<PRIMITIVE_TABLE_SIZE; i++)
		primitiveTable[i] = NULL;

	primitiveTable[PRIM_BASIC_AT] = primBasicAt;
	primitiveTable[PRIM_BASIC_AT_PUT] = primBasicAtPut;
	primitiveTable[PRIM_BASIC_SIZE] = primBasicSize;
	primitiveTable[PRIM_BYTESTRING_BASIC_AT] = primByteStringBasicAt;
	primitiveTable[PRIM_BYTESTRING_BASIC_AT_PUT] = primByteStringBasicAtPut;
	primitiveTable[PRIM_NEW] = primNew;
	primitiveTable[PRIM_NEW_COLON] = primNewColon;
	primitiveTable[PRIM_VALUE] = primBlockValue;
	primitiveTable[PRIM_VALUE_COLON] = primBlockValueColon;
	primitiveTable[PRIM_VALUE_VALUE] = primBlockValueValue;
	primitiveTable[PRIM_CHARACTER_AS_INTEGER] = primCharacterAsInteger;
	primitiveTable[PRIM_CHARACTER_NEW_COLON] = primCharacterNewColon;
	primitiveTable[PRIM_IDENTITY_HASH] = primIdentityHash;
	primitiveTable[PRIM_IDENTICAL] = primIdentical;
	primitiveTable[PRIM_CLASS] = primClass;
	primitiveTable[PRIM_SUSPEND] = primSuspend;
	primitiveTable[PRIM_LOG] = primLog;
	primitiveTable[PRIM_HALT] = primHalt;
	primitiveTable[PRIM_MILLISECONDS] = primMilliseconds;
	primitiveTable[PRIM_MICROSECONDS] = primMicroseconds;
	primitiveTable[PRIM_FINISH] = primFinish;
	primitiveTable[PRIM_INST_VAR_AT] = primInstVarAt;
	primitiveTable[PRIM_INST_VAR_AT_PUT] = primInstVarAtPut;
    primitiveTable[PRIM_FLOAT_AT] = primFloatAt;
    primitiveTable[PRIM_FLOAT_AT_PUT] = primFloatAtPut;
	primitiveTable[PRIM_UNINTERPRETED_BYTES_COPY] = primUninterpretedBytesCopy;
    primitiveTable[PRIM_PLATFORM] = primPlatform;
    primitiveTable[PRIM_SYSTEM_CURRENT_DATE_AND_TIME] = primSystemCurrentDateAndTime;
	primitiveTable[PRIM_SYMBOL_TABLE] = primSymbolTable;
    primitiveTable[PRIM_PERFORM_WITH_ARGS] = primPerformWithArgs;
	primitiveTable[PRIM_SYSTEM_DICTIONARY] = primSystemDictionary;
	primitiveTable[PRIM_EXCEPTION_HANDLERS] = primExceptionHandlers;
	primitiveTable[PRIM_BECOME] = primBecome;
	primitiveTable[PRIM_ALL_INSTANCES] = primAllInstances;
	primitiveTable[PRIM_WALKBACK] = primWalkback;
	primitiveTable[PRIM_SAVE_IMAGE] = primSaveImage;
	primitiveTable[PRIM_GLOBAL_GC] = primGlobalGarbageCollect;
	primitiveTable[PRIM_SET_CLASS] = primSetClass;

	primitiveTable[PRIM_IS_EMSCRIPTEN] = primIsEmscripten;

	primitiveTable[PRIM_WELL_KNOWN_AT] = primWellKnownAt;
	primitiveTable[PRIM_WELL_KNOWN_AT_PUT] = primWellKnownAtPut;
	primitiveTable[PRIM_WELL_KNOWN_LAST] = primWellKnownLast;
	primitiveTable[PRIM_MARK_VM_MIGRATION_NEW] = primMarkVMMigrationNew;
	primitiveTable[PRIM_UNMARK_VM_MIGRATION_NEW] = primUnmarkVMMigrationNew;
	primitiveTable[PRIM_IS_VM_MIGRATION_NEW] = primIsVMMigrationNew;

#ifdef __EMSCRIPTEN__
	primitiveTable[PRIM_EMSCRIPTEN_RUN_SCRIPT] = primEmscriptenRunScript;
	primitiveTable[PRIM_EMSCRIPTEN_RUN_SCRIPT_INT] = primEmscriptenRunScriptInt;
	primitiveTable[PRIM_EMSCRIPTEN_RUN_SCRIPT_STRING] = primEmscriptenRunScriptString;
#endif

    initializeIntegerPrimitives();
	initializeFloatPrimitives();
	initializeSocketPrimitives();
	initializeFilePrimitives();
	initializeMemoryPrimitives();
}


void invokePrimitive(unsigned short primitiveNumber)
{
	if (primitiveTable[primitiveNumber] == NULL)
	{
        LOGW ("Primitive not found - %"PRId64"\n", (uint64_t) primitiveNumber);
		sprintf(errorString, "Primitive not found - %"PRIx64"\n", (uint64_t) primitiveNumber);
		eventWaitingFlag = TRUE;
		return;
	}

	primitiveTable[primitiveNumber]();
}

