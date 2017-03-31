#ifndef __IBBS_2
#define __IBBS_2

#define FILEEXT "GAL"
#define VERSION "00006"

#ifdef WIN32
#define _MSC_VER 1
#endif // WIN32

typedef enum
   {
   eSuccess,
   eForwarded,
   eNoMoreMessages,
   eGeneralFailure,
   eBadParameter,
   eNoMemory,
   eMissingDir,
   eFileOpenError
   } tIBResult;

#ifndef tBool
typedef int tBool;
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define SYSTEM_NAME_CHARS 39

#ifdef _MSC_VER
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

typedef struct {
    uint32_t nodeNumber;
    char filebox[PATH_MAX + 1];
    char name[SYSTEM_NAME_CHARS + 1];
} tOtherNode;

typedef struct {
    uint32_t league;
    char defaultFilebox[PATH_MAX + 1];
    tOtherNode *myNode;
    tOtherNode **otherNodes;
    int otherNodeCount;
} tIBInfo;

tIBResult IBSend(tIBInfo *pInfo, int pszDestNode, void *pBuffer, uint32_t nBufferSize);
tIBResult IBSendAll(tIBInfo *pInfo, void *pBuffer, uint32_t nBufferSize);
tIBResult IBGet(tIBInfo *pInfo, void *pBuffer, uint32_t nMaxBufferSize);
tIBResult IBReadConfig(tIBInfo *pInfo, char *pszConfigFile);

#endif
