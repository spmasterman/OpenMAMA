/* $Id$
 *
 * OpenMAMA: The open middleware agnostic messaging API
 * Copyright (C) 2011 NYSE Technologies, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * 
 * mamalistencachedc - Example Mama API market data subscriber. 
 *
 * <pre>
 * Illustrates the following MAMA API features:
 *   1. Data dictionary initialization.
 *   2. Creating a transport.
 *   3. Creating different types of subscriptions (Snapshot, group etc)
 *   4. Walking all received fields in messages.
 *   5  Displaying only specific fields in a message.
 *
 * Examples:
 *
 * 1. Display data dictionary with dictionary requested from source
 *    "ORDER" (default dictionary symbol name space is "WOMBAT" and
 *    used in subsequent examples, below).
 *
 *    mamalistencahedc -S ORDER -D -d ORDER -s MSFT.ISLD -tport tport_name
 *
 * 2. Subscribing to top-of-book MSFT quote from Island ITCH feed (all
 *    fields) where the feed handler source is SOURCE:
 *
 *    mamalistencachedc -S ORDER -s MSFT.ISLD -tport tport_name
 *
 * 3. Subscribing to top-of-book MSFT & ORCL from Island ITCH feed
 *    (specific fields):
 *
 *    mamalistencachedc -S ORDER -s MSFT.ISLD -s MSFT.ISLD wBidPrice wBidSize wAskPrice 
 *
 * 4. Subscribing to a list of symbols from a file (all fields) and
 *    report updates quietly (an additional -q would eliminate printing
 *    of all normal updates):
 *
 *    mamalistencachedc -q -f file -tport tport_name
 *
 * <pre>
 *
 */

#include "wombat/port.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <mama/mama.h>
#include <mama/marketdata.h>
#include <mama/transport.h>
#include <mama/msgstatus.h>
#include <mama/status.h>
#include <mama/timer.h>
#include <mama/symbolmapfile.h>
#include <mama/fieldcache/fieldcache.h>
#include <mama/fieldcache/fieldcachefield.h>
#include <mama/fieldcache/fieldcacheiterator.h>

#define MAX_FIELDS 1000
#define MAX_SUBSCRIPTIONS 250000

static char *gUsageString[] =
{
"Example Mama API market data subscriber, which stores fields using a MamaFieldCache.",
"",
"Usage: mamalistenachedc -S symbolNameSpace [-s symbol | -f symbolFile]",
"                  [-tport transportName] [-dict_tport transportName]",
"                  [-d dictSymbolNameSpace] [-vDgE?IL1qWB] [-DF fileName]",
"                  [-hw highWaterMark] [-lw lowWaterMark]",
"                  [-threads numThreads] [-r throttleRate]",
"                  [-t subscriptionTimeout] [-version (requires -m)] [-m middleware]",
"",
"Options:",
"All the standard mamalistenc options are available, plus the following:",
"  -verifyCache    verifies the content of the cache against the message received.",
"  -verifyInterval changes the interval used for verification, (default 5s).",
"  -no_display      will not print out any field data.",
"  -printCache     Print content of the cache after printing the generated message.",
"",
NULL
};


/* This structure is used when verifying data held in the field cache
 * against a snapshot image of a subscription.
 */
typedef struct VerificationData
{
    /* The queue the subscription was created on. */    
    mamaQueue m_queue;

    /* The symbol name. */
    const char *m_symbol;

    /* The timer being used to verify the data. */
    mamaTimer m_timer;
    
    /* The index of the subscription/cache */
    int mIndex;

} VerificationData, * PVerificationData;

/* These members represent command line parameters detailed in the gUsageString above. */
static int      gVerifyCache        = 0;
static double   gVerifyInterval     = 5.0;
static int      gNoDisplay          = 0;
static int      gPrintCache         = 0;

/* There must be 1 field cache per symbol. */
static mamaFieldCache *gFieldCaches = NULL;
static int *gFieldCacheIndexes = NULL;

static mamaTransport     gTransport          = 0 ;
static long              gThrottle           = -1;
static long              gRecapThrottle      = -1;
static int               gMsgCount           = 0;
static long              gNumMsg             = 0;
static long              gNumMsgLast         = 0;
       long              gTotalMsg           = 0;
static int               gInterval           = 2;
static int               gThreads            = 0;
static int               gHasCreatedThreads  = 0;
static int               gQualityForAll      = 1;
static mamaQueue*        gQueues             = NULL;
static mamaDispatcher*   gDispatchers        = NULL;

static const char*       gTport              = NULL;

static const char*       gDictTport          = NULL;
static mamaTransport     gDictTransport      = NULL;

static mamaDictionary    gDictionary;
static const char*       gDictSymbolNamespace  = "WOMBAT";
static int               gDumpDataDict       = 0;
static int               gDictToFile         = 0;
static int               gDictFromFile         = 0;
static const char*       gDictFile           = NULL;
static int               gBuildDataDict      = 1;
static int               gExtractData        = 1;

static const char**      gFieldList          = NULL;
static int               gNumFields          = 0;

static int               gDictionaryComplete = 0;

static int               gGroupSubscription  = 0;
static int               gRequireInitial     = 1;
static int               gSnapshot           = 0;
static double            gTimeout            = 10.0;
static int               gQuietness          = 0;
static int               gPrintVersionAndExit= 0;

static mamaSubscription* gSubscriptionList   = NULL;

static int               gNumSubscriptions   = 0;
static const char**      gSymbolList         = NULL;
static int               gNumSymbols         = 0;
static MamaLogLevel      gSubscLogLevel      = MAMA_LOG_LEVEL_NORMAL;
static const char*       gFilename           = NULL;
static const char*       gMapFilename        = NULL;
static mamaSymbolMapFile gSymbolMapFromFile  = NULL;

static int               gNewIterators       = 0;
static const char*       gSymbolNamespace    = NULL;
static mamaSource        gSubscriptionSource = NULL;
static mamaSource        gDictSource         = NULL;
static const char*       gMiddleware         = "wmw";
static mamaBridge        gMamaBridge         = NULL;
static mamaQueue         gMamaDefaultQueue   = NULL;
static const char*       gAppName            = NULL;
static size_t            gHighWaterMark      = 0;
static size_t            gLowWaterMark       = 0;


/* Contains the amount of time that the example program will run for, if set to 0 then it
 * will run indefinitely.
 */
static int				 gShutdownTime		 = 0;


static void parseCommandLine    (int argc, const char* argv[]);
static void initializeMama      (void);

static void buildDataDictionary (void);
static void dumpDataDictionary  (void);
static void loadSymbolMap       (void);
static void readSymbolsFromFile (void);
static void subscribeToSymbols  (void);
static void mamashutdown            (void);
static void setQueueMonitors    (mamaQueue queue, int queueIndex);
static FILE* gLogFile = NULL;

static void MAMACALLTYPE
subscriptionOnMsg   (mamaSubscription    subscription,
                     mamaMsg             msg,
                     void*               closure,
                     void*               itemClosure);
static void MAMACALLTYPE
subscriptionOnQuality (mamaSubscription subsc,
                       mamaQuality      quality,
                       const char*      symbol,
                       short            cause,
                       const void*      platformInfo,
                       void*            closure);

static void MAMACALLTYPE
subscriptionOnError (mamaSubscription    subscription, 
                     mama_status         status,
                     void*               platformError,
                     const char*         subject,
                     void*               closure);
static void MAMACALLTYPE
subscriptionOnCreate (mamaSubscription    subscription, 
                      void*               closure);

static void displayFields       (mamaMsg             msg,
                                 mamaSubscription    subscription);
static void displaySpecificField(mamaFieldDescriptor field,
                                 mamaMsg             msg);
static void displayField        (mamaMsgField        field,
                                 const mamaMsg       msg,
                                 int                 indentLevel);
static void displayAllFields    (mamaMsg             msg,
                                 mamaSubscription    subscription,
                                 int                 indentLevel);

static void MAMACALLTYPE
rateReporterCallback       (mamaTimer           timer,
                            void *              closure);

static void MAMACALLTYPE
timeoutCb                  (mamaDictionary      dict,
                            void *              closure);
static void MAMACALLTYPE
errorCb                    (mamaDictionary      dict,
                            const char *        errMsg,
                            void *              closure);
static void MAMACALLTYPE
completeCb                 (mamaDictionary      dict,
                            void *              closure);

static void MAMACALLTYPE
displayCb                  (const mamaMsg       msg,
                            const mamaMsgField  field,
                            void *              closure);

/*Callback for event queue high watermark monitoring.*/
static void MAMACALLTYPE
highWaterMarkCallback (mamaQueue     queue,
                       size_t        size, 
                       void*         closure);

/*Callback for event queue low watermark monitoring.*/
static void MAMACALLTYPE
lowWaterMarkCallback  (mamaQueue     queue,
                       size_t        size, 
                       void*         closure);

static void MAMACALLTYPE
shutdownTimerCallback(mamaTimer timer, void *closure)
{
	/* Stop dispatching messages. */
	mama_stop(gMamaBridge);
}

                        
void usage                 (int                 exitStatus);


static void MAMACALLTYPE
verifyTimerCallback(mamaTimer timer, void *closure);

/* Function Prototypes. */
mama_status checkFieldIntegrityIds(mamaMsgField field, mamaFieldCache fieldCache, int *test);
mama_status checkSnapshotIntegrity(mamaFieldCache fieldCache, mamaMsg message);
mama_status createFieldCaches(void);
mama_status createVerificationTimer(mamaQueue queue, const char *symbol, int index);
mama_status destroyFieldCaches(void);
void        destroyVerificationTimer(PVerificationData data);
mama_status displayHeader(mamaMsg message, mamaSubscription subscription);
/*
mama_status getFieldAsString(char *buffer, long bufferLength, mamaMsgField field, mamaFieldType type);
*/
mama_status printAllCacheFields(mamaFieldCache fieldCache);
/*
mama_status printAllMessageFields(mamaFieldCache fieldCache, mamaMsg message);
*/
void        printField(mamaFieldCacheField field);
/*
mama_status printFieldUsingId(mamaMsgField field, mamaFieldCache fieldCache);
*/
/*
mama_status displayFields(mamaFieldCache fieldCache);
*/

