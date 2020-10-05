/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2018-2019 WireGuard LLC. All Rights Reserved.
 */

#include "pch.h"

#pragma warning(disable : 4100) /* unreferenced formal parameter */
#pragma warning(disable : 4204) /* nonstandard: non-constant aggregate initializer */
#pragma warning(disable : 4221) /* nonstandard: address of automatic in initializer */

#define DRIVER_SYS TEXT(PRODUCT_TAP_WIN_COMPONENT_ID) TEXT(".sys")

typedef struct _SP_DEVINFO_DATA_LIST
{
    SP_DEVINFO_DATA Data;
    struct _SP_DEVINFO_DATA_LIST *Next;
} SP_DEVINFO_DATA_LIST;

static VOID
NopLogger(_In_ LOGGER_LEVEL Level, _In_ const TCHAR *LogLine)
{
}

static LoggerFunction Logger = NopLogger;

VOID
SetLogger(_In_ LoggerFunction NewLogger)
{
    Logger = NewLogger;
}

static VOID
PrintError(_In_ LOGGER_LEVEL Level, _In_z_ const TCHAR *Prefix, ...)
{
    DWORD ErrorCode = GetLastError();
    TCHAR *Message = NULL, *SystemMessage = NULL, *FormattedMessage = NULL;
    va_list Args;
    va_start(Args, Prefix);
    FormatMessage(
        FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_MAX_WIDTH_MASK,
        Prefix,
        0,
        0,
        (VOID *)&Message,
        0,
        &Args);
    va_end(Args);
    FormatMessage(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_MAX_WIDTH_MASK,
        NULL,
        HRESULT_FROM_SETUPAPI(ErrorCode),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (VOID *)&SystemMessage,
        0,
        NULL);
    FormatMessage(
        FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_ARGUMENT_ARRAY |
            FORMAT_MESSAGE_MAX_WIDTH_MASK,
        SystemMessage ? TEXT("%1: %3(Code 0x%2!08X!)") : TEXT("%1: Code 0x%2!08X!"),
        0,
        0,
        (VOID *)&FormattedMessage,
        0,
        (va_list *)(DWORD_PTR[]){ (DWORD_PTR)Message, (DWORD_PTR)ErrorCode, (DWORD_PTR)SystemMessage });
    if (FormattedMessage)
        Logger(Level, FormattedMessage);
    LocalFree(FormattedMessage);
    LocalFree(SystemMessage);
    LocalFree(Message);
}

HINSTANCE ResourceModule;

static BOOL IsOurDriver(const TCHAR *HardwareID)
{
    return
        !_tcsicmp(HardwareID, TEXT("root\\") TEXT(PRODUCT_TAP_WIN_COMPONENT_ID)) ||
        !_tcsicmp(HardwareID, TEXT(PRODUCT_TAP_WIN_COMPONENT_ID));
}

static BOOL IsDriverLoaded(VOID)
{
    DWORD RequiredSize = 0, CurrentSize = 0;
    VOID **Drivers = NULL;
    BOOL Found = FALSE;
    for (;;)
    {
        if (!EnumDeviceDrivers(Drivers, CurrentSize, &RequiredSize))
            goto out;
        if (CurrentSize == RequiredSize)
            break;
        free(Drivers);
        Drivers = malloc(RequiredSize);
        if (!Drivers)
            goto out;
        CurrentSize = RequiredSize;
    }
    TCHAR BaseName[_countof(DRIVER_SYS)];
    for (DWORD i = CurrentSize / sizeof(Drivers[0]); i-- > 0;)
    {
        if (GetDeviceDriverBaseName(Drivers[i], BaseName, _countof(BaseName)) == _countof(DRIVER_SYS) - 1 &&
            !_tcsicmp(BaseName, DRIVER_SYS))
        {
            Found = TRUE;
            goto out;
        }
    }
out:
    free(Drivers);
    return Found;
}

static BOOL EnsureDriverUnloaded(VOID)
{
    BOOL Loaded;
    for (int i = 0; (Loaded = IsDriverLoaded()) != 0 && i < 300; ++i)
        Sleep(50);
    return !Loaded;
}

