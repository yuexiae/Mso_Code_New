// SiegeWarMgr.cpp: implementation of the CSiegeWarMgr class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "SiegeWarMgr.h"
#include "BattleSystem_Server.h"
#include "Guild.h"
#include "GuildManager.h"
#include "Network.h"
#include "Player.h"
#include "UserTable.h"
#include "Network.h"
#include "MapDBMsgParser.h"
#include "GuildUnionManager.h"
#include "ServerTable.h"
#include "PackedData.h"
#include "MHFile.h"
#include "QuestManager.h"
#include "ObjectStateManager.h"
#include "SiegeWarProfitMgr.h"
#include "MapObject.h"
#include "..\[CC]Header\GameResourceManager.h"
#include "CharMove.h"
#include "SiegeWarGiveItem.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

#define SIEGEWAR_FIGHTTIME	120

CSiegeWarMgr::CSiegeWarMgr()
{	
	m_DefenceProposalList.Initialize( 50 );
	m_DefenceAcceptList.Initialize( 50 );
	m_AttackGuildList.Initialize( 50 );
	m_AttackUnionList.Initialize( 20 );

	m_SiegeWarIdx		= 0;
	m_pBattle			= NULL;
	m_CastleUnionIdx	= 0;
	m_CastleGuildIdx	= 0;
	
	memset( m_SiegeWarMapNum, 0, sizeof(DWORD)*SIEGEWAR_MAX_SIEGEMAP*SIEGEWAR_MAX_AFFECTED_MAP );
	m_SiegeMapCount		= 0;

	m_TaxRate			= 0;
	m_EngraveIdx		= 0;
	m_EngraveTimer		= 0;
	m_EngraveGuildIdx	= 0;

	m_SiegeWarSuccessTimer	= 0;
	m_SiegeWarEndTimer	= 0;
	m_FightTime			= 0;

	SYSTEMTIME st;
	GetLocalTime(&st);
	m_wCurDay = st.wDayOfWeek;

	m_dwCurFlag = GAMERESRCMNGR->GetFlagFromDate(eSGFlg, m_wCurDay);
}

CSiegeWarMgr::~CSiegeWarMgr()
{

}


void CSiegeWarMgr::Init()
{
	CMHFile file;
	if(!file.Init(".\\Resource\\SiegeWarMapInfo.bin", "rb"))
	{
		ASSERT(0);
		return;
	}

	memset( m_SiegeWarMapNum, 0, sizeof(DWORD)*SIEGEWAR_MAX_SIEGEMAP*SIEGEWAR_MAX_AFFECTED_MAP );

	DWORD count = 0;
	while( 1 )
	{
		char buf[32] = { 0, };
		strcpy( buf, file.GetString() );

		if( strcmp( buf, "#VILLAGEMAP" ) == 0 )
		{
			DWORD mapcount = file.GetDword();

			for(DWORD i=0; i<mapcount; ++i)
				m_SiegeWarMapNum[count][i] = file.GetDword();
		}
		else if( strcmp( buf, "END" ) == 0 )
			break;

		++count;
	}
	file.Release();
	m_SiegeMapCount = count;

	m_SymbolIndex[NAKYANG_SYMBOL] = 295;
	LoadCastleGateInfo();
}


void CSiegeWarMgr::LoadCastleGateInfo()
{
	CMHFile file;
	if(!file.Init(".\\Resource\\CastleGateList.bin", "rb"))
	{
		ASSERT(0);
		return;
	}

	DWORD count = 0;
	m_CastleGateCount = 0;
	DWORD totalcount = file.GetDword();

	for(DWORD i=0; i<totalcount; ++i)
	{
		CASTLEGATE_BASEINFO info;
		memset( &info, 0, sizeof(CASTLEGATE_BASEINFO) );
		
		info.Index = file.GetDword();
		info.MapNum = file.GetDword();
		file.GetString( info.Name );
		info.Kind = file.GetDword();
		file.GetString( info.DataName );
		for(int k=0; k<eCastleGateLevel_Max; ++k)
		{
			info.Life[k] = file.GetDword();
			info.Shield[k] = file.GetDword();
		}
		info.Defence = file.GetDword();
		for(int k=0; k<ATTR_MAX; ++k)
			info.Regist.SetElement_Val( ATTR_FIRE+k, file.GetFloat() );
		info.Radius = file.GetFloat();
		info.Position.x = file.GetFloat();
		info.Position.z = file.GetFloat();
		info.Width = file.GetDword();
		info.Wide = file.GetDword();
		info.Angle = file.GetFloat();
		info.Scale = file.GetFloat();		

		if( info.MapNum == GetSiegeMapNum() )
		{
			memcpy( &m_CastleGateInfoList[m_CastleGateCount], &info, sizeof(CASTLEGATE_BASEINFO) );
			m_GateInfo.AddGate( info.Index, 0 );
			++m_CastleGateCount;
		}
	}

	file.Release();
}


void CSiegeWarMgr::CreateSiegeBattle()
{
	BATTLE_INFO_SIEGEWAR Info;
	memset( &Info, 0, sizeof(BATTLE_INFO_SIEGEWAR) );

	Info.BattleKind	= eBATTLE_KIND_SIEGEWAR;
	Info.BattleState = eBATTLE_STATE_READY;
	Info.BattleTime	= 0;	

	DWORD count = 0;
	CGuild* pGuild = NULL;
	if( m_CastleGuildIdx )
	{
		Info.GuildList[count] = m_CastleGuildIdx;
		++Info.DefenceCount;
		++count;
	}
	m_DefenceAcceptList.SetPositionHead();
	while( pGuild = m_DefenceAcceptList.GetData() )
	{
		Info.GuildList[count] = pGuild->GetIdx();
		++Info.DefenceCount;
		++count;
	}
	m_AttackGuildList.SetPositionHead();
	while( pGuild = m_AttackGuildList.GetData() )
	{
		Info.GuildList[count] = pGuild->GetIdx();
		++Info.AttackCount;
		++count;
	}

	BOOL res = BATTLESYSTEM->CreateBattle( &Info, g_pServerSystem->GetMapNum() );
	m_pBattle = (CBattle_SiegeWar*)BATTLESYSTEM->GetBattle( Info.BattleID );
}


void CSiegeWarMgr::SetBattleInfo()
{
	if( !m_pBattle )			return;

	BATTLE_INFO_SIEGEWAR Info;
	memset( &Info, 0, sizeof(BATTLE_INFO_SIEGEWAR) );

	BATTLE_INFO_SIEGEWAR cInfo;
	WORD size = 0;	
	m_pBattle->GetBattleInfo( (char*)&cInfo, &size );
	Info.BattleID = cInfo.BattleID;
	Info.BattleKind = cInfo.BattleKind;
	Info.BattleState = cInfo.BattleState;
	Info.BattleTime = cInfo.BattleTime;

	DWORD count = 0;
	CGuild* pGuild = NULL;
	if( m_CastleGuildIdx )
	{
		Info.GuildList[count] = m_CastleGuildIdx;
		++Info.DefenceCount;
		++count;
	}
	m_DefenceAcceptList.SetPositionHead();
	while( pGuild = m_DefenceAcceptList.GetData() )
	{
		Info.GuildList[count] = pGuild->GetIdx();
		++Info.DefenceCount;
		++count;
	}
	m_AttackGuildList.SetPositionHead();
	while( pGuild = m_AttackGuildList.GetData() )
	{
		Info.GuildList[count] = pGuild->GetIdx();
		++Info.AttackCount;
		++count;
	}

	if( m_pBattle )
		m_pBattle->SetBattleInfo( &Info );
}

void CSiegeWarMgr::SetSiegeWarInfo( DWORD SiegeWarIdx, DWORD TaxRate, DWORD RegistTime, DWORD SiegeWarTime, DWORD Level )
{
	m_SiegeWarIdx = SiegeWarIdx;
	m_TaxRate = TaxRate;
	if( RegistTime )
		m_SiegeWarTime[0].SetTime( RegistTime );	
	if( SiegeWarTime )
		m_SiegeWarTime[1].SetTime( SiegeWarTime );

	stCASTLEGATELEVEL tmp;
	tmp.value = Level;
	for(int i=0; i<m_CastleGateCount; ++i)
	{
		DWORD lev = tmp.GetLevel( m_CastleGateInfoList[i].Index );
		m_GateInfo.SetLevel( m_CastleGateInfoList[i].Index, lev );
	}
	if( m_SiegeWarIdx == 1 )
	{
		SetFirstSiegeWarTime();
		SiegeWarInfoUpdate( m_SiegeWarIdx, m_SiegeWarTime[0].value, m_SiegeWarTime[1].value, m_GateInfo.value, GetSiegeMapNum() );
	}


	stTIME ctime, cp, tp;
	ctime.SetTime( GetCurTime() );

	if( ctime > m_SiegeWarTime[0] )
	{
		if( ctime > m_SiegeWarTime[1] )
		{
			tp = m_SiegeWarTime[1];
			cp.SetTime( 0, 0, 0, 1, 0, 0 );	//[攻城战时间设置][by:十里坡剑神][QQ:112582793][2019/1/10]

			tp += cp;
			if( ctime > tp )
				m_SiegeWarState = eSWState_EndSiegeWar;
			else
				m_SiegeWarState = eSWState_SiegeWar;
		}
		else
		{
			tp = m_SiegeWarTime[0];
			cp.SetTime( 0, 0, 2, 0, 0, 0 );

			tp += cp;
			if( ctime > tp )
			{
				cp.SetTime( 0, 0, 3, 0, 0, 0 );
				tp += cp;
				if( ctime > tp )
					m_SiegeWarState = eSWState_BeforeSiegeWar;
				else
					m_SiegeWarState = eSWState_Acceptance;
			}
			else
				m_SiegeWarState = eSWState_Proclamation;
		}
	}
	else
		m_SiegeWarState = eSWState_Before;	
}



void CSiegeWarMgr::SetFirstSiegeWarTime()
{
	CMHFile file;
	if(!file.Init("./Resource/Server/SiegeWarStartTime.bin", "rb"))
	{
		ASSERT(0);
		return;
	}

	while( 1 )
	{
		char buf[32] = { 0, };
		strcpy( buf, file.GetString() );

		if( strcmp( buf, "#MAP" ) == 0 )
		{
			DWORD Map = file.GetDword();
			if( Map == GetSiegeMapNum() )
			{
				DWORD timetype = file.GetDword();
				DWORD year = file.GetDword();
				DWORD month = file.GetDword();
				DWORD day = file.GetDword();
				DWORD hour = file.GetDword();
				DWORD minute = file.GetDword();
				DWORD second = file.GetDword();

				m_SiegeWarTime[timetype].SetTime( year, month, day, hour, minute, second );
			}
		}
		else if( strcmp( buf, "#END" ) == 0 )
			break;
	}
	file.Release();
}



void CSiegeWarMgr::SetSiegeGuildInfo( SIEGEWAR_GUILDDBINFO* pGuildList, DWORD Count )
{
	if( !pGuildList )			return;

	CGuild* pGuild = NULL;

	for(DWORD i=0; i<Count; ++i)
	{
		if( !pGuildList[i].GuildIdx )		continue;
		pGuild = GUILDMGR->GetGuild( pGuildList[i].GuildIdx );
		if( !pGuild )						continue;

		if( pGuildList[i].Type == eSWGuildState_CastleGuild )				
		{
			m_CastleGuildIdx = pGuildList[i].GuildIdx;			
			m_CastleUnionIdx = pGuild->GetGuildUnionIdx();
		}
		else if( pGuildList[i].Type == eSWGuildState_DefenceGuild )			
		{
			m_DefenceAcceptList.Add( pGuild, pGuild->GetIdx() );
		}
		else if( pGuildList[i].Type == eSWGuildState_DefenceProposalGuild )	
		{
			m_DefenceProposalList.Add( pGuild, pGuild->GetIdx() );
		}
		else if( pGuildList[i].Type == eSWGuildState_AttackGuild )			
		{
			m_AttackGuildList.Add( pGuild, pGuild->GetIdx() );
			
			if( pGuild->GetGuildUnionIdx() &&
				!m_AttackGuildList.GetData( pGuild->GetGuildUnionIdx() ) )
			{
				CGuild* pMasterGuild = GUILDUNIONMGR->GetMasterGuildInUnion( pGuild->GetGuildUnionIdx() );
				if( pMasterGuild )
					m_AttackUnionList.Add( pMasterGuild, pGuild->GetGuildUnionIdx() );
			}
		}
	}
}


