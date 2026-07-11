// memory.c
//
// Beagle Smalltalk
// Copyright (c) 2025 Simberon Incorporated
// Released under the MIT License
// https://opensource.org/license/MIT

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "object.h"

memorySpaceStruct *EdenSpace;
memorySpaceStruct *SurvivorSpace1;
memorySpaceStruct *SurvivorSpace2;
memorySpaceStruct *RememberedSet;
memorySpaceStruct *OldSpace;
memorySpaceStruct *WellKnownObjects;
memorySpaceStruct *StackSpace;
memorySpaceStruct *Spaces[MAX_SPACES];

memorySpaceStruct *ActiveSurvivorSpace;
memorySpaceStruct *InactiveSurvivorSpace;
int EdenUsedForGC = 0;

extern oop tenure (oop object);
extern uint64_t gcCopyToInactiveObjectContents(oop object);
extern void relocateAllObjectPointers();
extern void relocateObjectPointersInObjectSpace(memorySpaceStruct *space);

void enumerateSpaces(spaceEnumerationFunction function, void *args)
{
	uint32_t i;
    for (i=0; i < MAX_SPACES && Spaces[i]->spaceSize > 0; i++) {
		function(Spaces[i], args);
	}

	function(Spaces[i], args);
}

void enumerateObjectsInStackSpace(memorySpaceStruct *space, enumerationFunction function, void *args)
{
	uint64_t index;

	for (index = space->lastFreeBlock + 1; index < spaceSize(space); index += (sizeof(objectHeaderStruct) / sizeof(oop))) {
		function(asOop(&space->space[index]), args);
	}
}
void enumerateObjectsInSpace(memorySpaceStruct *space, enumerationFunction function, void *args)
{
	uint64_t index;

	if (isTopHeaderSpace(space)) {
		enumerateObjectsInStackSpace(space, function, args);
		return;
	}
		
	for (index = 0; index < space->firstFreeBlock; index += (sizeof(objectHeaderStruct) / sizeof(oop))) {
		function(asOop(&space->space[index]), args);
	}
}

void enumeratePointersInSpace(memorySpaceStruct *space, pointerEnumerationFunction function, void *args)
{
	uint64_t index;

	for (index = 0; index < space->firstFreeBlock; index ++)
		function((oop *)(&space->space[index]), args);
}

int checkObject (oop pointer)
{
	if (isImmediate(pointer))
		return TRUE;

	if (isSpaceObject(pointer))
		return TRUE;

	if (isObjectInEdenSpace(pointer))
        if (isBodyInEdenSpace(pointer))
		    return TRUE;

	if (isObjectInActiveSurvivorSpace(pointer))
        if (isBodyInActiveSurvivorSpace(pointer))
		    return TRUE;

	if (isObjectInInactiveSurvivorSpace(pointer))
        if (isBodyInInactiveSurvivorSpace(pointer))
		    return TRUE;

	if (isObjectInOldSpace(pointer))
        if (isBodyInOldSpace(pointer))
		    return TRUE;

	LOGI ("Body is in the wrong space");
	char className[256];
	STStringToC (asClass(asObjectHeader(pointer)->stClass)->name, className);
	LOGI ("Class: %s  pointer: %" PRIx64 " body: %" PRIx64, className, pointer, asObjectHeader(pointer)->bodyPointer);
	dumpWalkback("Body is in the wrong space");
	return FALSE;
}

memorySpaceStruct *allocateSpace (uint64_t size)
{
	memorySpaceStruct *space;

	space = (memorySpaceStruct *) malloc ((size_t) size + sizeof(memorySpaceStruct));
	if (space == NULL)
		return NULL;

	space -> spaceSize = size;
	space -> firstFreeBlock = 0;
	space -> lastFreeBlock = size / sizeof(oop) - 1;
	space -> rememberedSetSpaceNumber = 0;
	
	return space;
}

oop allocateObjectInStackSpace (uint64_t size, memorySpaceStruct *space)
{
	uint64_t allocatedSize = (((size + 7) & 0xFFFFFFFFFFFFFFF8) - sizeof(objectHeaderStruct)) / sizeof(oop);
	objectHeaderStruct *result;


	if (isStackSpace(space) && (space->firstFreeBlock + allocatedSize + 64) >= space->lastFreeBlock)
	{
		LOGI ("Ran out of stack space");
		dumpWalkback("Ran out of stack space");
		ERROR_EXIT;
	}

	space->lastFreeBlock = space->lastFreeBlock - (sizeof(objectHeaderStruct) / sizeof(oop));
	result = (objectHeaderStruct *) &space->space[space->lastFreeBlock+1];

	result->bodyPointer = (oop) &space->space[space->firstFreeBlock];
		space->firstFreeBlock += allocatedSize;
	
	result->size = size;
	result->flags = 0;

	return asOop(result);
}

