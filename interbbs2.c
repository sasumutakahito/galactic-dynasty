#include <sys/stat.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include "interbbs2.h"

#ifdef _MSC_VER
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif // _MSC_VER

#define NUM_KEYWORDS 3

char *apszKeyWord[NUM_KEYWORDS] = {"LinkNodeNumber",
                                   "LinkFileOutbox",
                                   "LinkName"};



tIBResult ProcessFile(tIBInfo *pInfo, char *filename, void *pBuffer, int nBufferSize) {
    char version[6];
    FILE *fptr;
    memset(version, 0, 6);
    uint32_t destination;
    uint32_t memsize;
    int forward = 0;
    tIBResult result;
    uint32_t league;

    fptr = fopen(filename, "rb");

    fread(version, 5, 1, fptr);

    fread(&league, sizeof(uint32_t),1, fptr);
    league = ntohl(league);

    if (league != pInfo->league) {
        return eBadParameter;
    }

    if (strncmp(version, VERSION, 5) != 0) {
        fprintf(stderr, "Version Mismatch, is your game up to date? (Packet: %s vs Local: %s)\n", version, VERSION);
        return eBadParameter;
    }

    fread(&destination, 1, sizeof(uint32_t), fptr);
    destination = ntohl(destination);

    if (destination != pInfo->myNode->nodeNumber) {
        forward = 1;
    }
    fread(&memsize, sizeof(uint32_t), 1, fptr);
    memsize = ntohl(memsize);

    if (nBufferSize < memsize) {
        fclose(fptr);
        return eBadParameter;
    }
    fread(pBuffer, memsize, 1, fptr);

    if (forward) {
        result = IBSend(pInfo, destination, pBuffer, memsize);

	    if (result == eSuccess) {
            fclose(fptr);
	        unlink(filename);
            return eForwarded;
        }
        fclose(fptr);
        return result;
    }

    fclose(fptr);
    unlink(filename);

    return eSuccess;
}

tIBResult IBGet(tIBInfo *pInfo, void *pBuffer, uint32_t nBufferSize) {
    DIR *dirp;
    struct dirent *dp;
    char filename[PATH_MAX];
    tIBResult result;
    dirp = opendir(pInfo->myNode->filebox);
    if (!dirp) {
        return eMissingDir;
    }

    while ((dp = readdir(dirp)) != NULL) {
        if (strncmp(&dp->d_name[strlen(dp->d_name)-3], FILEEXT, 3) == 0) {
            snprintf(filename, PATH_MAX, "%s%s%s", pInfo->myNode->filebox, PATH_SEP, dp->d_name);
            result = ProcessFile(pInfo, filename, pBuffer, nBufferSize);
            if (result == eBadParameter) {
                // skip over invalid packets
                continue;
            }
            closedir(dirp);
            return result;
        }
    }

    closedir(dirp);
    return eNoMoreMessages;
}

tIBResult IBSendAll(tIBInfo *pInfo, void *pBuffer, uint32_t nBufferSize) {
    int i;
    int result;

    for (i=0;i<pInfo->otherNodeCount;i++) {
        if (pInfo->otherNodes[i]->nodeNumber == pInfo->myNode->nodeNumber)
            continue;
        result = IBSend(pInfo, pInfo->otherNodes[i]->nodeNumber, pBuffer, nBufferSize);
        if (result != eSuccess) {
            return result;
        }
    }

    return eSuccess;
}

