/*! \file TMReader.c
 * \brief A UHF tag reading appplication.
 *
 * This uses the Mercury API from ThingMagic to operate on one of their M6E readers
 * deployed within an embedded device. 
 * 
 */

#include <tm_reader.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <malloc.h>
#include <inttypes.h>
#include "Socket_Comms/socket_comms.h"
#include "ConfigUtils/configUtils.h"
#define VERSION_NUM "0.90 ALPHA"

int currentPanel[6];        /* current lot of cows on a panel                                           */
int indexPanel[6];          /* array index for current latest lot of cows on a panel                    */
int panel[6][100];          /* history of lots of cows for each panel                                   */
int currentTag = 0;         /* latest tag from the reader                                               */
int cleanCows = 0;          /* bit representation of cows that are certified clean                      */
int innocentBystanders = 0; /* bit representation of cows that comingled with comingled cows            */
int coMinglingCows = 0;     /* bit representation of cows that comingled with the infected cow          */
int panelStatus[6];         /* 0 means off, 1 means innocent bystander, 2 means comingled cow present,  */
                            /* 3 means infected cow present                                             */
int infectedCow = 128;        /* bitwise representation of the infected cow                               */
bool sumbit = false;        /* time to look for comingling cows? 1 for yes, 0 for no                    */
int readerNumber = 0;
TMR_Reader r1, r2;
TMR_Status ret;
TMR_Region region;
TMR_TransportListenerBlock tb1, tb2;
TMR_ReadListenerBlock rlb1, rlb2;
TMR_ReadExceptionListenerBlock reb1, reb2;
int readerTemp = 0;
int currentEPC = 0;
char tagList[100] = "";

void parseTags(int aggregate)
{
   strcpy(&tagList, "");
   if(aggregate & 1)
      strcat(&tagList, "6091 ");
   if(aggregate & 2)
      strcat(&tagList, "6092 ");
   if(aggregate & 4)
      strcat(&tagList, "6093 ");
   if(aggregate & 8)
      strcat(&tagList, "6094 ");
   if(aggregate & 16)
      strcat(&tagList, "6095 ");
   if(aggregate & 32)
      strcat(&tagList, "6096 ");
   if(aggregate & 64)
      strcat(&tagList, "6097 ");
   if(aggregate & 128)
      strcat(&tagList, "6098 ");
}

void initVariables()
{
   int i;
   int j;
   for(i = 0; i < 6; i++)
   {
      currentPanel[i] = 0;
      indexPanel[i] = 0;
      panelStatus[i] = 0;
      for(j = 0; j<100; j++)
      {
         panel[i][j] = 0;
      }
   }
}

/**
 * Signal handler for this application
 */
void sigHandle(int signal){
    printf("\nFreeing memory and closing gracefully...\n");
    configFreeAllocated();
    exit(0);
}
void errx(int exitval, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);

    exit(exitval);
}

void checkerr(TMR_Reader* rp, TMR_Status ret, int exitval, const char *msg)
{
    if (TMR_SUCCESS != ret)
    {
        errx(exitval, "Error %s: %s\n", msg, TMR_strerr(rp, ret));
    }
}

void serialPrinter(bool tx, uint32_t dataLen, const uint8_t data[],
                   uint32_t timeout, void *cookie)
{
    FILE *out = cookie;
    uint32_t i;

    fprintf(out, "%s", tx ? "Sending: " : "Received:");
    for (i = 0; i < dataLen; i++)
    {
        if (i > 0 && (i & 15) == 0)
            fprintf(out, "\n		   ");
        fprintf(out, " %02x", data[i]);
    }
    fprintf(out, "\n");
}

void stringPrinter(bool tx,uint32_t dataLen, const uint8_t data[],uint32_t timeout, void *cookie)
{
    FILE *out = cookie;

    fprintf(out, "%s", tx ? "Sending: " : "Received:");
    fprintf(out, "%s\n", data);
}

void aggregateCowsPerPanel(int antenna)
{
//    printf("Antenna: %d, Tag: %d\n", antenna, currentEPC);
    antenna -= 1;
    switch(currentEPC)
    {
         case(145):
            currentPanel[antenna] = (currentPanel[antenna] | 1);
            break;
         case(146):
            currentPanel[antenna] = (currentPanel[antenna] | 2);
            break;
         case(147):
            currentPanel[antenna] = (currentPanel[antenna] | 4);
            break;
         case(148):
            currentPanel[antenna] = (currentPanel[antenna] | 8);
            break;
         case(149):
            currentPanel[antenna] = (currentPanel[antenna] | 16);
            break;
         case(150):
            currentPanel[antenna] = (currentPanel[antenna] | 32);
            break;
         case(151):
            currentPanel[antenna] = (currentPanel[antenna] | 64);
            break;
         case(152):
            currentPanel[antenna] = (currentPanel[antenna] | 128);
            break;
         default:
            break;
   }
}

