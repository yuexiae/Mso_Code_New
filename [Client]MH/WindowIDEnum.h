#ifndef _WINDOWIDENUM_H_

#define _WINDOWIDENUM_H_

#ifdef WINDOW_ID
#undef WINDOW_ID
#endif

#ifdef WINDOW_ID_DEFINE
#undef WINDOW_ID_DEFINE
#endif


#define WINDOW_ID(a)	a

#define WINDOW_ID_DEFINE(a,def)	a = def

enum IDENUM
{
#include "WindowIDs.h"
};
extern int IDSEARCH(char * idName);

#endif //_WINDOWIDENUM_H_