oop allocateObjectInSpace (uint64_t size, memorySpaceStruct *space)
{
	uint64_t allocatedSize = (((size + 7) & 0xFFFFFFFFFFFFFFF8) - sizeof(objectHeaderStruct)) / sizeof(oop);
	objectHeaderStruct *result;

	if (isTopHeaderSpace(space))
		return (allocateObjectInStackSpace (size, space));

	if (isObjectSpace(space) && (space->firstFreeBlock + allocatedSize + 64) >= space->lastFreeBlock)
	{
		if (space == EdenSpace) {
			scavenge();
			space = EdenSpace;
		}
		if ((space->firstFreeBlock + allocatedSize + 64) >= space->lastFreeBlock)
		{
			if (space == EdenSpace) {
				LOGI ("Ran out of space in Eden");
				dumpWalkback("Ran out of space in Eden");
			}
			else {
				LOGI ("Ran out of space outside of Eden");
				dumpWalkback("Ran out of space outside of Eden");
			}
			ERROR_EXIT;
		}
	}

	result = (objectHeaderStruct *) &space->space[space->firstFreeBlock];

	space->firstFreeBlock += sizeof(objectHeaderStruct) / sizeof(oop);
	if (allocatedSize != 0) {
		space->space[space->lastFreeBlock] = (uint64_t) result;
		space->lastFreeBlock -= allocatedSize + 1;	// Write a pointer to the header after the bodyPointer
		result->bodyPointer = (oop) &space->space[space->lastFreeBlock + 1];
	}
	else
		result->bodyPointer = 0;

	result->size = size;
	result->flags = 0;

	return asOop(result);
}


oop newInstanceOfClass(oop behavior, uint64_t indexedVars, memorySpaceStruct *space)
{
	oop newObjectOop;
	uint64_t numberOfInstanceVariables = Behavior_NumberOfNamedInstVars(behavior);
	long flags = Behavior_Flags(behavior);
	int i;
	int isBytes = (flags & BEHAVIOR_BYTES) == BEHAVIOR_BYTES;
	uint64_t size;

	if (isBytes)
		size = indexedVars + (uint64_t) sizeof(objectHeaderStruct);
	else
		size = (numberOfInstanceVariables + indexedVars) * (uint64_t) sizeof(oop) + (uint64_t) sizeof(objectHeaderStruct);

	if ((currentContext != ST_NIL)  && (!isStackSpace(space)))
		push (behavior);

	newObjectOop = allocateObjectInSpace(size, space);

	if ((currentContext != ST_NIL)  && (!isStackSpace(space)))
		behavior = pop ();

	if (asObjectHeader(newObjectOop) == NULL) {
		LOGW ("Couldn't allocate");
		return (ST_NIL);
	}

	asObjectHeader(newObjectOop)->flips = 0;
	asObjectHeader(newObjectOop)->stClass = behavior;
	asObjectHeader(newObjectOop)->flags = (unsigned char) flags;

	if (isBytes)
		asObjectHeader(newObjectOop)->numberOfNamedInstanceVariables = 0;
	else
		asObjectHeader(newObjectOop)->numberOfNamedInstanceVariables = (unsigned short) numberOfInstanceVariables;

	asObjectHeader(newObjectOop)->identityHash = rand() % 0x0FFFFFFFFFFFFFFF;

	if (isBytes)
		// for (i=0; i< (size - sizeof(objectHeaderStruct)) / sizeof(oop); i++)
		for (i=0; i< (size - sizeof(objectHeaderStruct) + sizeof(oop) - 1) / sizeof(oop); i++) {
			basicInstVarAtIntPut(newObjectOop, i, asOop(0));
		}
	else {
		for (i=0; i< (numberOfInstanceVariables + indexedVars); i++) {
			instVarAtIntPut(newObjectOop, i, ST_NIL);
		}
	}

	return newObjectOop;
}