DWORD CSiegeWarMgr::AddProposalGuildList( DWORD GuildIdx )
{
	CGuild* pGuild = GUILDMGR->GetGuild( GuildIdx );
	if( !pGuild )		return eSWError_NoGuildInfo;

	if( m_CastleGuildIdx == 0 )
		return eSWError_NoCastleGuild;
	if( GuildIdx == m_CastleGuildIdx )
		return eSWError_MyUnionIsCastleUnion;
	if( m_SiegeWarState != eSWState_Acceptance)
		return eSWError_NoProposalTime;
	if( pGuild->GetGuildUnionIdx() && m_AttackUnionList.GetData( pGuild->GetGuildUnionIdx() ) )
		return eSWError_MyUnionIsOtherTeam;
	if( m_AttackGuildList.GetData( GuildIdx ) )
		return eSWError_AlreadyAttackRegist;
	if( pGuild->GetGuildUnionIdx() && pGuild->GetGuildUnionIdx() == m_CastleUnionIdx )
		return eSWError_MyUnionIsCastleUnion;
	if( m_DefenceProposalList.GetData( GuildIdx ) || m_DefenceAcceptList.GetData( GuildIdx ) )
		return eSWError_AlreadyDefenceProposal;

	if( SIEGEWAR_MAXGUILDCOUNT_PERTEAM < m_DefenceProposalList.GetDataNum() )
		return eSWError_OverGuildCount;

	m_DefenceProposalList.Add( pGuild, GuildIdx );

	return eSWError_NoError;
}


DWORD CSiegeWarMgr::AddAttackGuildList( DWORD GuildIdx )
{
	CGuild* pGuild = GUILDMGR->GetGuild( GuildIdx );
	if( !pGuild )		return eSWError_NoGuildInfo;

	if( m_SiegeWarState != eSWState_Acceptance )
		return eSWError_NoProposalTime;
	if( pGuild->GetGuildUnionIdx() &&
		pGuild->GetGuildUnionIdx() == m_CastleUnionIdx )
		return eSWError_MyUnionIsCastleUnion;
	if( m_CastleGuildIdx == GuildIdx )
		return eSWError_MyUnionIsCastleUnion;
	if( m_AttackGuildList.GetData( GuildIdx ) )
		return eSWError_AlreadyAttackRegist;
	if( m_DefenceAcceptList.GetData( GuildIdx ) )
		return eSWError_AlreadyDefenceProposal;
	if( m_DefenceProposalList.GetData( GuildIdx ) )
		return eSWError_AlreadyDefenceProposal;

	CGuild* ptmpGuild = NULL;
	if( pGuild->GetGuildUnionIdx() )
	{
		m_DefenceProposalList.SetPositionHead();
		while( ptmpGuild = m_DefenceProposalList.GetData() )
		{
			if( ptmpGuild->GetGuildUnionIdx() == pGuild->GetGuildUnionIdx() )
				return eSWError_MyUnionIsOtherTeam;
		}
		m_DefenceAcceptList.SetPositionHead();
		while( ptmpGuild = m_DefenceAcceptList.GetData() )
		{
			if( ptmpGuild->GetGuildUnionIdx() == pGuild->GetGuildUnionIdx() )
				return eSWError_MyUnionIsOtherTeam;
		}
	}
	
	if( SIEGEWAR_MAXGUILDCOUNT_PERTEAM < m_AttackGuildList.GetDataNum() )
		return eSWError_OverGuildCount;

	m_AttackGuildList.Add( pGuild, GuildIdx );
	if( pGuild->GetGuildUnionIdx() &&
		!m_AttackUnionList.GetData( pGuild->GetGuildUnionIdx() ) )
	{
		CGuild* pMasterGuild = GUILDUNIONMGR->GetMasterGuildInUnion( pGuild->GetGuildUnionIdx() );
		if( pMasterGuild )
			m_AttackUnionList.Add( pMasterGuild, pGuild->GetGuildUnionIdx() );
	}

	return eSWError_NoError;
}


void CSiegeWarMgr::AddPlayer( CPlayer* pPlayer )
{
	if( !pPlayer )												return;
	if( g_pServerSystem->GetMapNum() != GetSiegeMapNum() )		return;

	CGuild* pGuild = GUILDMGR->GetGuild( m_CastleGuildIdx );

	int BattleIdx = pPlayer->GetObserverBattleIdx();
	if( BattleIdx )
	{
		pPlayer->SetVisible( FALSE );
		if( m_pBattle && m_pBattle->AddObserverToBattle( pPlayer ) )
		{
			SEND_SW_INITINFO msg;
			SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_BATTLEJOIN_ACK );
			msg.Time = m_FightTime;
			if( pGuild )
				SafeStrCpy( msg.GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME+1 );
			else
				memset( msg.GuildName, 0, sizeof(char)*MAX_GUILD_NAME+1 );
			if( m_CastleGateList.GetCount() )
			{
				CMapObject* pGate = NULL;
				PTRLISTPOS pos = m_CastleGateList.GetHeadPosition();
				while( pos )
				{
					pGate = (CMapObject*)m_CastleGateList.GetNext( pos );
					if( !pGate )			continue;

					msg.GateInfo[msg.GateCount].Index = pGate->GetID();
					msg.GateInfo[msg.GateCount].Life = pGate->GetLife();
					msg.GateInfo[msg.GateCount].Shield = pGate->GetShield();
					msg.GateInfo[msg.GateCount].Level = (BYTE)m_GateInfo.GetLevel( pGate->GetID() );
					++msg.GateCount;
				}
			}
			pPlayer->SendMsg( &msg, msg.GetSize() );
			return;
		}
		else
			goto AddBattleFailed;
	}
	else
	{		
		pPlayer->SetVisible( TRUE );
		if( m_pBattle && m_pBattle->AddObjectToBattle( pPlayer ) )
		{
			SEND_SW_INITINFO msg;
			SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_BATTLEJOIN_ACK );
			msg.Time = m_FightTime;
			if( pGuild )
				SafeStrCpy( msg.GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME+1 );			
			else
				memset( msg.GuildName, 0, sizeof(char)*MAX_GUILD_NAME+1 );
			if( m_CastleGateList.GetCount() )
			{
				CMapObject* pGate = NULL;
				PTRLISTPOS pos = m_CastleGateList.GetHeadPosition();
				while( pos )
				{
					pGate = (CMapObject*)m_CastleGateList.GetNext( pos );
					if( !pGate )			continue;

					msg.GateInfo[msg.GateCount].Index = pGate->GetID();
					msg.GateInfo[msg.GateCount].Life = pGate->GetLife();
					msg.GateInfo[msg.GateCount].Shield = pGate->GetShield();
					msg.GateInfo[msg.GateCount].Level = (BYTE)m_GateInfo.GetLevel( pGate->GetID() );
					++msg.GateCount;
				}
			}
			pPlayer->SendMsg( &msg, msg.GetSize() );
			return;
		}
		else
			goto AddBattleFailed;
	}
	
AddBattleFailed:
	MSG_DWORD msg;
	SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_RETURNTOMAP );
	msg.dwData = pPlayer->GetReturnMapNum();
	pPlayer->SendMsg( &msg, sizeof(msg) );
	g_pServerSystem->RemovePlayer( pPlayer->GetID() );
	return;
}


BOOL CSiegeWarMgr::IsAcceptGuild( DWORD GuildIdx )
{
	if( m_DefenceAcceptList.GetData( GuildIdx ) )
		return TRUE;
	return FALSE;
}


BOOL CSiegeWarMgr::IsAttackGuild( DWORD GuildIdx )
{
	if( m_AttackGuildList.GetData( GuildIdx ) )
		return TRUE;

	return FALSE;
}


BOOL CSiegeWarMgr::IsAttackUnion( DWORD UnionIdx )
{
	if( m_AttackUnionList.GetData( UnionIdx ) )
		return TRUE;

	return FALSE;
}


void CSiegeWarMgr::SendMsgToSeigeGuild( MSGBASE* pMsg, DWORD dwLength )
{
	CGuild* pGuild = NULL;

	pGuild = GUILDMGR->GetGuild( m_CastleGuildIdx );
	if( pGuild )
		pGuild->SendMsgToAll( pMsg, dwLength );

	m_DefenceAcceptList.SetPositionHead();
	while( pGuild = m_DefenceAcceptList.GetData() )
	{
		pGuild->SendMsgToAll( pMsg, dwLength );
	}
	m_AttackGuildList.SetPositionHead();
	while( pGuild = m_AttackGuildList.GetData() )
	{
		pGuild->SendMsgToAll( pMsg, dwLength );
	}
}


