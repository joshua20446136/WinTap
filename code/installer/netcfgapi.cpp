/*
* Copyright 2013, New Tribes Mission Inc, (ntm.org)
*
* All rights reserved.
* 
* This file is part of WinTAP. WinTAP is released under 
* GNU General Public License, version 2.
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
* 
*/

#include "stdafx.h"
#include "NetCfgAPI.h"

HRESULT HrGetINetCfg (
    IN BOOL fGetWriteLock,
    IN LPCTSTR lpszAppName,
    OUT INetCfg** ppnc,
    OUT LPTSTR *lpszLockedBy
    )
{
    INetCfg      *pnc = NULL;
    INetCfgLock  *pncLock = NULL;
    HRESULT      hr = S_OK;

    //
    // Initialize the output parameters.
    //

    *ppnc = NULL;

    if ( lpszLockedBy )
    {
        *lpszLockedBy = NULL;
    }
    //
    // Initialize COM
    //

    hr = CoInitialize( NULL );

    if ( hr == S_OK ) {

        //
        // Create the object implementing INetCfg.
        //

        hr = CoCreateInstance( CLSID_CNetCfg,
            NULL, CLSCTX_INPROC_SERVER,
            IID_INetCfg,
            (void**)&pnc );
        if ( hr == S_OK ) {

            if ( fGetWriteLock ) {

                //
                // Get the locking reference
                //

                hr = pnc->QueryInterface( IID_INetCfgLock, (LPVOID *)&pncLock );
                if ( hr == S_OK ) {

                    //
                    // Attempt to lock the INetCfg for read/write
                    //

                    hr = pncLock->AcquireWriteLock( LOCK_TIME_OUT,
                        lpszAppName,
                        lpszLockedBy);
                    if (hr == S_FALSE ) {
                        hr = NETCFG_E_NO_WRITE_LOCK;
                    }
                }
            }

            if ( hr == S_OK ) {

                //
                // Initialize the INetCfg object.
                //

                hr = pnc->Initialize( NULL );

                if ( hr == S_OK ) {
                    *ppnc = pnc;
                    pnc->AddRef();
                }
                else {

                    //
                    // Initialize failed, if obtained lock, release it
                    //

                    if ( pncLock ) {
                        pncLock->ReleaseWriteLock();
                    }
                }
            }

            ReleaseRef( pncLock );
            ReleaseRef( pnc );
        }

        //
        // In case of error, uninitialize COM.
        //

        if ( hr != S_OK ) {
            CoUninitialize();
        }
    }

    return hr;
}

HRESULT HrReleaseINetCfg (
    IN INetCfg* pnc,
    IN BOOL fHasWriteLock
    )
{
    INetCfgLock    *pncLock = NULL;
    HRESULT        hr = S_OK;

    //
    // Uninitialize INetCfg
    //

    hr = pnc->Uninitialize();

    //
    // If write lock is present, unlock it
    //

    if ( hr == S_OK && fHasWriteLock ) {

        //
        // Get the locking reference
        //

        hr = pnc->QueryInterface( IID_INetCfgLock,
            (LPVOID *)&pncLock);
        if ( hr == S_OK ) {
            hr = pncLock->ReleaseWriteLock();
            ReleaseRef( pncLock );
        }
    }

    ReleaseRef( pnc );

    //
    // Uninitialize COM.
    //

    CoUninitialize();

    return hr;
}