void copyObjectTo(oop oldObject, oop newObject)
{
	uint64_t i, oopsToCopy = (sizeof(objectHeaderStruct) - sizeof(oop)) / sizeof(oop);

// Copy the header without copying the bodyPointer
	for (i=0; i<oopsToCopy; i++)
		((uint64_t *) oopPtr(newObject))[i] = ((uint64_t *)oopPtr(oldObject))[i];

	oopsToCopy = totalObjectSize(oldObject);
	for (i=0; i<oopsToCopy; i++) {
		oop longWordToCopy = oopPtr(asObjectHeader(oldObject)->bodyPointer)[i];
		oopPtr(asObjectHeader(newObject)->bodyPointer)[i] = longWordToCopy;
	}

	asObjectHeader(oldObject)->stClass = newObject; // leave a forwarding pointer to the new survivor space
	asObjectHeader(oldObject)->flags |= RELOCATED;
}

void moveObjectToSpace(oop pointer, memorySpaceStruct *space)
{
	oop newObject = allocateObjectInSpace(memorySize(pointer), space);
	copyObjectTo(pointer, newObject);
}
void copyToInactiveSurvivorSpace (oop pointer)
{
	oop newObject;

	if (asObjectHeader(pointer)->flips > 300) {
//		LOGI ("Tenuring %"PRIx64, pointer);
		newObject = tenure (pointer);
		copyObjectTo(pointer, newObject);
		gcCopyToInactiveObjectContents(newObject);
		registerRememberedSetObject(newObject);
	}
	else {
		newObject = allocateObjectInSpace(memorySize(pointer), InactiveSurvivorSpace);
		copyObjectTo(pointer, newObject);
		asObjectHeader(newObject)->flips++;
	}
}

uint64_t gcCopyToInactivePointer (oop *pointer)
{
//	LOGI ("Relocating pointer %"PRIx64": %"PRIx64"", asOop(pointer), asOop(*pointer));

	oop strippedPointer = stripTags(*pointer);
	int pointerWasContextPointer = isContextPointer(*pointer);

	if (isImmediate(*pointer) && ~pointerWasContextPointer)
		return 0;

	if (isObjectInInactiveSurvivorSpace(strippedPointer))
		return 1;


	if (!isObjectInNewSpace(strippedPointer))
		return 0;
	
	if (isSpaceObject(strippedPointer))
		return 0;

	if (!isRelocated(strippedPointer)) {
		copyToInactiveSurvivorSpace(*pointer);
	}

	(*pointer) = asObjectHeader(*pointer)->stClass;  // replace the oop with the forwarding pointer
	if (pointerWasContextPointer)
		*pointer = markAsContextPointer(*pointer);

	return isObjectInSpace(*pointer, InactiveSurvivorSpace)?1:0;
}

uint64_t gcCopyToInactivePointerRange (oop *pointer, uint64_t size)
{
//	LOGI ("Relocating Pointer Range: %"PRIx64" size: %lld", asOop(*pointer), size);

	oop *p = pointer;
	uint64_t i;
	uint64_t count = 0;

	for (i=0; i<size; i++)
	{
		count += gcCopyToInactivePointer (p);
		p++;
	}
	return (count);
}

uint64_t gcCopyToInactiveObjectContents(oop object)
{
	uint64_t result;

	if (isImmediate(object)) {
		//	LOGI ("  Immediate object - no action required: %lx", (unsigned long) object);
		return 0;
	}

	if (isBytes(object)) {
		// LOGI ("  Byte object - only relocate the class: %lx", (unsigned long) object);
		return gcCopyToInactivePointer((oop *)&(asObjectHeader(object)->stClass));
	}

	//  LOGI ("Relocating Object Contents: %lx size: %lx", (unsigned long) object, ((objectHeaderStruct *) object)->size);

	result =  gcCopyToInactivePointer((oop *)&(asObjectHeader(object)->stClass))
			+ gcCopyToInactivePointerRange (oopPtr(asObjectHeader(object)->bodyPointer), totalObjectSize(object));

	//  LOGI ("Finished relocating Object Contents: %lx", (unsigned long) object);
	return result;
}

uint64_t gcCopyToInactiveWellKnownObjects()
{
//	LOGI ("Relocating Well Known Objects");
	return gcCopyToInactivePointerRange ((oop *)&WellKnownObjects->space[0], WellKnownObjects->spaceSize);
//	LOGI ("Finished Relocating Well Known Objects");
}

uint64_t gcCopyToInactiveStack()
{
	oop frame;

	for (frame = currentContext;
		asOop(frame) != ST_NIL;
		frame = asContext(frame)->frame) {

		gcCopyToInactiveObjectContents(frame);
	}
    return 0;
}

uint64_t gcCopyToInactiveRegistry()
{
//	LOGI ("Relocating Registry");
//	return gcCopyToInactivePointerRange (&RegistrySpace->space[0], RegistrySpace->spaceSize);
	return 0;
}