static BOOL
CopyResource(
    _In_ const TCHAR *DestinationPath,
    _In_opt_ SECURITY_ATTRIBUTES *SecurityAttributes,
    _In_ const TCHAR *ResourceName)
{
    HRSRC FoundResource = FindResource(ResourceModule, ResourceName, RT_RCDATA);
    if (!FoundResource)
        return FALSE;
    DWORD SizeResource = SizeofResource(ResourceModule, FoundResource);
    if (!SizeResource)
        return FALSE;
    HGLOBAL LoadedResource = LoadResource(ResourceModule, FoundResource);
    if (!LoadedResource)
        return FALSE;
    LPVOID LockedResource = LockResource(LoadedResource);
    if (!LockedResource)
        return FALSE;
    HANDLE DestinationHandle = CreateFile(
        DestinationPath,
        GENERIC_WRITE,
        0,
        SecurityAttributes,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY,
        NULL);
    if (DestinationHandle == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD BytesWritten;
    BOOL Ret =
        WriteFile(DestinationHandle, LockedResource, SizeResource, &BytesWritten, NULL) && BytesWritten == SizeResource;
    CloseHandle(DestinationHandle);
    return Ret;
}

static BOOL
InstallDriverCertificate(const TCHAR *SignedResource)
{
    DWORD LastError = ERROR_SUCCESS;
    Logger(LOG_INFO, TEXT("Trusting code signing certificate"));
    BOOL Ret = TRUE;
    HRSRC FoundResource = FindResource(ResourceModule, SignedResource, RT_RCDATA);
    if (!FoundResource)
        return FALSE;
    DWORD SizeResource = SizeofResource(ResourceModule, FoundResource);
    if (!SizeResource)
        return FALSE;
    HGLOBAL LoadedResource = LoadResource(ResourceModule, FoundResource);
    if (!LoadedResource)
        return FALSE;
    LPVOID LockedResource = LockResource(LoadedResource);
    if (!LockedResource)
        return FALSE;
    const CERT_BLOB CertBlob = { .cbData = SizeResource, .pbData = LockedResource };
    HCERTSTORE QueriedStore;
    if (!CryptQueryObject(
            CERT_QUERY_OBJECT_BLOB,
            &CertBlob,
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
            CERT_QUERY_FORMAT_FLAG_ALL,
            0,
            0,
            0,
            0,
            &QueriedStore,
            0,
            NULL))
        return FALSE;
    HCERTSTORE TrustedStore =
        CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, TEXT("TrustedPublisher"));
    if (!TrustedStore)
    {
        LastError = GetLastError();
        goto cleanupQueriedStore;
    }
    LPSTR CodeSigningOid[] = { szOID_PKIX_KP_CODE_SIGNING };
    CERT_ENHKEY_USAGE EnhancedUsage = { .cUsageIdentifier = 1, .rgpszUsageIdentifier = CodeSigningOid };
    for (const CERT_CONTEXT *CertContext = NULL; (CertContext = CertFindCertificateInStore(
                                                      QueriedStore,
                                                      X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                                      CERT_FIND_EXT_ONLY_ENHKEY_USAGE_FLAG,
                                                      CERT_FIND_ENHKEY_USAGE,
                                                      &EnhancedUsage,
                                                      CertContext)) != NULL;)
    {
        CERT_EXTENSION *Ext = CertFindExtension(
            szOID_BASIC_CONSTRAINTS2, CertContext->pCertInfo->cExtension, CertContext->pCertInfo->rgExtension);
        CERT_BASIC_CONSTRAINTS2_INFO Constraints;
        DWORD Size = sizeof(Constraints);
        if (Ext &&
            CryptDecodeObjectEx(
                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                szOID_BASIC_CONSTRAINTS2,
                Ext->Value.pbData,
                Ext->Value.cbData,
                0,
                NULL,
                &Constraints,
                &Size) &&
            !Constraints.fCA)
            Ret &= CertAddCertificateContextToStore(TrustedStore, CertContext, CERT_STORE_ADD_REPLACE_EXISTING, NULL);
        if (!Ret)
            LastError = LastError ? LastError : GetLastError();
    }
    CertCloseStore(TrustedStore, 0);
cleanupQueriedStore:
    CertCloseStore(QueriedStore, 0);
    SetLastError(LastError);
    return Ret;
}

/* We can't use RtlGetVersion, because appcompat's aclayers.dll shims it to report Vista
 * when run from MSI context. So, we instead use the undocumented RtlGetNtVersionNumbers.
 *
 * Another way would be reading from the PEB directly:
 *   ((DWORD *)NtCurrentTeb()->ProcessEnvironmentBlock)[sizeof(void *) == 8 ? 70 : 41]
 * Or just read from KUSER_SHARED_DATA the same way on 32-bit and 64-bit:
 *    *(DWORD *)0x7FFE026C
 */
