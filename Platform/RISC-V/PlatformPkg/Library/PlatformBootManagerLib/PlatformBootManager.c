/** @file
  This file include all platform actions

Copyright (c) 2021-2022, Hewlett Packard Enterprise Development LP. All rights reserved.<BR>
Copyright (c) 2015, Intel Corporation. All rights reserved.<BR>

SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "PlatformBootManager.h"

EFI_GUID  mUefiShellFileGuid = {
  0x7C04A583, 0x9E3E, 0x4f1c, { 0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1 }
};

/**
  Perform the platform diagnostic, such like test memory. OEM/IBV also
  can customize this function to support specific platform diagnostic.

  @param MemoryTestLevel  The memory test intensive level
  @param QuietBoot        Indicate if need to enable the quiet boot

**/
VOID
PlatformBootManagerDiagnostics (
  IN EXTENDMEM_COVERAGE_LEVEL  MemoryTestLevel,
  IN BOOLEAN                   QuietBoot
  )
{
  EFI_STATUS  Status;

  //
  // Here we can decide if we need to show
  // the diagnostics screen
  // Notes: this quiet boot code should be remove
  // from the graphic lib
  //
  if (QuietBoot) {
    //
    // Perform system diagnostic
    //
    Status = PlatformBootManagerMemoryTest (MemoryTestLevel);
    return;
  }

  //
  // Perform system diagnostic
  //
  Status = PlatformBootManagerMemoryTest (MemoryTestLevel);
}

/**
  Return the index of the load option in the load option array.

  The function consider two load options are equal when the
  OptionType, Attributes, Description, FilePath and OptionalData are equal.

  @param Key    Pointer to the load option to be found.
  @param Array  Pointer to the array of load options to be found.
  @param Count  Number of entries in the Array.

  @retval -1          Key wasn't found in the Array.
  @retval 0 ~ Count-1 The index of the Key in the Array.
**/
INTN
PlatformFindLoadOption (
  IN CONST EFI_BOOT_MANAGER_LOAD_OPTION  *Key,
  IN CONST EFI_BOOT_MANAGER_LOAD_OPTION  *Array,
  IN UINTN                               Count
  )
{
  UINTN  Index;

  for (Index = 0; Index < Count; Index++) {
    if ((Key->OptionType == Array[Index].OptionType) &&
        (Key->Attributes == Array[Index].Attributes) &&
        (StrCmp (Key->Description, Array[Index].Description) == 0) &&
        (CompareMem (Key->FilePath, Array[Index].FilePath, GetDevicePathSize (Key->FilePath)) == 0) &&
        (Key->OptionalDataSize == Array[Index].OptionalDataSize) &&
        (CompareMem (Key->OptionalData, Array[Index].OptionalData, Key->OptionalDataSize) == 0))
    {
      return (INTN)Index;
    }
  }

  return -1;
}

/**
  Register a boot option using a file GUID in the FV.

  @param FileGuid     The file GUID name in FV.
  @param Description  The boot option description.
  @param Attributes   The attributes used for the boot option loading.
**/
VOID
PlatformRegisterFvBootOption (
  EFI_GUID  *FileGuid,
  CHAR16    *Description,
  UINT32    Attributes
  )
{
  EFI_STATUS                         Status;
  UINTN                              OptionIndex;
  EFI_BOOT_MANAGER_LOAD_OPTION       NewOption;
  EFI_BOOT_MANAGER_LOAD_OPTION       *BootOptions;
  UINTN                              BootOptionCount;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;
  EFI_LOADED_IMAGE_PROTOCOL          *LoadedImage;
  EFI_DEVICE_PATH_PROTOCOL           *DevicePath;

  Status = gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  ASSERT_EFI_ERROR (Status);

  EfiInitializeFwVolDevicepathNode (&FileNode, FileGuid);
  DevicePath = AppendDevicePathNode (
                 DevicePathFromHandle (LoadedImage->DeviceHandle),
                 (EFI_DEVICE_PATH_PROTOCOL *)&FileNode
                 );

  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             Attributes,
             Description,
             DevicePath,
             NULL,
             0
             );
  if (!EFI_ERROR (Status)) {
    BootOptions = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);

    OptionIndex = PlatformFindLoadOption (&NewOption, BootOptions, BootOptionCount);

    if (OptionIndex == -1) {
      Status = EfiBootManagerAddLoadOptionVariable (&NewOption, (UINTN)-1);
      ASSERT_EFI_ERROR (Status);
    }

    EfiBootManagerFreeLoadOption (&NewOption);
    EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
  }
}