int main (int argc, const char **argv)
{
    mamaTimer mamatimer;
	mamaTimer shutdownTimer = NULL;

    gSymbolList = (const char**)calloc (MAX_SUBSCRIPTIONS, sizeof (char*));
    gFieldList  = (const char**)calloc (MAX_FIELDS,        sizeof (char*));

    gSubscriptionList = (mamaSubscription*)calloc (MAX_SUBSCRIPTIONS,
                                                   sizeof (mamaSubscription));

    setbuf (stdout, NULL);
    parseCommandLine (argc, argv);

    if (gNumSymbols == 0 && !gPrintVersionAndExit)
    {
        readSymbolsFromFile ();
    }

    initializeMama ();
   
    buildDataDictionary ();
    dumpDataDictionary ();
    loadSymbolMap ();

    /* Create all of the field caches, note that there is 1 per symbol. */
    createFieldCaches();

    subscribeToSymbols ();

    printf ("Type CTRL-C to exit.\n");

    if (gMsgCount)
    {
        printf ("Timestamp           ,  Interval ,   Total\n");
        mamaTimer_create (&mamatimer,
                            gMamaDefaultQueue,
                            rateReporterCallback,
                            gInterval,
                            NULL);
    }

    /* Create the shutdown timer. */
    if(gShutdownTime > 0)
    {
        mamaTimer_create(&shutdownTimer, gMamaDefaultQueue, shutdownTimerCallback, gShutdownTime, NULL);
    }

    /* Start dispatching on the default event queue, this will block until Ctrl+C is pressed or the shutdown
	 * timer fires.
	 */
    mama_start (gMamaBridge);

    /* Destroy the shutdown timer if it was created. */
    if(shutdownTimer != NULL)
    {
        mamaTimer_destroy(shutdownTimer);
    }

    /* Destroy all of the field caches. */
    destroyFieldCaches();

    mamashutdown ();

    if (gMsgCount)
    {
        mamaTimer_destroy (mamatimer);
    }

    return 0;
}

/*
* Create subscriptions for all specified symbols.
*/
static void subscribeToSymbols (void)
{
    int              howMany = 0;
    mama_status      status  = MAMA_STATUS_OK;
    size_t           i;
    mamaMsgCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.onCreate  = subscriptionOnCreate;
    callbacks.onError   = subscriptionOnError;
    callbacks.onMsg     = subscriptionOnMsg;
    callbacks.onQuality = subscriptionOnQuality;    
    callbacks.onGap     = NULL;
    callbacks.onRecapRequest = NULL;
    /*
    * If use of multiple threads/queue was specified create a new mamaQueue
    * and mamaDispatcher for each one
    */
    if ((gThreads > 0) && (gHasCreatedThreads == 0))
    {
        gQueues      = (mamaQueue*)calloc 
                            (gThreads, sizeof (mamaQueue));
        gDispatchers = (mamaDispatcher*)calloc 
                            (gThreads, sizeof (mamaDispatcher));

        for (i = 0; i < gThreads; i++)
        {
            if (MAMA_STATUS_OK != (status = mamaQueue_create (&gQueues[i],
                            gMamaBridge)))
            {
                fprintf (stderr, "Error creating queue %s\n",
                         mamaStatus_stringForStatus( status ) );
                exit(1);
            }

            /*Set up queue monitors if specified.*/
            setQueueMonitors (gQueues[i], i);
            
            status = mamaDispatcher_create (&gDispatchers[i] , gQueues[i]); 

            if (status != MAMA_STATUS_OK)
            {
                fprintf (stderr, "Error creating dispatcher %s\n",
                         mamaStatus_stringForStatus( status ) );
                exit(1);
            }
        }
        gHasCreatedThreads = 1;
    }

    for (i = 0; i < gNumSymbols; i++)
    {
        /*If there is more than one queue round robin accross them*/
        mamaQueue queue = gQueues == 
                NULL ? gMamaDefaultQueue : gQueues[i % gThreads];

        {
        
            mamaSubscription_allocate (&gSubscriptionList[howMany]);
    
            mamaSubscription_setTimeout (gSubscriptionList[howMany],
                                        gTimeout);
    
            if (gSnapshot)
            {
                /* Create a snapshot subscription */
                mamaSubscription_setServiceLevel (gSubscriptionList[howMany],
                                                MAMA_SERVICE_LEVEL_SNAPSHOT,
                                                0);
            }
            else if (gGroupSubscription)
            {
                /* Create a group subscription */
                mamaSubscription_setSubscriptionType
                                                    (gSubscriptionList[howMany],
                                                    MAMA_SUBSC_TYPE_GROUP);
                mamaSubscription_setRequiresInitial (gSubscriptionList[howMany],
                                                    gRequireInitial);
            }
            else
            {
                /* Create a regular market data subscription.  */
                mamaSubscription_setRequiresInitial (gSubscriptionList[howMany],
                                                    gRequireInitial);
            }
    
            /*
                Logging at the individual subscription level can be controlled
                independantly from the global logging level.
            */
            mamaSubscription_setDebugLevel (gSubscriptionList[howMany],
                                            gSubscLogLevel);

            /* Create a verification timer for this subscription and queue. */
            createVerificationTimer(queue, gSymbolList[i], i);
    
            /*
            The subscription is not created immediately here. Rather an action
            to create the subscription is placed on the throttle queue.
            Creation of subscriptions starts once mama_start() is called
            */
            status = mamaSubscription_create (gSubscriptionList[howMany],
                                            queue,
                                            &callbacks,
                                            gSubscriptionSource,
                                            gSymbolList[i],
                                            (void*)&gFieldCacheIndexes[i]);
        
        }

        if (status != MAMA_STATUS_OK)
        {
            fprintf (stdout,
                     "Error creating subscription for %s. STATUS: %s\n",
                     gSymbolList[i], mamaStatus_stringForStatus(status));
            /*
            fprintf (stderr,
                     "Error creating subscription for %s. STATUS: %s\n",
                     gSymbolList[i], mamaStatus_stringForStatus(status));
            */
            continue;
        }

        if (++howMany % 1000 == 0)
        {
            printf ("Subscribed to %d subjects.\n", howMany);
        }
    }
}

/*
* Write the contents of the data dictionary to screen.
*/
static void dumpDataDictionary (void)
{
    mama_fid_t i = 0;

    /*
        Write the data dictionary to the file system.
        A data dictionary can be recreated from this file at a later stage.
    */
    if (gDictToFile)
    {
        printf ("Writing dict to %s\n", gDictFile);
        mamaDictionary_writeToFile (gDictionary, gDictFile);
    }

    if (gDumpDataDict)
    {
        size_t size = 0;
        
        mamaDictionary_getSize (gDictionary, &size);

        printf ("Number of fields in dictionary: %d\n", (unsigned int)size );

        for (i = 0; i < size; i++)
        {
            mamaFieldDescriptor field = NULL;
            mamaDictionary_getFieldDescriptorByIndex (gDictionary,
                                                      &field,
                                                      i);
            if (field)
            {
                const char* typeName = "unknown";
                char tmp[100];

                switch (mamaFieldDescriptor_getType (field))
                {
                    case MAMA_FIELD_TYPE_STRING:
                        typeName = "string";
                        break;
                    case MAMA_FIELD_TYPE_BOOL:
                        typeName = "boolean";
                        break;
                    case MAMA_FIELD_TYPE_CHAR:
                    case MAMA_FIELD_TYPE_I8:
                        typeName = "char";
                        break;
                    case MAMA_FIELD_TYPE_U8:
                    case MAMA_FIELD_TYPE_I16:
                    case MAMA_FIELD_TYPE_U16:
                    case MAMA_FIELD_TYPE_I32:
                    case MAMA_FIELD_TYPE_U32:
                    case MAMA_FIELD_TYPE_I64:
                    case MAMA_FIELD_TYPE_U64:
                        typeName = "integer";
                        break;
                    case MAMA_FIELD_TYPE_F64:
                        typeName = "double";
                        break;
                    case MAMA_FIELD_TYPE_PRICE:
                        typeName = "price";
                        break;
                    default:
                        snprintf (tmp, 100, "unknown(%d)",
                                  mamaFieldDescriptor_getType (field));
                        typeName = tmp;
                        break;
                }
                printf ("  %3d : %-7s : %s\n",
                        mamaFieldDescriptor_getFid(field), typeName,
                        mamaFieldDescriptor_getName(field));
                fflush(stdout);
            }
        }
    }
}

void MAMACALLTYPE
timeoutCb (mamaDictionary dict, void *closure)
{
    printf ("Timed out waiting for dictionary\n" );
    mama_stop(gMamaBridge);
}

void MAMACALLTYPE
errorCb (mamaDictionary dict, const char *errMsg, void *closure)
{
    fprintf (stderr, "Error getting dictionary: %s\n", errMsg );
    mama_stop(gMamaBridge);
}

void MAMACALLTYPE
completeCb (mamaDictionary dict, void *closure)
{
    gDictionaryComplete = 1;
    /*
       Stop processing events until all subscriptions have been created.
    */
    mama_stop (gMamaBridge);
}