extern VOID NTAPI
RtlGetNtVersionNumbers(_Out_opt_ DWORD *MajorVersion, _Out_opt_ DWORD *MinorVersion, _Out_opt_ DWORD *BuildNumber);

static BOOL
InstallDriver(_In_ BOOL UpdateExisting, _Inout_ BOOL *IsRebootRequired)
{
    DWORD LastError = ERROR_SUCCESS;
    TCHAR WindowsDirectory[MAX_PATH];
    if (!GetWindowsDirectory(WindowsDirectory, _countof(WindowsDirectory)))
        return FALSE;
    TCHAR WindowsTempDirectory[MAX_PATH];
    if (!PathCombine(WindowsTempDirectory, WindowsDirectory, TEXT("Temp")))
        return FALSE;
    UCHAR RandomBytes[32] = { 0 };
#pragma warning(suppress : 6387)
    if (!RtlGenRandom(RandomBytes, sizeof(RandomBytes)))
        return FALSE;
    TCHAR RandomSubDirectory[sizeof(RandomBytes) * 2 + 1];
    for (int i = 0; i < sizeof(RandomBytes); ++i)
        _stprintf_s(&RandomSubDirectory[i * 2], 3, TEXT("%02x"), RandomBytes[i]);
    TCHAR RandomTempSubDirectory[MAX_PATH];
    if (!PathCombine(RandomTempSubDirectory, WindowsTempDirectory, RandomSubDirectory))
        return FALSE;
    SECURITY_ATTRIBUTES SecurityAttributes = { .nLength = sizeof(SecurityAttributes) };
    if (!ConvertStringSecurityDescriptorToSecurityDescriptor(
            TEXT("O:SYD:P(A;;GA;;;SY)"), SDDL_REVISION_1, &SecurityAttributes.lpSecurityDescriptor, NULL))
        return FALSE;
    BOOL Ret = CreateDirectory(RandomTempSubDirectory, &SecurityAttributes);
    if (!Ret)
        goto cleanupFree;

    TCHAR CatPath[MAX_PATH] = { 0 };
    if (!PathCombine(CatPath, RandomTempSubDirectory, TEXT(PRODUCT_TAP_WIN_COMPONENT_ID) TEXT(".cat")))
        goto cleanupFree;
    TCHAR SysPath[MAX_PATH] = { 0 };
    if (!PathCombine(SysPath, RandomTempSubDirectory, TEXT(PRODUCT_TAP_WIN_COMPONENT_ID) TEXT(".sys")))
        goto cleanupFree;
    TCHAR InfPath[MAX_PATH] = { 0 };
    if (!PathCombine(InfPath, RandomTempSubDirectory, TEXT("OemVista.inf")))
        goto cleanupFree;

    BOOL UseWHQL = FALSE;
#if defined(HAVE_EV) && defined(HAVE_WHQL)
    DWORD MajorVersion;
    RtlGetNtVersionNumbers(&MajorVersion, NULL, NULL);
    UseWHQL = MajorVersion >= 10;
#elif defined(HAVE_EV)
    UseWHQL = FALSE;
#elif defined(HAVE_WHQL)
    UseWHQL = TRUE;
#else
    #error No driver available
#endif
    if (!UseWHQL && !InstallDriverCertificate(TEXT("driver.cat")))
        PrintError(LOG_WARN, TEXT("Unable to install code signing certificate"));

    Logger(LOG_INFO, TEXT("Copying resources to temporary path"));
    Ret = CopyResource(CatPath, &SecurityAttributes, UseWHQL ? TEXT("driver-whql.cat") : TEXT("driver.cat")) &&
          CopyResource(SysPath, &SecurityAttributes, UseWHQL ? TEXT("driver-whql.sys") : TEXT("driver.sys")) &&
          CopyResource(InfPath, &SecurityAttributes, UseWHQL ? TEXT("driver-whql.inf") : TEXT("driver.inf"));
    if (!Ret)
        goto cleanupDelete;

    Logger(LOG_INFO, TEXT("Installing driver"));
    Ret = SetupCopyOEMInf(InfPath, NULL, SPOST_PATH, 0, NULL, 0, NULL, NULL);
    static const TCHAR *HwID[] = {
        TEXT("root\\") TEXT(PRODUCT_TAP_WIN_COMPONENT_ID),
        TEXT(PRODUCT_TAP_WIN_COMPONENT_ID)
    };
    for (int i = 0; i < _countof(HwID); ++i) {
        BOOL RebootRequired = FALSE;
        if (UpdateExisting &&
            !UpdateDriverForPlugAndPlayDevices(
                NULL, HwID[i], InfPath, INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &RebootRequired))
            PrintError(LOG_WARN, TEXT("Could not update existing %1 adapters"), HwID[i]);
        if (RebootRequired) {
            Logger(LOG_WARN, TEXT("A reboot might be required"));
            *IsRebootRequired = TRUE;
        }
    }

cleanupDelete:
    LastError = GetLastError();
    DeleteFile(CatPath);
    DeleteFile(SysPath);
    DeleteFile(InfPath);
    RemoveDirectory(RandomTempSubDirectory);
cleanupFree:
    LastError = LastError ? LastError : GetLastError();
    LocalFree(SecurityAttributes.lpSecurityDescriptor);
    SetLastError(LastError);
    return Ret;
}