void CSiegeWarMgr::Process()
{
	if( GetSiegeMapNum() != g_pServerSystem->GetMapNum() )			return;

	switch( m_SiegeWarState )
	{
	case eSWState_Before:	
		{
			stTIME curTime;
			curTime.value = GetCurTime();
			if( m_SiegeWarTime[0].value && curTime > m_SiegeWarTime[0] )
			{
				m_SiegeWarState = eSWState_Proclamation;

				g_pServerTable->SetPositionHead();
 				SERVERINFO* pAgentInfo = g_pServerTable->GetNextAgentServer();
 				if(pAgentInfo == NULL)
  				{
  					ASSERT(0);
 					return;
 				}

				MSG_DWORD msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_CHANGESTATE );
				msg.dwData = m_SiegeWarState;
				MSG_DWORD tmsg = msg;
				PACKEDDATA_OBJ->SendToMapServer(/* pAgentInfo->dwConnectionIndex,*/ (MAPTYPE)GetVillageMapNum(), &tmsg, sizeof(tmsg) );

				MSG_DWORD msg1;
				SetProtocol( &msg1, MP_SIEGEWAR, MP_SIEGEWAR_FLAGCHANGE );
				msg1.dwData = m_SiegeWarState;
				PACKEDDATA_OBJ->SendToBroadCastMapServer( &msg1, sizeof(msg1) );

				CObject* pObject = NULL;
				g_pUserTable->SetPositionHead();
				while( pObject = g_pUserTable->GetData() )
				{
					if( pObject->GetObjectKind() != eObjectKind_Player )			continue;

					MSG_DWORD tmsg = msg;
					pObject->SendMsg( &tmsg, sizeof(tmsg) );
				}
			}			
		}
		break;
	case eSWState_Proclamation:	
		{
			stTIME curTime, cmpTime, tmpTime;
			curTime.value = GetCurTime();
			tmpTime.SetTime( 0, 0, 2, 0, 0, 0 );

			cmpTime = m_SiegeWarTime[0];
			cmpTime += tmpTime;
			if( curTime > cmpTime )
			{
				m_SiegeWarState = eSWState_Acceptance;

				g_pServerTable->SetPositionHead();
 				SERVERINFO* pAgentInfo = g_pServerTable->GetNextAgentServer();
 				if(pAgentInfo == NULL)
  				{
  					ASSERT(0);
 					return;
 				}

				MSG_DWORD msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_CHANGESTATE );
				msg.dwData = m_SiegeWarState;
				PACKEDDATA_OBJ->SendToMapServer(/* pAgentInfo->dwConnectionIndex, */(MAPTYPE)GetVillageMapNum(), &msg, sizeof(msg) );

				MSG_DWORD msg1;
				SetProtocol( &msg1, MP_SIEGEWAR, MP_SIEGEWAR_FLAGCHANGE );
				msg1.dwData = m_SiegeWarState;
				PACKEDDATA_OBJ->SendToBroadCastMapServer( &msg1, sizeof(msg1) );
			}
		}
		break;
	case eSWState_Acceptance:	
		{
			stTIME curTime, cmpTime, tmpTime;
			curTime.value = GetCurTime();
			tmpTime.SetTime( 0, 0, 5, 0, 0, 0 );

			cmpTime = m_SiegeWarTime[0];
			cmpTime += tmpTime;
			if( curTime > cmpTime )
			{
				m_SiegeWarState = eSWState_BeforeSiegeWar;

				g_pServerTable->SetPositionHead();
 				SERVERINFO* pAgentInfo = g_pServerTable->GetNextAgentServer();
 				if(pAgentInfo == NULL)
  				{
  					ASSERT(0);
 					return;
 				}

				MSG_DWORD msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_CHANGESTATE );
				msg.dwData = m_SiegeWarState;
				PACKEDDATA_OBJ->SendToMapServer( /*pAgentInfo->dwConnectionIndex, */(MAPTYPE)GetVillageMapNum(), &msg, sizeof(msg) );

				MSG_DWORD msg1;
				SetProtocol( &msg1, MP_SIEGEWAR, MP_SIEGEWAR_FLAGCHANGE );
				msg1.dwData = m_SiegeWarState;
				PACKEDDATA_OBJ->SendToBroadCastMapServer( &msg1, sizeof(msg1) );
			}
		}
		break;
	case eSWState_BeforeSiegeWar:	
		{
			stTIME curTime;
			curTime.value = GetCurTime();


			if( curTime > m_SiegeWarTime[1] )
			{
				StartSiegeWar();

				MSG_DWORD msg1;
				SetProtocol( &msg1, MP_SIEGEWAR, MP_SIEGEWAR_FLAGCHANGE );
				msg1.dwData = m_SiegeWarState;
				PACKEDDATA_OBJ->SendToBroadCastMapServer( &msg1, sizeof(msg1) );
			}
		}
		break;
	case eSWState_SiegeWar:			
		{
			stTIME curTime, cmpTime, tmpTime;
			curTime.value = GetCurTime();
			tmpTime.SetTime( 0, 0, 0, 1, 0, 0 );	//[攻城战时间设置][by:十里坡剑神][QQ:112582793][2019/1/10]

			cmpTime = m_SiegeWarTime[1];
			cmpTime += tmpTime;
			if( curTime > cmpTime )				
			{
				if( m_SiegeWarSuccessTimer == 0 )
				{
					EndSiegeWar();

					MSG_DWORD msg1;
					SetProtocol( &msg1, MP_SIEGEWAR, MP_SIEGEWAR_FLAGCHANGE );
					msg1.dwData = m_SiegeWarState;
					PACKEDDATA_OBJ->SendToBroadCastMapServer( &msg1, sizeof(msg1) );
					break;
				}
			}
			else if( m_FightTime && m_FightTime > gTickTime )
				m_FightTime -= gTickTime;
			else
				m_FightTime = 0;

			if( m_SiegeWarSuccessTimer )		
			{
				if( m_SiegeWarSuccessTimer > gTickTime )
					m_SiegeWarSuccessTimer -= gTickTime;
				else
				{
					SuccessSiegeWar( m_EngraveGuildIdx );
					m_SiegeWarSuccessTimer = 0;
				}
			}
			else if( m_EngraveTimer )			
			{
				if( m_EngraveTimer > gTickTime )
					m_EngraveTimer -= gTickTime;
				else
				{

					CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( m_EngraveIdx );
					if( !pPlayer )
					{
						m_EngraveIdx = 0;
						m_EngraveTimer = 0;
						m_SiegeWarSuccessTimer = 0;
						m_EngraveGuildIdx = 0;
						break;
					}

					SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), pPlayer->GetGuildIdx(), eSWLogKind_SiegeWarSucceed, m_EngraveIdx );

					m_EngraveIdx = 0;
					m_EngraveTimer = 0;
					m_SiegeWarSuccessTimer = 10000;
					m_EngraveGuildIdx = pPlayer->GetGuildIdx();
					OBJECTSTATEMGR_OBJ->EndObjectState( pPlayer, eObjectState_Engrave );

					MSG_DWORD2 msg;
					SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_ENGRAVE_ACK );
					msg.dwData1 = 1;			
					msg.dwData2 = pPlayer->GetID();
					PACKEDDATA_OBJ->QuickSend( pPlayer, &msg, sizeof(msg) );

					SEND_SW_SUCCESSINFO msg2;
					SetProtocol( &msg2, MP_SIEGEWAR, MP_SIEGEWAR_SUCCESS_SIEGEWAR );
					CGuild* pGuild = GUILDMGR->GetGuild( pPlayer->GetGuildIdx() );
					if( !pGuild )		break;
					SafeStrCpy( msg2.GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME+1 );

					CObject* pObject = NULL;
					g_pUserTable->SetPositionHead();
					while( pObject = g_pUserTable->GetData() )
					{
						if( pObject->GetObjectKind() != eObjectKind_Player )			continue;

						SEND_SW_SUCCESSINFO tmsg = msg2;
						pObject->SendMsg( &tmsg, sizeof(tmsg) );
					}
				}
			}
		}
		break;
	case eSWState_EndSiegeWar:	
		{
			if( m_SiegeWarEndTimer < 300000 )//[攻城战结束延迟处理时间设置][BY:十里坡剑神][QQ:112582793][2019-9-4][15:33:16]
				m_SiegeWarEndTimer += gTickTime;
			else
			{
				if( m_pBattle )
					m_pBattle->ReturnToMapAllPlayer();
				
				m_AttackGuildList.RemoveAll();				
				m_DefenceAcceptList.RemoveAll();
				m_AttackUnionList.RemoveAll();

				CGuild* pGuild = NULL;
				cPtrList GuildList;
				if( GUILDUNIONMGR->GetGuildListInUnion( m_CastleUnionIdx, &GuildList ) )
				{
					PTRLISTPOS pos = GuildList.GetHeadPosition();
					while( pos )
					{
						pGuild = (CGuild*)GuildList.GetNext( pos );
						if( !pGuild )			continue;

						if( pGuild->GetIdx() == m_CastleGuildIdx )		continue;

						m_DefenceAcceptList.Add( pGuild, pGuild->GetIdx() );
					}
				}
				
				m_EngraveIdx = 0;
				m_EngraveTimer = 0;
				m_EngraveGuildIdx = 0;
				m_SiegeWarEndTimer = 0;

				m_SiegeWarState = eSWState_Before;
			}
		}
		break;
	}
}


void CSiegeWarMgr::StartSiegeWar()
{
	m_SiegeWarState = eSWState_SiegeWar;

	if( m_pBattle )
	{
		SetBattleInfo();
		m_pBattle->StartBattle();					
	}

	g_pServerTable->SetPositionHead();
 	SERVERINFO* pAgentInfo = g_pServerTable->GetNextAgentServer();
 	if(pAgentInfo == NULL)
  	{
  		ASSERT(0);
 		return;
 	}

	MSG_DWORD msg;
	SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_CHANGESTATE );
	msg.dwData = m_SiegeWarState;
	PACKEDDATA_OBJ->SendToMapServer( /*pAgentInfo->dwConnectionIndex,*/ (MAPTYPE)GetVillageMapNum(), &msg, sizeof(msg) );

	m_EngraveIdx = 0;
	m_EngraveTimer = 0;
	m_EngraveGuildIdx = 0;

	stTIME ctime, retime;
	ctime.value = GetCurTime();
	ctime -= m_SiegeWarTime[1];
	retime.SetTime( 0, 0, 0, 1, 0, 0 );	//[攻城战时间设置][by:十里坡剑神][QQ:112582793][2019/1/10]

	retime -= ctime;
	
	m_FightTime = 0;
	m_FightTime += retime.GetHour()*HOURTOSECOND;
	m_FightTime += retime.GetMinute()*MINUTETOSECOND;
	m_FightTime += retime.GetSecond();
	m_FightTime *= 1000;
	
	m_SiegeWarSuccessTimer = 0;

	CreateCastleGate();

	SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), 0, eSWLogKind_StartSiegeWar, 0 );
}


void CSiegeWarMgr::CreateCastleGate()
{
	if( m_CastleGateList.GetCount() )
	{
		CMapObject* pObject = NULL;
		PTRLISTPOS pos = m_CastleGateList.GetHeadPosition();
		while( pos )
		{
			pObject = (CMapObject*)m_CastleGateList.GetNext( pos );
			if( !pObject )		continue;

			DeleteCastleGate( pObject->GetID() );
		}
		m_CastleGateList.RemoveAll();
	}


	BASEOBJECT_INFO obInfo;
	MAPOBJECT_INFO	moinfo;
	DWORD Level = 0;

	for(int i=0; i<m_CastleGateCount; ++i)
	{
		Level = m_GateInfo.GetLevel( m_CastleGateInfoList[i].Index );

		obInfo.BattleID = m_pBattle->GetBattleID();
		obInfo.BattleTeam = eBattleTeam1;
		obInfo.dwObjectID = m_CastleGateInfoList[i].Index;
		SafeStrCpy( obInfo.ObjectName, m_CastleGateInfoList[i].Name, CASTLEGATE_NAME_LENGTH+1 );

		moinfo.Life = m_CastleGateInfoList[i].Life[Level];
		moinfo.MaxLife = m_CastleGateInfoList[i].Life[Level];
		moinfo.Shield = m_CastleGateInfoList[i].Shield[Level];
		moinfo.MaxShield = m_CastleGateInfoList[i].Shield[Level];			
		moinfo.Radius = m_CastleGateInfoList[i].Radius;
		moinfo.PhyDefence = m_CastleGateInfoList[i].Defence;
		moinfo.AttrRegist = m_CastleGateInfoList[i].Regist;

		CMapObject* pGate = (CMapObject*)g_pServerSystem->AddMapObject( eObjectKind_CastleGate, &obInfo, &moinfo, &m_CastleGateInfoList[i].Position );
		if( !pGate )			continue;

		pGate->SetLevel( Level );
		m_CastleGateList.AddTail( pGate );
	}
}


void CSiegeWarMgr::AddCastleGate()
{
	
}


void CSiegeWarMgr::DeleteCastleGate( DWORD GateID )
{
	CObject* pObject = g_pUserTable->FindUser( GateID );
	if( !pObject )		return;

	m_CastleGateList.Remove( pObject );
	g_pServerSystem->RemoveMapObject( GateID );

	MSG_DWORD msg;
	SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_CASTLEGATE_DELETE );
	msg.dwData = GateID;
	
	pObject = NULL;
	g_pUserTable->SetPositionHead();
	while( pObject = g_pUserTable->GetData() )
	{
		if( pObject->GetObjectKind() != eObjectKind_Player )			continue;
		MSG_DWORD tmsg = msg;
		pObject->SendMsg( &tmsg, sizeof(tmsg) );
	}
}