static void buildDataDictionary (void)
{
    mamaDictionaryCallbackSet dictionaryCallback;
    mama_status result  =   MAMA_STATUS_OK;
    if (gBuildDataDict)
    {
        memset(&dictionaryCallback, 0, sizeof(dictionaryCallback));
        dictionaryCallback.onComplete = completeCb;
        dictionaryCallback.onTimeout  = timeoutCb;
        dictionaryCallback.onError    = errorCb;

        /*
           The dictionary is created asychronously. The dictionary has been
           successfully retreived once the onComplete callback has been
           invoked.
        */
        result = mama_createDictionary (&gDictionary,
                                        gMamaDefaultQueue,/*Use the default event queue*/
                                        dictionaryCallback,
                                        gDictSource,
                                        10.0,
                                        3,
                                        NULL);/*Don't specify a closure*/

        if (result != MAMA_STATUS_OK)
        {
            fprintf (stderr, "Exception creating "
                            "dictionary: MamaStatus: %s\n",
                            mamaStatus_stringForStatus (result));
            exit (1);
        }

        /*
            Start dispatching on the default event queue. Dispatching on the
            queue is unblocked once one of the dictionary callbacks is invoked.
        */
        mama_start (gMamaBridge);

        /*
            True only if onComplete resulted in the unblocking of the queue
            dispatching
        */
        if (!gDictionaryComplete)
        {
            fprintf (stderr, "Could not create dictionary.\n" );
            exit(1);
        }
        

    }
    else if (gDictFromFile)
    {
        mamaDictionary_create (&gDictionary);
        mamaDictionary_populateFromFile(
                gDictionary,
                gDictFile);
    }
}
static void mamashutdown (void)
{
    int i;
    
    /* Stop all the queues. */
    if (gQueues != NULL)
    {
        fprintf(stderr, "mamashutdown: destroying dispatch queues\n");
        for (i = 0; i < gThreads; i++)
        {
            mamaDispatcher_destroy (gDispatchers[i]);
        }
        free (gDispatchers);
    }
    
    fprintf(stderr, "mamashutdown: free-ing symbol and field lists\n");
    for (i = 0; i < gNumSymbols; i++)
    {
        free ((char *)gSymbolList[i]);
    }
    free ((void*)gSymbolList);
    
    for (i = 0; i < gNumFields; i++)
    {
        free ((char *)gFieldList[i]);
    }
    free ((void *)gFieldList);


    fprintf(stderr, "mamashutdown: destroying subscriptions\n");

    {
    
        for (i = 0; i < gNumSymbols; i++)
        {
            if (gSubscriptionList[i])
            {
                mamaSubscription_destroy (gSubscriptionList[i]);
                mamaSubscription_deallocate (gSubscriptionList[i]);
            }
        }
        free (gSubscriptionList);
    }
    
    /* Destroy the source. */
    if(NULL != gSubscriptionSource)
    {
        mamaSource_destroy(gSubscriptionSource);
    }
    
    if (gQueues != NULL)
    {
        fprintf(stderr, "mamashutdown: destroying queues\n");
        for (i = 0; i < gThreads; i++)
        {
            mamaQueue_destroyWait (gQueues[i]);
        }
        free (gQueues);
    }

    /* Destroy the dictionary. */
    if (gDictionary != NULL)
    {
        fprintf(stderr, "mamashutdown: destroying dictionary\n");
        mamaDictionary_destroy(gDictionary);
    }
    
    /* Destroy the dictionary source. */
    if(NULL != gDictSource)
    {
        mamaSource_destroy(gDictSource);
    }
    
    if (gTransport != NULL)
    {
        fprintf(stderr, "mamashutdown: destroying transport\n");
        mamaTransport_destroy (gTransport);
    }
    
    mama_close ();
}

static void MAMACALLTYPE
transportCb (mamaTransport      tport,
             mamaTransportEvent ev,
             short              cause,
             const void*        platformInfo,
             void*              closure)
{
    printf ("Transport callback: %s\n", mamaTransportEvent_toString (ev));
}

/*Steps required for initializing the API*/
void initializeMama (void)
{
    mama_status               status  =   MAMA_STATUS_OK;
  
    /*
        mama_setApplicationName should be called before mama_open().
    */
    if (gAppName)
    {
        mama_setApplicationName (gAppName); 
    }

    status = mama_loadBridge (&gMamaBridge, gMiddleware); 

    if (gPrintVersionAndExit)
    {
        if (gMamaBridge != NULL)
        {
            printf ("%s\n",mama_getVersion(gMamaBridge));
        }
        exit (0);
    }

    if (MAMA_STATUS_OK!=status)
    {
        fprintf (stderr,"Could not open MAMA %s bridge.\n",
                                         gMiddleware);
        exit (1);
    }

    /*
        mama_open() should be the first MAMA API call made in an
        application, with the exception of mama_loadBridge, 
        mama_setApplicationName(), mama_setApplicationClassName() 
        and mama_setProperty().
        It is reference counted. mama_close() must be called a
        corresponding number of times.
    */
    if (MAMA_STATUS_OK!=(status=mama_open ()))
    {
        fprintf (stderr,
                 "Failed to initialize MAMA STATUS: %d (%s)\n", status,
                 mamaStatus_stringForStatus (status));
        exit(1);
    }

    /*Get the default event queue*/
    mama_getDefaultEventQueue (gMamaBridge, &gMamaDefaultQueue);

    /*Check if we are monitoring queue activity*/
    setQueueMonitors (gMamaDefaultQueue, -1);

    /*Using a single transport for all subscriptions*/
    status = mamaTransport_allocate (&gTransport);

    if (status != MAMA_STATUS_OK)
    {
        fprintf (stderr,
                 "Failed to allocate transport STATUS: %d %s\n", status,
                 mamaStatus_stringForStatus (status));
        exit(1);
    }

    /*Register interest in transport related events*/
    status = mamaTransport_setTransportCallback(gTransport, transportCb, NULL);

    /*The default throttle rate is 500 msg/sec*/
    if (gThrottle != -1)
    {
        if (MAMA_STATUS_OK!=(status=mamaTransport_setOutboundThrottle (gTransport, 
                                           MAMA_THROTTLE_DEFAULT,
                                           gThrottle)))
        {
            fprintf (stderr, "Could not set default throttle"
                      "rate [%s]\n", mamaStatus_stringForStatus (status));
        }
    }

    /*The default recap throttle rate is 250 msg/sec*/
    if (gRecapThrottle != -1)
    {
        if (MAMA_STATUS_OK!=(status=mamaTransport_setOutboundThrottle (gTransport, 
                                           MAMA_THROTTLE_RECAP,
                                           gRecapThrottle)))
        {
            fprintf (stderr, "Could not set recap throttle"
                      "rate [%s]\n", mamaStatus_stringForStatus (status));
        }
    }
    
    /* Create the transport after any properties have been set. */
    if (MAMA_STATUS_OK!=
       (status=mamaTransport_create (gTransport, gTport, gMamaBridge)))
    {
        fprintf (stderr,
                 "Failed to create transport STATUS: %d %s\n", status,
                 mamaStatus_stringForStatus (status));
        exit(1);
    }

    if (!gQualityForAll)
    {
        mamaTransport_setInvokeQualityForAllSubscs (gTransport, 0);
    }
    
    /*
        The mamaSource replaces the individual specifying of the
        symbolNameSpace and the transport. The mamaSource acts as a unique
        identifier for a source of data. Multiple subscriptions will typically
        be created using a single mamaSource instance.
    */
    
    /*The mamaSource used for all subscription creation*/
    if (MAMA_STATUS_OK!=(status=mamaSource_create (&gSubscriptionSource)))
    {
        fprintf (stderr,
                 "Failed to create subscription mamaSource STATUS: %d %s\n",
                 status, mamaStatus_stringForStatus (status));
        exit(1);
    }
    
    mamaSource_setId (gSubscriptionSource, "Subscription_Source");
    mamaSource_setTransport (gSubscriptionSource, gTransport);
    mamaSource_setSymbolNamespace (gSubscriptionSource, gSymbolNamespace);
    
    /*Specify a separate transport on which to retreive the dictioanry*/
    if (gDictTport != NULL)
    {
        fprintf (stderr,
                 "Creating data dictionary transport using name: %s\n",
                 gDictTport);
        if (strlen(gDictTport)==0) gDictTport = NULL;
        
        status = mamaTransport_allocate (&gDictTransport);
        status = mamaTransport_create (gDictTransport, gDictTport, gMamaBridge);
        if (status != MAMA_STATUS_OK)
        {
            fprintf (stderr,
                     "Failed to create data dictionary transport STATUS: %d %s\n",
                     status, mamaStatus_stringForStatus (status));
            exit(1);
        }
    }
    else
    {
        gDictTransport = gTransport;
    }

    /*The mamaSource used for creating the dictionary*/
    if (MAMA_STATUS_OK!=(status=mamaSource_create (&gDictSource)))
    {
        fprintf (stderr,
                 "Failed to create dictionary mamaSource STATUS: %d %s\n",
                 status, mamaStatus_stringForStatus (status));
        exit(1);
    }       

    mamaSource_setId (gDictSource, "Dictionary_Source");
    mamaSource_setTransport (gDictSource, gDictTransport);
    mamaSource_setSymbolNamespace (gDictSource, gDictSymbolNamespace);
}

static void readSymbolsFromFile (void) 
{
    /* get subjects from file or interactively */
    FILE* fp = NULL;
    char charbuf[1024];

    if (gFilename)
    {
        if ((fp = fopen (gFilename, "r")) == (FILE *)NULL)
        {
            perror (gFilename);
            exit (1);
        }
    }
    else
    {
        fp = stdin;
    }
    if (isatty(fileno (fp)))
    {
        printf ("Enter one symbol per line and terminate with a .\n");
        printf ("Symbol> ");
    }
    while (fgets (charbuf, 1023, fp))
    {
        char *c = charbuf;

        /* Input terminate case */
        if (*c == '.')
            break;

        /* replace newlines with NULLs */
        while ((*c != '\0') && (*c != '\n')) 
            c++;
        *c = '\0';
        
        gSymbolList[gNumSymbols++] = strdup (charbuf);
        if (isatty(fileno (fp)))
            printf ("Symbol> ");
    }
}

static void loadSymbolMap (void)
{
    if (gMapFilename)
    {
        mama_status status = mamaSymbolMapFile_create (&gSymbolMapFromFile);
        if (status == MAMA_STATUS_OK)
        {
            status = mamaSymbolMapFile_load (gSymbolMapFromFile,
                                             gMapFilename);
        }

        if (status != MAMA_STATUS_OK)
        {
            fprintf (stderr, 
                     "Failed to initialize symbol map file: %s\n",
                     gMapFilename);
            exit (1);
        }

        mamaTransport_setSymbolMapFunc (
                            gTransport,
                            (mamaSymbolMapFunc)mamaSymbolMapFile_map,
                            gSymbolMapFromFile);
    }
}