tIBResult IBSend(tIBInfo *pInfo, int pszDestNode, void *pBuffer, uint32_t nBufferSize) {
    struct stat s;
    int i;
    char filename[PATH_MAX];
    tOtherNode *dest = NULL;
    FILE *fptr;
    time_t now;
    struct tm *now_tm;
    int minutes;
	int oldminutes;
	int packetno = 0;
	
	fptr = fopen("ibbssync.dat", "rb");
	if (!fptr) {
		oldminutes = 0;
	} else {
		fread(&oldminutes, sizeof(int), 1, fptr);
		
		if (oldminutes == minutes) {
			fread(&packetno, sizeof(int), 1, fptr);
		}
		fclose(fptr);
	}

    for (i=0;i<pInfo->otherNodeCount;i++) {
        if (pInfo->otherNodes[i]->nodeNumber == pszDestNode) {
            dest = pInfo->otherNodes[i];
            break;
        }
    }

    if (dest == NULL) {
        return eBadParameter;
    }

    now = time(NULL);
    now_tm = localtime(&now);
    minutes = (now_tm->tm_mday + 1) * 24 * 60;
    minutes += (now_tm->tm_hour) * 60;
    minutes += now_tm->tm_min;

    if (packetno == 0x100) {
        return eBadParameter;
    }

    snprintf(filename, PATH_MAX, "%s%s%04X%02X%02X.%s", dest->filebox, PATH_SEP, minutes, pInfo->myNode->nodeNumber, packetno, FILEEXT);
    packetno++;

    fptr = fopen(filename, "wb");
    if (!fptr) {
        return eFileOpenError;
    }

    uint32_t nwNbufferSize = htonl(nBufferSize);
    uint32_t leagueno = htonl(pInfo->league);
    uint32_t destn = htonl(pszDestNode);
    fwrite(VERSION, 5, 1, fptr);
    fwrite(&leagueno, sizeof(uint32_t), 1, fptr);
    fwrite(&destn, sizeof(uint32_t), 1, fptr);
    fwrite(&nwNbufferSize, sizeof(uint32_t), 1, fptr);
    fwrite(pBuffer, nBufferSize, 1, fptr);
    fclose(fptr);

	fptr = fopen("ibbssync.dat", "wb");
	if (!fptr) {
		fprintf(stderr, "Unable to open ibbssync.dat");
	} else {
		fwrite(&minutes, sizeof(int), 1, fptr);
		fwrite(&packetno, sizeof(int), 1, fptr);
	}
    return eSuccess;
}



void ProcessConfigLine(int nKeyword, char *pszParameter, void *pCallbackData)
   {
   tIBInfo *pInfo = (tIBInfo *)pCallbackData;
   tOtherNode **paNewNodeArray;

   switch(nKeyword)
      {
      case 0:
         if(pInfo->otherNodeCount == 0)
            {
            pInfo->otherNodes = (tOtherNode **)malloc(sizeof(tOtherNode *));
            if(pInfo->otherNodes == NULL)
               {
               break;
               }
            }
         else
            {
            if((paNewNodeArray = (tOtherNode **)realloc(pInfo->otherNodes, sizeof(tOtherNode *) * (pInfo->otherNodeCount + 1))) == NULL)
               {
               break;
               } else {
                   pInfo->otherNodes = paNewNodeArray;
               }
            }
         pInfo->otherNodes[pInfo->otherNodeCount] = (tOtherNode *)malloc(sizeof(tOtherNode));
         if (!pInfo->otherNodes[pInfo->otherNodeCount]) {
             break;
         }
         
         pInfo->otherNodes[pInfo->otherNodeCount]->nodeNumber = atoi(pszParameter);

         strncpy(pInfo->otherNodes[pInfo->otherNodeCount]->filebox, pInfo->defaultFilebox, PATH_MAX);
         pInfo->otherNodes[pInfo->otherNodeCount]->filebox[PATH_MAX] = '\0';


         ++pInfo->otherNodeCount;
         break;

      case 1:
         if(pInfo->otherNodeCount != 0)
            {
            strncpy(pInfo->otherNodes[pInfo->otherNodeCount - 1]->filebox, pszParameter, PATH_MAX);
            pInfo->otherNodes[pInfo->otherNodeCount - 1]->filebox[PATH_MAX] = '\0';
            }
         break;
      case 2:
         if(pInfo->otherNodeCount != 0)
            {
            strncpy(pInfo->otherNodes[pInfo->otherNodeCount - 1]->name, pszParameter, SYSTEM_NAME_CHARS);
            pInfo->otherNodes[pInfo->otherNodeCount - 1]->name[SYSTEM_NAME_CHARS] = '\0';
            
            }
         break;        
      }
   }