void CSiegeWarMgr::EndSiegeWar()
{
	m_SiegeWarState = eSWState_EndSiegeWar;
	m_SiegeWarEndTimer = 0;
	m_FightTime = 0;

	if( m_pBattle )
		m_pBattle->EndBattle();
	
	g_pServerTable->SetPositionHead();
	SERVERINFO* pAgentInfo = g_pServerTable->GetNextAgentServer();
	if(pAgentInfo == NULL)
	{
		ASSERT(0);
		return;
	}
	
	MSG_DWORD msg;
	SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_CHANGESTATE );
	msg.dwData = m_SiegeWarState;
	MSG_DWORD tmsg = msg;
	PACKEDDATA_OBJ->SendToMapServer( /*pAgentInfo->dwConnectionIndex,*/ (MAPTYPE)GetVillageMapNum(), &tmsg, sizeof(tmsg) );
	
	CObject* pObject = NULL;
	CGuild* ptGuild = NULL;
	ptGuild =  GUILDMGR->GetGuild( m_CastleGuildIdx);
	
	g_pUserTable->SetPositionHead();
	while( pObject = g_pUserTable->GetData() )
	{
		if( pObject->GetObjectKind() != eObjectKind_Player )			continue;
		
		MSG_DWORD tmsg = msg;
		pObject->SendMsg( &tmsg, sizeof(tmsg) );
		if(ptGuild!=NULL&&g_pServerSystem->GetMapNum()==5)
		{
			if(((CPlayer*)pObject)->GetGuildIdx()==m_CastleGuildIdx)
			{
				if(ptGuild->GetMasterIdx()==((CPlayer*)pObject)->GetID())
					SIEGEWARGIVEITEM->SendItem((CPlayer*)pObject,eGuildMaster);//[帮主奖励][By:十里坡剑神][QQ:112582793][2019/3/27][14:15:12]
				else 
					SIEGEWARGIVEITEM->SendItem((CPlayer*)pObject,eNomorPlayer);//[帮会成员奖励][By:十里坡剑神][QQ:112582793][2019/3/27][14:15:18]
			}
			else if(((CPlayer*)pObject)->GetGuildUnionIdx()==ptGuild->GetGuildUnionIdx())
			{
					SIEGEWARGIVEITEM->SendItem((CPlayer*)pObject,eUnionPlayer);//[联盟成员奖励][By:十里坡剑神][QQ:112582793][2019/3/27][14:15:27]
			}
			else
			{
				SIEGEWARGIVEITEM->SendItem((CPlayer*)pObject, eLoserPlayer);//[安慰奖][BY:十里坡剑神][QQ:112582793][2019-9-4][14:44:18]
			}
		}
		
	}
	
	DWORD day = 0;
	SYSTEMTIME st;
	GetLocalTime( &st );
	if( st.wDayOfWeek == 6 )				day = 9;
	else if( st.wDayOfWeek == 0 )			day = 8;
	
	stTIME curTime, nextTime;
	curTime.value = GetCurTime();
	nextTime.SetTime( 0, 0, day, 0, 0, 0 );
	curTime += nextTime;
	curTime.SetTime( curTime.GetYear(), curTime.GetMonth(), curTime.GetDay(), 0, 0, 0 );
	m_SiegeWarTime[0] = curTime;
	m_SiegeWarTime[1] = curTime;
	nextTime.SetTime( 0, 0, 6, 18, 0, 0 );	
	m_SiegeWarTime[1] += nextTime;
	
	CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( m_EngraveIdx );
	if( pPlayer )
	{		
		MSG_DWORD2 engravemsg;
		SetProtocol( &engravemsg, MP_SIEGEWAR, MP_SIEGEWAR_ENGRAVE_CANCEL );
		engravemsg.dwData1 = pPlayer->GetID();
		engravemsg.dwData2 = 0;
		pPlayer->SendMsg( &engravemsg, sizeof(engravemsg) );
		
		if( OBJECTSTATEMGR_OBJ->GetObjectState( pPlayer ) == eObjectState_Engrave )
			OBJECTSTATEMGR_OBJ->EndObjectState( pPlayer, eObjectState_Engrave );
	}

	m_GateInfo.SetZeroAllLevel();
	m_EngraveIdx = 0;
	m_EngraveTimer = 0;
	++m_SiegeWarIdx;
	//[更新攻城战胜利帮会][By:十里坡剑神][QQ:112582793][2018/2/21]
	SWPROFITMGR->SetProfitGuild( m_CastleGuildIdx );

	SiegeWarInfoUpdate( 9999, m_SiegeWarIdx, 0, 0, GetSiegeMapNum() );	
	SiegeWarInfoInsert( m_SiegeWarIdx, m_SiegeWarTime[0].value, m_SiegeWarTime[1].value, m_GateInfo.value, GetSiegeMapNum() );

	if( m_CastleGuildIdx )
	{
		SiegeWarGuildInsert( m_SiegeWarIdx, m_CastleGuildIdx, eSWGuildState_CastleGuild, GetSiegeMapNum() );
		SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), m_CastleGuildIdx, eSWLogKind_EndCastleGuild, 0 );
	}

	CGuild* pGuild = NULL;
	cPtrList GuildList;
	if( GUILDUNIONMGR->GetGuildListInUnion( m_CastleUnionIdx, &GuildList ) )
	{
		PTRLISTPOS pos = GuildList.GetHeadPosition();
		while( pos )
		{
			pGuild = (CGuild*)GuildList.GetNext( pos );
			if( !pGuild )			continue;

			if( pGuild->GetIdx() == m_CastleGuildIdx )		continue;

			SiegeWarGuildInsert( m_SiegeWarIdx, pGuild->GetIdx(), eSWGuildState_DefenceGuild, GetSiegeMapNum() );
			SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), pGuild->GetIdx(), eSWLogKind_EndDefenceUnionGuild, 0 );
		}
	}
	SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), 0, eSWLogKind_EndSiegeWar, 0 );
}


void CSiegeWarMgr::SuccessSiegeWar( DWORD GuildIdx )
{
	if( !m_pBattle )
	{
		ASSERT(0);
		return;
	}

	CGuild* pNewCastleGuild = GUILDMGR->GetGuild( GuildIdx );
	if( !pNewCastleGuild )			return;

	DWORD AtkCount = 0;
	DWORD DefCount = 0;
	DWORD tmpAttackGuildList[SIEGEWAR_MAXGUILDCOUNT_PERTEAM];
	DWORD tmpDefenceGuildList[SIEGEWAR_MAXGUILDCOUNT_PERTEAM];
	memset( tmpAttackGuildList, 0, sizeof(DWORD)*SIEGEWAR_MAXGUILDCOUNT_PERTEAM );
	memset( tmpDefenceGuildList, 0, sizeof(DWORD)*SIEGEWAR_MAXGUILDCOUNT_PERTEAM );

	CGuild* pGuild = NULL;
	m_AttackGuildList.SetPositionHead();
	while( pGuild = m_AttackGuildList.GetData() )
	{
		if( pGuild->GetIdx() == pNewCastleGuild->GetIdx() )			continue;

		tmpAttackGuildList[AtkCount] = pGuild->GetIdx();
		++AtkCount;
	}
	m_AttackGuildList.RemoveAll();
	m_DefenceAcceptList.SetPositionHead();

	if( m_CastleGuildIdx )
	{
		tmpDefenceGuildList[0] = m_CastleGuildIdx;
		++DefCount;
	}
	while( pGuild = m_DefenceAcceptList.GetData() )
	{
		tmpDefenceGuildList[DefCount] = pGuild->GetIdx();
		++DefCount;
	}
	m_DefenceAcceptList.RemoveAll();
	m_AttackUnionList.RemoveAll();

	for(DWORD i=0; i<AtkCount; ++i)
	{
		pGuild = GUILDMGR->GetGuild( tmpAttackGuildList[i] );
		if( pGuild )
		{
			if( pGuild->GetGuildUnionIdx() && pGuild->GetGuildUnionIdx() == pNewCastleGuild->GetGuildUnionIdx() )
			{
				m_DefenceAcceptList.Add( pGuild, pGuild->GetIdx() );				
				SiegeWarGuildUpdate( m_SiegeWarIdx, pGuild->GetIdx(), eSWGuildState_CastleUnionGuild, GetSiegeMapNum() );
				SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), pGuild->GetIdx(), eSWLogKind_SucceedMoveToDefence, 0 );				
			}
			else
			{
				m_AttackGuildList.Add( pGuild, pGuild->GetIdx() );
				SiegeWarGuildUpdate( m_SiegeWarIdx, pGuild->GetIdx(), eSWGuildState_AttackGuild, GetSiegeMapNum() );
				SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), pGuild->GetIdx(), eSWLogKind_SucceedAttack, 0 );
			}
		}
	}
	for(int i=0; i<DefCount; ++i)
	{
		pGuild = GUILDMGR->GetGuild( tmpDefenceGuildList[i] );
		if( pGuild )
		{
			m_AttackGuildList.Add( pGuild, pGuild->GetIdx() );
			if( pGuild->GetGuildUnionIdx() &&
				!m_AttackUnionList.GetData( pGuild->GetGuildUnionIdx() ) )
			{
				CGuild* pMasterGuild = GUILDUNIONMGR->GetMasterGuildInUnion( pGuild->GetGuildUnionIdx() );
				if( pMasterGuild )
					m_AttackUnionList.Add( pMasterGuild, pGuild->GetGuildUnionIdx() );
			}
			SiegeWarGuildUpdate( m_SiegeWarIdx, pGuild->GetIdx(), eSWGuildState_AttackGuild, GetSiegeMapNum() );
			SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), pGuild->GetIdx(), eSWLogKind_SucceedMoveToAttack, 0 );
		}
	}

	m_EngraveGuildIdx = 0;
	m_CastleGuildIdx = pNewCastleGuild->GetIdx();
	m_CastleUnionIdx = pNewCastleGuild->GetGuildUnionIdx();
	SiegeWarGuildUpdate( m_SiegeWarIdx, m_CastleGuildIdx, eSWGuildState_CastleGuild, GetSiegeMapNum() );

	SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), m_CastleGuildIdx, eSWLogKind_SucceedCastleGuild, 0 );

	SetBattleInfo();
	m_pBattle->BattleTeamChange();
	m_pBattle->SendBattleInfoToPlayer();
	m_pBattle->BattleTeamPositionChange();

	SendBattleInfoToVillageMap();
}



void CSiegeWarMgr::SendBattleInfoToVillageMap()
{
	CGuild* pGuild = NULL;
	SEND_SW_GUILDLIST msg;
	SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_BATTLEINFO );
	
	DWORD count = 0;
	
	pGuild = GUILDMGR->GetGuild( m_CastleGuildIdx );
	if( pGuild )
	{
		msg.GuildList[count].Info.GuildIdx = pGuild->GetIdx();
		msg.GuildList[count].Info.Type = eSWGuildState_CastleGuild;
		strncpy( msg.GuildList[count].GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME );
		++msg.DefenceCount;
		++count;
	}
	m_DefenceAcceptList.SetPositionHead();
	while( pGuild = m_DefenceAcceptList.GetData() )
	{
		msg.GuildList[count].Info.GuildIdx = pGuild->GetIdx();
		msg.GuildList[count].Info.Type = eSWGuildState_CastleUnionGuild;
		strncpy( msg.GuildList[count].GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME );
		++msg.DefenceCount;
		++count;
	}
	m_AttackGuildList.SetPositionHead();
	while( pGuild = m_AttackGuildList.GetData() )
	{
		msg.GuildList[count].Info.GuildIdx = pGuild->GetIdx();
		msg.GuildList[count].Info.Type = eSWGuildState_AttackGuild;
		strncpy( msg.GuildList[count].GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME );
		++msg.AttackCount;
		++count;
	}
	
	g_pServerTable->SetPositionHead();
	SERVERINFO* pAgentInfo = g_pServerTable->GetNextAgentServer();
	if( !pAgentInfo )			return;

	PACKEDDATA_OBJ->SendToMapServer( /*pAgentInfo->dwConnectionIndex,*/ (MAPTYPE)GetVillageMapNum(), &msg, msg.GetSize() );	
}



DWORD CSiegeWarMgr::GetGuildTeamIdx( DWORD GuildIdx )
{
	if( m_AttackGuildList.GetData( GuildIdx ) )
		return 1;
	else if( m_DefenceAcceptList.GetData( GuildIdx) || m_CastleGuildIdx == GuildIdx)
		return 0;

	return 2;
}



DWORD CSiegeWarMgr::IsAbleOrganizeUnion( DWORD GuildIdx1, DWORD GuildIdx2 )
{
	if( m_AttackGuildList.GetData( GuildIdx1 ) )
	{
		if( m_CastleGuildIdx == GuildIdx2 )
			return 1;
		if( m_DefenceAcceptList.GetData( GuildIdx2 ) )
			return 2;
		if( m_DefenceProposalList.GetData( GuildIdx2 ) )
			return 3;
	}
	if( m_DefenceAcceptList.GetData( GuildIdx1 ) )
	{
		if( m_AttackGuildList.GetData( GuildIdx2 ) )
			return 4;
	}
	if( m_DefenceProposalList.GetData( GuildIdx1 ) )
	{
		if( m_AttackGuildList.GetData( GuildIdx2 ) )
			return 5;
	}
	
	if( m_AttackGuildList.GetData( GuildIdx2 ) )
	{
		if( m_CastleGuildIdx == GuildIdx1 )
			return 1;
		if( m_DefenceAcceptList.GetData( GuildIdx1 ) )
			return 2;
		if( m_DefenceProposalList.GetData( GuildIdx1 ) )
			return 3;
	}
	if( m_DefenceAcceptList.GetData( GuildIdx2 ) )
	{
		if( m_AttackGuildList.GetData( GuildIdx1 ) )
			return 4;
	}
	if( m_DefenceProposalList.GetData( GuildIdx2 ) )
	{
		if( m_AttackGuildList.GetData( GuildIdx1 ) )
			return 5;
	}

	return 0;
}


DWORD CSiegeWarMgr::GetVillageMapNum()
{
	for(DWORD i=0; i<m_SiegeMapCount; ++i)
	{
		if( m_SiegeWarMapNum[i][0] == g_pServerSystem->GetMapNum() ||
			m_SiegeWarMapNum[i][1] == g_pServerSystem->GetMapNum() )
			 return m_SiegeWarMapNum[i][0];
	}

	return 0;
}


DWORD CSiegeWarMgr::GetSiegeMapNum()
{
	for(DWORD i=0; i<m_SiegeMapCount; ++i)
	{
		if( m_SiegeWarMapNum[i][0] == g_pServerSystem->GetMapNum() ||
			m_SiegeWarMapNum[i][1] == g_pServerSystem->GetMapNum() )
				 return m_SiegeWarMapNum[i][1];
	}

	return 0;
}


BOOL CSiegeWarMgr::IsNeedLoadSiegeInfo()
{
	for(DWORD i=0; i<m_SiegeMapCount; ++i)
	{
		if( m_SiegeWarMapNum[i][0] == g_pServerSystem->GetMapNum() ||
			m_SiegeWarMapNum[i][1] == g_pServerSystem->GetMapNum() )
			return TRUE;
	}

	return FALSE;
}