static void parseCommandLine (int argc, const char** argv)
{
    int i = 1;
    
    for (i = 1; i < argc; )
    {
        if ((strcmp (argv[i], "-S") == 0) ||
            (strcmp (argv[i], "-source") == 0)||
            (strcmp (argv[i], "-SN") == 0)||
            (strcmp (argv[i], "-symbolnamespace") == 0))
        {
            gSymbolNamespace = argv[i + 1];
            i += 2;
        }
        else if (strcmp (argv[i], "-d") == 0)
        {
            gDictSymbolNamespace = argv[i + 1];
            i += 2;
        }
        else if (strcmp (argv[i], "-D") == 0)
        {
            gDumpDataDict = 1;
            i++;
        }
        else if (strcmp (argv[i], "-DF") == 0)
        {
            gDictToFile = 1;
            gDictFile = argv[i+1];
            i+=2;
        }
        else if (strcmp (argv[i], "-B") == 0)
        {
            gBuildDataDict = 0;
            i++;
        }
        else if (strcmp (argv[i], "-I") == 0)
        {
            gRequireInitial = 0;
            i++;
        }
        else if (strcmp (argv[i], "-g") == 0)
        {
            gGroupSubscription = 1;
            i++;
        }
        else if (strcmp (argv[i], "-s") == 0)
        {
            if (argv[i + 1] != NULL)
            {
                gSymbolList[gNumSymbols++]=strdup (argv[i + 1]);
                i += 2;
            }
            else
            {
                gSymbolList[gNumSymbols++]="";
                i += 2;
            }
        }
        else if (strcmp (argv[i], "-f") == 0)
        {
            gFilename = argv[i + 1];
            i += 2;
        }
        else if (strcmp (argv[i], "-1") == 0)
        {
            gSnapshot = 1;
            i++;
        }
        else if (strcmp ("-m", argv[i]) == 0)
        {
            gMiddleware = argv[i+1];
            i += 2;               
        }
        else if (strcmp (argv[i], "-r") == 0)
        {
            gThrottle = strtol (argv[i+1], NULL, 10);
            i += 2;
        }
        else if (strcmp (argv[i], "-rr") == 0)
        {
            gRecapThrottle = strtol (argv[i+1], NULL, 10);
            i += 2;
        }
        else if (strcmp (argv[i], "-t") == 0)
        {
            gTimeout = strtod (argv[i+1], NULL);
            i += 2;
        }
        else if (strcmp (argv[i], "-q") == 0)
        {
            gQuietness++;
            i++;
        }
        else if (strcmp (argv[i], "-E") == 0)
        {
            gExtractData=0;
            i++;
        }
        else if (strcmp (argv[i], "-c") == 0)
        {
            gMsgCount=1;
            gInterval = strtol (argv[i+1], NULL,10);
            if (gInterval < 1)
            {
                gInterval=2;
            }
            i+=2;
        }
        else if (strcmp (argv[i], "-mp") == 0)
        {
            gMapFilename = argv[i + 1];
            i += 2;
        }
        else if (strcmp (argv[i], "-threads") == 0)
        {
            gThreads = atoi (argv[i + 1]);
            i += 2;
        }
        else if (strcmp (argv[i], "-A") == 0)
        {
            gQualityForAll = 0;
            i++;
        }
        else if (strcmp (argv[i], "-tport") == 0)
        {
            gTport = argv[i+1];
            i += 2;
        }
        else if (strcmp (argv[i], "-dict_tport") == 0)
        {
            gDictTport = argv[i+1];
            i += 2;
        }
        else if (strcmp (argv[i], "-v") == 0)
        {
            if (mama_getLogLevel () == MAMA_LOG_LEVEL_WARN)
            {
                mama_enableLogging (stderr, MAMA_LOG_LEVEL_NORMAL);
            }
            else if (mama_getLogLevel () == MAMA_LOG_LEVEL_NORMAL)
            {
                mama_enableLogging (stderr, MAMA_LOG_LEVEL_FINE);
            }
            else if (mama_getLogLevel () == MAMA_LOG_LEVEL_FINE)
            {
                mama_enableLogging (stderr, MAMA_LOG_LEVEL_FINER);
            }
            else
            {
                mama_enableLogging (stderr, MAMA_LOG_LEVEL_FINEST);
            }
            i++;
        }
        else if ( strcmp( argv[i], "-log_file" ) == 0 )
        {
            gLogFile = fopen( argv[i+1],"w+" );
            if( gLogFile == NULL)
            {
                fprintf (stderr, "Failed open log file %s\n",argv[i+1] );
                exit(1);
            }
            else
            {
                mama_enableLogging (gLogFile, mama_getLogLevel ());
            }
            i += 2;
        }
        else if ( strcmp( argv[i], "-app" ) == 0 )
        {
            gAppName = argv[i+1];
            i += 2;
        }
        else if ( strcmp( argv[i], "-ni" ) == 0 )
        {
            gNewIterators = 1;
            i++;
        }
        else if (strcmp( argv[i], "-hw" ) == 0)
        {
            gHighWaterMark = atoi (argv [i+1]);    
            i += 2;
        }
        else if (strcmp( argv[i], "-lw" ) == 0)
        {
            gLowWaterMark = atoi (argv [i+1]);    
            i += 2;
        }
        else if ( strcmp( argv[i], "-V" ) == 0 )
        {
            if (gSubscLogLevel == MAMA_LOG_LEVEL_NORMAL)
            {
                gSubscLogLevel = MAMA_LOG_LEVEL_FINE;
            }
            else if (gSubscLogLevel == MAMA_LOG_LEVEL_FINE)
            {
                gSubscLogLevel = MAMA_LOG_LEVEL_FINER;
            }
            else
            {
                gSubscLogLevel = MAMA_LOG_LEVEL_FINEST;
            }

            i++;
        }
        else if (strcmp (argv[i], "-version") == 0)
        {
            gPrintVersionAndExit = 1;
            i+=1;
        }
        else if ((strcmp (argv[i], "-?") == 0)||
                 ((strcmp (argv[i], "--help") == 0)))
        {
            usage (0);
        }
        else if (strcmp (argv[i], "-use_dict_file") == 0)
        {
            gDictFromFile = 1;
            gBuildDataDict =0;
            gDictFile = strdup (argv[i+1]);
            i+=2;
        }
        
        else if (strcmp ("-shutdown", argv[i]) == 0)
        {
            gShutdownTime = atoi (argv[i + 1]);
            i += 2;
        }
        else if (strcmp (argv[i], "-verifyCache") == 0)
        {
            gVerifyCache = 1;
            i++;
        }
        else if (strcmp (argv[i], "-verifyInterval") ==  0)
        {
            gVerifyInterval = strtod (argv[i+1], NULL);
            i += 2;
        }
        else if (strcmp (argv[i], "-no_display") == 0)
        {
            gNoDisplay = 1;
            i++;
        }
        else if (strcmp (argv[i],"-printCache") == 0)
        {
            gPrintCache = 1;
            i++;
        }
        else
        {
            gFieldList[gNumFields++] = strdup (argv[i]);
            i++;
        }
    }
}

/*
    This callback function, passed to the mamaSubscription upon creation, is
    where most of the processing within a MAMA based application will occur.
    This callback is invoked for each message received by the API.
*/
void MAMACALLTYPE
subscriptionOnMsg  (mamaSubscription subscription, 
                    mamaMsg msg,
                    void *closure,
                    void *itemClosure)
{
    mamaMsg message;
    /* The closure is the index of the subscription within the list. */

    mamaFieldCache fieldCache = NULL;

    int subscriptionIndex = 0;
    
    if (closure)
    {
        subscriptionIndex = *(int*)closure;

        /* Get the field cache for this symbol. */
        fieldCache = gFieldCaches[subscriptionIndex];
    }
    
    gNumMsg++;

    /*
        The type and status of a mamaMsg can indicate how the message should
        be interpretated.
    */
    switch (mamaMsgType_typeForMsg (msg))
    {
    case MAMA_MSG_TYPE_SNAPSHOT:    
        {
            checkSnapshotIntegrity(fieldCache, msg);
            return; 
        }
    case MAMA_MSG_TYPE_DELETE:
    case MAMA_MSG_TYPE_EXPIRE:
        mamaSubscription_destroy (subscription);
        mamaSubscription_deallocate (subscription);
        gSubscriptionList[subscriptionIndex] = 0;

        if (--gNumSubscriptions == 0)
        {
            fprintf (stderr,
                     "Symbol deleted or expired. No more subscriptions\n");
            exit(1);
        }
        return; 
    default:
        break;
    }

    switch (mamaMsgStatus_statusForMsg (msg))
    {
    case MAMA_MSG_STATUS_BAD_SYMBOL:
    case MAMA_MSG_STATUS_EXPIRED:
    case MAMA_MSG_STATUS_TIMEOUT:
        mamaSubscription_destroy (subscription);
        mamaSubscription_deallocate (subscription);
        gSubscriptionList[subscriptionIndex] = 0;
        
        if (--gNumSubscriptions == 0)
        {
            fprintf (stderr, "Bad or expired symbol.\n" );
            exit(1);
        }
        return;
    default:
        break;
    }
    
    /* Apply the message to the field cache. */
    mamaFieldCache_applyMessage(fieldCache, msg, gDictionary);

    mamaMsg_create(&message);
    mamaFieldCache_getFullMessage(fieldCache, message);

    if (!gNoDisplay)
    {
        if (gNumFields == 0)
        {
            displayAllFields(message, subscription, 0);
        }
        else
        {
            displayFields(message, subscription);
        }

        if (gPrintCache)
        {
            displayHeader(msg, subscription);
            printAllCacheFields(fieldCache);
        }
    }

    mamaMsg_clear(message);
    mamaFieldCache_getDeltaMessage(fieldCache, message);
    mamaMsg_destroy(message);

    fflush(stdout);
}

/*
    The onQuality callback is invoked when the quality of a subscription
    changes state. E.g. From OK to STALE and vise versa.
*/
void MAMACALLTYPE
subscriptionOnQuality (mamaSubscription subsc,
                       mamaQuality      quality,
                       const char*      symbol,
                       short            cause,
                       const void*      platformInfo,
                       void*            closure)
{
    printf ("Quality changed to %s for %s, cause %d, platformInfo: %s\n",
            mamaQuality_convertToString (quality), symbol, cause,
            platformInfo ? (char*)platformInfo: "");
}


