/*****************************************************************************
*
*  PROJECT:     Multi Theft Auto v1.0
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        mods/deathmatch/logic/CRegisteredCommands.cpp
*  PURPOSE:     Registered (lua) command manager class
*  DEVELOPERS:  Christian Myhre Lundheim <>
*               Ed Lyons <>
*               Jax <>
*               Oliver Brown <>
*               Alberto Alonso <rydencillo@gmail.com>
*               lil_Toady <>
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#include "StdInc.h"

CRegisteredCommands::CRegisteredCommands ( CAccessControlListManager* pACLManager )
{
    m_pACLManager = pACLManager;
    m_bIteratingList = false;
}


CRegisteredCommands::~CRegisteredCommands ( void )
{
    ClearCommands ();
}


bool CRegisteredCommands::AddCommand ( CLuaMain* pLuaMain, const char* szKey, const CLuaFunctionRef& iLuaFunction, bool bRestricted, bool bCaseSensitive )
{
    assert ( pLuaMain );
    assert ( szKey );

    // Check if we already have this key and handler
    SCommand* pCommand = GetCommand ( szKey, pLuaMain );

    if ( pCommand && iLuaFunction == pCommand->iLuaFunction )
        return false;

    // Create the entry
    pCommand = new SCommand;
    pCommand->pLuaMain = pLuaMain;
    pCommand->strKey.AssignLeft ( szKey, MAX_REGISTERED_COMMAND_LENGTH );
    pCommand->iLuaFunction = iLuaFunction;
    pCommand->bRestricted = bRestricted;
    pCommand->bCaseSensitive = bCaseSensitive;

    // Add it to our list
    m_Commands.push_back ( pCommand );

    return true;
}


bool CRegisteredCommands::RemoveCommand ( CLuaMain* pLuaMain, const char* szKey, const CLuaFunctionRef& iLuaFunction )
{
    assert ( pLuaMain );
    assert ( szKey );

    // Call the handler for every virtual machine that matches the given key
    int iCompareResult;
    bool bFound = false;
    list < SCommand* > ::iterator iter = m_Commands.begin ();

    while ( iter != m_Commands.end () )
    {
        if ( ( *iter )->bCaseSensitive )
            iCompareResult = strcmp ( (*iter)->strKey.c_str (), szKey );
        else
            iCompareResult = stricmp ( (*iter)->strKey.c_str (), szKey );

        // Matching VM's and names?
        if ( ( *iter )->pLuaMain == pLuaMain && iCompareResult == 0 )
        {
            if ( VERIFY_FUNCTION ( iLuaFunction ) && ( *iter )->iLuaFunction != iLuaFunction )
            {
                iter++;
                continue;
            }

            // Delete it and remove it from our list
            if ( m_bIteratingList )
            {
                m_TrashCan.push_back ( *iter );
                ++iter;
            }
            else
            {
                delete *iter;
                iter = m_Commands.erase ( iter );
            }

            bFound = true;
        }
        else
            ++iter;
    }

    return bFound;
}


void CRegisteredCommands::ClearCommands ( void )
{
    // Delete all the commands
    list < SCommand* > ::const_iterator iter = m_Commands.begin ();

    for ( ; iter != m_Commands.end (); iter++ )
        delete *iter;

    // Clear the list
    m_Commands.clear ();
}


void CRegisteredCommands::CleanUpForVM ( CLuaMain* pLuaMain )
{
    assert ( pLuaMain );

    // Delete every command that matches
    list < SCommand* > ::iterator iter = m_Commands.begin ();

    while ( iter != m_Commands.end () )
    {
        // Matching VM's?
        if ( ( *iter )->pLuaMain == pLuaMain )
        {
            // Delete the entry and remove it from the list
            delete *iter;
            iter = m_Commands.erase ( iter );
        }
        else
            ++iter;
    }
}


bool CRegisteredCommands::CommandExists ( const char* szKey, CLuaMain* pLuaMain )
{
    assert ( szKey );

    return GetCommand ( szKey, pLuaMain ) != NULL;
}


bool CRegisteredCommands::ProcessCommand ( const char* szKey, const char* szArguments, CClient* pClient )
{
    assert ( szKey );

    // Call the handler for every virtual machine that matches the given key
    bool bHandled = false;
    int iCompareResult;
    list < SCommand* > ::const_iterator iter = m_Commands.begin ();

    m_bIteratingList = true;

    for ( ; iter != m_Commands.end (); iter++ )
    {
        if ( ( *iter )->bCaseSensitive )
            iCompareResult = strcmp ( ( *iter )->strKey.c_str(), szKey );
        else
            iCompareResult = stricmp ( ( *iter )->strKey.c_str(), szKey );

        // Matching names?
        if ( iCompareResult == 0 )
        {
            if ( m_pACLManager->CanObjectUseRight ( pClient->GetAccount ()->GetName ().c_str (),
                                                    CAccessControlListGroupObject::OBJECT_TYPE_USER,
                                                    ( *iter )->strKey,
                                                    CAccessControlListRight::RIGHT_TYPE_COMMAND,
                                                    !( *iter )->bRestricted ) )   // If this command is restricted, the default access should be false unless granted specially
            {
                // Call it
                CallCommandHandler ( ( *iter )->pLuaMain, ( *iter )->iLuaFunction, ( *iter )->strKey, szArguments, pClient );
                bHandled = true;
            }
        }
    }

    m_bIteratingList = false;
    TakeOutTheTrash ();

    // Return whether some handler was called or not
    return bHandled;
}


CRegisteredCommands::SCommand* CRegisteredCommands::GetCommand ( const char* szKey, class CLuaMain* pLuaMain )
{
    assert ( szKey );

    // Try to find an entry with a matching name in our list
    int iCompareResult;
    list < SCommand* > ::const_iterator iter = m_Commands.begin ();

    for ( ; iter != m_Commands.end (); iter++ )
    {
        if ( ( *iter )->bCaseSensitive )
            iCompareResult = strcmp ( ( *iter )->strKey.c_str (), szKey );
        else
            iCompareResult = stricmp ( ( *iter )->strKey.c_str (), szKey );

        // Matching name and no given VM or matching VM
        if ( iCompareResult == 0 && ( !pLuaMain || pLuaMain == ( *iter )->pLuaMain ) )
            return *iter;
    }

    // Doesn't exist
    return NULL;
}


void CRegisteredCommands::CallCommandHandler ( CLuaMain* pLuaMain, const CLuaFunctionRef& iLuaFunction, const char* szKey, const char* szArguments, CClient* pClient )
{
    assert ( pLuaMain );
    assert ( szKey );

    CLuaArguments Arguments;

    // First, try to call a handler with the same number of arguments
    if ( pClient )
    {
        switch ( pClient->GetClientType () )
        {
            case CClient::CLIENT_PLAYER:
            {
                Arguments.PushElement ( static_cast < CPlayer* > ( pClient ) );
                break;
            }
            case CClient::CLIENT_CONSOLE:
            {
                Arguments.PushElement ( static_cast < CConsoleClient* > ( pClient ) );
                break;
            }
            default:
            {
                Arguments.PushBoolean ( false );
                break;
            }
        }
    }
    else
        Arguments.PushBoolean ( false );

    Arguments.PushString ( szKey );

    if ( szArguments )
    {
        // Create a copy and strtok modifies the string
        char * szTempArguments = new char [ strlen ( szArguments ) + 1 ];
        strcpy ( szTempArguments, szArguments );

        char * arg;
        arg = strtok ( szTempArguments, " " );

        while ( arg )
        {
            Arguments.PushString ( arg );
            arg = strtok ( NULL, " " );
        }

        delete [] szTempArguments;
    }

    // Call the handler with the arguments we pushed
    Arguments.Call ( pLuaMain, iLuaFunction );
}


void CRegisteredCommands::GetCommands ( lua_State* luaVM )
{
    unsigned int uiIndex = 0;
    m_bIteratingList = true;
    list < SCommand* > ::iterator iter = m_Commands.begin ();

    // Create the main table
    lua_newtable( luaVM );

    for ( ; iter != m_Commands.end (); iter++ )
    {
        // Create a subtable ({'command', resource})
        lua_pushinteger ( luaVM, ++uiIndex );
        lua_createtable ( luaVM, 0, 2 );
        {
            lua_pushstring ( luaVM, (*iter)->strKey );
            lua_rawseti ( luaVM, -2, 1 );

            lua_pushresource ( luaVM, (*iter)->pLuaMain->GetResource() );
            lua_rawseti ( luaVM, -2, 2 );
        }
        lua_settable ( luaVM, -3 );
    }

    m_bIteratingList = false;
}


void CRegisteredCommands::GetCommands ( lua_State* luaVM, CLuaMain* pTargetLuaMain )
{
    unsigned int uiIndex = 0;
    m_bIteratingList = true;
    list < SCommand* > ::iterator iter = m_Commands.begin ();

    // Create the table
    lua_newtable( luaVM );

    for ( ; iter != m_Commands.end (); iter++ )
    {
        // Matching VMs?
        if ( (*iter)->pLuaMain == pTargetLuaMain )
        {
            lua_pushinteger ( luaVM, ++uiIndex );
            lua_pushstring ( luaVM, (*iter)->strKey );
            lua_settable ( luaVM, -3 );
        }
    }

    //lua_settable ( luaVM, -3 );

    m_bIteratingList = false;
}


void CRegisteredCommands::TakeOutTheTrash ( void )
{
    list < SCommand* > ::iterator iter = m_TrashCan.begin ();

    for ( ; iter != m_TrashCan.end (); iter++ )
    {
        m_Commands.remove ( *iter );
        delete *iter;
    }

    m_TrashCan.clear ();
}
