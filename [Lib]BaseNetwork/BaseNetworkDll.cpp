#define UNICODE
#define _DLLEXPORT_
#include "stdafx.h"
#include "basenetwork.h"
#include "basenetworkfactory.h"

HINSTANCE g_hDllInst = NULL;

BOOL WINAPI DllMain(
              HINSTANCE hDllInst,
              DWORD fdwReason,
              LPVOID lpvReserved)
{
	
	// Dispatch this main call based on the reason it was called.

	
	switch (fdwReason)
  	{
  	case DLL_PROCESS_ATTACH:

		g_hDllInst = hDllInst;

		// The DLL is being loaded for the first time by a given process.
      	// Perform per-process initialization here.  If the initialization
      	// is successful, return TRUE; if unsuccessful, return FALSE.
		break;

    case DLL_PROCESS_DETACH:
    	// The DLL is being unloaded by a given process.  Do any
    	// per-process clean up here, such as undoing what was done in
    	// DLL_PROCESS_ATTACH.  The return value is ignored.
		break;

    case DLL_THREAD_ATTACH:
		// A thread is being created in a process that has already loaded
      	// this DLL.  Perform any per-thread initialization here.  The
      	// return value is ignored.
		break;

    case DLL_THREAD_DETACH:
 		// A thread is exiting cleanly in a process that has already
		// loaded this DLL.  Perform any per-thread clean up here.  The
    	// return value is ignored.
    	break;
	default:
		break;
	}
	return TRUE;
}


/*F+F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F
  Function: DllGetClassObject

  Summary:  The standard exported function that the COM service library
            uses to obtain an object class of the class factory for a
            specified component provided by this server DLL.

  Args:     REFCLSID rclsid,
              [in] The CLSID of the requested Component.
            REFIID riid,
              [in] GUID of the requested interface on the Class Factory.
            PPVOID ppv)
              [out] Address of the caller's pointer variable that will
              receive the requested interface pointer.

  Returns:  HRESULT
              E_FAIL if requested component isn't supported.
              E_OUTOFMEMORY if out of memory.
              Error code out of the QueryInterface.
F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F-F*/
// 080924 LUJ, 함수 포인터를 받아올 수 있도록 수정
STDAPI DllGetClassObject(
	REFCLSID	referenceClassId,
	REFIID		referenceInterfaceId,
	PPVOID		function )
{
	CSC_BaseNetworkFactory factory;

	IUnknown* unknownInterface = 0;
	return factory.CreateInstance(
		unknownInterface,
		referenceInterfaceId,
		function );
}

/*F+F++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  Function: SetRegKeyValue

  Summary:  Internal utility function to set a Key, Subkey, and value
            in the system Registry under HKEY_CLASSES_ROOT.

  Args:     LPTSTR pszKey,
            LPTSTR pszSubkey,
            LPTSTR pszValue)

  Returns:  BOOL
              TRUE if success; FALSE if not.
------------------------------------------------------------------------F-F*/
BOOL SetRegKeyValue(
       LPTSTR pszKey,
       LPTSTR pszSubkey,
       LPTSTR pszValue)
{
  BOOL bOk = FALSE;
  LONG ec;
  HKEY hKey;
  TCHAR szKey[MAX_STRING_LENGTH];

  lstrcpy(szKey, pszKey);

  if (NULL != pszSubkey)
  {
    lstrcat(szKey, TEXT("\\"));
    lstrcat(szKey, pszSubkey);
  }

  ec = RegCreateKeyEx(
         HKEY_CLASSES_ROOT,
         szKey,
         0,
         NULL,
         REG_OPTION_NON_VOLATILE,
         KEY_ALL_ACCESS,
         NULL,
         &hKey,
         NULL);

  if (ERROR_SUCCESS == ec)
  {
    if (NULL != pszValue)
    {
      ec = RegSetValueEx(
             hKey,
             NULL,
             0,
             REG_SZ,
             (BYTE *)pszValue,
             (lstrlen(pszValue)+1)*sizeof(TCHAR));
    }
    if (ERROR_SUCCESS == ec)
      bOk = TRUE;
    RegCloseKey(hKey);
  }

  return bOk;
}


/*F+F++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  Function: AddRegNamedValue

  Summary:  Internal utility function to add a named data value to an
            existing Key (with optional Subkey) in the system Registry
            under HKEY_CLASSES_ROOT.

  Args:     LPTSTR pszKey,
            LPTSTR pszSubkey,
            LPTSTR pszValueName,
            LPTSTR pszValue)

  Returns:  BOOL
              TRUE if success; FALSE if not.
------------------------------------------------------------------------F-F*/
BOOL AddRegNamedValue(
       LPTSTR pszKey,
       LPTSTR pszSubkey,
       LPTSTR pszValueName,
       LPTSTR pszValue)
{
  BOOL bOk = FALSE;
  LONG ec;
  HKEY hKey;
  TCHAR szKey[MAX_STRING_LENGTH];

  lstrcpy(szKey, pszKey);

  if (NULL != pszSubkey)
  {
    lstrcat(szKey, TEXT("\\"));
    lstrcat(szKey, pszSubkey);
  }

  ec = RegOpenKeyEx(
         HKEY_CLASSES_ROOT,
         szKey,
         0,
         KEY_ALL_ACCESS,
         &hKey);

  if (NULL != pszValue && ERROR_SUCCESS == ec)
  {
    ec = RegSetValueEx(
           hKey,
           pszValueName,
           0,
           REG_SZ,
           (BYTE *)pszValue,
           (lstrlen(pszValue)+1)*sizeof(TCHAR));
    if (ERROR_SUCCESS == ec)
      bOk = TRUE;
    RegCloseKey(hKey);
  }

  return bOk;
}