BOOL CSiegeWarMgr::IsRegistTime( DWORD dwTime )
{
	stTIME time, ctime;
	time.SetTime( dwTime );
	ctime.SetTime( GetCurTime() );

	if( ctime > time )			return FALSE;

	stTIME t1, t2;
	t1.SetTime( time.GetYear(), time.GetMonth(), time.GetDay(), 0, 0, 0 );
	t2.SetTime( ctime.GetYear(), ctime.GetMonth(), ctime.GetDay(), 0, 0, 0 );
	t1 -= t2;

	if( t1.GetYear() || t1.GetMonth() )			return FALSE;

	SYSTEMTIME curtime;
	GetLocalTime( &curtime );
	if( curtime.wDayOfWeek == eGTDay_MONDAY )
	{
		if( t1.GetDay() == 5 || t1.GetDay() == 6 )
		if( time.GetHour() >= 14 && time.GetHour() <= 20 )
		{
			m_SiegeWarTime[1] = time;
			return TRUE;
		}
	}
	else if( curtime.wDayOfWeek == eGTDay_TUESDAY )
	{
		if( t1.GetDay() == 4 || t1.GetDay() == 5 )
		if( time.GetHour() >= 14 && time.GetHour() <= 20 )
		{
			m_SiegeWarTime[1] = time;
			return TRUE;
		}
	}

	return FALSE;
}


void CSiegeWarMgr::BreakUpGuild( DWORD GuildIdx )
{
	CGuild* pGuild = GUILDMGR->GetGuild( GuildIdx );
	if( !pGuild )			return;

	if( m_DefenceProposalList.GetData( GuildIdx ) )
		m_DefenceProposalList.Remove( GuildIdx );
	else if( m_DefenceAcceptList.GetData( GuildIdx ) )
		m_DefenceAcceptList.Remove( GuildIdx );
	else if( m_AttackGuildList.GetData( GuildIdx ) )
		m_AttackGuildList.Remove( GuildIdx );
}


BOOL CSiegeWarMgr::IsPossibleUnion( DWORD GuildIdx1, DWORD GuildIdx2 )
{
	CGuild* pGuild1 = GUILDMGR->GetGuild( GuildIdx1 );
	if( !pGuild1 )			return FALSE;
	CGuild* pGuild2 = GUILDMGR->GetGuild( GuildIdx2 );
	if( !pGuild2 )			return FALSE;

	if( GuildIdx1 == m_CastleGuildIdx || 
		m_DefenceProposalList.GetData( GuildIdx1 ) || 
		m_DefenceAcceptList.GetData( GuildIdx1 ) )
	{
		if( m_AttackGuildList.GetData( GuildIdx2 ) )
			return FALSE;
	}
	if( GuildIdx2 == m_CastleGuildIdx || 
		m_DefenceProposalList.GetData( GuildIdx2 ) || 
		m_DefenceAcceptList.GetData( GuildIdx2 ) )
	{
		if( m_AttackGuildList.GetData( GuildIdx1 ) )
			return FALSE;
	}

	return TRUE;		
}


void CSiegeWarMgr::CreateUnionCheck( DWORD UnionIdx )
{
	cPtrList	GuildList;
	m_CastleUnionIdx = UnionIdx;

	if( GUILDUNIONMGR->GetGuildListInUnion( UnionIdx, &GuildList ) )
	{
		BOOL bCastleGuild = FALSE;
		CGuild* pGuild = NULL;
		PTRLISTPOS pos = GuildList.GetHeadPosition();
		while( pos )
		{
			pGuild = (CGuild*)GuildList.GetNext( pos );
			if( !pGuild )		continue;

			if( m_CastleGuildIdx == pGuild->GetIdx() )
			{
				bCastleGuild = TRUE;
				break;
			}
		}

		if( bCastleGuild )
		{
			m_CastleUnionIdx = pGuild->GetGuildUnionIdx();

			PTRLISTPOS pos = GuildList.GetHeadPosition();
			while( pos )
			{
				pGuild = (CGuild*)GuildList.GetNext( pos );
				if( !pGuild )		continue;
				
				if( m_CastleGuildIdx != pGuild->GetIdx() )
				{
					m_DefenceAcceptList.Add( pGuild, pGuild->GetIdx() );
 					if( g_pServerSystem->GetMapNum() == SIEGEWARMGR->GetSiegeMapNum() )
					{
 						SiegeWarGuildInsert( m_SiegeWarIdx, pGuild->GetIdx(), eSWGuildState_DefenceGuild, GetSiegeMapNum() );
						SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), pGuild->GetIdx(), eSWLogKind_DefenceUnion, 0 );
					}
				}
			}
		}		
	}

	GuildList.RemoveAll();
}


void CSiegeWarMgr::AddAcceptGuild( DWORD GuildIdx )
{
	CGuild* pGuild = GUILDMGR->GetGuild( GuildIdx );
	if( !pGuild )			return;

	if( m_DefenceProposalList.GetData( GuildIdx ) )
		m_DefenceProposalList.Remove( GuildIdx );
	if( !m_DefenceAcceptList.GetData( GuildIdx ) )
		m_DefenceAcceptList.Add( pGuild, GuildIdx );

	if( g_pServerSystem->GetMapNum() == SIEGEWARMGR->GetSiegeMapNum() )
	{
		SiegeWarGuildInsert( m_SiegeWarIdx, GuildIdx, eSWGuildState_DefenceGuild, GetSiegeMapNum() );
		SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), GuildIdx, eSWLogKind_Defence, 0 );
	}
}


void CSiegeWarMgr::DeleteAcceptGuild( DWORD GuildIdx )
{
	CGuild* pGuild = GUILDMGR->GetGuild( GuildIdx );
	if( !pGuild )			return;

	if( pGuild->GetIdx() == m_CastleGuildIdx )
	{
		DWORD gl[30] = { 0, };
		int count = 0;

		CGuild* pAGuild = NULL;
		m_DefenceAcceptList.SetPositionHead();
		while( pAGuild = m_DefenceAcceptList.GetData() )
		{
			if( pAGuild->GetGuildUnionIdx() == pGuild->GetGuildUnionIdx() )
			{
				gl[count] = pGuild->GetIdx();
				++count;
			}
		}

		for(int i=0; i<count; ++i)
		{
			m_DefenceAcceptList.Remove( gl[i] );
			if( g_pServerSystem->GetMapNum() == SIEGEWARMGR->GetSiegeMapNum() )
			{
				SiegeWarGuildDelete( m_SiegeWarIdx, GuildIdx, GetSiegeMapNum() );
				SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), GuildIdx, eSWLogKind_CancelByCastleGuild, 0 );
			}
		}

		m_CastleUnionIdx = 0;
	}
	else
	{
		if( m_DefenceProposalList.GetData( GuildIdx ) )
			m_DefenceProposalList.Remove( GuildIdx );
		if( m_DefenceAcceptList.GetData( GuildIdx ) )
			m_DefenceAcceptList.Remove( GuildIdx );
		if( g_pServerSystem->GetMapNum() == SIEGEWARMGR->GetSiegeMapNum() )
		{
			SiegeWarGuildDelete( m_SiegeWarIdx, GuildIdx, GetSiegeMapNum() );
			SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), GuildIdx, eSWLogKind_CancelByUnionSecession, 0 );
		}
	}
}


void CSiegeWarMgr::DestoryCastleUnion()
{
	CGuild* pGuild = NULL;

	m_DefenceAcceptList.SetPositionHead();
	while( pGuild = m_DefenceAcceptList.GetData() )
	{
		if( pGuild->GetGuildUnionIdx() == m_CastleUnionIdx )
		{
			DeleteAcceptGuild( pGuild->GetIdx() );
		}
	}

	m_CastleUnionIdx = 0;
}



int CSiegeWarMgr::AddEngraveSyn( CPlayer* pPlayer, DWORD GuildIdx )
{
	CGuild* pGuild = GUILDMGR->GetGuild( GuildIdx );

	if( !pPlayer )												return eSWError_NoGuildMaster;
	if( pPlayer->GetBattleTeam() == 2 )							return eSWError_Observer;
	if( !pGuild )												return eSWError_NoGuildInfo;
	if( m_SiegeWarState != eSWState_SiegeWar )					return eSWError_NoSiegeWarTime;
	if( m_DefenceAcceptList.GetData( GuildIdx ) )				return eSWError_Error;
	if( pPlayer->GetGuildMemberRank() != GUILD_MASTER )			return eSWError_NoGuildMaster;
	if( m_EngraveIdx )											return eSWError_OtherPlayerEngrave;	
	if( pGuild->GetLevel() != 5 )								return eSWError_NoPerfectGuild;
	if( pGuild->GetLocation() != GetVillageMapNum() )			return eSWError_NoBaseVillage;
	if( GuildIdx == m_CastleGuildIdx )							return eSWError_CastleGuild;
	if( m_EngraveGuildIdx )										return eSWError_Error;
	if( m_AttackGuildList.GetData( GuildIdx ) == NULL )			return eSWError_Error;
	if( m_SiegeWarSuccessTimer )								return eSWError_Error;

	if( pPlayer->GetState() == eObjectState_Die )				return eSWError_Error;

	DWORD uniqueIdx = 0;
	if( GetSiegeMapNum() == nakyang_siege )
		uniqueIdx = m_SymbolIndex[NAKYANG_SYMBOL];

	STATIC_NPCINFO* pNpc = GAMERESRCMNGR->GetStaticNpcInfo( (WORD)uniqueIdx );
	if( pNpc )
	{
		VECTOR3 ObjectPos	= *CCharMove::GetPosition(pPlayer);
		VECTOR3 TObjectPos	= pNpc->vPos;
		DWORD	Distance	= (DWORD)CalcDistanceXZ( &ObjectPos, &TObjectPos );
		if(Distance > 1000.0f)
			return eSWError_Error;
	}

	m_EngraveIdx = pPlayer->GetID();
	m_EngraveTimer = 60000;	

	if( pPlayer->GetState() == eObjectState_Deal )
		OBJECTSTATEMGR_OBJ->EndObjectState( pPlayer, eObjectState_Deal );
	OBJECTSTATEMGR_OBJ->StartObjectState( pPlayer, eObjectState_Engrave, 0 );

	SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), GuildIdx, eSWLogKind_EngraveSyn, m_EngraveIdx );

	SEND_SW_ENGRAVE msg;
	SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_ENGRAVE_START );
	SafeStrCpy( msg.CharacterName, pPlayer->GetObjectName(), MAX_NAME_LENGTH+1 );
	SafeStrCpy( msg.GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME+1 );
	CObject* pObject = NULL;
	g_pUserTable->SetPositionHead();
	while( pObject = g_pUserTable->GetData() )
	{
		if( pObject->GetObjectKind() != eObjectKind_Player )			continue;

		SEND_SW_ENGRAVE tmsg = msg;
		pObject->SendMsg( &tmsg, sizeof(tmsg) );
	}

	return eSWError_NoError;
}



void CSiegeWarMgr::CancelEngraveIdx()
{
	if( !m_EngraveIdx )			return;


	CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( m_EngraveIdx );
	if( !pPlayer )				return;

	m_EngraveIdx = 0;
	m_EngraveTimer = 0;

	MSG_DWORD2 msg;
	SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_ENGRAVE_CANCEL );
	msg.dwData1 = pPlayer->GetID();				
	msg.dwData2 = 1;			
	PACKEDDATA_OBJ->QuickSend( pPlayer, &msg, sizeof(msg) );

	OBJECTSTATEMGR_OBJ->EndObjectState( pPlayer, eObjectState_Engrave );

	SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), pPlayer->GetGuildIdx(), eSWLogKind_EngraveNack, pPlayer->GetID() );
}


void CSiegeWarMgr::UserLogOut( CPlayer* pPlayer )
{
	if( pPlayer->GetID() == m_EngraveIdx )
	{
		m_EngraveIdx = 0;
		m_EngraveTimer = 0;
		MSG_DWORD2 msg;
		SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_ENGRAVE_CANCEL );
		msg.dwData1 = pPlayer->GetID();				
		msg.dwData2 = 3;						
		PACKEDDATA_OBJ->QuickSend( pPlayer, &msg, sizeof(msg) );
		OBJECTSTATEMGR_OBJ->EndObjectState( pPlayer, eObjectState_Engrave );

		SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), pPlayer->GetGuildIdx(), eSWLogKind_EngraveNackLogOut, pPlayer->GetID() );	
	}
}