VOID PlatformRegisterOptionsAndKeys(VOID)
{
    EFI_STATUS Status;
    EFI_INPUT_KEY Enter;
    EFI_INPUT_KEY F2;
    EFI_INPUT_KEY Esc;
    EFI_BOOT_MANAGER_LOAD_OPTION BootOption;

    //
    // Register ENTER as CONTINUE key
    //
    Enter.ScanCode = SCAN_NULL;
    Enter.UnicodeChar = CHAR_CARRIAGE_RETURN;
    Status = EfiBootManagerRegisterContinueKeyOption(0, &Enter, NULL);
    ASSERT_EFI_ERROR(Status);

    //
    // Map F2 to Boot Manager Menu
    //
    F2.ScanCode = SCAN_F2;
    F2.UnicodeChar = CHAR_NULL;
    Esc.ScanCode = SCAN_ESC;
    Esc.UnicodeChar = CHAR_NULL;
    Status = EfiBootManagerGetBootManagerMenu(&BootOption);
    ASSERT_EFI_ERROR(Status);
    Status = EfiBootManagerAddKeyOptionVariable(NULL, (UINT16)BootOption.OptionNumber, 0, &F2, NULL);
    ASSERT(Status == EFI_SUCCESS || Status == EFI_ALREADY_STARTED);
    Status = EfiBootManagerAddKeyOptionVariable(NULL, (UINT16)BootOption.OptionNumber, 0, &Esc, NULL);
    ASSERT(Status == EFI_SUCCESS || Status == EFI_ALREADY_STARTED);
}

/**
  Do the platform specific action before the console is connected.

  Such as:
    Update console variable;
    Register new Driver#### or Boot####;
    Signal ReadyToLock event.
**/
VOID
EFIAPI
PlatformBootManagerBeforeConsole (
  VOID
  )
{
  UINTN                         Index;
  EFI_STATUS                    Status;

  DEBUG((DEBUG_INFO, "PlatformBootManagerBeforeConsole\n"));

  //
  // Signal EndOfDxe PI Event
  //
  EfiEventGroupSignal (&gEfiEndOfDxeEventGroupGuid);

  //
  // Update the console variables.
  //
  for (Index = 0; gPlatformConsole[Index].DevicePath != NULL; Index++) {
    DEBUG ((DEBUG_INFO, "Check gPlatformConsole %d\n", Index));
    if ((gPlatformConsole[Index].ConnectType & CONSOLE_IN) == CONSOLE_IN) {
      Status = EfiBootManagerUpdateConsoleVariable (ConIn, gPlatformConsole[Index].DevicePath, NULL);
      DEBUG ((DEBUG_INFO, "CONSOLE_IN variable set %s : %r\n", ConvertDevicePathToText (gPlatformConsole[Index].DevicePath, FALSE, FALSE), Status));
    }

    if ((gPlatformConsole[Index].ConnectType & CONSOLE_OUT) == CONSOLE_OUT) {
      Status = EfiBootManagerUpdateConsoleVariable (ConOut, gPlatformConsole[Index].DevicePath, NULL);
      DEBUG ((DEBUG_INFO, "CONSOLE_OUT variable set %s : %r\n", ConvertDevicePathToText (gPlatformConsole[Index].DevicePath, FALSE, FALSE), Status));
    }

    if ((gPlatformConsole[Index].ConnectType & STD_ERROR) == STD_ERROR) {
      Status = EfiBootManagerUpdateConsoleVariable (ErrOut, gPlatformConsole[Index].DevicePath, NULL);
      DEBUG ((DEBUG_INFO, "STD_ERROR variable set %r", Status));
    }
  }

  PlatformRegisterOptionsAndKeys();

  // 462CAA21-7614-4503-836E-8AB6F4662331
  EFI_GUID UiAppGuid    = {0x462CAA21, 0x7614, 0x4503, {0x83, 0x6E, 0x8A, 0xB6, 0xF4, 0x66, 0x23, 0x31}};

  // EEC25BDC-67F2-4D95-B1D5-F81B2039D11D
  EFI_GUID BootMgrGuid  = {0xEEC25BDC, 0x67F2, 0x4D95, {0xF8, 0xD5, 0xF8, 0x1B, 0x20, 0x39, 0xD1, 0x1D}};

  PlatformRegisterFvBootOption(&BootMgrGuid, L"UEFI BootManagerMenuApp", LOAD_OPTION_ACTIVE);

  PlatformRegisterFvBootOption(&UiAppGuid, L"Firmware Setup", LOAD_OPTION_ACTIVE);
}