uint64_t nextObjectIncrement(oop object)
{
	if (memorySize(object) == 0)
		return 0;

	return objectHeaderOopSize();
}

uint64_t gcCopyToInactiveSpace(memorySpaceStruct *space)
{
//	LOGI ("Relocating Space %lx", (unsigned long) space);
	uint64_t count = 0;
	uint64_t index;

	for (index = 0;
			index < space->firstFreeBlock;
			index += nextObjectIncrement(asOop(&space->space[index])))
		count += gcCopyToInactiveObjectContents(asOop(&space->space[index]));

	return count;
}

uint64_t findRememberedSetObject(oop object)
{

	uint64_t spaceMaxIndex = (RememberedSet->spaceSize)/sizeof(oop);

	uint64_t index = asObjectHeader(object)->identityHash % spaceMaxIndex;
	uint64_t startIndex = index;

	while (oopPtr(RememberedSet->space[index]) != NULL)
	{
		if (RememberedSet->space[index] == object)
			return 1;

		index++;
		if (index >= spaceMaxIndex)
			index = 0;

		if (startIndex == index)
		{
			LOGE ("Remembered Set is full");
			return 0;
		}
	}
	return 0;

}

void registerRememberedSetObject(oop object)
{
		
	uint64_t spaceMaxIndex = (RememberedSet->spaceSize)/sizeof(oop);

	uint64_t index = asObjectHeader(object)->identityHash % spaceMaxIndex;
	uint64_t startIndex = index;

//	LOGI ("registerRememberedSetObject spaceMaxIndex: %"PRIx64" index: %"PRIx64"", spaceMaxIndex, index);

	while (oopPtr(RememberedSet->space[index]) != NULL)
	{
		if (RememberedSet->space[index] == object)
			return;

		index++;
		if (index >= spaceMaxIndex)
			index = 0;

		if (startIndex == index)
		{
			LOGE ("Remembered Set is full");
			return;
		}
	}

	RememberedSet->space[index] = object;
}

int unregisterRememberedSetObject(oop object)
{
	uint64_t spaceMaxIndex = (RememberedSet->spaceSize)/sizeof(oop);

	uint64_t index = asObjectHeader(object)->identityHash % spaceMaxIndex;
	uint64_t startIndex = index;

	while (oopPtr(RememberedSet->space[index]) != NULL)
	{
		if (RememberedSet->space[index] == object) {
			RememberedSet->space[index] = 0;
			return 1;
		}

		index++;
		if (index >= spaceMaxIndex)
			index = 0;

		if (startIndex == index)
		{
			LOGE ("Remembered Set is full");
			return 0;
		}
	}
	return 0;
}

void rehashRememberedSet()
{
	int i;
//	LOGI ("rehashRememberedSet");

	for (i=0; i < ((RememberedSet->spaceSize) / sizeof(oop)); i++)
	{
		if (oopPtr(RememberedSet->space[i]) != NULL)
		{
			oop object = RememberedSet->space[i];
			RememberedSet->space[i] = 0;
			registerRememberedSetObject(object);
		}
	}
}

void gcCopyToInactiveRememberedSet()
{
	int i;
//	LOGI ("gcCopyToInactiveRememberedSet");

	for (i=0; i < ((RememberedSet->spaceSize) / sizeof(oop)); i++)
	{
		if (oopPtr(RememberedSet->space[i]) != NULL)
		{
			oop object = RememberedSet->space[i];
			if (gcCopyToInactiveObjectContents(object) == 0)
				RememberedSet->space[i] = 0;
		}
	}
}


void flipSurvivorSpaces()
{
	memorySpaceStruct *temp;

//	LOGI ("Flipping Survivor Spaces");
	temp = ActiveSurvivorSpace;
	ActiveSurvivorSpace = InactiveSurvivorSpace;
	InactiveSurvivorSpace = temp;
	markSpaceAsCurrent(ActiveSurvivorSpace);
	markSpaceAsNotCurrent(InactiveSurvivorSpace);
}