BOOL CSiegeWarMgr::IsInSiegeWar( DWORD GuildIdx )
{
	if( GuildIdx == 0 )		return FALSE;

	if( GuildIdx == m_CastleGuildIdx )					return TRUE;
	if( m_DefenceProposalList.GetData( GuildIdx ) )		return TRUE;
	if( m_DefenceAcceptList.GetData( GuildIdx ) )		return TRUE;
	if( m_AttackGuildList.GetData( GuildIdx ) )			return TRUE;

	return FALSE;
}

void CSiegeWarMgr::GetSiegeWarTime( DWORD* pTime0, DWORD* pTime1 )
{
	*pTime0 = m_SiegeWarTime[0].value;
	*pTime1 = m_SiegeWarTime[1].value;
}


void CSiegeWarMgr::NetworkMsgParse( DWORD dwConnectionIndex, BYTE Protocol,void* pMsg )
{
	switch( Protocol )
	{
	case MP_SIEGEWAR_REGISTTIME_SYN:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )		return;
			
			MSG_DWORD errmsg;
			SetProtocol( &errmsg, MP_SIEGEWAR, MP_SIEGEWAR_REGISTTIME_NACK );
			if( pPlayer->GetGuildIdx() != m_CastleGuildIdx )
			{
				errmsg.dwData = eSWError_NoCastleGuild;
				goto FailedRegistSiegeWarTime;
			}
			if( pPlayer->GetGuildMemberRank() != GUILD_MASTER )
			{
				errmsg.dwData = eSWError_NoGuildMaster;
				goto FailedRegistSiegeWarTime;
			}
			if( m_SiegeWarState != eSWState_Proclamation )
			{
				errmsg.dwData = eSWError_NoProposalTime;
				goto FailedRegistSiegeWarTime;
			}

			if( IsRegistTime( pmsg->dwData ) )
			{
				MSG_DWORD smsg;
				SetProtocol( &smsg, MP_SIEGEWAR, MP_SIEGEWAR_REGISTTIME );				
				smsg.dwData = m_SiegeWarTime[1].value;				
				PACKEDDATA_OBJ->SendToMapServer( /*dwConnectionIndex,*/ (MAPTYPE)GetSiegeMapNum(), &smsg, sizeof(smsg) );

				SiegeWarInfoUpdate( m_SiegeWarIdx, m_SiegeWarTime[0].value, m_SiegeWarTime[1].value, m_GateInfo.value, GetSiegeMapNum() );


				MSGBASE msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_REGISTTIME_ACK );
				pPlayer->SendMsg( &msg, sizeof(msg) );
				return;
			}
			else
				errmsg.dwData = eSWError_FailedRegistSiegeWarTime;

FailedRegistSiegeWarTime:
			pPlayer->SendMsg( &errmsg, sizeof(errmsg) );
		}
		break;
	case MP_SIEGEWAR_REGISTTIME:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			m_SiegeWarTime[1].SetTime( pmsg->dwData );
		}
		break;
	case MP_SIEGEWAR_DEFENCE_REGIST_SYN:
		{
			MSGBASE* pmsg = (MSGBASE*)pMsg;		

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )			return;

			DWORD GuildIdx = pPlayer->GetGuildIdx();
			DWORD res = AddProposalGuildList( GuildIdx );
			if( res == eSWError_NoError )
			{
				MSGBASE msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_DEFENCE_REGIST_ACK );
				pPlayer->SendMsg( &msg, sizeof(msg) );

				MSG_DWORD smsg;
				SetProtocol( &smsg, MP_SIEGEWAR, MP_SIEGEWAR_DEFENCE_REGIST );
				smsg.dwData = GuildIdx;
				PACKEDDATA_OBJ->SendToMapServer( /*dwConnectionIndex,*/ (MAPTYPE)GetSiegeMapNum(), &smsg, sizeof(smsg) );

				SiegeWarGuildInsert( m_SiegeWarIdx, GuildIdx, eSWGuildState_DefenceProposalGuild, GetSiegeMapNum() );
				SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), GuildIdx, eSWLogKind_DefenceProposal, 0 );
			}
			else
			{
				MSG_DWORD msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_DEFENCE_REGIST_NACK );				
				msg.dwData = res;
				pPlayer->SendMsg( &msg, sizeof(msg) );
			}
		}
		break;
	case MP_SIEGEWAR_DEFENCE_REGIST:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			AddProposalGuildList( pmsg->dwData );
		}
		break;
	case MP_SIEGEWAR_ATTACK_REGIST_SYN:
		{
			MSGBASE* pmsg = (MSGBASE*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )			return;

			DWORD GuildIdx = pPlayer->GetGuildIdx();
			DWORD res = AddAttackGuildList( GuildIdx );
			if( res == eSWError_NoError )
			{
				MSG_DWORD msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_ATTACK_REGIST_ACK );
				pPlayer->SendMsg( &msg, sizeof(msg) );

				MSG_DWORD smsg;
				SetProtocol( &smsg, MP_SIEGEWAR, MP_SIEGEWAR_ATTACK_REGIST );				
				smsg.dwData = GuildIdx;
				PACKEDDATA_OBJ->SendToMapServer( /*dwConnectionIndex,*/ (MAPTYPE)GetSiegeMapNum(), &smsg, sizeof(smsg) );

				SiegeWarGuildInsert( m_SiegeWarIdx, GuildIdx, eSWGuildState_AttackGuild, GetSiegeMapNum() );
				SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), GuildIdx, eSWLogKind_Attack, 0 );
			}
			else
			{
				MSG_DWORD msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_ATTACK_REGIST_NACK );				
				msg.dwData = res;
				pPlayer->SendMsg( &msg, sizeof(msg) );
			}
		}
		break;
	case MP_SIEGEWAR_ATTACK_REGIST:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			AddAttackGuildList( pmsg->dwData );
		}
		break;
	case MP_SIEGEWAR_ACCEPTGUILD_SYN:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )			return;

			CGuild* pGuild = NULL;
			MSG_DWORD errmsg;
			SetProtocol( &errmsg, MP_SIEGEWAR, MP_SIEGEWAR_ACCEPTGUILD_NACK );
			if( m_SiegeWarState != eSWState_Acceptance )
			{
				errmsg.dwData = eSWError_NoAcceptTime;
				goto FailedAcceptProposalGuild;
			}
			if( pPlayer->GetGuildIdx() != m_CastleGuildIdx )
			{
				errmsg.dwData = eSWError_NoCastleGuild;
				goto FailedAcceptProposalGuild;
			}
			else if( pPlayer->GetGuildMemberRank() != GUILD_MASTER )
			{
				errmsg.dwData = eSWError_NoGuildMaster;
				goto FailedAcceptProposalGuild;
			}

			pGuild = GUILDMGR->GetGuild( pmsg->dwData );
			if( !pGuild )
			{
				errmsg.dwData = eSWError_NoGuildInfo;
				goto FailedAcceptProposalGuild;
			}			

			if( m_DefenceProposalList.GetData( pmsg->dwData ) && !m_DefenceAcceptList.GetData( pmsg->dwData ) )
			{
				m_DefenceProposalList.Remove( pmsg->dwData );
				m_DefenceAcceptList.Add( pGuild, pmsg->dwData );

				MSG_DWORD smsg;
				SetProtocol( &smsg, MP_SIEGEWAR, MP_SIEGEWAR_ACCEPTGUILD );
				smsg.dwData = pmsg->dwData;
				PACKEDDATA_OBJ->SendToMapServer( /*dwConnectionIndex,*/ (MAPTYPE)GetSiegeMapNum(), &smsg, sizeof(smsg) );

				SiegeWarGuildUpdate( m_SiegeWarIdx, pmsg->dwData, eSWGuildState_DefenceGuild, GetSiegeMapNum() );
				SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), pmsg->dwData, eSWLogKind_DefenceAccept, 0 );


				MSGBASE msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_ACCEPTGUILD_ACK );
				pPlayer->SendMsg( &msg, sizeof(msg) );
				return;
			}
			else
				errmsg.dwData = eSWError_NoProposalGuild;