/*F+F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F
  Function: DllRegisterServer

  Summary:  The standard exported function that can be called to command
            this DLL server to register itself in the system registry.

  Args:     void.

  Returns:  HRESULT
              NOERROR
F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F-F*/
STDAPI DllRegisterServer(void)
{
  HRESULT  hr = NOERROR;
  TCHAR    szID[GUID_SIZE+1];
  TCHAR    szCLSID[GUID_SIZE+32];
  TCHAR    szModulePath[MAX_PATH];

  // Obtain the path to this module's executable file for later use.
  GetModuleFileName(g_hDllInst,szModulePath,sizeof(szModulePath)/sizeof(TCHAR));

  
  //  Create registry entries for the DllSndBall Component.
  
  // Create some base key strings.

  StringFromGUID2(CLSID_SC_BASENETWORK_DLL, szID, GUID_SIZE);
  lstrcpy(szCLSID, TEXT("CLSID\\"));
  lstrcat(szCLSID, szID);

  // Create ProgID keys.
  SetRegKeyValue(
    TEXT("SC.BASENETWORK.1"),
    NULL,
    TEXT("SC BASENETWORK DLL by slowboat, eSofnet"));
  
  SetRegKeyValue(
    TEXT("SC.CLSID_SC_BASENETWORK_DLL.1"),
    TEXT("CLSID"),
    szID);
  

  // Create VersionIndependentProgID keys.
  SetRegKeyValue(
    TEXT("CS.CLSID_SC_BASENETWORK_DLL"),
    NULL,
   TEXT("SC BASENETWORK DLL by slowboat, eSofnet"));


   SetRegKeyValue(
    TEXT("SC.CLSID_SC_BASENETWORK_DLL.1"),
    TEXT("CurVer"),
    TEXT("SC.CLSID_SC_BASENETWORK_DLL.1"));
  SetRegKeyValue(
    TEXT("SC.CLSID_SC_BASENETWORK_DLL.1"),
    TEXT("CLSID"),
    szID);

  // Create entries under CLSID.
  SetRegKeyValue(
    szCLSID,
    NULL,
	TEXT("SC BASENETWORK DLL by slowboat, eSofnet"));
	
  SetRegKeyValue(
    szCLSID,
    TEXT("ProgID"),
    TEXT("SC.CLSID_SC_BASENETWORK_DLL.1"));
  SetRegKeyValue(
    szCLSID,
    TEXT("VersionIndependentProgID"),
    TEXT("SC.CLSID_SC_BASENETWORK_DLL.1"));
  SetRegKeyValue(
    szCLSID,
    TEXT("NotInsertable"),
    NULL);
  SetRegKeyValue(
    szCLSID,
    TEXT("InprocServer32"),
    szModulePath);

  AddRegNamedValue(
    szCLSID,
    TEXT("InprocServer32"),
    TEXT("ThreadingModel"),
    TEXT("Apartment"));

  return hr;
}


/*F+F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F+++F
  Function: DllUnregisterServer

  Summary:  The standard exported function that can be called to command
            this DLL server to unregister itself from the system Registry.

  Args:     void.

  Returns:  HRESULT
              NOERROR
F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F---F-F*/
STDAPI DllUnregisterServer(void)
{
  HRESULT  hr = NOERROR;
  TCHAR    szID[GUID_SIZE+1];
  TCHAR    szCLSID[GUID_SIZE+32];
  TCHAR    szTemp[MAX_PATH+GUID_SIZE];


   //Create some base key strings.
  StringFromGUID2(CLSID_SC_BASENETWORK_DLL, szID, GUID_SIZE);
  lstrcpy(szCLSID, TEXT("CLSID\\"));
  lstrcat(szCLSID, szID);

  RegDeleteKey(HKEY_CLASSES_ROOT, TEXT("SC.CLSID_SC_BASENETWORK_DLL\\CurVer"));
  RegDeleteKey(HKEY_CLASSES_ROOT, TEXT("SC.CLSID_SC_BASENETWORK_DLL\\CLSID"));
  RegDeleteKey(HKEY_CLASSES_ROOT, TEXT("SC.CLSID_SC_BASENETWORK_DLL"));

  RegDeleteKey(HKEY_CLASSES_ROOT, TEXT("SC.CLSID_SC_BASENETWORK_DLL.1\\CLSID"));
  RegDeleteKey(HKEY_CLASSES_ROOT, TEXT("SC.CLSID_SC_BASENETWORK_DLL.1"));


  wsprintf(szTemp, TEXT("%s\\%s"), szCLSID, TEXT("ProgID"));
  RegDeleteKey(HKEY_CLASSES_ROOT, szTemp);

  wsprintf(szTemp, TEXT("%s\\%s"), szCLSID, TEXT("VersionIndependentProgID"));
  RegDeleteKey(HKEY_CLASSES_ROOT, szTemp);

  wsprintf(szTemp, TEXT("%s\\%s"), szCLSID, TEXT("NotInsertable"));
  RegDeleteKey(HKEY_CLASSES_ROOT, szTemp);

  wsprintf(szTemp, TEXT("%s\\%s"), szCLSID, TEXT("InprocServer32"));
  RegDeleteKey(HKEY_CLASSES_ROOT, szTemp);

  RegDeleteKey(HKEY_CLASSES_ROOT, szCLSID);

  return hr;
}