void clearSpace(memorySpaceStruct *space)
{
	if (spaceHasSpaceObject(space))
	{
		if (isTopHeaderSpace(space)) {
			// For top header spaces, we have the following structure:
			// |spaceObjectBody|..other bodies...|otherHeaders|spaceObjectHeader|
			// so our firstFreeBlock is 0 and our last free block is one objectHeaderStruct size from the end of the space
			space->firstFreeBlock = 0;
			space->lastFreeBlock = (space->spaceSize - sizeof(objectHeaderStruct)) / sizeof(oop) - 1;
		} else {
			// For bottom header spaces, we have the following structure:
			// |spaceObjectBody|spaceObjectHeader|otherHeaders|...|otherBodies|
			// so our firstFreeBlock is after one objectHeader and our lastFreeBlock is the end of the space
			space->firstFreeBlock = sizeof(objectHeaderStruct) / sizeof(oop);
			space->lastFreeBlock = space->spaceSize / sizeof(oop) - 1;
		}
	} else {
		space->firstFreeBlock = 0;
		space->lastFreeBlock = space->spaceSize / sizeof(oop) - 1;
	}
}

void clearEden()
{
//	LOGI ("Clearing Eden");

	clearSpace(InactiveSurvivorSpace);
	clearSpace(EdenSpace);

	EdenUsedForGC = 0;
}


oop tenure (oop object)
{
	oop newObject;
	newObject = allocateObjectInSpace(memorySize(object), OldSpace);

	return newObject;
}

void gcCopyToInactiveForScavenge()
{
	gcCopyToInactiveWellKnownObjects();
	gcCopyToInactiveStack();
	gcCopyToInactiveRememberedSet();
	gcCopyToInactiveSpace(InactiveSurvivorSpace);
	rehashRememberedSet();
}

void scavenge()
{
//	LOGI ("Scavenging from %"PRIx64" to %"PRIx64"", ActiveSurvivorSpace, InactiveSurvivorSpace);

	gcCopyToInactiveForScavenge();
	flipSurvivorSpaces();
	clearEden();
	captureFastContext(currentContext);
//	LOGI ("Scavenge finished");
}

void gcPrepareEdenForGC()
{
	EdenSpace->firstFreeBlock = 0;
	EdenSpace->lastFreeBlock = 0;
	EdenUsedForGC = 1;
}

void unmarkObjectFunction(oop object, void *args)
{
	unmarkObject(object);
}

void gcMarkSpaceUnused(memorySpaceStruct *space)
{
	enumerateObjectsInSpace(space, &unmarkObjectFunction, NULL);
}

void gcQueueMarkObject(oop object)
{
	if (object == 0)
		return;

	if (isImmediate(object))
		return;

	if (isMarked(object))
		return;

	if (isQueuedForMark(object))
		return;

	queueForMarkObject(object);

	EdenSpace->space[EdenSpace->firstFreeBlock] = object;

	EdenSpace->firstFreeBlock = (EdenSpace->firstFreeBlock + 1) % spaceSize(EdenSpace);
	if (EdenSpace->firstFreeBlock == EdenSpace->lastFreeBlock) {
		LOGE("Out of space in Eden during global GC");
		ERROR_EXIT;
	}
}

void gcQueueMarkStack(oop context)
{
	oop frame;

	for (frame = context;
		asOop(frame) != ST_NIL;
		frame = asContext(frame)->frame) {

		gcQueueMarkObject(frame);
	}
}

void gcQueueMarkPointerSpace(memorySpaceStruct *space)
{
	int i;
	for (i=0; i<space->firstFreeBlock; i++)
		gcQueueMarkObject(space->space[i]);
}

void gcMarkObject(oop object)
{
	if (isImmediate(object))
		return;

	if (isMarked(object))
		return;

	markObject(object);
	unqueueForMarkObject(object);

	gcQueueMarkObject(asObjectHeader(object)->stClass);

	if (!(isBytes(object))) {
		int i;
		for (i = 0; i < totalObjectSize(object); i++) {
			oop instVarOop = instVarAtInt(object, i);
			gcQueueMarkObject(instVarOop);
		}
	}
}

void gcPropagateMarks()
{
	for (; EdenSpace->lastFreeBlock != EdenSpace->firstFreeBlock;  EdenSpace->lastFreeBlock = (EdenSpace->lastFreeBlock + 1) % spaceSize(EdenSpace)) {
		gcMarkObject(EdenSpace->space[EdenSpace->lastFreeBlock]);
	}
}

void sweepObject(oop object, void *args)
{
	if (isFree(object)) {return;}
	if (isMarked(object)) {return;}
	if (isSpaceObject(object)) {return;}

	unregisterRememberedSetObject(object);
	markObjectFree(object);
}

void gcSweep(memorySpaceStruct *space)
{
	enumerateObjectsInSpace(space, sweepObject, NULL);
}