void updateAllPanels()
/* compare the aggregate tags to the last lot of tags on that panel */
/* if the aggregate is different, this is a new lot, update the history  */
{
int i;

   for(i = 0; i < 6; i++)
   {
      if (!indexPanel[i])
      {
          panel[i][indexPanel[i]] = currentPanel[i];
          indexPanel[i]++;
      }
      if (currentPanel[i] != panel[i][indexPanel[i]])
         {
             indexPanel[i]++;
             panel[i][indexPanel[i]] = currentPanel[i];
         }
      currentPanel[i] = 0;
   }
}

void coMingling()
/* step through the history of each panel looking for the infected cow                       */
/* uses a bitwise AND to compare the aggregate tag to the infected tag                       */
/* a non-zero result indicates the presence of the infected cow on that panel in the current */
/* aggregate being looked at                                                                 */
/* then uses a bitwise OR to aggregate the current aggregate to our running tab on cows that */
/* comingled with the infected cow                                                           */
/* lastly, remove the infected cow from the comingling cows                                  */
/* the end result is an integer representation with bits set high for all cows that          */
/* comingled with the infected cow                                                           */
/* then repeat with the comingling cows, to find who comingled with the comingling cows      */
{
   int i;
   int j;
   for(j = 0; j < 6; j++)
   {
      for(i = 0; i < indexPanel[j] ; i++)
      {
         if (infectedCow & panel[j][i])
         {
            coMinglingCows = (coMinglingCows | panel[j][i]);
         }
      }
   }
   coMinglingCows = (coMinglingCows & ~infectedCow);

   for(j = 0; j < 6; j++)
   {
      for(i = 0; i < indexPanel[j]; i++)
      {
         if (coMinglingCows & panel[j][i])
         {
            innocentBystanders = (innocentBystanders | panel[j][i]);
         }
      }
   }
   innocentBystanders = (innocentBystanders & ~coMinglingCows);
   innocentBystanders = (innocentBystanders & ~infectedCow);
   parseTags(infectedCow);
   printf("Infected Cow is:        %s\n", tagList);
//   printf("Comingling Cows aggregate is: %d", coMinglingCows);
   parseTags(coMinglingCows);
   printf("Comingling Cows is:     %s\n", tagList);
//   printf("Innocent Bystanders aggregate is: %d", innocentBystanders);
   parseTags(innocentBystanders);
   printf("Innocent ByStanders is: %s\n", tagList);
   coMinglingCows = 0;
   innocentBystanders = 0;
}
 
void callback(TMR_Reader *reader, const TMR_TagReadData *t, void *cookie)
{
    char epcStr[128];
    char uri1[256]= "GET http://";
    char uri2[256] = "\0";
    char temp[90];
    char temp2[10];
    int RSSI = 0;
    int socket = 0;
    int antenna = t->antenna;
    if (readerNumber == 2)
       antenna += 3;
    currentEPC = (int)t->tag.epc[1];
    aggregateCowsPerPanel(antenna);
    currentEPC = 0;
/*
    strcpy(temp, uri1);
    strcat(temp, "127.0.0.1");
    strcat(temp, "/alma/read?readerId=");
    socket = open_socket("127.0.0.1","80");
*/
//    TMR_bytesToHex(t->tag.epc, t->tag.epcByteCount, epcStr);
//    RSSI = t->rssi;
//    antenna = t->antenna - 1;
    //Convert the RSSI int to a string into the temp2 char buffer
//    sprintf(temp2, "%d", RSSI);
    //printf("just before digital write\n");

    //Create the string that will be sent to the socket.
    //strcpy(temp, uri1);
/*
    strcat(temp, "TableTop");
    sprintf(temp2, "%d", readerNumber);
    strcat(temp, temp2);
    //printf("After READERID assignment\n");
    strcat(temp, "&antenna=");
    sprintf(temp2, "%d", antenna);
    strcat(temp, temp2);
    //printf("After Antenna assignment\n");
    strcat(temp, "&TagNumber=");
    strcat(temp, epcStr);
    //printf("After epc assignment\n");
    strcat(temp, "\r\n");
    printf("%s\n", temp);
    // write the tag number to the socket.
    send_data(socket, temp);
    close(socket);
*/
}


void exceptionCallback(TMR_Reader *reader, TMR_Status error, void *cookie)
{
    fprintf(stdout, "Error:%s\n", TMR_strerr(reader, error));
}