/* Configuration file reader settings */
#define CONFIG_LINE_SIZE 128
#define MAX_TOKEN_CHARS 32

tBool ProcessConfigFile(char *pszFileName, int nKeyWords, char **papszKeyWord,
                  void (*pfCallBack)(int, char *, void *), void *pCallBackData)
   {
   FILE *pfConfigFile;
   char szConfigLine[CONFIG_LINE_SIZE + 1];
   char *pcCurrentPos;
   unsigned int uCount;
   char szToken[MAX_TOKEN_CHARS + 1];
   int iKeyWord;

   /* Attempt to open configuration file */
   if((pfConfigFile = fopen(pszFileName, "rt")) == NULL)
      {
      return(FALSE);
      }

   /* While not at end of file */
   while(!feof(pfConfigFile))
      {
      /* Get the next line */
      if(fgets(szConfigLine, CONFIG_LINE_SIZE + 1 ,pfConfigFile) == NULL) break;

      /* Ignore all of line after comments or CR/LF char */
      pcCurrentPos=(char *)szConfigLine;
      while(*pcCurrentPos)
         {
         if(*pcCurrentPos=='\n' || *pcCurrentPos=='\r' || *pcCurrentPos==';')
            {
            *pcCurrentPos='\0';
            break;
            }
         ++pcCurrentPos;
         }

      /* Search for beginning of first token on line */
      pcCurrentPos=(char *)szConfigLine;
      while(*pcCurrentPos && isspace(*pcCurrentPos)) ++pcCurrentPos;

      /* If no token was found, proceed to process the next line */
      if(!*pcCurrentPos) continue;

      /* Get first token from line */
      uCount=0;
      while(*pcCurrentPos && !isspace(*pcCurrentPos))
         {
         if(uCount<MAX_TOKEN_CHARS) szToken[uCount++]=*pcCurrentPos;
         ++pcCurrentPos;
         }
      if(uCount<=MAX_TOKEN_CHARS)
         szToken[uCount]='\0';
      else
         szToken[MAX_TOKEN_CHARS]='\0';

      /* Find beginning of configuration option parameters */
      while(*pcCurrentPos && isspace(*pcCurrentPos)) ++pcCurrentPos;

       /* Trim trailing spaces from setting string */
      for(uCount=strlen(pcCurrentPos)-1;uCount>0;--uCount)
         {
         if(isspace(pcCurrentPos[uCount]))
            {
            pcCurrentPos[uCount]='\0';
            }
         else
            {
            break;
            }
         }

      /* Loop through list of keywords */
      for(iKeyWord = 0; iKeyWord < nKeyWords; ++iKeyWord)
         {
         /* If keyword matches */
         if(strcasecmp(szToken, papszKeyWord[iKeyWord]) == 0)
            {
            /* Call keyword processing callback function */
            (*pfCallBack)(iKeyWord, pcCurrentPos, pCallBackData);
            }
         }
      }

   /* Close the configuration file */
   fclose(pfConfigFile);

   /* Return with success */
   return(TRUE);
   }

   tIBResult IBReadConfig(tIBInfo *pInfo, char *pszConfigFile)
   {
   /* Set default values for pInfo settings */
   pInfo->otherNodeCount = 0;
   pInfo->otherNodes = NULL;

   /* Process configuration file */
   if(!ProcessConfigFile(pszConfigFile, NUM_KEYWORDS, apszKeyWord,
                         ProcessConfigLine, (void *)pInfo))
      {
      return(eFileOpenError);
      }

   /* else */
   return(eSuccess);
   }
