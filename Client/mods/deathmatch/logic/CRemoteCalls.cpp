/*****************************************************************************
*
*  PROJECT:     Multi Theft Auto v1.0
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        mods/deathmatch/logic/CRemoteCalls.cpp
*  PURPOSE:     Remote HTTP call (callRemote) class
*  DEVELOPERS:  Ed Lyons <>
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#include "StdInc.h"

using std::list;

CRemoteCalls::CRemoteCalls()
{
    
}

CRemoteCalls::~CRemoteCalls()
{
    list< CRemoteCall* >::iterator iter = m_calls.begin ();
    for ( ; iter != m_calls.end (); iter++ )
    {
        delete (*iter);
    }

    m_calls.clear();
}


void CRemoteCalls::Call ( const char * szServerHost, const char * szResourceName, const char * szFunctionName, CLuaArguments * arguments, CLuaMain * luaMain, const CLuaFunctionRef& iFunction, const SString& strQueueName, uint uiConnectionAttempts, uint uiConnectTimeoutMs )
{
    m_calls.push_back ( new CRemoteCall ( szServerHost, szResourceName, szFunctionName, arguments, luaMain, iFunction, strQueueName, uiConnectionAttempts, uiConnectTimeoutMs ) );
    m_calls.back ()->MakeCall ();
}

void CRemoteCalls::Call ( const char * szURL, CLuaArguments * arguments, CLuaMain * luaMain, const CLuaFunctionRef& iFunction, const SString& strQueueName, uint uiConnectionAttempts, uint uiConnectTimeoutMs )
{
    m_calls.push_back ( new CRemoteCall ( szURL, arguments, luaMain, iFunction, strQueueName, uiConnectionAttempts, uiConnectTimeoutMs ) );
    m_calls.back ()->MakeCall ();
}

void CRemoteCalls::Call ( const char * szURL, CLuaArguments * fetchArguments, const SString& strPostData, bool bPostBinary, CLuaMain * luaMain, const CLuaFunctionRef& iFunction, const SString& strQueueName, uint uiConnectionAttempts, uint uiConnectTimeoutMs )
{
    m_calls.push_back ( new CRemoteCall ( szURL, fetchArguments, strPostData, bPostBinary, luaMain, iFunction, strQueueName, uiConnectionAttempts, uiConnectTimeoutMs ) );
    m_calls.back ()->MakeCall ();
}

void CRemoteCalls::Remove ( CLuaMain * lua )
{
    list<CRemoteCall *> trash;
    list< CRemoteCall* >::iterator iter = m_calls.begin ();
    for ( ; iter != m_calls.end (); iter++ )
    {
        if ( (*iter)->GetVM() == lua )
        {
            trash.push_back((*iter));
        }
    }

    iter = trash.begin ();
    for ( ; iter != trash.end (); iter++ )
    {
        m_calls.remove ( (*iter));
        delete (*iter);
    }
}

void CRemoteCalls::Remove ( CRemoteCall * call )
{
    m_calls.remove(call);
    delete call;
}

bool CRemoteCalls::CallExists ( CRemoteCall * call )
{
    list< CRemoteCall* >::iterator iter = m_calls.begin ();
    for ( ; iter != m_calls.end (); iter++ )
    {
        if ( (*iter) == call )
            return true;
    }
    return false;
}

// Map queue index into download manager id
EDownloadModeType CRemoteCalls::GetDownloadModeFromQueueIndex( uint uiIndex, bool bAnyHost )
{
    uiIndex %= MAX_CALL_REMOTE_QUEUES;
    uiIndex += bAnyHost ? EDownloadMode::CALL_REMOTE_ANY_HOST : EDownloadMode::CALL_REMOTE_RESTRICTED;
    return (EDownloadModeType)uiIndex;
}

// Map queue name to download manager id
EDownloadModeType CRemoteCalls::GetDownloadModeForQueueName( const SString& strQueueName, bool bAnyHost )
{
    uint* pIndex = MapFind( m_QueueIndexMap, strQueueName );
    if ( pIndex )
    {
        return GetDownloadModeFromQueueIndex( *pIndex, bAnyHost );
    }
    else
    {
        // Find lowest unused index
        uint idx = 0;
        while( MapContainsValue( m_QueueIndexMap, idx ) )
        {
            idx++;
        }
        // Add new mapping
        MapSet( m_QueueIndexMap, strQueueName, idx );
        return GetDownloadModeFromQueueIndex( idx, bAnyHost );
    }
}


void CRemoteCalls::ProcessQueuedFiles( void )
{
    for ( auto iter = m_QueueIndexMap.cbegin(); iter != m_QueueIndexMap.cend(); )
    {
        EDownloadModeType downloadMode = GetDownloadModeFromQueueIndex( iter->second, false );
        EDownloadModeType downloadModeAnyIp = GetDownloadModeFromQueueIndex( iter->second, true );
        if ( g_pNet->GetHTTPDownloadManager( downloadMode )->ProcessQueuedFiles()
          && g_pNet->GetHTTPDownloadManager( downloadModeAnyIp )->ProcessQueuedFiles() )
        {
            // Queue empty, so remove name mapping if not default queue
            if ( iter->first != CALL_REMOTE_DEFAULT_QUEUE_NAME )
            {
                iter = m_QueueIndexMap.erase( iter );
                continue;
            }
        }
        ++iter;
    }
}

////////////////////////////////////////////////////////////////////////////////

CRemoteCall::CRemoteCall ( const char * szServerHost, const char * szResourceName, const char * szFunctionName, CLuaArguments * arguments, CLuaMain * luaMain, const CLuaFunctionRef& iFunction, const SString& strQueueName, uint uiConnectionAttempts, uint uiConnectTimeoutMs )
{
    m_VM = luaMain;
    m_iFunction = iFunction;

    arguments->WriteToJSONString ( m_strData, true );
    m_bPostBinary = false;
    m_bIsFetch = false;

    m_strURL = SString ( "http://%s/%s/call/%s", szServerHost, szResourceName, szFunctionName );
    m_strQueueName = strQueueName;
    m_uiConnectionAttempts = uiConnectionAttempts;
    m_uiConnectTimeoutMs = uiConnectTimeoutMs;
}

//arbitary URL version
CRemoteCall::CRemoteCall ( const char * szURL, CLuaArguments * arguments, CLuaMain * luaMain, const CLuaFunctionRef& iFunction, const SString& strQueueName, uint uiConnectionAttempts, uint uiConnectTimeoutMs )
{
    m_VM = luaMain;
    m_iFunction = iFunction;

    arguments->WriteToJSONString ( m_strData, true );
    m_bPostBinary = false;
    m_bIsFetch = false;

    m_strURL = szURL;
    m_strQueueName = strQueueName;
    m_uiConnectionAttempts = uiConnectionAttempts;
    m_uiConnectTimeoutMs = uiConnectTimeoutMs;
}

//Fetch version
CRemoteCall::CRemoteCall ( const char * szURL, CLuaArguments * fetchArguments, const SString& strPostData, bool bPostBinary, CLuaMain * luaMain, const CLuaFunctionRef& iFunction, const SString& strQueueName, uint uiConnectionAttempts, uint uiConnectTimeoutMs )
    : m_FetchArguments ( *fetchArguments )
{
    m_VM = luaMain;
    m_iFunction = iFunction;

    m_strData = strPostData;
    m_bPostBinary = bPostBinary;
    m_bIsFetch = true;

    m_strURL = szURL;
    m_strQueueName = strQueueName;
    m_uiConnectionAttempts = uiConnectionAttempts;
    m_uiConnectTimeoutMs = uiConnectTimeoutMs;
}


CRemoteCall::~CRemoteCall () 
{
}

void CRemoteCall::MakeCall()
{
    // Bypass net module IP check if we are allowed to access the URL
    bool bAnyHost = (g_pCore->GetWebCore()->GetDomainState(g_pCore->GetWebCore()->GetDomainFromURL(m_strURL)) == eURLState::WEBPAGE_ALLOWED);
    EDownloadModeType downloadMode = g_pClientGame->GetRemoteCalls()->GetDownloadModeForQueueName(m_strQueueName, bAnyHost);
    CNetHTTPDownloadManagerInterface* pDownloadManager = g_pNet->GetHTTPDownloadManager(downloadMode);
    pDownloadManager->QueueFile(m_strURL, NULL, 0, m_strData.c_str(), m_strData.length(), m_bPostBinary, this, DownloadFinishedCallback, false, m_uiConnectionAttempts, m_uiConnectTimeoutMs);
}

void CRemoteCall::DownloadFinishedCallback( char * data, size_t dataLength, void * obj, bool bSuccess, int iErrorCode )
{
    //printf("Progress: %s\n", data);
    if ( bSuccess )
    {
        CRemoteCall * call = (CRemoteCall*)obj;
        if ( g_pClientGame->GetRemoteCalls()->CallExists(call) )
        {
            //printf("RECIEVED: %s\n", data);
            CLuaArguments arguments;
            if ( call->IsFetch () )
            {
                arguments.PushString ( std::string ( data, dataLength ) );
                arguments.PushNumber ( 0 );
                for ( uint i = 0 ; i < call->GetFetchArguments ().Count () ; i++ )
                    arguments.PushArgument ( *( call->GetFetchArguments ()[i] ) );
            }
            else
                arguments.ReadFromJSONString ( data );

            arguments.Call ( call->m_VM, call->m_iFunction);   

            g_pClientGame->GetRemoteCalls()->Remove(call); // delete ourselves
        }
    }
    else
    {
        CRemoteCall * call = (CRemoteCall*)obj;
        if ( g_pClientGame->GetRemoteCalls()->CallExists(call) )
        {
            CLuaArguments arguments;
            arguments.PushString("ERROR");
            arguments.PushNumber( iErrorCode );
            if ( call->IsFetch () )
                for ( uint i = 0 ; i < call->GetFetchArguments ().Count () ; i++ )
                    arguments.PushArgument ( *( call->GetFetchArguments ()[i] ) );

            arguments.Call ( call->m_VM, call->m_iFunction);   

            g_pClientGame->GetRemoteCalls()->Remove(call); // delete ourselves
        }
    }
}