FailedAcceptProposalGuild:
			pPlayer->SendMsg( &errmsg, sizeof(errmsg) );
		}
		break;
	case MP_SIEGEWAR_ACCEPTGUILD:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			CGuild* pGuild = GUILDMGR->GetGuild( pmsg->dwData );
			if( !pGuild )				return;
			
			if( m_DefenceProposalList.GetData( pmsg->dwData ) )
				m_DefenceProposalList.Remove( pmsg->dwData );

			m_DefenceAcceptList.Add( pGuild, pmsg->dwData );
		}
		break;
	case MP_SIEGEWAR_TAXRATE_SYN:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )		return;
			
			MSG_DWORD errmsg;
			SetProtocol( &errmsg, MP_SIEGEWAR, MP_SIEGEWAR_TAXRATE_NACK );
			if( pPlayer->GetGuildIdx() != m_CastleGuildIdx )
			{
				errmsg.dwData = eSWError_NoCastleGuild;
				pPlayer->SendMsg( &errmsg, sizeof(errmsg) );
				return;
			}
			else if( pPlayer->GetGuildMemberRank() != GUILD_MASTER )
			{
				errmsg.dwData = eSWError_NoGuildMaster;
				pPlayer->SendMsg( &errmsg, sizeof(errmsg) );
				return;
			}
			
			m_TaxRate = pmsg->dwData;

			SEND_AFFECTED_MAPLIST smsg;
			SetProtocol( &smsg, MP_SIEGEWAR, MP_SIEGEWAR_TAXRATE );
			smsg.Param = m_TaxRate;			// TaxRate
			for(DWORD i=0; i<m_SiegeMapCount; ++i)
			{
				if( m_SiegeWarMapNum[i][0] == g_pServerSystem->GetMapNum() )
				{
					for(int k=1; k<SIEGEWAR_MAX_AFFECTED_MAP; ++k)
					{
						if( m_SiegeWarMapNum[i][k] )
						{
							smsg.MapList[smsg.Count] = (WORD)m_SiegeWarMapNum[i][k];
							++smsg.Count;
						}
					}
					break;
				}
			}
			g_Network.Send2Server( dwConnectionIndex, (char*)&smsg, smsg.GetSize() );

			SiegeWarInfoUpdate( m_SiegeWarIdx, m_SiegeWarTime[0].value, m_SiegeWarTime[1].value, m_GateInfo.value, GetSiegeMapNum() );

			MSGBASE msg;
			SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_TAXRATE_ACK );
			pPlayer->SendMsg( &msg, sizeof(msg) );
		}
		break;
	case MP_SIEGEWAR_TAXRATE:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			m_TaxRate = pmsg->dwData;
		}
		break;
	case MP_SIEGEWAR_MOVEIN_SYN:
		{
			MSG_DWORD2* pmsg = (MSG_DWORD2*)pMsg;
			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )			return;
			
			MSG_DWORD msg;
			if( m_SiegeWarState == eSWState_SiegeWar )
			{
				if( pmsg->dwData2 == 0 )
				{
					if( m_CastleGuildIdx == pmsg->dwData1 || IsAcceptGuild( pmsg->dwData1 ) )
					{
						g_pServerSystem->RemovePlayer( pPlayer->GetID() );

						SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_MOVEIN_ACK );
						msg.dwData = GetSiegeMapNum();
						pPlayer->SendMsg( &msg, sizeof(msg) );
						return;
					}
				}
				else if( pmsg->dwData2 == 1 )
				{
					if( IsAttackGuild( pmsg->dwData1 ) )
					{
						g_pServerSystem->RemovePlayer( pPlayer->GetID() );

						SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_MOVEIN_ACK );
						msg.dwData = GetSiegeMapNum();
						pPlayer->SendMsg( &msg, sizeof(msg) );
						return;
					}
				}
				else if( pmsg->dwData2 == 2 )
				{
					g_pServerSystem->RemovePlayer( pPlayer->GetID() );

					SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_MOVEIN_ACK );
					msg.dwData = 0;
					pPlayer->SendMsg( &msg, sizeof(msg) );
					return;
				}
			}
			else
			{
				if( pmsg->dwData2 == 0 )
				{
					CGuild* pGuild = GUILDMGR->GetGuild( pmsg->dwData1 );
					if( pGuild )
					{
						if( pGuild->GetGuildUnionIdx() && m_CastleUnionIdx == pGuild->GetGuildUnionIdx() )
						{
							g_pServerSystem->RemovePlayer( pPlayer->GetID() );
							
							SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_MOVEIN_ACK );
							msg.dwData = pmsg->dwObjectID;
							pPlayer->SendMsg( &msg, sizeof(msg) );
							return;
						}
					}
				}
			}

			SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_MOVEIN_NACK );
			msg.dwData = pmsg->dwObjectID;
			pPlayer->SendMsg( &msg, sizeof(msg) );
		}
		break;
	case MP_SIEGEWAR_BATTLEJOIN_SYN:
		{
			SEND_SIEGEWAR_JOININFO* pmsg = (SEND_SIEGEWAR_JOININFO*)pMsg;

			CObject* pObject = g_pUserTable->FindUser(pmsg->dwObjectID);				
			if( pObject )
			{
				if( pObject->GetObjectKind() == eObjectKind_Player )
				{
					MSG_DWORD msg;
					SetProtocol( &msg, MP_USERCONN, MP_USERCONN_GAMEIN_NACK );
					msg.dwData = pmsg->dwObjectID;					
					g_Network.Send2Server( dwConnectionIndex, (char*)&msg, sizeof(msg) );
					return;
				}
			}
			
			CPlayer* pPlayer = g_pServerSystem->AddPlayer( pmsg->dwObjectID, dwConnectionIndex, pmsg->AgentIdx, 0 );
			if(!pPlayer) return;
			
			pPlayer->SetUserLevel( pmsg->UserLevel );
			//[过图黑屏修复][By:十里坡剑神][QQ:112582793][2017/12/5]
			CharacterNumSendAndCharacterInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterMugongInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterItemOptionInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterItemRareOptionInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterAbilityInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterSkinInfo(pPlayer->GetID());

			pPlayer->SetReturnMapNum( (WORD)pmsg->ReturnMapNum );
			pPlayer->SetObserverBattleIdx( pmsg->bObserver );
		}
		break;
	case MP_SIEGEWAR_OBSERVERJOIN_SYN:
		{
			SEND_SIEGEWAR_JOININFO* pmsg = (SEND_SIEGEWAR_JOININFO*)pMsg;

			CObject* pObject = g_pUserTable->FindUser(pmsg->dwObjectID);				
			if( pObject )
			{
				if( pObject->GetObjectKind() == eObjectKind_Player )
				{
					MSG_DWORD msg;
					SetProtocol( &msg, MP_USERCONN, MP_USERCONN_GAMEIN_NACK );
					msg.dwData		= pmsg->dwObjectID;					
					g_Network.Send2Server( dwConnectionIndex, (char*)&msg, sizeof(msg) );
					return;
				}
			}
			
			CPlayer* pPlayer = g_pServerSystem->AddPlayer( pmsg->dwObjectID, dwConnectionIndex, pmsg->AgentIdx, 0 );
			if(!pPlayer) return;
			
			pPlayer->SetUserLevel( pmsg->UserLevel );
			//[过图黑屏修复][By:十里坡剑神][QQ:112582793][2017/12/5]
			CharacterNumSendAndCharacterInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterMugongInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterItemOptionInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterItemRareOptionInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterAbilityInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterSkinInfo(pPlayer->GetID());

			pPlayer->SetReturnMapNum( (WORD)pmsg->ReturnMapNum );
			pPlayer->SetObserverBattleIdx( pmsg->bObserver );
		}
		break;
	case MP_SIEGEWAR_PROPOSALLIST_SYN:
		{
			MSGBASE* pmsg = (MSGBASE*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )			return;

			CGuild* pGuild = NULL;
			SEND_SW_PROPOSALGUILDLIST msg;			
			SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_PROPOSALLIST_ACK );
			MSG_DWORD failmsg;
			SetProtocol( &failmsg, MP_SIEGEWAR, MP_SIEGEWAR_PROPOSALLIST_NACK );

			pGuild = GUILDMGR->GetGuild( pPlayer->GetGuildIdx() );
			if( !pGuild )
			{
				failmsg.dwData = 1;
				goto PROPOSALLIST_SYN_FAILED;
			}
			if( pGuild->GetIdx() != m_CastleGuildIdx )
			{
				failmsg.dwData = 2;
				goto PROPOSALLIST_SYN_FAILED;
			}
			if( pPlayer->GetGuildMemberRank() != GUILD_MASTER )
			{
				failmsg.dwData = 3;
				goto PROPOSALLIST_SYN_FAILED;
			}
			
			m_DefenceProposalList.SetPositionHead();
			while( pGuild = m_DefenceProposalList.GetData() )
			{				
				msg.GuildList[msg.Count].Info.GuildIdx = pGuild->GetIdx();
				msg.GuildList[msg.Count].Info.Type = eSWGuildState_DefenceProposalGuild;
				strncpy( msg.GuildList[msg.Count].GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME );
				++msg.Count;
			}

			pPlayer->SendMsg( &msg, msg.GetSize() );
			return;