objectHeaderStruct *findFirstFreeHeader(memorySpaceStruct *space, objectHeaderStruct *currentHeader)
{
	objectHeaderStruct *header;
	for (header = currentHeader; header <= asObjectHeader(&space->space[space->firstFreeBlock - 1]); header++)
		if (isFree(header))
			return header;

	return NULL;
}

objectHeaderStruct *findLastUsedHeader(memorySpaceStruct *space, objectHeaderStruct *currentHeader)
{
	objectHeaderStruct *header;
	for (header = currentHeader; header >= asObjectHeader(&space->space[0]); header--)
		if (!isFree(header))
			return header;

	return NULL;
}

void relocateObjectPointer (oop *oopPointer, void *args)
{
	if (*oopPointer == 0)
		return;

	if (isImmediate(*oopPointer))
		return;

	if (isObjectInStackSpace(*oopPointer))
		return;

	if (isRelocated(*oopPointer))
		*oopPointer = asObjectHeader(*oopPointer)->stClass;	
}

void relocateObjectVariables(oop object, void *args)
{
	if (object == 0)
		return;
	
	if (isImmediate(object))
		return;

	if (isFree(object))
		return;

	if (isRelocated(object))
		return;

	relocateObjectPointer(&asObjectHeader(object)->stClass, NULL);
	if (isBytes(object))
		return;

	oop *instVarPointer;

	for (instVarPointer = (oop *) asObjectHeader(object)->bodyPointer;
		instVarPointer < &instVarAtInt(object, totalObjectSize(object));
		instVarPointer++)
		relocateObjectPointer(instVarPointer, NULL);
}

void dumpString (oop p, FILE *file)
{
	char *displayString;
	int i;
	displayString = malloc((size_t) basicByteSize(p)+1);

	for (i=1; i <= basicByteSize(p); i++)
		displayString[i-1] = basicByteAtInt(p, i);

	displayString[basicByteSize(p)] = '\0';

	fprintf (file, "%s", displayString);
	free (displayString);
}

void dumpOop(oop p, FILE *file)
{
	if (p == ST_NIL)
		fprintf (file, "nil");

	else if (p == ST_TRUE)
		fprintf (file, "true");

	else if (p == ST_FALSE)
		fprintf (file, "false");

	else if (isSmallInteger(p))
		fprintf (file, "%" PRId64, stIntToC(p));

	else if (isCharacter(p))
		fprintf (file, "%c", (char) stIntToC(p));

	else if (isFloat(p))
		fprintf (file, "%lf", (double) stFloatToC(p));

	else if ((p >= asOop(&StackSpace->space[0])) && (p <= asOop(&StackSpace->space[StackSpace->spaceSize / sizeof(oop)]))) {
		fprintf (file, "stack pointer");
		}
	else if (classOf(p) == ST_BYTE_STRING_CLASS)
	{
		dumpString(p, file);
	}
	else if (classOf(p) == ST_BYTE_SYMBOL_CLASS)
	{
		fprintf(file, "#");
		dumpString(p, file);
	}
	else if (classOf(p) == ST_METACLASS_CLASS)
	{
		fprintf (file, "metaclass ");
		dumpString (asClass(asMetaclass(p)->thisClass)->name, file);
	}
	else if (classOf(classOf(p)) == ST_METACLASS_CLASS)
	{
		fprintf (file, "class ");
		dumpString (asClass(p) -> name, file);
	}
	else {
		fprintf (file, "a ");
		dumpString (asClass(classOf(p))->name, file);
	}
}

#define headerNumber(h, s) (asObjectHeader (h) - (objectHeaderStruct *) (&(s)->space[0]))
void dumpHeader(objectHeaderStruct *header, memorySpaceStruct *space, FILE *file)
{
	int headerNumber = headerNumber(header, space);
	if (isRelocated(header))
		fprintf (file, "Header: %"PRId32" => %"PRId64"\n", headerNumber, (uint64_t) headerNumber(header->stClass, space));
	else if (isFree(header))
		fprintf (file, "Header: %"PRId32" Free - Size: %"PRId64"\n", headerNumber, (uint64_t)memorySize(header));
	else {
		fprintf (file, "Header: %"PRId32" Marked: %"PRId32" Size: %"PRId64" - ", headerNumber, isMarked(header), (uint64_t)memorySize(header));
		dumpOop(asOop(header), file);
		fprintf(file, "\n");
	}
}

void dumpHeadersInSpace(memorySpaceStruct *space, char *filename)
{
	objectHeaderStruct *header;
	FILE *file;
	
	file = fopen(filename, "w");
	for (header = asObjectHeader(&space->space[0]); header < asObjectHeader(&space->space[space->firstFreeBlock]); header++)
		dumpHeader(header, space, file);
	fclose(file);
}