void setupReaders()
{
    ret = TMR_create(&r1, "tmr:///dev/ttyACM0");
    checkerr(&r1, ret, 1, "Creating first reader.");
//    ret = TMR_create(&r2, "tmr:///dev/ttyACM1");
//    checkerr(&r2, ret, 1, "Creating second reader.");

    if (TMR_READER_TYPE_SERIAL == r1.readerType)
    {
        tb1.listener = serialPrinter;
    }
    else
    {
        tb1.listener = stringPrinter;
    }
    tb1.cookie = stdout;

/*    if (TMR_READER_TYPE_SERIAL == r2.readerType)
    {
        tb2.listener = serialPrinter;
    }
    else
    {
        tb2.listener = stringPrinter;
    }
    tb2.cookie = stdout;*/
#if 0
    TMR_addTransportListener(rp, &tb);
#endif

    ret = TMR_connect(&r1);
    checkerr(&r1, ret, 1, "Connecting to first reader.");
//    ret = TMR_connect(&r2);
//    checkerr(&r2, ret, 1, "Connecting to second reader.");
    int readPowerToSet = 2000;
    ret = TMR_paramSet(&r1, TMR_PARAM_RADIO_READPOWER, &readPowerToSet);
    checkerr(&r1, ret, 1, "Setting read power on first reader.");
//    ret = TMR_paramSet(&r2, TMR_PARAM_RADIO_READPOWER, &readPowerToSet);
//    checkerr(&r2, ret, 1, "Setting read power on second reader.");
    // Set a read plan that can be adhered to by the reader in order to ensure that
    // all antennaes get a chance to read.

    //configFreeAllocated();
    region = TMR_REGION_NONE;
    ret = TMR_paramGet(&r1, TMR_PARAM_REGION_ID, &region);
    checkerr(&r1, ret, 1, "Getting region.");

    if (TMR_REGION_NONE == region)
    {
        TMR_RegionList regions;
        TMR_Region _regionStore[32];
        regions.list = _regionStore;
        regions.max = sizeof(_regionStore)/sizeof(_regionStore[0]);
        regions.len = 0;

        ret = TMR_paramGet(&r1, TMR_PARAM_REGION_SUPPORTEDREGIONS, &regions);
        checkerr(&r1, ret, __LINE__, "Getting supported regions.");

        if (regions.len < 1)
        {
            checkerr(&r1, TMR_ERROR_INVALID_REGION, __LINE__, "Reader doesn't supportany regions.");
        }
        region = regions.list[0];
        ret = TMR_paramSet(&r1, TMR_PARAM_REGION_ID, &region);
        checkerr(&r1, ret, 1, "Setting region on first reader.");
//        ret = TMR_paramSet(&r2, TMR_PARAM_REGION_ID, &region);
//        checkerr(&r2, ret, 1, "Setting region on second reader.");
    }

    rlb1.listener = callback;
    rlb1.cookie = NULL;
    reb1.listener = exceptionCallback;
    reb1.cookie = NULL;
    ret = TMR_addReadListener(&r1, &rlb1);
    checkerr(&r1, ret, 1, "Adding first read listener.");
    ret = TMR_addReadExceptionListener(&r1, &reb1);
    checkerr(&r1, ret, 1, "Adding first exception listener.");
//    rlb2.listener = callback;
//    rlb2.cookie = NULL;
//    reb2.listener = exceptionCallback;
//    reb2.cookie = NULL;
//    ret = TMR_addReadListener(&r2, &rlb2);
//    checkerr(&r2, ret, 1, "Adding second read listener.");
//    ret = TMR_addReadExceptionListener(&r2, &reb2);
//    checkerr(&r2, ret, 1, "Adding second exception listener.");

}

void doReads()
{
     readerNumber=1;
     ret = TMR_startReading(&r1);
     checkerr(&r1, ret, 1, "Starting reading on first reader.");
     sleep(1);
     ret = TMR_stopReading(&r1);
     checkerr(&r1, ret, 1, "Stopping reading on first reader.");
//     readerNumber=2;
//     ret = TMR_startReading(&r2);
//     checkerr(&r2, ret, 1, "Starting reading on second reader.");
//     sleep(1);
//     ret = TMR_stopReading(&r2);
//     checkerr(&r2, ret, 1, "Stopping reading on second reader.");
     TMR_paramGet(&r1, TMR_PARAM_RADIO_TEMPERATURE, &readerTemp);
     printf("Temp on first reader is %d\n", readerTemp);
//     TMR_paramGet(&r2, TMR_PARAM_RADIO_TEMPERATURE, &readerTemp);
//     printf("Temp on second reader is %d\n", readerTemp);
//     sleep(1);
 
}

int main(int argc, char *argv[])
{
int i;
int count = 0;
#ifndef TMR_ENABLE_BACKGROUND_READS
    errx(1, "This sample requires background read functionality.\n"
         "Please enable TMR_ENABLE_BACKGROUND_READS in tm_config.h\n"
         "to run this codelet\n");
    return -1;
#else
    initVariables();
	//Install Signal Handler
    signal(SIGINT, sigHandle);
	if(argv[1] != NULL && strcmp(argv[1], "-v") == 0){
		printf("Version is %s\n", VERSION_NUM);
		exit(1);
	}
    setupReaders();
    while(1)
    {
       doReads();
       for(i = 0; i < 6; i++)
       {
          parseTags(currentPanel[i]);
          printf("Panel %d = %s\n", i+1, tagList);
       }
       printf("Count is %d\n", count);
       updateAllPanels();
       if(count > 10)
       {
          coMingling();
          count = 0;
       }
       else count++;
    }
    configFreeAllocated();
    TMR_destroy(&r1);
//    TMR_destroy(&r2);

#endif /* TMR_ENABLE_BACKGROUND_READS */
    return 0;
}