static BOOL RemoveDriver(VOID)
{
    BOOL Ret = FALSE;
    HDEVINFO DeviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_NET, NULL, NULL, 0);
    if (!DeviceInfoSet)
        return FALSE;
    if (!SetupDiBuildDriverInfoList(DeviceInfoSet, NULL, SPDIT_CLASSDRIVER))
        goto cleanupDeviceInfoSet;
    Ret = TRUE;
    for (DWORD EnumIndex = 0;; ++EnumIndex)
    {
        SP_DRVINFO_DATA DriverInfo = { .cbSize = sizeof(DriverInfo) };
        if (!SetupDiEnumDriverInfo(DeviceInfoSet, NULL, SPDIT_CLASSDRIVER, EnumIndex, &DriverInfo))
        {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                break;
            goto cleanupDriverInfoList;
        }
        DWORD RequiredSize;
        if (SetupDiGetDriverInfoDetail(DeviceInfoSet, NULL, &DriverInfo, NULL, 0, &RequiredSize) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            goto cleanupDriverInfoList;
        PSP_DRVINFO_DETAIL_DATA DriverDetail = calloc(1, RequiredSize);
        if (!DriverDetail)
            goto cleanupDriverInfoList;
        DriverDetail->cbSize = sizeof(*DriverDetail);
        if (!SetupDiGetDriverInfoDetail(DeviceInfoSet, NULL, &DriverInfo, DriverDetail, RequiredSize, &RequiredSize))
        {
            free(DriverDetail);
            goto cleanupDriverInfoList;
        }
        if (IsOurDriver(DriverDetail->HardwareID))
        {
            PathStripPath(DriverDetail->InfFileName);
            Logger(LOG_INFO, TEXT("Removing existing driver"));
            if (!SetupUninstallOEMInf(DriverDetail->InfFileName, 0, NULL))
            {
                PrintError(LOG_WARN, TEXT("Unable to remove existing driver"));
                Ret = FALSE;
            }
        }
        free(DriverDetail);
    }

cleanupDriverInfoList:
    SetupDiDestroyDriverInfoList(DeviceInfoSet, NULL, SPDIT_CLASSDRIVER);
cleanupDeviceInfoSet:
    SetupDiDestroyDeviceInfoList(DeviceInfoSet);
    return Ret;
}