HRESULT HrInstallNetComponent(
    IN INetCfg *pnc,
    IN LPCTSTR lpszComponentId,
    IN const GUID *pguidClass,
    IN LPCTSTR lpszInfFullPath
    )
{
    DWORD     dwError;
    HRESULT   hr = S_OK;
    WCHAR     szDrive[ _MAX_DRIVE ];
    WCHAR     szDir[ _MAX_DIR ];
    WCHAR     szDirWithDrive[_MAX_DRIVE+_MAX_DIR];
	WCHAR     szfname[ _MAX_FNAME ];
	WCHAR     szExt[ _MAX_EXT ];

    //
    // If full path to INF has been specified, the INF
    // needs to be copied using Setup API to ensure that any other files
    // that the primary INF copies will be correctly found by Setup API
    //
    if ( lpszInfFullPath )
    {
        //
        // Get the path where the INF file is.
        //
        _wsplitpath_s( lpszInfFullPath, szDrive, _countof(szDrive), szDir, _countof(szDir), szfname, _countof(szfname), szExt, _countof(szExt) );

		wcscpy_s( szDirWithDrive, szDrive );
        wcscpy_s( szDirWithDrive, szDir );

        //
        // Copy the Service INF file to the \Windows\Inf Folder
        //
        if ( !SetupCopyOEMInf(
            lpszInfFullPath,
            szDirWithDrive, // Other files are in the same dir as primary INF
            SPOST_PATH,    // First param is path to INF
            0,             // Default copy style
            NULL,          // Name of the INF after it's copied to %windir%\inf
            0,             // Max buf. size for the above
            NULL,          // Required size if non-null
            NULL)          // Optionally get the filename part of Inf name after it is copied.
            )
        {
            dwError = GetLastError();

            hr = HRESULT_FROM_WIN32( dwError );
        }
    }

    if ( S_OK == hr )
    {
        //
        // Install the network component.
        //
        hr = HrInstallComponent( pnc, lpszComponentId, pguidClass );

        if ( hr == S_OK )
        {
            //
            // On success, apply the changes
            //
            hr = pnc->Apply();
        }
    }

    return hr;
}

HRESULT HrInstallComponent(
    IN INetCfg* pnc,
    IN LPCTSTR szComponentId,
    IN const GUID* pguidClass
    )
{
    INetCfgClassSetup   *pncClassSetup = NULL;
    INetCfgComponent    *pncc = NULL;
    OBO_TOKEN           OboToken;
    HRESULT             hr = S_OK;

    //
    // OBO_TOKEN specifies on whose behalf this
    // component is being installed.
    // Set it to OBO_USER so that szComponentId will be installed
    // on behalf of the user.
    //

    ZeroMemory( &OboToken,
        sizeof(OboToken) );
    OboToken.Type = OBO_USER;

    //
    // Get component's setup class reference.
    //
    hr = pnc->QueryNetCfgClass ( pguidClass,
        IID_INetCfgClassSetup,
        (void**)&pncClassSetup );

    if ( hr == S_OK )
    {
        hr = pncClassSetup->Install( szComponentId,
            &OboToken,
            0,
            0,       // Upgrade from build number.
            NULL,    // Answerfile name
            NULL,    // Answerfile section name
            &pncc ); // Reference after the component
        if ( S_OK == hr ) {                   // is installed.

            //
            // we don't need to use pncc (INetCfgComponent), release it
            //

            ReleaseRef( pncc );
        }

        ReleaseRef( pncClassSetup );
    }

    return hr;
}

HRESULT HrUninstallNetComponent(
    IN INetCfg* pnc,
    IN LPCTSTR szComponentId
    )
{
    INetCfgComponent    *pncc = NULL;
    INetCfgClass        *pncClass = NULL;
    INetCfgClassSetup   *pncClassSetup = NULL;
    OBO_TOKEN           OboToken;
    GUID                guidClass;
    HRESULT             hr = S_OK;

    //
    // OBO_TOKEN specifies on whose behalf this
    // component is being installed.
    // Set it to OBO_USER so that szComponentId will be installed
    // on behalf of the user.
    //

    ZeroMemory( &OboToken,
        sizeof(OboToken) );
    OboToken.Type = OBO_USER;

    //
    // Get the component's reference.
    //

    hr = pnc->FindComponent( szComponentId,
        &pncc );

    if (S_OK == hr) {

        //
        // Get the component's class GUID.
        //

        hr = pncc->GetClassGuid( &guidClass );

        if ( hr == S_OK ) {

            //
            // Get component's class reference.
            //

            hr = pnc->QueryNetCfgClass( &guidClass,
                IID_INetCfgClass,
                (void**)&pncClass );
            if ( hr == S_OK ) {

                //
                // Get Setup reference.
                //

                hr = pncClass->QueryInterface( IID_INetCfgClassSetup,
                    (void**)&pncClassSetup );
                if ( hr == S_OK ) {

                    hr = pncClassSetup->DeInstall( pncc,
                        &OboToken,
                        NULL);
                    if ( hr == S_OK ) {

                        //
                        // Apply the changes
                        //

                        hr = pnc->Apply();
                    }

                    ReleaseRef( pncClassSetup );
                }

                ReleaseRef( pncClass );
            }
        }

        ReleaseRef( pncc );
    }

    return hr;
}

VOID ReleaseRef (IN IUnknown* punk)
{
    if ( punk ) {
        punk->Release();
    }

    return;
}

