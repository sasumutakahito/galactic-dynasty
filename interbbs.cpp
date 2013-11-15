#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <dos.h>
#include <fcntl.h>
#include <sys\stat.h>
#include <Windows.h>

#include "interbbs.h"
#include "interbbs_jam.h"

char aszShortMonthName[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

tBool DirExists(const char *pszDirName)
   {
    if( _access( pszDirName, 0 ) == 0 ){

        struct stat status;
        stat( pszDirName, &status );

        return (status.st_mode & S_IFDIR) != 0;
    }
    return false;
   }


void MakeFilename(const char *pszPath, const char *pszFilename, char *pszOut)
   {
   /* Validate parameters in debug mode */
   assert(pszPath != NULL);
   assert(pszFilename != NULL);
   assert(pszOut != NULL);
   assert(pszPath != pszOut);
   assert(pszFilename != pszOut);

   /* Copy path to output filename */
   strcpy(pszOut, pszPath);

   /* Ensure there is a trailing backslash */
   if(pszOut[strlen(pszOut) - 1] != '\\')
      {
      strcat(pszOut, "\\");
      }

   /* Append base filename */
   strcat(pszOut, pszFilename);
   }


tIBResult IBSendAll(tIBInfo *pInfo, void *pBuffer, int nBufferSize)
   {
   tIBResult ToReturn;
   int iCurrentSystem;

   if(pBuffer == NULL) return(eBadParameter);

   /* Validate information structure */
   ToReturn = ValidateInfoStruct(pInfo);
   if(ToReturn != eSuccess) return(ToReturn);

   if(pInfo->paOtherSystem == NULL && pInfo->nTotalSystems != 0)
      {
      return(eBadParameter);
      }

   /* Loop for each system in other systems array */
   for(iCurrentSystem = 0; iCurrentSystem < pInfo->nTotalSystems;
    ++iCurrentSystem)
      {
      /* Send information to that system */
      ToReturn = IBSend(pInfo, pInfo->paOtherSystem[iCurrentSystem].szAddress,
                        pBuffer, nBufferSize);
      if(ToReturn != eSuccess) return(ToReturn);
      }

   return(eSuccess);
   }


tIBResult IBSend(tIBInfo *pInfo, char *pszDestNode, void *pBuffer,
                 int nBufferSize)
   {
   tIBResult ToReturn;
   tMessageHeader MessageHeader;
   time_t lnSecondsSince1970;
   struct tm *pTimeInfo;
   char szTOPT[13];
   char szFMPT[13];
   char szINTL[43];
   char szMSGID[42];
   int nKludgeSize;
   int nTextSize;
   char *pszMessageText;
   tFidoNode DestNode;
   tFidoNode OrigNode;
   s_JamSubPacket *subPacket;
   s_JamSubfield subField;
   s_JamMsgHeader jamHeader;
   s_JamBase *jamMsgPtr;
   int rc;

   if(pszDestNode == NULL) return(eBadParameter);
   if(pBuffer == NULL) return(eBadParameter);

   /* Validate information structure */
   ToReturn = ValidateInfoStruct(pInfo);
   if(ToReturn != eSuccess) return(ToReturn);
   if (pInfo->tNetmailType == 0) {

		/* Get destination node address from string */
		ConvertStringToAddress(&DestNode, pszDestNode);

		/* Get origin address from string */
		ConvertStringToAddress(&OrigNode, pInfo->szThisNodeAddress);

		/* Construct message header */
		/* Construct to, from and subject information */
		strcpy(MessageHeader.szFromUserName, pInfo->szProgName);
		strcpy(MessageHeader.szToUserName, pInfo->szProgName);
		strcpy(MessageHeader.szSubject, MESSAGE_SUBJECT);

		/* Construct date and time information */
		lnSecondsSince1970 = time(NULL);
		pTimeInfo = localtime(&lnSecondsSince1970);
		sprintf(MessageHeader.szDateTime, "%02.2d %s %02.2d  %02.2d:%02.2d:%02.2d",
           pTimeInfo->tm_mday,
           aszShortMonthName[pTimeInfo->tm_mon],
           pTimeInfo->tm_year,
           pTimeInfo->tm_hour,
           pTimeInfo->tm_min,
           pTimeInfo->tm_sec);

		/* Construct misc. information */
		MessageHeader.wTimesRead = 0;
		MessageHeader.wCost = 0;
		MessageHeader.wReplyTo = 0;
		MessageHeader.wNextReply = 0;

		/* Construct destination address */
		MessageHeader.wDestZone = DestNode.wZone;
		MessageHeader.wDestNet = DestNode.wNet;
		MessageHeader.wDestNode = DestNode.wNode;
		MessageHeader.wDestPoint = DestNode.wPoint;

		/* Construct origin address */
		MessageHeader.wOrigZone = OrigNode.wZone;
		MessageHeader.wOrigNet = OrigNode.wNet;
		MessageHeader.wOrigNode = OrigNode.wNode;
		MessageHeader.wOrigPoint = OrigNode.wPoint;

		/* Construct message attributes */
		MessageHeader.wAttribute = ATTRIB_PRIVATE | ATTRIB_LOCAL;
		if(pInfo->bCrash) MessageHeader.wAttribute |= ATTRIB_CRASH;
		if(pInfo->bHold) MessageHeader.wAttribute |= ATTRIB_HOLD;
		if(pInfo->bEraseOnSend) MessageHeader.wAttribute |= ATTRIB_KILL_SENT;

		/* Create message control (kludge) lines */
		/* Create TOPT kludge line if destination point is non-zero */
		if(DestNode.wPoint != 0)
		{
			sprintf(szTOPT, "\1TOPT %u\r", DestNode.wPoint);
		}
		else
		{
			strcpy(szTOPT, "");
		}

		/* Create FMPT kludge line if origin point is non-zero */
		if(OrigNode.wPoint != 0)
		{
			sprintf(szFMPT, "\1FMPT %u\r", OrigNode.wPoint);
		}
		else
		{
			strcpy(szFMPT, "");
		}

		/* Create INTL kludge line if origin and destination zone addresses differ */
		if(DestNode.wZone != OrigNode.wZone)
		{
			sprintf(szINTL, "\1INTL %u:%u/%u %u:%u/%u\r",
              DestNode.wZone,
              DestNode.wNet,
              DestNode.wNode,
              OrigNode.wZone,
              OrigNode.wNet,
              OrigNode.wNode);
		}
		else
		{
			strcpy(szINTL, "");
		}

		/* Create MSGID kludge line, including point if non-zero */
		if(OrigNode.wPoint != 0)
		{
			sprintf(szMSGID, "\1MSGID: %u:%u/%u.%u %lx\r",
              OrigNode.wZone,
              OrigNode.wNet,
              OrigNode.wNode,
              OrigNode.wPoint,
              GetNextMSGID());
		}
		else
		{
			sprintf(szMSGID, "\1MSGID: %u:%u/%u %lx\r",
				OrigNode.wZone,
				OrigNode.wNet,
				OrigNode.wNode,
				GetNextMSGID());
		}

		/* Determine total size of kludge lines */
		nKludgeSize = strlen(szTOPT)
                + strlen(szFMPT)
                + strlen(szINTL)
                + strlen(szMSGID)
                + strlen(MESSAGE_PID);

		/* Determine total size of message text */
		nTextSize = GetMaximumEncodedLength(nBufferSize)
              + strlen(MESSAGE_HEADER)
              + nKludgeSize
              + strlen(MESSAGE_FOOTER)
              + 1;

		/* Attempt to allocate space for message text */
		if((pszMessageText = (char *)malloc(nTextSize)) == NULL)
		{
			return(eNoMemory);
		}

		/* Construct message text */
		strcpy(pszMessageText, szTOPT);
		strcat(pszMessageText, szFMPT);
		strcat(pszMessageText, szINTL);
		strcat(pszMessageText, szMSGID);
		strcat(pszMessageText, MESSAGE_PID);
		strcat(pszMessageText, MESSAGE_HEADER);
		EncodeBuffer(pszMessageText + strlen(pszMessageText), pBuffer, nBufferSize);
		strcat(pszMessageText, MESSAGE_FOOTER);

		/* Attempt to send the message */
		if(CreateMessage(pInfo->szNetmailDir, &MessageHeader, pszMessageText))
		{
			ToReturn = eSuccess;
		}
		else
		{
			ToReturn = eGeneralFailure;
		}

		/* Deallocate message text buffer */
		free(pszMessageText);

		/* Return appropriate value */
		return(ToReturn);
	} else if (pInfo->tNetmailType == 1) {
		
		JAM_ClearMsgHeader(&jamHeader);
		jamHeader.DateWritten = time(NULL);
		subPacket = JAM_NewSubPacket();
		if (!subPacket) {
			return eGeneralFailure;
		}
		subField.LoID = JAMSFLD_OADDRESS;
		subField.HiID = 0;
		subField.DatLen = strlen(pInfo->szThisNodeAddress);
		subField.Buffer = (uchar *)pInfo->szThisNodeAddress;

		JAM_PutSubfield(subPacket, &subField);

		subField.LoID = JAMSFLD_DADDRESS;
		subField.HiID = 0;
		subField.DatLen = strlen(pszDestNode);
		subField.Buffer = (uchar *)pszDestNode;

		JAM_PutSubfield(subPacket, &subField);

		subField.LoID = JAMSFLD_SENDERNAME;
		subField.HiID = 0;
		subField.DatLen = strlen(pInfo->szProgName);
		subField.Buffer = (uchar *)pInfo->szProgName;

		JAM_PutSubfield(subPacket, &subField);

		subField.LoID = JAMSFLD_RECVRNAME;
		subField.HiID = 0;
		subField.DatLen = strlen(pInfo->szProgName);
		subField.Buffer = (uchar *)pInfo->szProgName;

		JAM_PutSubfield(subPacket, &subField);

		subField.LoID = JAMSFLD_SUBJECT;
		subField.HiID = 0;
		subField.DatLen = strlen(MESSAGE_SUBJECT);
		subField.Buffer = (uchar *)MESSAGE_SUBJECT;

		JAM_PutSubfield(subPacket, &subField);

		subField.LoID = JAMSFLD_PID;
		subField.HiID = 0;
		subField.DatLen = strlen(JAM_MSG_PID);
		subField.Buffer = (uchar *)JAM_MSG_PID;

		JAM_PutSubfield(subPacket, &subField);
		ConvertStringToAddress(&OrigNode, pInfo->szThisNodeAddress);
		if(OrigNode.wPoint != 0)
		{
			sprintf(szMSGID, "%u:%u/%u.%u %lx",
              OrigNode.wZone,
              OrigNode.wNet,
              OrigNode.wNode,
              OrigNode.wPoint,
              GetNextMSGID());
		}
		else
		{
			sprintf(szMSGID, "%u:%u/%u %lx",
				OrigNode.wZone,
				OrigNode.wNet,
				OrigNode.wNode,
				GetNextMSGID());
		}

		subField.LoID = JAMSFLD_MSGID;
		subField.HiID = 0;
		subField.DatLen = strlen(szMSGID);
		subField.Buffer = (uchar *)szMSGID;

		JAM_PutSubfield(subPacket, &subField);
		/* Determine total size of message text */
		nTextSize = GetMaximumEncodedLength(nBufferSize)
              + strlen(MESSAGE_HEADER)
              + strlen(MESSAGE_FOOTER)
              + 1;

		/* Attempt to allocate space for message text */
		if((pszMessageText = (char *)malloc(nTextSize)) == NULL)
		{
			JAM_DelSubPacket(subPacket);
			return(eNoMemory);
		}

		strcpy(pszMessageText, MESSAGE_HEADER);
		EncodeBuffer(pszMessageText + strlen(pszMessageText), pBuffer, nBufferSize);
		strcat(pszMessageText, MESSAGE_FOOTER);
		
		rc = JAM_OpenMB((uchar *)pInfo->szNetmailDir, &jamMsgPtr);

		if (rc != 0) {
		   if (jamMsgPtr != NULL) {
			   free(jamMsgPtr);
		   }
		   if (rc == JAM_IO_ERROR) {
			   if (JAM_CreateMB((uchar *)pInfo->szNetmailDir, 1,  &jamMsgPtr) != 0) {
					if (jamMsgPtr != NULL) {
						free(jamMsgPtr);
					}
			   }
		   } else {
			   return eGeneralFailure;
		   }
		}
		JAM_LockMB(jamMsgPtr, -1);
		if (JAM_AddMessage(jamMsgPtr, &jamHeader, subPacket, (uchar *)pszMessageText, strlen(pszMessageText)) != 0) {
			ToReturn = eGeneralFailure;
		} else {
			ToReturn = eSuccess;
		}
		JAM_UnlockMB(jamMsgPtr);

		JAM_CloseMB(jamMsgPtr);
		free(pszMessageText);
		return (ToReturn);
	}
}

int GetMaximumEncodedLength(int nUnEncodedLength)
   {
   int nEncodedLength;

   /* The current encoding algorithm uses two characters to represent   */
   /* each byte of data, plus 1 byte per MAX_LINE_LENGTH characters for */
   /* the carriage return character.                                    */

   nEncodedLength = nUnEncodedLength * 2;

   return(nEncodedLength + (nEncodedLength / MAX_LINE_LENGTH - 1) + 2);
   }


void EncodeBuffer(char *pszDest, const void *pBuffer, int nBufferSize)
   {
   int iSourceLocation;
   int nOutputChars = 0;
   char *pcDest = pszDest;
   const char *pcSource = (const char *)pBuffer;

   /* Loop for each byte of the source buffer */
   for(iSourceLocation = 0; iSourceLocation < nBufferSize; ++iSourceLocation)
      {
      /* First character contains bits 0 - 5, with 01 in upper two bits */
      *pcDest++ = (*pcSource & 0x3f) | 0x40;
      /* Second character contains bits 6 & 7 in positions 4 & 5. Upper */
      /* two bits are 01, and all remaining bits are 0. */
      *pcDest++ = ((*pcSource & 0xc0) >> 2) | 0x40;

      /* Output carriage return when needed */
      if((nOutputChars += 2) >= MAX_LINE_LENGTH - 1)
         {
         nOutputChars = 0;
         *pcDest++ = '\r';
         }

      /* Increment source pointer */
      ++pcSource;
      }

   /* Add one last carriage return, regardless of what has come before */
   *pcDest++ = '\r';

   /* Terminate output string */
   *pcDest++ = '\0';
   }


void DecodeBuffer(const char *pszSource, void *pDestBuffer, int nBufferSize)
   {
   const char *pcSource = pszSource;
   char *pcDest = (char *)pDestBuffer;
   int iDestLocation;
   tBool bFirstOfByte = TRUE;

   /* Search for beginning of buffer delimiter char, returning if not found */
   while(*pcSource && *pcSource != DELIMITER_CHAR) ++pcSource;
   if(!*pcSource) return;

   /* Move pointer to first char after delimiter char */
   ++pcSource;

   /* Loop until destination buffer is full, delimiter char is encountered, */
   /* or end of source buffer is encountered */
   iDestLocation = 0;
   while(iDestLocation < nBufferSize && *pcSource
    && *pcSource != DELIMITER_CHAR)
      {
      /* If this is a valid data character */
      if(*pcSource >= 0x40 && *pcSource <= 0x7f)
         {
         /* If this is first character of byte */
         if(bFirstOfByte)
            {
            *pcDest = *pcSource & 0x3f;

            /* Toggle bFirstOfByte */
            bFirstOfByte = FALSE;
            }
         else /* if(!bFirstOfByte) */
            {
            *pcDest |= (*pcSource & 0x30) << 2;

            /* Increment destination */
            ++iDestLocation;
            ++pcDest;

            /* Toggle bFirstOfByte */
            bFirstOfByte = TRUE;
            }
         }

      /* Increment source byte pointer */
      ++pcSource;
      }
   }


DWORD GetNextMSGID(void)
   {
   /* MSGID should be unique for every message, for as long as possible.   */
   /* This technique adds the current time, in seconds since midnight on   */
   /* January 1st, 1970 to a psuedo-random number. The random generator    */
   /* is not seeded, as the application may have already seeded it for its */
   /* own purposes. Even if not seeded, the inclusion of the current time  */
   /* will cause the MSGID to almost always be different.                  */
   return((DWORD)time(NULL) + (DWORD)rand());
   }


tBool CreateMessage(char *pszMessageDir, tMessageHeader *pHeader,
                    char *pszText)
   {
   DWORD lwNewMsgNum;

   /* Get new message number */
   lwNewMsgNum = GetFirstUnusedMsgNum(pszMessageDir);

   /* Use WriteMessage() to create new message */
   return(WriteMessage(pszMessageDir, lwNewMsgNum, pHeader, pszText));
   }


void GetMessageFilename(char *pszMessageDir, DWORD lwMessageNum,
                        char *pszOut)
   {
   char szFileName[FILENAME_CHARS + 1];

   sprintf(szFileName, "%ld.msg", lwMessageNum);
   MakeFilename(pszMessageDir, szFileName, pszOut);
   }


tBool WriteMessage(char *pszMessageDir, DWORD lwMessageNum,
                   tMessageHeader *pHeader, char *pszText)
   {
   char szFileName[PATH_CHARS + FILENAME_CHARS + 2];
   int hFile;
   size_t nTextSize;

   /* Get fully qualified filename of message to write */
   GetMessageFilename(pszMessageDir, lwMessageNum, szFileName);

   /* Open message file */
   hFile = open(szFileName, O_WRONLY|O_BINARY|O_CREAT|OF_SHARE_EXCLUSIVE,
                S_IREAD|S_IWRITE);

   /* If open failed, return FALSE */
   if(hFile == -1) return(FALSE);

   /* Attempt to write header */
   if(write(hFile, pHeader, sizeof(tMessageHeader)) != sizeof(tMessageHeader))
      {
      /* On failure, close file, erase file, and return FALSE */
      close(hFile);
      unlink(szFileName);
      return(FALSE);
      }

   /* Determine size of message text, including string terminator */
   nTextSize = strlen(pszText) + 1;

   /* Attempt to write message text */
   if(write(hFile, pszText, nTextSize) != nTextSize)
      {
      /* On failure, close file, erase file, and return FALSE */
      close(hFile);
      unlink(szFileName);
      return(FALSE);
      }

   /* Close message file */
   close(hFile);

   /* Return with success */
   return(TRUE);
   }


tBool ReadMessage(char *pszMessageDir, DWORD lwMessageNum,
                  tMessageHeader *pHeader, char **ppszText)
   {
   char szFileName[PATH_CHARS + FILENAME_CHARS + 2];
   int hFile;
   size_t nTextSize;

   /* Get fully qualified filename of message to read */
   GetMessageFilename(pszMessageDir, lwMessageNum, szFileName);

   /* Open message file */
   hFile = open(szFileName, O_RDONLY|O_BINARY|OF_SHARE_DENY_WRITE);

   /* If open failed, return FALSE */
   if(hFile == -1) return(FALSE);

   /* Determine size of message body */
   nTextSize = (size_t)filelength(hFile) - sizeof(tMessageHeader);

   /* Attempt to allocate space for message body, plus character for added */
   /* string terminator.                                                   */
   if((*ppszText = (char *)malloc(nTextSize + 1)) == NULL)
      {
      /* On failure, close file and return FALSE */
      close(hFile);
      return(FALSE);
      }

   /* Attempt to read header */
   if(read(hFile, pHeader, sizeof(tMessageHeader)) != sizeof(tMessageHeader))
      {
      /* On failure, close file, deallocate message buffer and return FALSE */
      close(hFile);
      free(*ppszText);
      return(FALSE);
      }

   /* Attempt to read message text */
   if(read(hFile, *ppszText, nTextSize) != nTextSize)
      {
      /* On failure, close file, deallocate message buffer and return FALSE */
      close(hFile);
      free(*ppszText);
      return(FALSE);
      }

   /* Ensure that message buffer is NULL-terminated */
   (*ppszText)[nTextSize] = '\0';

   /* Close message file */
   close(hFile);

   /* Return with success */
   return(TRUE);
   }


DWORD GetFirstUnusedMsgNum(char *pszMessageDir)
   {
   DWORD lwHighestMsgNum = 0;
   DWORD lwCurrentMsgNum;
   char szFileName[PATH_CHARS + FILENAME_CHARS + 2];
   int i;

   MakeFilename(pszMessageDir, "*.msg", szFileName);

   WIN32_FIND_DATA fd;
   HANDLE h = FindFirstFile((const char*)szFileName, &fd);

   while (1) {
	   i = atoi(fd.cFileName);   
	   if (i > lwHighestMsgNum) {
		   lwHighestMsgNum = i;
	   }
	   if(FindNextFile(h, &fd) == FALSE)
            break;
   }

   return(lwHighestMsgNum + 1);
   }


tIBResult ValidateInfoStruct(tIBInfo *pInfo)
   {
   if(pInfo == NULL) return(eBadParameter);

   if (pInfo->tNetmailType == 0) {
	   if(!DirExists(pInfo->szNetmailDir)) return(eMissingDir);
   }
   if(strlen(pInfo->szProgName) == 0) return(eBadParameter);

   return(eSuccess);
   }


tIBResult IBGet(tIBInfo *pInfo, void *pBuffer, int nMaxBufferSize)
{
	tIBResult ToReturn;
	DWORD lwCurrentMsgNum;
	tMessageHeader MessageHeader;
	char szFileName[PATH_CHARS + FILENAME_CHARS + 2];
	char *pszText;
	tFidoNode ThisNode;
	s_JamBase *jamMsgPtr;
	ulong Crc_I;
	ulong Msg_I;
	s_JamMsgHeader msgHeader;
	s_JamSubPacket *subPack;
	s_JamSubfield *subField;
	int i;
	int forUs;
	char whosItFor[NODE_ADDRESS_CHARS + 1];
	/* Validate information structure */
	ToReturn = ValidateInfoStruct(pInfo);
	if(ToReturn != eSuccess) return(ToReturn);

	/* Get this node's address from string */
	ConvertStringToAddress(&ThisNode, pInfo->szThisNodeAddress);

	if (pInfo->tNetmailType == 0) {

		MakeFilename(pInfo->szNetmailDir, "*.msg", szFileName);

		/* Seach through each message file in the netmail directory, in no */
		/* particular order.*/

		WIN32_FIND_DATA fd;
		HANDLE h = FindFirstFile((const char*)szFileName, &fd);

		if(h != INVALID_HANDLE_VALUE)
		{
	
			do
			{
				lwCurrentMsgNum = atol(fd.cFileName);

				/* If able to read message */
				if(ReadMessage(pInfo->szNetmailDir, lwCurrentMsgNum, &MessageHeader,
					&pszText))
				{
					
					/* If message is for us, and hasn't be read yet */
					if(strcmp(MessageHeader.szToUserName, pInfo->szProgName) == 0
						&& ThisNode.wZone == MessageHeader.wDestZone
		               && ThisNode.wNet == MessageHeader.wDestNet
						&& ThisNode.wNode == MessageHeader.wDestNode
				       && ThisNode.wPoint == MessageHeader.wDestPoint
						&& !(MessageHeader.wAttribute & ATTRIB_RECEIVED))
					{
						 /* Decode message text, placing information in buffer */
			   
						DecodeBuffer(pszText, pBuffer, nMaxBufferSize);
			   
						 /* If received messages should be deleted */
						if(pInfo->bEraseOnReceive)
						{
							/* Determine filename of message to erase */
							GetMessageFilename(pInfo->szNetmailDir, lwCurrentMsgNum,
										szFileName);

							/* Attempt to erase file */
							if(unlink(szFileName) == -1)
							{
								ToReturn = eGeneralFailure;
							}
							else
							{
								ToReturn = eSuccess;
							}
						}

						/* If received messages should not be deleted */
						else /* if(!pInfo->bEraseOnReceive) */
						{
							/* Mark message as read */
							MessageHeader.wAttribute |= ATTRIB_RECEIVED;
							++MessageHeader.wTimesRead;

		                  /* Attempt to rewrite message */
				          if(!WriteMessage(pInfo->szNetmailDir, lwCurrentMsgNum,
						   &MessageHeader, pszText))
							{
							ToReturn = eGeneralFailure;
							}
							else
							{
							ToReturn = eSuccess;
							}
						}

		               /* Deallocate message text buffer */
				free(pszText);

				/* Return appropriate value */
               return(ToReturn);
               }
            free(pszText);
            }
         } while(FindNextFile(h, &fd) != FALSE);
      }

   /* If no new messages were found */
		return(eNoMoreMessages);
   } else if (pInfo->tNetmailType == 1) {
		// Jam message type
	   if (JAM_OpenMB((uchar *)pInfo->szNetmailDir, &jamMsgPtr) != 0) {
		   if (jamMsgPtr != NULL) {
			   free(jamMsgPtr);
		   }
		   return eGeneralFailure;
	   }
		/* Seach through each message file in the netmail directory, in no */
	   /* particular order.*/
	   Crc_I = JAM_Crc32((uchar *)pInfo->szProgName, strlen(pInfo->szProgName));
	   i = 0;
	   while(JAM_FindUser(jamMsgPtr, Crc_I, i, &Msg_I) == 0) {
			/* If able to read message */

		   forUs = 0;
		   if (JAM_ReadMsgHeader(jamMsgPtr, Msg_I, &msgHeader, &subPack) == 0) {
			   for (subField = JAM_GetSubfield(subPack); subField; subField = JAM_GetSubfield(NULL)) {
				   if (subField->LoID == JAMSFLD_DADDRESS) {
					   if (subField->DatLen <= NODE_ADDRESS_CHARS) {
						   memcpy(whosItFor, subField->Buffer, subField->DatLen);
						   whosItFor[subField->DatLen] = '\0';
						   if (strcmp(whosItFor, pInfo->szThisNodeAddress) == 0) {
							   forUs = 1;
						   }
					   }
					   break;
				   }
			   }

			   char str[256];

			   if (msgHeader.DateReceived == 0 && forUs == 1) {
				   
					/* Decode message text, placing information in buffer */
				   pszText = (char *)malloc(msgHeader.TxtLen + 1);
				   if (pszText == NULL) {
					   JAM_CloseMB(jamMsgPtr);
					   printf("OOM");
					   exit(-1);
				   }
				   if (JAM_ReadMsgText(jamMsgPtr, msgHeader.TxtOffset, msgHeader.TxtLen, (uchar *)pszText) == 0) {
					   pszText[msgHeader.TxtLen] = '\0';
					   DecodeBuffer(pszText, pBuffer, nMaxBufferSize);
						/* If received messages should be deleted */
					   	if(pInfo->bEraseOnReceive)
						{
							// Lock message base
							JAM_LockMB(jamMsgPtr, -1);

							JAM_DeleteMessage(jamMsgPtr, Msg_I);

							// Unlock message base
							JAM_UnlockMB(jamMsgPtr);
						} else {
					   /* If received messages should not be deleted */
							msgHeader.DateReceived = time(NULL);
							JAM_LockMB(jamMsgPtr, -1);
							JAM_ChangeMsgHeader(jamMsgPtr, Msg_I, &msgHeader);
							JAM_UnlockMB(jamMsgPtr);
						}
						/* Deallocate message text buffer */
					   free(pszText);
						/* Return appropriate value */
					   JAM_CloseMB(jamMsgPtr);
					   return(eSuccess);
				   }
			   }
		   }
		   i++;
	   }
	   JAM_CloseMB(jamMsgPtr);
	   return(eNoMoreMessages);
	}
	return(eNoMoreMessages);
}
   

void ConvertAddressToString(char *pszDest, const tFidoNode *pNode)
   {
   if(pNode->wPoint == 0)
      {
      sprintf(pszDest, "%u:%u/%u", pNode->wZone, pNode->wNet, pNode->wNode);
      }
   else /* if(pNode->wPoint !=0) */
      {
      sprintf(pszDest, "%u:%u/%u.%u", pNode->wZone, pNode->wNet, pNode->wNode,
              pNode->wPoint);
      }
   }


void ConvertStringToAddress(tFidoNode *pNode, const char *pszSource)
   {
   int i;
   pNode->wZone = 0;
   pNode->wNet = 0;
   pNode->wNode = 0;
   pNode->wPoint = 0;
   i=0;
   while (pszSource[i] != ':' && pszSource[i] != '\0') {
	   pNode->wZone = pNode->wZone * 10 + (pszSource[i] - '0');
	   i++;
   }
   i++;
   while (pszSource[i] != '/' && pszSource[i] != '\0') {
	   pNode->wNet = pNode->wNet * 10 + (pszSource[i] - '0');
	   i++;
   }
   i++;
   while (pszSource[i] != '.' && pszSource[i] != '\0') {
	   pNode->wNode = pNode->wNode * 10 + (pszSource[i] - '0');
	   i++;
   }
   i++;
   while (pszSource[i] != '\0') {
	   pNode->wPoint = pNode->wPoint * 10 + (pszSource[i] - '0');
	   i++;
   }
}

#define NUM_KEYWORDS       11

#define KEYWORD_ADDRESS    0
#define KEYWORD_USER_NAME  1
#define KEYWORD_MAIL_DIR   2
#define KEYWORD_CRASH      3
#define KEYWORD_HOLD       4
#define KEYWORD_KILL_SENT  5
#define KEYWORD_KILL_RCVD  6
#define KEYWORD_LINK_WITH  7
#define KEYWORD_LINK_NAME  8
#define KEYWORD_LINK_LOC   9
#define KEYWORD_NETMAIL_TYPE 10

char *apszKeyWord[NUM_KEYWORDS] = {"SystemAddress",
                                   "UserName",
                                   "NetmailDir",
                                   "Crash",
                                   "Hold",
                                   "EraseOnSend",
                                   "EraseOnReceive",
                                   "LinkWith",
                                   "LinkName",
                                   "LinkLocation",
								   "NetmailAreaType"};

tIBResult IBReadConfig(tIBInfo *pInfo, char *pszConfigFile)
   {
   /* Set default values for pInfo settings */
   pInfo->nTotalSystems = 0;
   pInfo->paOtherSystem = NULL;

   /* Process configuration file */
   if(!ProcessConfigFile(pszConfigFile, NUM_KEYWORDS, apszKeyWord,
                         ProcessConfigLine, (void *)pInfo))
      {
      return(eFileOpenError);
      }

   /* else */
   return(eSuccess);
   }


void ProcessConfigLine(int nKeyword, char *pszParameter, void *pCallbackData)
   {
   tIBInfo *pInfo = (tIBInfo *)pCallbackData;
   tOtherNode *paNewNodeArray;

   switch(nKeyword)
      {
      case KEYWORD_ADDRESS:
         strncpy(pInfo->szThisNodeAddress, pszParameter, NODE_ADDRESS_CHARS);
         pInfo->szThisNodeAddress[NODE_ADDRESS_CHARS] = '\0';
         break;

      case KEYWORD_USER_NAME:
         strncpy(pInfo->szProgName, pszParameter, PROG_NAME_CHARS);
         pInfo->szProgName[PROG_NAME_CHARS] = '\0';
         break;

      case KEYWORD_MAIL_DIR:
         strncpy(pInfo->szNetmailDir, pszParameter, PATH_CHARS);
         pInfo->szNetmailDir[PATH_CHARS] = '\0';
         break;

      case KEYWORD_CRASH:
         if(stricmp(pszParameter, "Yes") == 0)
            {
            pInfo->bCrash = TRUE;
            }
         else if(stricmp(pszParameter, "No") == 0)
            {
            pInfo->bCrash = FALSE;
            }
         break;

      case KEYWORD_HOLD:
         if(stricmp(pszParameter, "Yes") == 0)
            {
            pInfo->bHold = TRUE;
            }
         else if(stricmp(pszParameter, "No") == 0)
            {
            pInfo->bHold = FALSE;
            }
         break;

      case KEYWORD_KILL_SENT:
         if(stricmp(pszParameter, "Yes") == 0)
            {
            pInfo->bEraseOnSend = TRUE;
            }
         else if(stricmp(pszParameter, "No") == 0)
            {
            pInfo->bEraseOnSend = FALSE;
            }
         break;

      case KEYWORD_KILL_RCVD:
         if(stricmp(pszParameter, "Yes") == 0)
            {
            pInfo->bEraseOnReceive = TRUE;
            }
         else if(stricmp(pszParameter, "No") == 0)
            {
            pInfo->bEraseOnReceive = FALSE;
            }
         break;
	  case KEYWORD_NETMAIL_TYPE:
		  if(stricmp(pszParameter, "JAM") == 0)
		  {
			  pInfo->tNetmailType = 1;
		  }
		  else if(stricmp(pszParameter, "MSG") == 0)
		  {
			  pInfo->tNetmailType = 0;
		  }
		  break;
      case KEYWORD_LINK_WITH:
         if(pInfo->nTotalSystems == 0)
            {
            pInfo->paOtherSystem = (tOtherNode *)malloc(sizeof(tOtherNode));
            if(pInfo->paOtherSystem == NULL)
               {
               break;
               }
            }
         else
            {
            if((paNewNodeArray = (tOtherNode *)malloc(sizeof(tOtherNode) *
             (pInfo->nTotalSystems + 1))) == NULL)
               {
               break;
               }

            memcpy(paNewNodeArray, pInfo->paOtherSystem, sizeof(tOtherNode) *
               pInfo->nTotalSystems);

            free(pInfo->paOtherSystem);

            pInfo->paOtherSystem = paNewNodeArray;
            }

         strncpy(pInfo->paOtherSystem[pInfo->nTotalSystems].szAddress,
                 pszParameter, NODE_ADDRESS_CHARS);
         pInfo->paOtherSystem[pInfo->nTotalSystems].
            szAddress[NODE_ADDRESS_CHARS] = '\0';
         ++pInfo->nTotalSystems;
         break;

      case KEYWORD_LINK_NAME:
         if(pInfo->nTotalSystems != 0)
            {
            strncpy(pInfo->paOtherSystem[pInfo->nTotalSystems - 1].szSystemName,
                    pszParameter, SYSTEM_NAME_CHARS);
            pInfo->paOtherSystem[pInfo->nTotalSystems - 1].
               szSystemName[SYSTEM_NAME_CHARS] = '\0';
            }
         break;

      case KEYWORD_LINK_LOC:
         if(pInfo->nTotalSystems != 0)
            {
            strncpy(pInfo->paOtherSystem[pInfo->nTotalSystems - 1].szLocation,
                    pszParameter, LOCATION_CHARS);
            pInfo->paOtherSystem[pInfo->nTotalSystems - 1].
               szLocation[LOCATION_CHARS] = '\0';
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
         if(stricmp(szToken, papszKeyWord[iKeyWord]) == 0)
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
