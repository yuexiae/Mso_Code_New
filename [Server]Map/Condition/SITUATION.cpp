




// SITUATION.cpp: implementation of the SITUATION class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "SITUATION.h"

//////////////////////////////////////////////////////////////////////

// Construction/Destruction
//////////////////////////////////////////////////////////////////////


SITUATION::SITUATION()

{

}

SITUATION::~SITUATION()

{


}


CRETURN SITUATION::Execute(CObject * pObj)
{
	if( m_pFunc(pObj))


		return RETURN_TRUE;

	else
		return RETURN_FALSE;
}