void gcCompactHeaders(memorySpaceStruct *space)
{
	objectHeaderStruct *firstFreeHeader;
	objectHeaderStruct *lastUsedHeader;

	firstFreeHeader = findFirstFreeHeader(space, asObjectHeader(&space->space[0]));
	lastUsedHeader = findLastUsedHeader(space, asObjectHeader(&space->space[space->firstFreeBlock - (sizeof(objectHeaderStruct) / sizeof(oop))]));

	while ((firstFreeHeader != NULL) && (lastUsedHeader != NULL) && (firstFreeHeader < lastUsedHeader))
	{
		int wasRegistered;
		wasRegistered = unregisterRememberedSetObject(asOop(lastUsedHeader));
//		LOGI("Move object from %d to %d", headerNumber(lastUsedHeader, space), headerNumber(firstFreeHeader, space));
		*firstFreeHeader = *lastUsedHeader;
		if (wasRegistered)
			registerRememberedSetObject(asOop(firstFreeHeader));
		markObjectRelocated(lastUsedHeader)
		;
		markObjectFree(lastUsedHeader);
		lastUsedHeader->stClass = asOop(firstFreeHeader);
		setBodyHeaderPointer((oop) firstFreeHeader);
		firstFreeHeader = findFirstFreeHeader(space, firstFreeHeader);
		lastUsedHeader = findLastUsedHeader(space, lastUsedHeader);
	}

	lastUsedHeader = findLastUsedHeader(space, asObjectHeader(&space->space[space->firstFreeBlock - ((sizeof(objectHeaderStruct)) / sizeof(oop))]));
	if (lastUsedHeader == NULL)
		space->firstFreeBlock = 0;
	else
		space->firstFreeBlock = ((uint64_t)(lastUsedHeader + 1) - (uint64_t)asObjectHeader(&space->space[0])) / sizeof(oop);

	clearEden();
	relocateAllObjectPointers();
}

int objectBodyCompare(const void *a, const void *b)
{
	return asObjectHeader(a)->bodyPointer > asObjectHeader(b)->bodyPointer;
}

uint64_t *copyBody(oop object, uint64_t *lastBodyPointer)
{
	uint64_t *startOfBody = lastBodyPointer - totalObjectSize(object);

	if ((uint64_t *) asObjectHeader(object)->bodyPointer == startOfBody)
		return startOfBody;

	memmove (startOfBody, (oop *) asObjectHeader(object)->bodyPointer, (totalObjectSize(object) + 1) * sizeof(oop));
	asObjectHeader(object)->bodyPointer = (oop) startOfBody;
	return startOfBody;
}

void gcCompactBodies(memorySpaceStruct *space)
{
	uint64_t *copyToBody, *copyFromBody, *nextCopyFromBody;

	copyToBody = copyFromBody = &space->space[spaceSize(space) - 1];
	nextCopyFromBody = copyFromBody - totalObjectSize(*copyFromBody) -1;

// loop backwards through bodies from top of space towards bottom
	for( ;
		copyFromBody  > &space->space[space->lastFreeBlock];
		copyFromBody = nextCopyFromBody)
		{
			// Find the header for this body
			if (!isObjectInSpace(*copyFromBody, space))
				LOGI ("Object %" PRIx64 " isn't in the same space %" PRIx64, asOop(*copyFromBody), asOop(space));

			oop copyFromObject = asOop(*copyFromBody);
			if (asObjectHeader(copyFromObject)->bodyPointer == 0) {
				LOGI ("Object has empty body: %" PRIx64 " -> %" PRIx64, asOop(copyFromBody), asOop(copyFromObject));
				return;
			}

			// Capture the next header pointer now because we may overwrite that memory
			nextCopyFromBody = copyFromBody - totalObjectSize(*copyFromBody) -1;

			// If this header isn't free, we may need to move it
			if (!isFree(copyFromObject)) {
				copyToBody = copyBody (copyFromObject, copyToBody) - 1;
			}
		}
	space->lastFreeBlock = ((uint64_t)copyToBody - (uint64_t)&space->space[0]) / sizeof(oop);
}

void gcCompactSpace(memorySpaceStruct *space)
{
	gcCompactBodies(space);
	gcCompactHeaders(space);
}

void relocateObjectPointersInObjectSpace(memorySpaceStruct *space)
{
	enumerateObjectsInSpace(space, &relocateObjectVariables, NULL);
}

