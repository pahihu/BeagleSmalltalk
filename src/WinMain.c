// WinMain.c
//
// Beagle Smalltalk
// Copyright (c) 2025 Simberon Incorporated
// Released under the MIT License
// https://opensource.org/license/MIT

#include <stdio.h>
#include <strings.h>
#include <signal.h>
#include "object.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

char logString[10000], *logPtr;
int already_hooked_up = 0;
char *imageFilename;
int webSocketPortNumber = 4000;
int debugWebSocketPort = 4100;

uint64_t readFromFile(uint8_t *buffer, uint64_t size, void *data){
    FILE *memoryStream = (FILE *) data;

    fread(buffer, 1, size, memoryStream);
    if(ferror(memoryStream)){
        LOGE("There was an error reading data from the file | fp: %" PRId64, asOop(memoryStream));
        LOGE("There was an error reading data from the file | fp: %" PRId64, asOop(memoryStream));
    }

    return (uint64_t) size;
}


void loadImageFromFile(char *filename){
    FILE *fileStream = fopen(filename, "rb");
    if(!fileStream)
		{LOGE("Could not open file %s!", filename);}
    else {
        loadImage(&readFromFile, (void *) fileStream, filename);
        LOGI("Image loaded successfully");
    }
    fclose(fileStream);
}

void finish(void)
{
	exit(0);
}

void handle_signal(int signal) {
    switch (signal) {
    case SIGTERM:
    case SIGABRT:
    case SIGINT:
    case SIGSEGV:
    case SIGILL:
    case SIGFPE:
	  LOGI ("SIGNAL: %d", signal);
      dumpWalkback("Signal Received" );	  
      break;
    default:
      break;
    }
	exit(1);
  }

void HookupHandler() {
    if (already_hooked_up) {
      LOGE("Tried to hookup signal handlers more than once.");
    }
    already_hooked_up = TRUE;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGABRT, handle_signal);
    signal(SIGSEGV, handle_signal);
    signal(SIGILL, handle_signal);
    signal(SIGFPE, handle_signal);

    struct sigaction sa;
    // Setup the handler
    sa.sa_handler = &handle_signal;
    // Restart the system call, if at all possible
    sa.sa_flags = SA_RESTART;
    // Block every signal during the handler
    sigfillset(&sa.sa_mask);
    // Intercept SIGHUP and SIGINT
    if (sigaction(SIGHUP, &sa, NULL) == -1) {
      LOGE ("Cannot install SIGHUP handler.");
    }
    if (sigaction(SIGINT, &sa, NULL) == -1) {
      LOGE ("Cannot install SIGINT handler.");
    }
  }
  
 void UnhookHandler() {
    if (already_hooked_up) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGABRT, SIG_DFL);
        signal(SIGFPE, SIG_DFL);
        signal(SIGILL, SIG_DFL);
        signal(SIGSEGV, SIG_DFL);

      struct sigaction sa;
      // Setup the sighub handler
      sa.sa_handler = SIG_DFL;
      // Restart the system call, if at all possible
      sa.sa_flags = SA_RESTART;
      // Block every signal during the handler
      sigfillset(&sa.sa_mask);
      // Intercept SIGHUP and SIGINT
      if (sigaction(SIGHUP, &sa, NULL) == -1) {
        LOGE("Cannot uninstall SIGHUP handler.");
      }
      if (sigaction(SIGINT, &sa, NULL) == -1) {
        LOGE("Cannot uninstall SIGINT handler.");
      }

      already_hooked_up = FALSE;
    }
  }

void processCommandLineArguments(int argc, char **argv)
{
	int i;
    char *endPtr;

	for (i=1; i<argc; i++) {
		if ((argv[i][0] == '-') && (argv[i][1] == 'p')) {
            webSocketPortNumber = strtol(&(argv[i][2]), &endPtr, 10);
		continue;
		}
		if ((argv[i][0] == '-') && (argv[i][1] == 'd')) {
            debugWebSocketPort = strtol(&(argv[i][2]), &endPtr, 10);
		continue;
		}
		imageFilename = argv[i];
	}
}

int beagleMain(int argc, char **argv)
{
#ifndef __EMSCRIPTEN__
	HookupHandler();

	if (!suspended) {
		logString[0] = '\0';
		logPtr = logString;

		if (argc == 1) {
			printf ("Usage: %s <imageName>", argv[0]);
			exit(0);
		}
	processCommandLineArguments(argc, argv);

#else
	imageFilename = "beagle.im";
#endif
		loadImageFromFile(imageFilename);
//		Development = 1;

#ifndef __EMSCRIPTEN__
		if (Development) {
			setupRemoteSocket();
		}
#endif
		launchImage();

#ifndef __EMSCRIPTEN__
	}

    interpret();
#endif

#ifndef __EMSCRIPTEN__
	if (Development){
        terminateSocket();
    }
#endif

#ifndef __EMSCRIPTEN__
	UnhookHandler();
#endif

	return 0;
}

#ifdef __EMSCRIPTEN__
int main ()
#else
int main(int argc, char **argv)
#endif
{
    beagleMain(argc, argv);
}