void displayFields(mamaMsg msg, mamaSubscription subscription )
{
    const char* source = NULL;
    const char* symbol = NULL;
    int         i      = 0;

    mamaSubscription_getSource (subscription, &source);
    mamaSubscription_getSymbol (subscription, &symbol);

    if (gQuietness < 2)
    {
        const char* issueSymbol = NULL;
        mamaMsg_getString (msg, NULL, 305, &issueSymbol);
        printf ("%s.%s.%s Type: %s Status %s \n",
                       issueSymbol,
                       source,
                       symbol,
                       mamaMsgType_stringForMsg (msg),
                       mamaMsgStatus_stringForMsg (msg));
    }

    for (i = 0; i < gNumFields; i++)
    {
        mamaFieldDescriptor field = NULL;

        /*
            Get the field descriptor for the specified field from the data
            dictionary. This will be used to extract data for that field from
            the message.
        */
        mamaDictionary_getFieldDescriptorByName (
                    gDictionary,
                    &field,
                    gFieldList[i]);

        displaySpecificField (field, msg);
    }

}
/*
void displayFields(mamaFieldCache fieldCache)
{
    mama_status ret = MAMA_STATUS_OK;
    mamaFieldCacheField cacheField = NULL;
    int fieldIndex = 0;
    mamaFieldDescriptor descriptor = NULL;

    while((ret == MAMA_STATUS_OK) && (fieldIndex < gNumFields))    
    {
        ret = mamaDictionary_getFieldDescriptorByName(gDictionary,
                                                      &descriptor,
                                                      gFieldList[fieldIndex]);
        if(ret == MAMA_STATUS_OK)
        {
            ret = mamaFieldCache_find(fieldCache,
                    mamaFieldDescriptor_getFid(descriptor),
                    NULL,
                    &cacheField);
            if(ret == MAMA_STATUS_OK)
            {
                printField(cacheField);
            }
        }
        fieldIndex++;
    }
    return ret;
}
*/

void displaySpecificField (mamaFieldDescriptor field,
                           mamaMsg             msg)
{
    const char* fieldName   =   NULL;
    short       fid         =   0;
    char value[256];

    if (field == NULL) return;

    /*
        The mamaFieldDescriptor contains meta data describing a fields
        attributes. The name and the fid for a field can be obtained from the
        descriptor.
    */
    fieldName = mamaFieldDescriptor_getName (field);
    fid = mamaFieldDescriptor_getFid (field);

    if (fieldName == NULL || strlen (fieldName) == 0)
    {
        mamaFieldDescriptor dictEntry;
        mamaDictionary_getFieldDescriptorByFid (gDictionary, &dictEntry, fid);

        fieldName = mamaFieldDescriptor_getName (dictEntry);
    }

    if (gQuietness < 3)
    {
        /*
          If performance is an issue do not use getFieldAsString. Rather, use
          the strongly typed accessors on the message or field objects.
          For absolute performance use field iteration rather than direct
          message access.
        */
        mama_status status = mamaMsg_getFieldAsString (msg,
                                                       fieldName,
                                                       fid,
                                                       value,
                                                       256);
        if (gQuietness < 1)
        {
            if (MAMA_STATUS_NOT_FOUND == status)
            {
                sprintf (value,"%s","Field Not Found");
            }

            printf ("%-20s | %3d | %20s | %s\n",
                    fieldName,
                    fid,
                    mamaFieldDescriptor_getTypeName (field),
                    value);
        }
    }
}

#define printData(value, format)                \
do                                              \
{                                               \
    if (gQuietness==0)                           \
    {                                           \
        printf (format, value);                 \
    }                                           \
} while (0)                                     \

void displayField (mamaMsgField field, const mamaMsg msg, int indentLevel)
{
    mamaFieldType   fieldType       =   0;
    const char*     fieldName       = NULL;
    const char*     fieldTypeName   = NULL;
    uint16_t        fid             = 0;
    const char*     indentOffset    = NULL;
    const char*     indentOffsetAll = "%-20s | %4d | %13s | ";

    if (!gExtractData)return;

    /*For formatting of output only*/
    switch (indentLevel)
    {
        case 0:
            indentOffset = "%2s";
            break;
        case 1:
            indentOffset = "%4s";
            break;
        case 2:
            indentOffset = "%6s";
            break;
        case 3:
            indentOffset = "%8s";
            break;
        default:
            indentOffset = "%10s";
    }

    if (gQuietness<1)
    {
        printData ("",indentOffset);
        /*
            Attributes for a field can be obtained directly from the field
            rather than from the field descriptor describing that field.
        */
        mamaMsgField_getName (field, &fieldName);
        mamaMsgField_getFid  (field, &fid);
        mamaMsgField_getTypeName (field, &fieldTypeName);
        printf (indentOffsetAll,
                fieldName ? fieldName : "",
                fid,
                fieldTypeName);
    }

    /*
        The most efficient way of extracting data while iterating fields is to
        obtain the field type and then call the associated strongly typed
        accessor.

        mamaMsgField_getAsString() will return a stringified version of the
        data but is considerably less efficient and is not recommended for
        production use.
    */
    mamaMsgField_getType (field, &fieldType);

    switch (fieldType)
    {
        case MAMA_FIELD_TYPE_BOOL:
            {
                mama_bool_t result;
                mamaMsgField_getBool (field, &result);
                printData (result, "%d\n");
                break;
            }
        case MAMA_FIELD_TYPE_CHAR:
            {
                char result;
                mamaMsgField_getChar (field, &result);
                printData (result, "%c\n");
                break;
            }
        case MAMA_FIELD_TYPE_I8:
            {
                int8_t result;
                mamaMsgField_getI8 (field, &result);
                printData (result, "%d\n");
                break;
            }
         case MAMA_FIELD_TYPE_U8:
            {
                uint8_t result;
                mamaMsgField_getU8 (field, &result);
                printData (result, "%u\n");
                break;
            }
         case MAMA_FIELD_TYPE_I16:
            {
                int16_t result;
                mamaMsgField_getI16 (field, &result);
                printData (result, "%d\n");
                break;
            }
         case MAMA_FIELD_TYPE_U16:
            {
                uint16_t result;
                mamaMsgField_getU16 (field, &result);
                printData (result, "%u\n");
                break;
            }
         case MAMA_FIELD_TYPE_I32:
            {
                int32_t result;
                mamaMsgField_getI32 (field, &result);
                printData (result, "%d\n");
                break;
            }
         case MAMA_FIELD_TYPE_U32:
            {
                uint32_t result;
                mamaMsgField_getU32 (field, &result);
                printData (result, "%u\n");
                break;
            }
         case MAMA_FIELD_TYPE_I64:
            {
                int64_t result;
                mamaMsgField_getI64 (field, &result);
                printData (result, "%"PRId64"\n");
                break;
            }
         case MAMA_FIELD_TYPE_U64:
            {
                uint64_t result;
                mamaMsgField_getU64 (field, &result);
                printData (result, "%"PRId64"\n");
                break;
            }
         case MAMA_FIELD_TYPE_F32:
            {
               mama_f32_t result;
               mamaMsgField_getF32 (field, &result);
               printData (result, "%f\n");
               break;
            }
         case MAMA_FIELD_TYPE_F64:
            {
                mama_f64_t result;
                mamaMsgField_getF64 (field, &result);
                printData (result, "%f\n");
                break;
            }
         case MAMA_FIELD_TYPE_TIME:
            {
                mamaDateTime result = NULL;
                char         dateTimeString[56];
                mamaDateTime_create (&result);
                mamaMsgField_getDateTime (field, result);
                mamaDateTime_getAsString (result,dateTimeString, 56);
                printData (dateTimeString, "%s\n");
                mamaDateTime_destroy (result);
                break;
            }
         case MAMA_FIELD_TYPE_PRICE:
            {
                mamaPrice result;
                char      priceString[56];
                mamaPrice_create (&result);
                mamaMsgField_getPrice (field, result);
                mamaPrice_getAsString (result, priceString, 56);
                printData (priceString, "%s\n");
                mamaPrice_destroy (result);
                break;
            }
         case MAMA_FIELD_TYPE_STRING:
            {
                const char* result = NULL;
                mamaMsgField_getString (field, &result);
                printData (result, "%s\n");
                break;
            }
         case MAMA_FIELD_TYPE_VECTOR_STRING:
            {
                const char** result = NULL;
                mama_size_t size = 0;
                mama_size_t i;
                mamaMsgField_getVectorString (
                    field,
                    &result,
                    &size);
                printData("","%s\n");
                for (i=0;i<size;i++)
                {
                    printData("",indentOffset);
                    printData(result[i], "  %s\n");
                }
                break;
            }
         case MAMA_FIELD_TYPE_VECTOR_I32:
            {
              const mama_i32_t* result = NULL;
              mama_size_t size = 0;
              mama_size_t i =0;
              mamaMsgField_getVectorI32 (field,&result,&size);
              printData("","%s\n");
              for (i=0;i<size;i++)
              {
                 printData("", indentOffset);
                 printData(result[i], " %d\n");
              }
                break;
            }

         case MAMA_FIELD_TYPE_VECTOR_F64:
            {
              const mama_f64_t* result = NULL;
              mama_size_t size = 0;
              mama_size_t i =0;
              mamaMsgField_getVectorF64 (field,&result,&size);
              printData("","%s\n");
              for (i=0;i<size;i++)
              {
                 printData("", indentOffset);
                 printData(result[i], " %f\n");
              }
                break;
            }
         case MAMA_FIELD_TYPE_MSG:
            {
                mamaMsg result;
                mamaMsgField_getMsg (field, &result);
                printData ("", "%s\n");
                displayAllFields (result, NULL, indentLevel+1);
                break;
            }
         case MAMA_FIELD_TYPE_VECTOR_MSG:
            {
                /*
                    Vectors of messages are only supported using WomabtMsg as
                    the wire encoding data format. The example here
                    recursively prints all data in all messages.
                */
                const mamaMsg*  result;
                mama_size_t     resultLen;
                int             i;
                mamaMsgField_getVectorMsg (field, &result, &resultLen);
                printData ("", "%s\n");
                printData ("",indentOffset);
                printData ("{", "%s\n");
                for (i=0;i<resultLen;i++)
                {
                    displayAllFields (result[i], NULL, indentLevel+1);
                }
                printData ("",indentOffset);
                printData ("}", "%s\n");
                break;
            }
        default:
            {
                printData ("Unknown", "%s\n");
            }
            break;
    }/*End switch*/
}

void MAMACALLTYPE
displayCb (const mamaMsg       msg,
                const mamaMsgField  field,
                void*               closure)
{
    displayField (field, msg, (int)closure);
}