void relocateObjectPointersInPointerSpace(memorySpaceStruct *space)
{
	enumeratePointersInSpace(space, &relocateObjectPointer, NULL);
}

void relocateAllObjectPointers()
{
	relocateObjectPointersInObjectSpace(OldSpace);
	relocateObjectPointersInObjectSpace(EdenSpace);
	relocateObjectPointersInObjectSpace(ActiveSurvivorSpace);
	relocateObjectPointersInPointerSpace(WellKnownObjects);
	relocateObjectPointersInObjectSpace(StackSpace);
	relocateObjectPointersInPointerSpace(RememberedSet);
	rehashRememberedSet();
}

void globalGarbageCollect()
{
	LOGI ("Starting global garbage collection");

	scavenge();
	gcMarkSpaceUnused(OldSpace);
	gcMarkSpaceUnused(ActiveSurvivorSpace);
	gcMarkSpaceUnused(StackSpace);

	gcPrepareEdenForGC();

	gcQueueMarkStack(currentContext);
	gcQueueMarkPointerSpace(WellKnownObjects);

	gcPropagateMarks();

	gcSweep(OldSpace);
	gcSweep(ActiveSurvivorSpace);
	gcSweep(StackSpace);

	gcCompactSpace(OldSpace);

	clearEden();
	gcMarkSpaceUnused(OldSpace);
	gcMarkSpaceUnused(ActiveSurvivorSpace);
	gcMarkSpaceUnused(StackSpace);

	auditImage();
	captureFastContext(currentContext);
}


void reallocateSpace(int spaceIndex, uint64_t size)
{
	memorySpaceStruct *sourceSpace = Spaces[spaceIndex];
	memorySpaceStruct *destinationSpace = allocateSpace(size + sizeof(objectHeaderStruct));
	objectHeaderStruct *spaceObject;

	uint64_t index;

	if (destinationSpace == NULL) {
		LOGE ("Cannot allocate new space");
		return;
	}

	destinationSpace->spaceFlags = sourceSpace->spaceFlags;
	destinationSpace->spaceType = sourceSpace->spaceType;

	spaceObject = asObjectHeader(newInstanceOfClass(ST_MEMORY_SPACE_CLASS, 0, destinationSpace));
	spaceObject->bodyPointer = (uint64_t) destinationSpace;
	spaceObject->size = destinationSpace->spaceSize + sizeof(memorySpaceStruct);

	markSpaceObject(spaceObject);
	markHasSpaceObject(destinationSpace);

	if (isTopHeaderSpace(sourceSpace))
		makeTopHeaderSpace(destinationSpace);

	if (spaceIsScavenged(sourceSpace))
		makeScavengedSpace(destinationSpace);
	
	if (spaceIsStackManaged(sourceSpace))
		makeStackManagedSpace(destinationSpace);

	if (spaceIsMarkSweepManaged(sourceSpace))
		makeMarkSweepManagedSpace(destinationSpace);

	if (isObjectSpace(sourceSpace)) {
		for (index = 0; index < sourceSpace->firstFreeBlock; index += (sizeof(objectHeaderStruct) / sizeof(oop))) {
			moveObjectToSpace(asOop(&sourceSpace->space[index]), destinationSpace);
		}
		relocateAllObjectPointers();
		relocateObjectPointersInObjectSpace(destinationSpace);
	} else {
		// Pointer space
		for (index = 0; index < sourceSpace->firstFreeBlock; index += 1) {
			destinationSpace->space[index] = sourceSpace->space[index];
			destinationSpace->firstFreeBlock++;
		}
	}

	if (Spaces[spaceIndex] == EdenSpace) EdenSpace = destinationSpace;
	if (Spaces[spaceIndex] == SurvivorSpace1) SurvivorSpace1 = destinationSpace;
	if (Spaces[spaceIndex] == SurvivorSpace2) SurvivorSpace2 = destinationSpace;
	if (Spaces[spaceIndex] == OldSpace) OldSpace = destinationSpace;
	if (Spaces[spaceIndex] == ActiveSurvivorSpace) ActiveSurvivorSpace = destinationSpace;
	if (Spaces[spaceIndex] == InactiveSurvivorSpace) InactiveSurvivorSpace = destinationSpace;
	if (Spaces[spaceIndex] == StackSpace) StackSpace = destinationSpace;
	if (Spaces[spaceIndex] == WellKnownObjects) WellKnownObjects = destinationSpace;
	if (Spaces[spaceIndex] == RememberedSet) RememberedSet = destinationSpace;

	Spaces[spaceIndex] = destinationSpace;

	free (sourceSpace);
}