static BOOL
IsOurAdapter(_In_ HDEVINFO DeviceInfoSet, _Inout_ SP_DEVINFO_DATA *DeviceInfo)
{
    BOOL Found = FALSE;
    if (!SetupDiBuildDriverInfoList(DeviceInfoSet, DeviceInfo, SPDIT_COMPATDRIVER))
        return FALSE;
    for (DWORD EnumIndex = 0;; ++EnumIndex)
    {
        SP_DRVINFO_DATA DriverInfo = { .cbSize = sizeof(SP_DRVINFO_DATA) };
        if (!SetupDiEnumDriverInfo(DeviceInfoSet, DeviceInfo, SPDIT_COMPATDRIVER, EnumIndex, &DriverInfo))
        {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                break;
            continue;
        }
        DWORD RequiredSize;
        if (SetupDiGetDriverInfoDetail(DeviceInfoSet, DeviceInfo, &DriverInfo, NULL, 0, &RequiredSize) ||
            GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            continue;
        PSP_DRVINFO_DETAIL_DATA DriverDetail = calloc(1, RequiredSize);
        if (!DriverDetail)
            continue;
        DriverDetail->cbSize = sizeof(*DriverDetail);
        if (SetupDiGetDriverInfoDetail(
                DeviceInfoSet, DeviceInfo, &DriverInfo, DriverDetail, RequiredSize, &RequiredSize) &&
            IsOurDriver(DriverDetail->HardwareID))
        {
            free(DriverDetail);
            Found = TRUE;
            break;
        }
        free(DriverDetail);
    }
    SetupDiDestroyDriverInfoList(DeviceInfoSet, DeviceInfo, SPDIT_COMPATDRIVER);
    return Found;
}

static BOOL
DisableOurAdapters(_In_ HDEVINFO DeviceInfoSet, _Inout_ SP_DEVINFO_DATA_LIST **DisabledAdapters)
{
    SP_PROPCHANGE_PARAMS Params = { .ClassInstallHeader = { .cbSize = sizeof(SP_CLASSINSTALL_HEADER),
                                                            .InstallFunction = DIF_PROPERTYCHANGE },
                                    .StateChange = DICS_DISABLE,
                                    .Scope = DICS_FLAG_GLOBAL };
    BOOL Ret = TRUE;
    DWORD LastError = ERROR_SUCCESS;
    for (DWORD EnumIndex = 0;; ++EnumIndex)
    {
        SP_DEVINFO_DATA_LIST *DeviceNode = malloc(sizeof(SP_DEVINFO_DATA_LIST));
        if (!DeviceNode)
            return FALSE;
        DeviceNode->Data.cbSize = sizeof(SP_DEVINFO_DATA);
        if (!SetupDiEnumDeviceInfo(DeviceInfoSet, EnumIndex, &DeviceNode->Data))
        {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
            {
                free(DeviceNode);
                break;
            }
            goto cleanupDeviceInfoData;
        }
        if (!IsOurAdapter(DeviceInfoSet, &DeviceNode->Data))
            goto cleanupDeviceInfoData;

        ULONG Status, ProblemCode;
        if (CM_Get_DevNode_Status(&Status, &ProblemCode, DeviceNode->Data.DevInst, 0) != CR_SUCCESS ||
            ((Status & DN_HAS_PROBLEM) && ProblemCode == CM_PROB_DISABLED))
            goto cleanupDeviceInfoData;

        Logger(LOG_INFO, TEXT("Disabling existing adapter"));
        if (!SetupDiSetClassInstallParams(
                DeviceInfoSet, &DeviceNode->Data, &Params.ClassInstallHeader, sizeof(Params)) ||
            !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, DeviceInfoSet, &DeviceNode->Data))
        {
            LastError = LastError ? LastError : GetLastError();
            PrintError(LOG_WARN, TEXT("Unable to disable existing adapter"));
            Ret = FALSE;
            goto cleanupDeviceInfoData;
        }

        DeviceNode->Next = *DisabledAdapters;
        *DisabledAdapters = DeviceNode;
        continue;

    cleanupDeviceInfoData:
        free(&DeviceNode->Data);
    }
    SetLastError(LastError);
    return Ret;
}

static BOOL
RemoveOurAdapters(_In_ HDEVINFO DeviceInfoSet)
{
    SP_REMOVEDEVICE_PARAMS Params = { .ClassInstallHeader = { .cbSize = sizeof(SP_CLASSINSTALL_HEADER),
                                                              .InstallFunction = DIF_REMOVE },
                                      .Scope = DI_REMOVEDEVICE_GLOBAL };
    BOOL Ret = TRUE;
    DWORD LastError = ERROR_SUCCESS;
    for (DWORD EnumIndex = 0;; ++EnumIndex)
    {
        SP_DEVINFO_DATA DeviceInfo = { .cbSize = sizeof(SP_DEVINFO_DATA) };
        if (!SetupDiEnumDeviceInfo(DeviceInfoSet, EnumIndex, &DeviceInfo))
        {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                break;
            continue;
        }
        if (!IsOurAdapter(DeviceInfoSet, &DeviceInfo))
            continue;

        Logger(LOG_INFO, TEXT("Removing existing adapter"));
        if (!SetupDiSetClassInstallParams(DeviceInfoSet, &DeviceInfo, &Params.ClassInstallHeader, sizeof(Params)) ||
            !SetupDiCallClassInstaller(DIF_REMOVE, DeviceInfoSet, &DeviceInfo))
        {
            LastError = LastError ? LastError : GetLastError();
            PrintError(LOG_WARN, TEXT("Unable to remove existing adapter"));
            Ret = FALSE;
        }
    }
    SetLastError(LastError);
    return Ret;
}