void displayAllFields (mamaMsg msg, mamaSubscription subscription, int
        indentLevel)
{
    mamaMsgField    field       =   NULL;
    mama_status     status      =   MAMA_STATUS_OK;

    if (gQuietness < 2 && subscription)
    {
        const char* source = NULL;
        const char* symbol = NULL;
        const char* issueSymbol = NULL;

        mamaSubscription_getSource (subscription, &source);
        mamaSubscription_getSymbol (subscription, &symbol);
        mamaMsg_getString (msg, NULL, 305, &issueSymbol);
        printf ("%s.%s.%s Type: %s Status %s \n",
                issueSymbol == NULL ? "" : issueSymbol,
                source      == NULL ? "" : source,
                symbol      == NULL ? "" : symbol,
                mamaMsgType_stringForMsg (msg),
                mamaMsgStatus_stringForMsg (msg));
    }

    /*
        Iterating over all the fields in a message is more efficient than
        accessing data directly on the message object itself. When accessing
        the message directly the message must first find the field before
        returning the data. For messages which do not contain the requested
        field this requires traversal of the whole message before returning.
        Using the iteration approach requires that each field within the
        message is only visited once.
    */
    if (gNewIterators == 0)
    {
        mamaMsg_iterateFields (msg, displayCb, gDictionary, (void*)indentLevel);
    }
    else
    {
	if (gNewIterators != 0)
    	{
		mamaMsgIterator iterator = NULL;
        	/*An iterator can be reused for efficiency - however, it cannot
	  	be shared across all queues*/
        	mamaMsgIterator_create(&iterator, gDictionary);

        	if (MAMA_STATUS_OK!=(status=mamaMsgIterator_associate(iterator, msg)))
        	{
            		fprintf (stderr, "Could not associate iterator "
                      		"with message. [%s]\n", mamaStatus_stringForStatus (status));
        	}
        	else
        	{
            		while ((field = mamaMsgIterator_next(iterator)) != NULL)
            		{
                		displayField (field, msg, 0);
            		}
        	}

		mamaMsgIterator_destroy(iterator);
    	}
    }
}

/*
    Creation of subscriptions is throttled by the API. The onCreate callback
    is invoked whenther the subscription request is sent from the throttle
    queue.
*/

void MAMACALLTYPE
subscriptionOnCreate (mamaSubscription subscription, void* closure)
{   
    const char* source = NULL;
    const char* symbol = NULL;
    mamaQueue queue;

    gNumSubscriptions++;
    mamaSubscription_getSource (subscription, &source);
    mamaSubscription_getSymbol (subscription, &symbol); 

    mamaSubscription_getQueue(subscription, &queue);

    if (gQuietness < 3)
    {
        fprintf(stdout,
            "Created subscription: source=[%s], symbol=[%s]\n",
            source, symbol);
    }
}

static void MAMACALLTYPE
subscriptionOnError (mamaSubscription subscription, 
                     mama_status      status,
                     void*            platformError,
                     const char*      subject,
                     void*            closure)
{
    fprintf (stdout,
             "An error occurred creating subscription for %s: %s\n",
             subject ? subject : "(null)",
             mamaStatus_stringForStatus (status));
}

static void MAMACALLTYPE
rateReporterCallback (mamaTimer  timer, void*   closure)
{
    int           msgInterval = gNumMsg - gNumMsgLast;
    char          timeStr[20];
    mamaDateTime  now;

    mamaDateTime_create (&now);
    mamaDateTime_setToNow (now);
    mamaDateTime_getAsFormattedString (now, timeStr, 20, "%F %T");
    mamaDateTime_destroy (now);
    printf ("%s, %-10d, %-12ld\n", timeStr, msgInterval, gNumMsg);
    gNumMsgLast = gNumMsg;
}

void usage (int exitStatus)
{
    int i = 0;
    while (gUsageString[i]!=NULL)
    {
        printf ("%s\n", gUsageString[i++]);
    }
    exit (exitStatus);
}

static void MAMACALLTYPE
highWaterMarkCallback (mamaQueue     queue,
                       size_t        size,
                       void*         closure)
{
    mama_status status      = MAMA_STATUS_OK;
    const char* queueName   = NULL;

    if (queue)
    {
        if (MAMA_STATUS_OK!=(status=mamaQueue_getQueueName (queue, &queueName)))
        {
            fprintf (stderr, "Could not get queue name [%s]",
                     mamaStatus_stringForStatus (status));
        }
    }
    else /*Default queue*/
    {
        queueName = "DEFAULT_QUEUE";
    }

    printf ("%s high water mark exceeded. Num events "
            "on queue: %"PRI_MAMA_SIZE_T"\n",
            queueName == NULL ? "" : queueName, size);
}

static void MAMACALLTYPE
lowWaterMarkCallback  (mamaQueue     queue,
                       size_t        size,
                       void*         closure)
{
    mama_status status      = MAMA_STATUS_OK;
    const char* queueName   = NULL;

    if (queue)
    {
        if (MAMA_STATUS_OK!=(status=mamaQueue_getQueueName (queue, &queueName)))
        {
            fprintf (stderr, "Could not get queue name [%s]",
                     mamaStatus_stringForStatus (status));
        }
    }
    else /*Default queue*/
    {
        queueName = "DEFAULT_QUEUE";
    }

    printf ("%s low water mark exceeded. Num events "
            "on queue: %"PRI_MAMA_SIZE_T"\n",
            queueName == NULL ? "" : queueName, size);
}

void setQueueMonitors (mamaQueue queue, int queueIndex)
{
    if (gHighWaterMark>0 || gLowWaterMark>0)
    {
        mamaQueueMonitorCallbacks queueCallbacks;
        char                      queueNameBuf[10];
        mama_status               status = MAMA_STATUS_OK;

        snprintf (queueNameBuf, 10, "QUEUE %d", queueIndex);    

        mamaQueue_setQueueName (queue, queueNameBuf);

        queueCallbacks.onQueueHighWatermarkExceeded =
            highWaterMarkCallback;
        queueCallbacks.onQueueLowWatermark  =   lowWaterMarkCallback;

        mamaQueue_setQueueMonitorCallbacks (queue, 
                                            &queueCallbacks, 
                                            NULL);

        if (gHighWaterMark>0)
        {
            if (MAMA_STATUS_OK!=(status=mamaQueue_setHighWatermark 
                        (queue, gHighWaterMark)))
            {
                fprintf (stderr,
                         "Could not set high water mark for queue %s. [%s]", 
                         queueNameBuf,
                         mamaStatus_stringForStatus (status));
            }
        }

        if (gLowWaterMark>0)
        {
            if (MAMA_STATUS_OK!=(status=mamaQueue_setLowWatermark
                        (queue, gLowWaterMark)))
            {
                fprintf (stderr,
                         "Could not set low water mark for queue %s. [%s]", 
                         queueNameBuf,
                         mamaStatus_stringForStatus (status));
            }
        }
    }
    return;
}