PROPOSALLIST_SYN_FAILED:
			pPlayer->SendMsg( &failmsg, sizeof(failmsg) );
		}
		break;
	case MP_SIEGEWAR_GUILDLIST_SYN:
		{
			MSGBASE* pmsg = (MSGBASE*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )			return;

			CGuild* pGuild = NULL;
			SEND_SW_GUILDLIST msg;
			SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_GUILDLIST_ACK );
			
			DWORD count = 0;
			
			pGuild = GUILDMGR->GetGuild( m_CastleGuildIdx );
			if( pGuild )
			{
				msg.GuildList[count].Info.GuildIdx = pGuild->GetIdx();
				msg.GuildList[count].Info.Type = eSWGuildState_CastleGuild;
				strncpy( msg.GuildList[count].GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME );
				++msg.DefenceCount;
				++count;
			}
			m_DefenceAcceptList.SetPositionHead();
			while( pGuild = m_DefenceAcceptList.GetData() )
			{
				msg.GuildList[count].Info.GuildIdx = pGuild->GetIdx();
				if( m_CastleUnionIdx && pGuild->GetGuildUnionIdx() == m_CastleUnionIdx )
					msg.GuildList[count].Info.Type = eSWGuildState_DefenceGuild;
				else					
					msg.GuildList[count].Info.Type = eSWGuildState_DefenceGuild;
				strncpy( msg.GuildList[count].GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME );
				++msg.DefenceCount;
				++count;
			}
			m_AttackGuildList.SetPositionHead();
			while( pGuild = m_AttackGuildList.GetData() )
			{
				msg.GuildList[count].Info.GuildIdx = pGuild->GetIdx();
				msg.GuildList[count].Info.Type = eSWGuildState_AttackGuild;
				strncpy( msg.GuildList[count].GuildName, pGuild->GetGuildName(), MAX_GUILD_NAME );
				++msg.AttackCount;
				++count;
			}

			pPlayer->SendMsg( &msg, msg.GetSize() );
		}
		break;
	case MP_SIEGEWAR_LEAVE_SYN:	
		{
			MSG_DWORD3* pmsg = (MSG_DWORD3*)pMsg;
						
			CObject* pObject = g_pUserTable->FindUser(pmsg->dwObjectID);
			if( pObject != NULL )
			{
				if( pObject->GetObjectKind() == eObjectKind_Player )
				{
					MSG_DWORD msg;
					SetProtocol( &msg, MP_GTOURNAMENT, MP_USERCONN_GAMEIN_NACK );
					msg.dwData	= pmsg->dwObjectID;					
					g_Network.Send2Server( dwConnectionIndex, (char*)&msg, sizeof(msg) );
					return;
				}
			}
			
			CPlayer* pPlayer = g_pServerSystem->AddPlayer( pmsg->dwObjectID, dwConnectionIndex, pmsg->dwData1, pmsg->dwData3 );
			if(!pPlayer) return;
			
			pPlayer->SetUserLevel( pmsg->dwData2 );
			//[过图黑屏修复][By:十里坡剑神][QQ:112582793][2017/12/5]
			CharacterNumSendAndCharacterInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterMugongInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterItemOptionInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterItemRareOptionInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterAbilityInfo(pmsg->dwObjectID, MP_USERCONN_GAMEIN_SYN);
			CharacterSkinInfo(pPlayer->GetID());

			QuestTotalInfo(pmsg->dwObjectID);

			QUESTMGR->CreateQuestForPlayer( pPlayer );
			QuestSubQuestLoad(pmsg->dwObjectID);
		}
		break;
	case MP_SIEGEWAR_ENGRAVE_SYN:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )			return;

			int res = AddEngraveSyn( pPlayer, pmsg->dwData );
			if( res == eSWError_NoError )
			{
				MSG_DWORD2 msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_ENGRAVE_ACK );
				msg.dwData1 = 0;
				msg.dwData2 = pPlayer->GetID();
				PACKEDDATA_OBJ->QuickSend( pPlayer, &msg, sizeof(msg) );
			}
			else
			{
				MSG_DWORD msg;
				SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_ENGRAVE_NACK );
				msg.dwData = res;
				pPlayer->SendMsg( &msg, sizeof(msg) );
			}
		}
		break;
	case MP_SIEGEWAR_RESTRAINT_SYN:
		{
			MSGBASE* pmsg = (MSGBASE*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )			return;

			MSG_DWORD msg;
			SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_RESTRAINT_NACK );
			if( g_pServerSystem->GetMapNum() != GetSiegeMapNum() )
			{
				msg.dwData = 1;
				pPlayer->SendMsg( &msg, sizeof(msg) );
				return;
			}
			if( m_SiegeWarState != eSWState_SiegeWar )
			{
				msg.dwData = 2;
				pPlayer->SendMsg( &msg, sizeof(msg) );
				return;
			}
			if( pPlayer->GetBattleTeam() == 2 )
			{
				msg.dwData = 3;
				pPlayer->SendMsg( &msg, sizeof(msg) );
				return;
			}

			MSG_DWORD ackmsg;		

			if( pPlayer->IsRestraintMode() )
			{
				SetProtocol( &ackmsg, MP_SIEGEWAR, MP_SIEGEWAR_RESTRAINT_OFF );
				pPlayer->SetRestraintMode( FALSE );
			}
			else
			{
				SetProtocol( &ackmsg, MP_SIEGEWAR, MP_SIEGEWAR_RESTRAINT_ON );
				pPlayer->SetRestraintMode( TRUE );
			}

			ackmsg.dwData = pPlayer->GetID();
			PACKEDDATA_OBJ->QuickSend( pPlayer, &ackmsg, sizeof(ackmsg) );			
		}
		break;
	case MP_SIEGEWAR_ENGRAVE_CANCEL:
		{
			MSGBASE* pmsg = (MSGBASE*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )			return;


			MSG_DWORD2 msg;
			SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_ENGRAVE_CANCEL );
			msg.dwData1 = pPlayer->GetID();

			CGuild* pGuild = GUILDMGR->GetGuild( pPlayer->GetGuildIdx() );
			if( !pGuild )
				msg.dwData2 = 2;

			if( pPlayer->GetBattleTeam() != eBattleTeam2 )
				msg.dwData2 = 2;

			if( pPlayer->GetID() == m_EngraveIdx )
			{
				msg.dwData2 = 3;

				m_EngraveIdx = 0;
				m_EngraveTimer = 0;

				if( OBJECTSTATEMGR_OBJ->GetObjectState( pPlayer ) == eObjectState_Engrave )
				{
					OBJECTSTATEMGR_OBJ->EndObjectState( pPlayer, eObjectState_Engrave );
					SiegeWarAddLog( m_SiegeWarIdx, GetSiegeMapNum(), pPlayer->GetGuildIdx(), eSWLogKind_EngraveNackCancel, pPlayer->GetID() );
				}
			}

			
			PACKEDDATA_OBJ->QuickSend( pPlayer, &msg, sizeof(msg) );
		}
		break;
	case MP_SIEGEWAR_CHANGESTATE:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			m_SiegeWarState = pmsg->dwData;
				
			switch( m_SiegeWarState )
			{
			case eSWState_Before:
			case eSWState_Proclamation:
			case eSWState_Acceptance:
			case eSWState_BeforeSiegeWar:
			case eSWState_SiegeWar:
				{
					CObject* pObject = NULL;
					g_pUserTable->SetPositionHead();
					while( pObject = g_pUserTable->GetData() )
					{
						if( pObject->GetObjectKind() != eObjectKind_Player )			continue;
						
						MSG_DWORD tmsg = *pmsg;
						pObject->SendMsg( &tmsg, sizeof(tmsg) );
					}
				}
				break;
			case eSWState_EndSiegeWar:
				{
					m_DefenceProposalList.RemoveAll();
					m_DefenceAcceptList.RemoveAll();
					m_AttackGuildList.RemoveAll();
					m_AttackUnionList.RemoveAll();

					CGuild* pGuild = NULL;
					cPtrList GuildList;
					if( GUILDUNIONMGR->GetGuildListInUnion( m_CastleUnionIdx, &GuildList ) )
					{
						PTRLISTPOS pos = GuildList.GetHeadPosition();
						while( pos )
						{
							pGuild = (CGuild*)GuildList.GetNext( pos );
							if( !pGuild )			continue;

							if( pGuild->GetIdx() == m_CastleGuildIdx )		continue;

							m_DefenceAcceptList.Add( pGuild, pGuild->GetIdx() );
						}
					}
					SiegeWarInfoLoad( 5 );
					++m_SiegeWarIdx;
				}
				break;
			}
		}
		break;
	case MP_SIEGEWAR_BATTLEINFO:
		{
			SEND_SW_GUILDLIST* pmsg = (SEND_SW_GUILDLIST*)pMsg;

			if( g_pServerSystem->GetMapNum() != GetVillageMapNum() )		return;
			
			m_DefenceProposalList.RemoveAll();
			m_DefenceAcceptList.RemoveAll();
			m_AttackGuildList.RemoveAll();
			m_AttackUnionList.RemoveAll();
			m_CastleUnionIdx = 0;
			m_CastleGuildIdx = 0;

			CGuild* pGuild = NULL;
			for(int i=0; i<pmsg->DefenceCount; ++i)
			{
				pGuild = GUILDMGR->GetGuild( pmsg->GuildList[i].Info.GuildIdx );
				if( !pGuild )			continue;

				if( i == 0 )
				{
					m_CastleGuildIdx = pGuild->GetIdx();
					m_CastleUnionIdx = pGuild->GetGuildUnionIdx();
				}
				else
					m_DefenceAcceptList.Add( pGuild, pGuild->GetIdx() );
			}
			for(int i=0; i<pmsg->AttackCount; ++i)
			{
				pGuild = GUILDMGR->GetGuild( pmsg->GuildList[ pmsg->DefenceCount+i ].Info.GuildIdx );
				if( !pGuild )			continue;

				m_AttackGuildList.Add( pGuild, pGuild->GetIdx() );
			}
		}
		break;		
	case MP_SIEGEWAR_TIMEINFO_SYN:
		{
			MSGBASE* pmsg = (MSGBASE*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )		return;

			DWORD dwTime0 = 0;
			DWORD dwTime1 = 0;
			GetSiegeWarTime( &dwTime0, &dwTime1 );

			MSG_DWORD2 Msg;
			Msg.Category = MP_SIEGEWAR;
			Msg.dwData1 = dwTime0;
			Msg.dwData2 = dwTime1;
			if( dwTime0 || dwTime1 )
			{
				Msg.Protocol = MP_SIEGEWAR_TIMEINFO_ACK;
			}
			else
			{
				Msg.Protocol = MP_SIEGEWAR_TIMEINFO_NACK;
			}
			pPlayer->SendMsg( &Msg, sizeof(Msg) );
		}
		break;
		
	case MP_SIEGEWAR_UPGRADE_GATE_SYN:
		{
			MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;

			CPlayer* pPlayer = (CPlayer*)g_pUserTable->FindUser( pmsg->dwObjectID );
			if( !pPlayer )		return;

			MSG_DWORD MsgNack;
			MsgNack.Category = MP_SIEGEWAR;
			MsgNack.Protocol = MP_SIEGEWAR_UPGRADE_GATE_NACK;
			if( pPlayer->GetGuildIdx() == 0 )
			{
				MsgNack.dwData = 0;
				pPlayer->SendMsg( &MsgNack, sizeof(MsgNack) );
				return;
			}
			if( pPlayer->GetGuildMemberRank() != GUILD_MASTER )
			{
				MsgNack.dwData = 1;
				pPlayer->SendMsg( &MsgNack, sizeof(MsgNack) );
				return;
			}
			if( pPlayer->GetGuildIdx() != m_CastleGuildIdx )
			{
				MsgNack.dwData = 2;
				pPlayer->SendMsg( &MsgNack, sizeof(MsgNack) );
				return;
			}
			DWORD dwMoney = 0;
			DWORD dwCastleIdx = 0;
			DWORD dwLevel = 0;
			switch( pmsg->dwData )
			{
			case 1:	{	dwMoney = 1500000;	dwCastleIdx = 1001;	dwLevel = 1;	}	break;
			case 2:	{	dwMoney = 2500000;	dwCastleIdx = 1001;	dwLevel = 2;	}	break;
			case 3:	{	dwMoney = 4000000;	dwCastleIdx = 1001;	dwLevel = 3;	}	break;
			case 4:	{	dwMoney = 1000000;	dwCastleIdx = 1000;	dwLevel = 1;	}	break;
			case 5:	{	dwMoney = 2000000;	dwCastleIdx = 1000;	dwLevel = 2;	}	break;
			case 6:	{	dwMoney = 3000000;	dwCastleIdx = 1000;	dwLevel = 3;	}	break;
			}
			if( pPlayer->GetMoney() < dwMoney )
			{
				MsgNack.dwData = 3;
				pPlayer->SendMsg( &MsgNack, sizeof(MsgNack) );
				return;
			}
			if( m_GateInfo.GetLevel( dwCastleIdx ) >= dwLevel )
			{
				MsgNack.dwData = 4;
				pPlayer->SendMsg( &MsgNack, sizeof(MsgNack) );
				return;
			}
			if( m_SiegeWarState != eSWState_Acceptance )
			{
				MsgNack.dwData = 5;
				pPlayer->SendMsg( &MsgNack, sizeof(MsgNack) );
				return;
			}
			m_GateInfo.SetLevel( dwCastleIdx, dwLevel );

			pPlayer->SetMoney( dwMoney, MONEY_SUBTRACTION );

			MSG_DWORD Msg;
			Msg.Category = MP_SIEGEWAR;
			Msg.Protocol = MP_SIEGEWAR_UPGRADE_GATE_ACK;
			Msg.dwData = pmsg->dwData;
			pPlayer->SendMsg( &Msg, sizeof(Msg) );

			g_pServerTable->SetPositionHead();
 			SERVERINFO* pAgentInfo = g_pServerTable->GetNextAgentServer();
 			if(pAgentInfo == NULL)
  			{
  				ASSERT(0);
 				return;
 			}
			MSG_WORD2 msg2;
			SetProtocol( &msg2, MP_SIEGEWAR, MP_SIEGEWAR_UPGRADE_GATE );
			msg2.wData1 = (WORD)dwCastleIdx;
			msg2.wData2 = (WORD)dwLevel;
			PACKEDDATA_OBJ->SendToMapServer( /*pAgentInfo->dwConnectionIndex,*/ (WORD)GetSiegeMapNum(), &msg2, sizeof(msg2) );
		}
		break;
	case MP_SIEGEWAR_UPGRADE_GATE:
		{
			MSG_WORD2* pmsg = (MSG_WORD2*)pMsg;

			m_GateInfo.SetLevel( pmsg->wData1, pmsg->wData2 );
			SiegeWarInfoUpdate( m_SiegeWarIdx, m_SiegeWarTime[0].value, m_SiegeWarTime[1].value, m_GateInfo.value, GetSiegeMapNum() );
		}
		break;
	case MP_SIEGEWAR_CHEAT:	
		{
			MSG_DWORD4* pmsg = (MSG_DWORD4*)pMsg;

			switch( pmsg->dwData1 )
			{
			case 1:
				{
					m_SiegeWarState = eSWState_Before;
					m_EngraveIdx = 0;
					m_EngraveTimer = 0;

					m_DefenceProposalList.RemoveAll();
					m_DefenceAcceptList.RemoveAll();
					m_AttackGuildList.RemoveAll();
					m_AttackUnionList.RemoveAll();

					stTIME tmp;
					m_SiegeWarTime[0].SetTime( GetCurTime() );
					tmp.SetTime( 0, 0, 0, 0, 0, 10 );
					m_SiegeWarTime[0] += tmp;

					tmp.SetTime( 0, 0, 0, 0, 15, 0 );
					m_SiegeWarTime[1] = m_SiegeWarTime[0];
					m_SiegeWarTime[1] += tmp;

					SiegeWarInfoUpdate( m_SiegeWarIdx, m_SiegeWarTime[0].value, m_SiegeWarTime[1].value, m_GateInfo.value, GetSiegeMapNum() );
				}
				break;
			case 2:
				{
					CGuild* pGuild = GUILDMGR->GetGuild( pmsg->dwData4 );
					if( !pGuild )		return;

					m_CastleGuildIdx = pGuild->GetIdx();
					m_CastleUnionIdx = pGuild->GetGuildUnionIdx();

					cPtrList GuildList;
					if( GUILDUNIONMGR->GetGuildListInUnion( m_CastleUnionIdx, &GuildList ) )
					{
						PTRLISTPOS pos = GuildList.GetHeadPosition();
						while( pos )
						{
							pGuild = (CGuild*)GuildList.GetNext( pos );
							if( !pGuild )			continue;

							if( pGuild->GetIdx() == m_CastleGuildIdx )		continue;

							m_DefenceAcceptList.Add( pGuild, pGuild->GetIdx() );
						}
					}
				}
				break;
			case 3:
				{
					m_SiegeWarTime[1].SetTime( GetCurTime() );
					m_SiegeWarState = eSWState_SiegeWar;
					SiegeWarInfoUpdate( m_SiegeWarIdx, m_SiegeWarTime[0].value, m_SiegeWarTime[1].value, m_GateInfo.value, GetSiegeMapNum() );

					BATTLE_INFO_SIEGEWAR Info;
					memset( &Info, 0, sizeof(BATTLE_INFO_SIEGEWAR) );

					Info.BattleKind	= eBATTLE_KIND_SIEGEWAR;
					Info.BattleState = eBATTLE_STATE_READY;
					Info.BattleTime	= 0;

					DWORD count = 0;
					CGuild* pGuild = NULL;
					if( m_CastleGuildIdx )
					{
						Info.GuildList[count] = m_CastleGuildIdx;
						++Info.DefenceCount;
						++count;
					}
					m_DefenceAcceptList.SetPositionHead();
					while( pGuild = m_DefenceAcceptList.GetData() )
					{
						Info.GuildList[count] = pGuild->GetIdx();
						++Info.DefenceCount;
						++count;
					}
					m_AttackGuildList.SetPositionHead();
					while( pGuild = m_AttackGuildList.GetData() )
					{
						Info.GuildList[count] = pGuild->GetIdx();
						++Info.AttackCount;
						++count;
					}
					
					if( m_pBattle )
						m_pBattle->SetBattleInfo( &Info );
				}
				break;
			case 4:
				{
					m_SiegeWarTime[0].SetTime( GetCurTime() );
					stTIME st, tp;
					st.SetTime( GetCurTime() );
					tp.SetTime( 0, 0, 0, 0, 3, 10 );
					st += tp;
					m_SiegeWarTime[1] = st;
					m_SiegeWarState = eSWState_Proclamation;

					MSG_DWORD msg;
					SetProtocol( &msg, MP_SIEGEWAR, MP_SIEGEWAR_CHANGESTATE );
					msg.dwData = m_SiegeWarState;
					CObject* pObject = NULL;
					g_pUserTable->SetPositionHead();
					while( pObject = g_pUserTable->GetData() )
					{
						if( pObject->GetObjectKind() != eObjectKind_Player )			continue;

						MSG_DWORD tmsg = msg;
						pObject->SendMsg( &tmsg, sizeof(tmsg) );
					}
				}
				break;
			case 5:
				{
					m_pBattle->ReturnToMapAllPlayer();
				}
				break;
			case 6:
				{
					
				}
				break;
			}
		}
		break;
	case MP_SIEGEWAR_FLAGCHANGE:
			{
				MSG_DWORD* pmsg = (MSG_DWORD*)pMsg;
				m_SiegeWarState = pmsg->dwData;
			}
			break;
	}
}

void CSiegeWarMgr::CheckDateforFlagNPC()
{
	static SYSTEMTIME st;
	GetLocalTime(&st);

	if( st.wDayOfWeek != m_wCurDay )
	{
		MSG_DWORD msg;
		msg.Category = MP_SIEGEWAR;
		msg.Protocol = MP_SIEGEWAR_FLAGCHANGE;
		msg.dwData = GAMERESRCMNGR->GetFlagFromDate(eSGFlg, st.wDayOfWeek);

		g_Network.Broadcast2AgentServer( (char*)&msg, sizeof(msg) );

		m_wCurDay = st.wDayOfWeek;
	}
}