static BOOL
EnableOurAdapters(_In_ HDEVINFO DeviceInfoSet, _In_ SP_DEVINFO_DATA_LIST *AdaptersToEnable)
{
    SP_PROPCHANGE_PARAMS Params = { .ClassInstallHeader = { .cbSize = sizeof(SP_CLASSINSTALL_HEADER),
                                                            .InstallFunction = DIF_PROPERTYCHANGE },
                                    .StateChange = DICS_ENABLE,
                                    .Scope = DICS_FLAG_GLOBAL };
    BOOL Ret = TRUE;
    DWORD LastError = ERROR_SUCCESS;

    for (SP_DEVINFO_DATA_LIST *DeviceNode = AdaptersToEnable; DeviceNode; DeviceNode = DeviceNode->Next)
    {
        Logger(LOG_INFO, TEXT("Enabling existing adapter"));
        if (!SetupDiSetClassInstallParams(
                DeviceInfoSet, &DeviceNode->Data, &Params.ClassInstallHeader, sizeof(Params)) ||
            !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, DeviceInfoSet, &DeviceNode->Data))
        {
            PrintError(LOG_WARN, TEXT("Unable to enable existing adapter"));
            LastError = LastError ? LastError : GetLastError();
            Ret = FALSE;
        }
    }
    SetLastError(LastError);
    return Ret;
}

BOOL InstallOrUpdate(_Inout_ BOOL *IsRebootRequired)
{
    BOOL Ret = FALSE;
    HDEVINFO DeviceInfoSet = SetupDiGetClassDevsEx(&GUID_DEVCLASS_NET, NULL, NULL, DIGCF_PRESENT, NULL, NULL, NULL);
    if (DeviceInfoSet == INVALID_HANDLE_VALUE)
    {
        PrintError(LOG_ERR, TEXT("Failed to get present class devices"));
        return FALSE;
    }
    SP_DEVINFO_DATA_LIST *ExistingAdapters = NULL;
    if (IsDriverLoaded())
    {
        DisableOurAdapters(DeviceInfoSet, &ExistingAdapters);
        Logger(LOG_INFO, TEXT("Waiting for driver to unload from kernel"));
        if (!EnsureDriverUnloaded())
            Logger(LOG_WARN, TEXT("Unable to unload driver, which means a reboot will likely be required"));
    }
    if (!RemoveDriver())
    {
        PrintError(LOG_WARN, TEXT("Failed to uninstall old drivers, probably some are in use by adapters. Continuing."));
    }
    if (!InstallDriver(!!ExistingAdapters, IsRebootRequired))
    {
        PrintError(LOG_ERR, TEXT("Failed to install driver"));
        goto cleanupAdapters;
    }
    Logger(LOG_INFO, TEXT("Installation successful"));
    Ret = TRUE;

cleanupAdapters:
    if (ExistingAdapters)
    {
        EnableOurAdapters(DeviceInfoSet, ExistingAdapters);
        while (ExistingAdapters)
        {
            SP_DEVINFO_DATA_LIST *Next = ExistingAdapters->Next;
            free(ExistingAdapters);
            ExistingAdapters = Next;
        }
    }
    SetupDiDestroyDeviceInfoList(DeviceInfoSet);
    return Ret;
}

BOOL Uninstall(VOID)
{
    HDEVINFO DeviceInfoSet = SetupDiGetClassDevsEx(&GUID_DEVCLASS_NET, NULL, NULL, DIGCF_PRESENT, NULL, NULL, NULL);
    if (DeviceInfoSet == INVALID_HANDLE_VALUE)
    {
        PrintError(LOG_ERR, TEXT("Failed to get present class devices"));
        return FALSE;
    }
    RemoveOurAdapters(DeviceInfoSet);
    BOOL Ret = RemoveDriver();
    if (!Ret)
        PrintError(LOG_ERR, TEXT("Failed to uninstall driver"));
    else
        Logger(LOG_INFO, TEXT("Uninstallation successful"));
    return Ret;
}