/* mamalistencachedc specific methods */
mama_status checkFieldIntegrityIds(mamaMsgField field, mamaFieldCache fieldCache, int *test)
{
    mama_status ret = MAMA_STATUS_OK;
    mamaFieldType type = 0;
    mamaFieldCacheField fieldCacheField = NULL;
    const char* fieldName = NULL;
    int localTest = 1;

    mama_fid_t fieldId = 0;
    ret = mamaMsgField_getFid(field, &fieldId);
    if(ret == MAMA_STATUS_OK)
    {
        if(fieldId > WOMBAT_MAX_RESERVED_FID)
        {
            ret = mamaMsgField_getType(field, &type);
            if(ret == MAMA_STATUS_OK)
            {
                mamaMsgField_getName(field, &fieldName);
                ret = mamaFieldCache_find(fieldCache, fieldId, NULL, &fieldCacheField);
                if(ret == MAMA_STATUS_OK)
                {
                    switch (type)
                    {
                    case MAMA_FIELD_TYPE_BOOL:
                    {
                        mama_bool_t cacheVal = -1, msgVal;
                        mamaMsgField_getBool(field, &msgVal);
                        mamaFieldCacheField_getBool(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %d, %d",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_CHAR:
                    {
                        char cacheVal = '\0', msgVal;
                        mamaMsgField_getChar(field, &msgVal);
                        mamaFieldCacheField_getChar(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %d, %d",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_I8:
                    {
                        mama_i8_t cacheVal = -1, msgVal;
                        mamaMsgField_getI8(field, &msgVal);
                        mamaFieldCacheField_getI8(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %d, %d",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_U8:         
                    {
                        mama_u8_t cacheVal = 1, msgVal;
                        mamaMsgField_getU8(field, &msgVal);
                        mamaFieldCacheField_getU8(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %d, %d",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }            
                        break;
                    }
                    case MAMA_FIELD_TYPE_I16:
                    {
                        mama_i16_t cacheVal = -987, msgVal;
                        mamaMsgField_getI16(field, &msgVal);
                        mamaFieldCacheField_getI16(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %d, %d",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_U16:
                    {
                        mama_u16_t cacheVal = 987, msgVal;
                        mamaMsgField_getU16(field, &msgVal);
                        mamaFieldCacheField_getU16(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %d, %d",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_I32:
                    {
                        mama_i32_t cacheVal = -987, msgVal;
                        mamaMsgField_getI32(field, &msgVal);
                        mamaFieldCacheField_getI32(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %d, %d",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_U32:
                    {
                        mama_u32_t cacheVal = 987, msgVal;
                        mamaMsgField_getU32(field, &msgVal);
                        mamaFieldCacheField_getU32(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %d, %d",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_I64:
                    {
                        mama_i64_t cacheVal = -987, msgVal;
                        mamaMsgField_getI64(field, &msgVal);
                        mamaFieldCacheField_getI64(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %"PRId64", %"PRId64,
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_U64:
                    {
                        mama_u64_t cacheVal = 987, msgVal;
                        mamaMsgField_getU64(field, &msgVal);
                        mamaFieldCacheField_getU64(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %"PRId64", %"PRId64,
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_F32:
                    {
                        mama_f32_t cacheVal = -1, msgVal;
                        mamaMsgField_getF32(field, &msgVal);
                        mamaFieldCacheField_getF32(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %f, %f",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_F64:
                    {
                        mama_f64_t cacheVal = -1, msgVal;
                        mamaMsgField_getF64(field, &msgVal);
                        ret = mamaFieldCacheField_getF64(fieldCacheField, &cacheVal);
                        if (cacheVal != msgVal)
                        {
                            printf ("\nThe Failed Field is %s and the values are %g, %g",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_STRING:                           
                    {
                        const char *cacheVal = NULL, *msgVal = NULL;
                        mama_size_t len = 0;
                        mamaMsgField_getString(field, &msgVal);
                        mamaFieldCacheField_getString(fieldCacheField, &cacheVal, &len);
                        if (strcmp(cacheVal, msgVal) != 0)
                        {
                            printf ("\nThe Failed Field is %s and the values are %s, %s",
                                fieldName,msgVal,cacheVal);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_TIME:
                    {
                        mamaDateTime cacheVal = NULL, msgVal = NULL;
                        mama_u64_t msgValNumber, cacheValNumber;
                        mamaMsgField_getDateTime(field, msgVal);
                        mamaFieldCacheField_getDateTime(fieldCacheField, &cacheVal);
                        if (mamaDateTime_equal(msgVal, cacheVal) != 0)
                        {
                            mamaDateTime_getEpochTimeMicroseconds(msgVal, &msgValNumber);
                            mamaDateTime_getEpochTimeMicroseconds(cacheVal, &cacheValNumber);
                            printf ("\nThe Failed Field is %s and the values are %"PRId64", %"PRId64,
                                fieldName,msgValNumber,cacheValNumber);
                            localTest = 0;
                        }
                        break;
                    }
                    case MAMA_FIELD_TYPE_PRICE:
                    {  
                        mamaPrice cacheVal = NULL, msgVal = NULL;
                        double msgValNumber, cacheValNumber;
                        mamaMsgField_getPrice(field, &msgVal);
                        mamaFieldCacheField_getPrice(fieldCacheField, &cacheVal);
                        if (mamaPrice_equal(msgVal, cacheVal) != 0)
                        {
                            mamaPrice_getValue(msgVal, &msgValNumber);
                            mamaPrice_getValue(cacheVal, &cacheValNumber);
                            printf ("\nThe Failed Field is %s and the values are %f, %f",
                                fieldName,msgValNumber,cacheValNumber);
                            localTest = 0;
                        }
                        break;
                    }
                    default:
                        printf("\nFailed: default");
                        break;
                    }
                }
                else
                {
                    printf("\nFailed: cannot find field in cache");
                }
            }
            else
            {
                printf("\nFailed: cannot get field type for field");
            }
        }
    }
    else
    {
        printf("\nFailed: cannot get field if for field");
    }

    if(ret != MAMA_STATUS_OK)
    {
        localTest = 0;
    }    

    *test = localTest;

    return ret;
}

mama_status checkSnapshotIntegrity(mamaFieldCache fieldCache, mamaMsg message)
{    
    /* Create a new iterator. */
    mamaMsgIterator iterator = NULL;
    mama_status ret = mamaMsgIterator_create(&iterator, gDictionary);
    if(ret == MAMA_STATUS_OK)
    {
        /* Associate the iterator with the message. */
        ret = mamaMsgIterator_associate(iterator, message);
        if(ret == MAMA_STATUS_OK)
        {
            /* This flag will be set to 1 for each field that has passed the test. */
            int test = 1;

            /* Create an enumerator. */
            mamaMsgField nextField = mamaMsgIterator_next(iterator);

            printf("Field cache integrity check start.\n");
    
            /* Enumerate all the fields. */
            while((nextField != NULL) && (ret == MAMA_STATUS_OK) && (test == 1))
            {
                /* Check the integrity of this field, if there is a dictionary then
                 * this will be done using field names.
                 */
                ret = checkFieldIntegrityIds(nextField, fieldCache, &test);

                /* Get the next field. */
                nextField = mamaMsgIterator_next(iterator);
            }

            /* Print a message if all the tests passed. */
            if(test == 0)
            {
                printf ("\ncheckSnapshotIntegrity: FAILED");
            }
            else
            {
                printf ("\ncheckSnapshotIntegrity: SUCCESSFUL");
            }
            printf("\nField cache integrity check end.\n");            
        } 

        /* Destroy the iterator. */
        {
            mama_status error = mamaMsgIterator_destroy(iterator);
            if(ret == MAMA_STATUS_OK)
            {
                ret = error;
            }
        }
    }

    return ret;
}

mama_status createFieldCaches(void)
{
    mama_status ret = MAMA_STATUS_NOMEM;
    int cacheIndex = 0;

    /* Allocate the array of field caches. */
    gFieldCaches = (mamaFieldCache *)calloc(gNumSymbols, sizeof(mamaFieldCache));
    if(gFieldCaches != NULL)
    {
        gFieldCacheIndexes = (int*)calloc(gNumSymbols, sizeof(int));
        /* Enumerate the array and create each one in turn. */
        cacheIndex = 0;
        ret = MAMA_STATUS_OK;
        while((ret == MAMA_STATUS_OK) && (cacheIndex<gNumSymbols))        
        {
            /* Create the cache with typical values and ensure that auto string format is on. */
            ret = mamaFieldCache_create(&gFieldCaches[cacheIndex]);
            mamaFieldCache_setUseLock(gFieldCaches[cacheIndex], 0);
            gFieldCacheIndexes[cacheIndex] = cacheIndex;

            /* Increment the loop counter. */
            cacheIndex++;
        }

        /* If something went wrong then destroy the caches. */
        if(ret != MAMA_STATUS_OK)
        {
            destroyFieldCaches();
        }
    }

    return ret;
}

mama_status createVerificationTimer(mamaQueue queue, const char *symbol, int index)
{
    /* Returns. */
    mama_status ret = MAMA_STATUS_OK;

    /* If field cache data must be verified then create a timer that will obtain
     * snapshot subscriptions at a regular interval.
     */
    if((gVerifyCache == 1) && (gVerifyInterval > 0))
    {
        /* Allocate a structure. */
        PVerificationData data = calloc(1, sizeof(VerificationData));
        ret = MAMA_STATUS_NOMEM;
        if(data != NULL)
        {
            /* Save arguments in member variables. */
            data->m_queue    = queue;
            data->m_symbol   = strdup(symbol);
            data->mIndex     = index;
            {
                /* Create the timer. */            
                ret = mamaTimer_create(&data->m_timer, queue, verifyTimerCallback, gVerifyInterval, data);

                /* If it didn't work then delete the data object. */
                if(ret != MAMA_STATUS_OK)
                {
                    destroyVerificationTimer(data);
                }
            }
        }
    }   

    return ret;
}

mama_status destroyFieldCaches(void)
{
    mama_status ret = MAMA_STATUS_OK;
    int cacheIndex = 0;

    if(gFieldCaches != NULL)
    {
        return ret;
    }

    /* Enumerate the array and destroy each one in turn. */
    for(cacheIndex=0; cacheIndex<gNumSymbols; cacheIndex++)
    {
        /* Destroy the cache and preserve the function return code. */
        mamaFieldCache_destroy(gFieldCaches[cacheIndex]);
    }

    /* Free the array itself. */
    free(gFieldCaches);
    gFieldCaches = NULL;
    
    free(gFieldCacheIndexes);
    gFieldCacheIndexes = NULL;

    return ret;
}

void destroyVerificationTimer(PVerificationData data)
{
    /* Only continue if the data is valid. */
    if(data != NULL)
    {
        /* Free the symbol. */
        if(data->m_symbol != NULL)
        {
            free((void *)data->m_symbol);
        }

        /* Destroy the timer. */
        if(data->m_timer != NULL)
        {
            mamaTimer_destroy(data->m_timer);
        }

        /* Delete the structure. */
        free(data);
    }
}

mama_status displayHeader(mamaMsg message, mamaSubscription subscription)
{
    mama_status ret = MAMA_STATUS_OK;

    if(gQuietness < 2)
    {
        const char* source = NULL;
        ret = mamaSubscription_getSource(subscription, &source);
        if(ret == MAMA_STATUS_OK)
        {
            const char* symbol = NULL;
            ret = mamaSubscription_getSymbol(subscription, &symbol); 
            if(ret == MAMA_STATUS_OK)
            {
                const char* issueSymbol = NULL;
                ret = mamaMsg_getString(message, NULL, 305, &issueSymbol);
                if(ret == MAMA_STATUS_OK)
                {
                    printf ("%s.%s.%s Type: %s Status %s \n", 
                           issueSymbol,
                           source,
                           symbol,
                           mamaMsgType_stringForMsg(message),
                           mamaMsgStatus_stringForMsg(message));
                }
            }
        }
    }

    return ret;
}


mama_status printAllCacheFields(mamaFieldCache fieldCache)
{
    mama_status ret = MAMA_STATUS_OK;
    mamaFieldCacheIterator cacheIterator = NULL;
    mamaFieldCacheField field = NULL;
    mama_size_t size = 0;

    mamaFieldCache_getSize(fieldCache, &size);
    printf("Cache size: %"PRI_MAMA_SIZE_T"\n", size);
    
    ret = mamaFieldCacheIterator_create(&cacheIterator, fieldCache);
    while (mamaFieldCacheIterator_hasNext(cacheIterator))
    {
        field = mamaFieldCacheIterator_next(cacheIterator);
        printField(field);
    }
    
    mamaFieldCacheIterator_destroy(cacheIterator);
    return ret;
}
/*
mama_status printAllMessageFields(mamaFieldCache fieldCache, mamaMsg message)
{
    mama_status ret = MAMA_STATUS_OK;
    mamaMsgIterator iterator = NULL;
    mamaMsgField field = NULL;
    
    if (gPrintUsingName)
    {
        return MAMA_STATUS_INVALID_ARG;
    }
        
    ret = mamaMsgIterator_create(&iterator, gDictionary);
    ret = mamaMsgIterator_associate(iterator, message);
    field = mamaMsgIterator_next(iterator);
    while((field != NULL) && (ret == MAMA_STATUS_OK))
    {
        ret = printFieldUsingId(field, fieldCache);
        field = mamaMsgIterator_next(iterator);                
    }

    mamaMsgIterator_destroy(iterator);
    return ret;
}
*/
void printField(mamaFieldCacheField field)
{
    mama_fid_t fid;
    mamaFieldType type;
    const char *typeName;
    char valueChar;
    const char* valueStr;
    mama_bool_t valueBool;
    mama_i8_t valueI8;
    mama_u8_t valueU8;
    mama_i16_t valueI16;
    mama_u16_t valueU16;
    mama_i32_t valueI32;
    mama_u32_t valueU32;
    mama_i64_t valueI64;
    mama_u64_t valueU64;
    mama_f32_t valueFloat;
    mama_f64_t valueDouble;
    mama_size_t strLen;
    const char *fieldName = "";
    mamaFieldDescriptor fieldDesc;
    mama_bool_t modified = 0;
    
    if(gQuietness < 1)
    {
        char fieldBuffer[256] = "";

        mamaFieldCacheField_getType(field, &type);
        typeName = mamaFieldTypeToString(type);
        
        mamaFieldCacheField_getFid(field, &fid);
        
        mamaFieldCacheField_isModified(field, &modified);

        if(gDictionary != NULL)
        {
            mamaDictionary_getFieldDescriptorByFid(gDictionary, &fieldDesc, fid);
            fieldName = mamaFieldDescriptor_getName(fieldDesc);
        }

        sprintf(
            fieldBuffer,
            "  %-30s | %4d | %10s | %c | ",
            fieldName,
            fid,
            typeName,
            modified ? '*': ' ');
        printf("%s", (const char*)fieldBuffer);

        switch (type)
        {
        case MAMA_FIELD_TYPE_CHAR:
            mamaFieldCacheField_getChar(field, &valueChar);
            printf("%c\n", valueChar);
            break;
        case MAMA_FIELD_TYPE_BOOL:
            mamaFieldCacheField_getBool(field, &valueBool);
            printf("%d\n", valueBool);
            break;
        case MAMA_FIELD_TYPE_I8:
            mamaFieldCacheField_getI8(field, &valueI8);
            printf("%d\n", valueI8);
            break;
        case MAMA_FIELD_TYPE_U8:
            mamaFieldCacheField_getU8(field, &valueU8);
            printf("%d\n", valueU8);
            break;
        case MAMA_FIELD_TYPE_I16:
            mamaFieldCacheField_getI16(field, &valueI16);
            printf("%d\n", valueI16);
            break;
        case MAMA_FIELD_TYPE_U16:
            mamaFieldCacheField_getU16(field, &valueU16);
            printf("%d\n", valueU16);
            break;
        case MAMA_FIELD_TYPE_I32:
            mamaFieldCacheField_getI32(field, &valueI32);
            printf("%d\n", valueI32);
            break;
        case MAMA_FIELD_TYPE_U32:
            mamaFieldCacheField_getU32(field, &valueU32);
            printf("%d\n", valueU32);
            break;
        case MAMA_FIELD_TYPE_I64:
            mamaFieldCacheField_getI64(field, &valueI64);
            printf("%"PRId64"\n", valueI64);
            break;
        case MAMA_FIELD_TYPE_U64:
            mamaFieldCacheField_getU64(field, &valueU64);
            printf("%"PRId64"\n", valueU64);
            break;
        case MAMA_FIELD_TYPE_F32:
            mamaFieldCacheField_getF32(field, &valueFloat);
            printf("%f\n", valueFloat);
            break;
        case MAMA_FIELD_TYPE_F64:
            mamaFieldCacheField_getF64(field, &valueDouble);
            printf("%f\n", valueDouble);
            break;
        case MAMA_FIELD_TYPE_STRING:
            mamaFieldCacheField_getString(field, &valueStr, &strLen);
            printf("%s\n", valueStr);
            break;
        case MAMA_FIELD_TYPE_TIME:
        {
            const mamaDateTime result = NULL;
            char         dateTimeString[56];
            mamaFieldCacheField_getDateTime (field, &result);
            mamaDateTime_getAsString (result,dateTimeString, 56);
            printf ("%s\n", dateTimeString);
            break;
        }
         case MAMA_FIELD_TYPE_PRICE:
        {
            const mamaPrice result = NULL;
            char      priceString[56];
            mamaFieldCacheField_getPrice (field, &result);
            mamaPrice_getAsString (result, priceString, 56);
            printf ("%s\n", priceString);
            break;
        }
         case MAMA_FIELD_TYPE_VECTOR_BOOL:
         {
             const mama_bool_t* result = NULL;
             mama_size_t size = 0;
             mama_size_t i =0;
             mamaFieldCacheField_getBoolVector(field,&result,&size);
             printf("\n");
             for (i=0;i<size;i++)
             {
                 printf("   %d\n", (int)result[i]);
             }
             break;
         }
         case MAMA_FIELD_TYPE_VECTOR_CHAR:
         {
             const char* result = NULL;
             mama_size_t size = 0;
             mama_size_t i =0;
             mamaFieldCacheField_getCharVector(field,&result,&size);
             printf("\n");
             for (i=0;i<size;i++)
             {
                 printf("   %c\n", (int)result[i]);
             }
             break;
         }
        case MAMA_FIELD_TYPE_VECTOR_I8:
        {
            const mama_i8_t* result = NULL;
            mama_size_t size = 0;
            mama_size_t i =0;
            mamaFieldCacheField_getI8Vector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("   %d\n", (int)result[i]);
            }
            break;
        }
        case MAMA_FIELD_TYPE_VECTOR_U8:
        {
            const mama_u8_t* result = NULL;
            mama_size_t size = 0;
            mama_size_t i =0;
            mamaFieldCacheField_getU8Vector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("   %d\n", (unsigned int)result[i]);
            }
            break;
        }
        case MAMA_FIELD_TYPE_VECTOR_I16:
        {
            const mama_i16_t* result = NULL;
            mama_size_t size = 0;
            mama_size_t i =0;
            mamaFieldCacheField_getI16Vector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("   %d\n", result[i]);
            }
            break;
        }
        case MAMA_FIELD_TYPE_VECTOR_U16:
        {
            const mama_u16_t* result = NULL;
            mama_size_t size = 0;
            mama_size_t i =0;
            mamaFieldCacheField_getU16Vector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("   %d\n", result[i]);
            }
            break;
        }
        case MAMA_FIELD_TYPE_VECTOR_I32:
        {
            const mama_i32_t* result = NULL;
            mama_size_t size = 0;
            mama_size_t i =0;
            mamaFieldCacheField_getI32Vector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("   %d\n", result[i]);
            }
            break;
        }
        case MAMA_FIELD_TYPE_VECTOR_U32:
        {
            const mama_u32_t* result = NULL;
            mama_size_t size = 0;
            mama_size_t i =0;
            mamaFieldCacheField_getU32Vector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("   %d\n", result[i]);
            }
            break;
        }
        case MAMA_FIELD_TYPE_VECTOR_I64:
        {
            const mama_i64_t* result = NULL;
            mama_size_t size = 0;
            mama_size_t i =0;
            mamaFieldCacheField_getI64Vector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("   %"PRId64"\n", result[i]);
            }
            break;
        }
        case MAMA_FIELD_TYPE_VECTOR_U64:
        {
            const mama_u64_t* result = NULL;
            mama_size_t size = 0;
            mama_size_t i =0;
            mamaFieldCacheField_getU64Vector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("   %"PRId64"\n", result[i]);
            }
            break;
        }
        case MAMA_FIELD_TYPE_VECTOR_F32:
        {
            const mama_f32_t* result = NULL;
            mama_size_t size = 0;
            mama_size_t i =0;
            mamaFieldCacheField_getF32Vector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("   %f\n", result[i]);
            }
            break;
        }
        case MAMA_FIELD_TYPE_VECTOR_F64:
        {
            const mama_f64_t* result = NULL;
            mama_size_t size = 0;
            mama_size_t i =0;
            mamaFieldCacheField_getF64Vector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("   %f\n", result[i]);
            }
            break;
        }
        case MAMA_FIELD_TYPE_VECTOR_STRING:
        {
            const char** result = NULL;
            mama_size_t size = 0;
            mama_size_t i;
            mamaFieldCacheField_getStringVector(field,&result,&size);
            printf("\n");
            for (i=0;i<size;i++)
            {
                printf("    %s\n", result[i]);
            }
            break;
        }
        default:
            break;
        }
    }
}

/*
mama_status printFieldUsingId(mamaMsgField field, mamaFieldCache fieldCache)
{
    mama_status ret = MAMA_STATUS_OK;

    mama_fid_t id = 0;
    ret = mamaMsgField_getFid(field, &id);
    if(ret == MAMA_STATUS_OK)
    {
        mamaFieldType type = 0;
        ret = mamaMsgField_getType(field, &type);
        if(ret == MAMA_STATUS_OK)
        {
            mamaFieldCacheField cacheField = NULL;
            ret = mamaFieldCache_find(fieldCache, id, NULL, &cacheField);
            if(ret == MAMA_STATUS_OK)
            {
                printField(cacheField);
            }
        }
    }

    return ret;
}
*/

static void MAMACALLTYPE
verifyTimerCallback(mamaTimer timer, void *closure)
{   
    /* Cast the closure to a data structure. */
    PVerificationData verificationData = (PVerificationData)closure;

    /* We verify the cache by creating a snapshot subscription, when the callback
     * is received the contents of the snapshot will be compared against the cache.
     */
    mamaSubscription snapShotSubscription = NULL;
    mama_status error = mamaSubscription_allocate(&snapShotSubscription);
    if(error == MAMA_STATUS_OK)
    {
        /* Re-use all the existing callback functions. */
        mamaMsgCallbacks callbacks;
        memset(&callbacks, 0, sizeof(callbacks));
        callbacks.onCreate          = subscriptionOnCreate;
        callbacks.onError           = subscriptionOnError;
        callbacks.onMsg             = subscriptionOnMsg;
        callbacks.onQuality         = subscriptionOnQuality;    
        callbacks.onGap             = NULL;
        callbacks.onRecapRequest    = NULL;
        
        /* Set properties. */
        mamaSubscription_setTimeout(snapShotSubscription, 10.0);
        mamaSubscription_setRetries(snapShotSubscription, 3);
        mamaSubscription_setRequiresInitial(snapShotSubscription, 1);
        
        /* Create the subscription, note that it will be deleted whenever it expires. */
        mamaSubscription_createSnapshot(snapShotSubscription, verificationData->m_queue, &callbacks,
                    gSubscriptionSource, verificationData->m_symbol, &verificationData->mIndex);
    }
}