/**
  The function is called when no boot option could be launched,
  including platform recovery options and options pointing to applications
  built into firmware volumes.

  If this function returns, BDS attempts to enter an infinite loop.
**/
VOID EFIAPI PlatformBootManagerUnableToBoot(VOID)
{
    EFI_STATUS Status;
    EFI_INPUT_KEY Key;
    EFI_BOOT_MANAGER_LOAD_OPTION BootManagerMenu;
    UINTN Index;

    /*if (FeaturePcdGet(PcdBootRestrictToFirmware))
    {
        AsciiPrint("%a: No bootable option was found.\n", gEfiCallerBaseName);
        CpuDeadLoop();
    }*/

    //
    // BootManagerMenu doesn't contain the correct information when return status
    // is EFI_NOT_FOUND.
    //
    Status = EfiBootManagerGetBootManagerMenu(&BootManagerMenu);
    if (EFI_ERROR(Status))
    {
        return;
    }

    //
    // Normally BdsDxe does not print anything to the system console, but this is
    // a last resort -- the end-user will likely not see any DEBUG messages
    // logged in this situation.
    //
    // AsciiPrint() will NULL-check gST->ConOut internally. We check gST->ConIn
    // here to see if it makes sense to request and wait for a keypress.
    //
    if (gST->ConIn != NULL)
    {
        AsciiPrint("%a: No bootable option or device was found.\n"
                   "%a: Press any key to enter the Boot Manager Menu.\n",
                   gEfiCallerBaseName, gEfiCallerBaseName);
        Status = gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
        ASSERT_EFI_ERROR(Status);
        ASSERT(Index == 0);

        //
        // Drain any queued keys.
        //
        while (!EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &Key)))
        {
            //
            // just throw away Key
            //
        }
    }

    for (;;)
    {
        EfiBootManagerBoot(&BootManagerMenu);
    }
}

/**
  Do the platform specific action after the console is connected.

  Such as:
    Dynamically switch output mode;
    Signal console ready platform customized event;
    Run diagnostics like memory testing;
    Connect certain devices;
    Dispatch additional option roms.
**/
VOID
EFIAPI
PlatformBootManagerAfterConsole (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Black;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  White;

  DEBUG((DEBUG_INFO, "PlatformBootManagerAfterConsole\n"));

  //
  // Register UEFI Shell
  //
  PlatformRegisterFvBootOption(&mUefiShellFileGuid, L"EFI Internal Shell",
                               LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP);

  Black.Blue = Black.Green = Black.Red = Black.Reserved = 0;
  White.Blue = White.Green = White.Red = White.Reserved = 0xFF;

  EfiBootManagerConnectAll();
  EfiBootManagerRefreshAllBootOption();

  PlatformBootManagerDiagnostics(QUICK, TRUE);
}
