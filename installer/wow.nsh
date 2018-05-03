; ---------------------
;       wow.nsh
; ---------------------
;
; A few simple macros for building installers that need to be aware of whether
; it is running on the same processor architecture as the OS.
;
; RunningX64 checks if the installer is running on an x86_64 64-bit OS.
; RunningX86 checks if the installer is running on an x86 32-bit OS.
; RunningARM64 checks if the installer is running on an ARM64 64-bit OS.
; IsWow64 checks if the native OS and the installer architectures differ.
; ** Please Note: ** IsWow64 no longer denotes x86_64 hosting x86.
;
;   ${If} ${RunningX64}
;     MessageBox MB_OK "running on x64"
;   ${EndIf}
;
; DisableX64FSRedirection disables file system redirection.
; EnableX64FSRedirection enables file system redirection.
; ** Please Note: ** X64 is an incorrect yet backwards-compatible name for this
;                    macro.
;
;   SetOutPath $SYSDIR
;   ${DisableX64FSRedirection}
;   File some.dll # extracts to C:\Windows\System32
;   ${EnableX64FSRedirection}
;   File some.dll # extracts to C:\Windows\SysWOW64
;

!ifndef ___WOW__NSH___
!define ___WOW__NSH___

!include LogicLib.nsh

; Per https://msdn.microsoft.com/en-us/library/windows/desktop/mt804345(v=vs.85).aspx
; 0x0
!define IMAGE_FILE_MACHINE_UNKNOWN 0
; 0x014c
!define IMAGE_FILE_MACHINE_I386 332
; 0x0200
!define IMAGE_FILE_MACHINE_IA64 512
; 0x8664
!define IMAGE_FILE_MACHINE_AMD64 34404
; 0x01c4 (ARM32)
!define IMAGE_FILE_MACHINE_ARMNT 452
; 0xAA64
!define IMAGE_FILE_MACHINE_ARM64 43620

Var _Wow_WowHost
Var _Wow_WowGuest
Var _Wow_ReturnValue
Var _Wow_CurrentProcess

!macro _Wow_GetWowArchitectures LabelPrefix
  ; Get the current process handle
  System::Call kernel32::GetCurrentProcess()p.s
  Pop $_Wow_CurrentProcess
  Push $_Wow_CurrentProcess
  ; push order is return value, left-to-right output args
  System::Call "kernel32::IsWow64Process2(p, *i, *i)i (s., .s, .s).s"
  ; Return value
  Pop $_Wow_ReturnValue
  ; ProcessMachine
  Pop $_Wow_WowGuest
  ; NativeMachine
  Pop $_Wow_WowHost

  ; To get around the fact that functions need un. versions and LogicLib can't be
  ; used in macros due to label duplication, this section uses StrCmp and prefixed labels.
  ;${If} $_Wow_ReturnValue != 1
  StrCmp $_Wow_ReturnValue 1 _Wow_${LabelPrefix}_EndIf0 0
    DetailPrint "IsWow64Process2 not available; falling back to IsWow64Process."
    ; Either the function call failed or this is an older OS with only IsWow64Process.
    ; Fall back and imitate the better function per the MSDN docs.
	Push $_Wow_CurrentProcess
    System::Call "kernel32::IsWow64Process(p, *i)i (s., .s).s"
    Pop $_Wow_ReturnValue
    Pop $_Wow_WowGuest
    ;${If} $_Wow_WowGuest != 0
    StrCmp $_Wow_WowGuest 0 _Wow_${LabelPrefix}_Else1 0
      ; This is x86 on AMD64.
      StrCpy $_Wow_WowGuest ${IMAGE_FILE_MACHINE_I386}
      StrCpy $_Wow_WowHost ${IMAGE_FILE_MACHINE_AMD64}
    Goto _Wow_${LabelPrefix}_EndIf1
    ;${Else}
	_Wow_${LabelPrefix}_Else1:
      StrCpy $_Wow_WowGuest ${IMAGE_FILE_MACHINE_UNKNOWN}
      ; This is either x86 native or AMD64 native. This is known at script compile time.
      !if ${NSIS_PTR_SIZE} > 4
        StrCpy $_Wow_WowHost ${IMAGE_FILE_MACHINE_AMD64}
      !else
        StrCpy $_Wow_WowHost ${IMAGE_FILE_MACHINE_I386}
      !endif
    ;${EndIf}
    _Wow_${LabelPrefix}_EndIf1:
  ;${EndIf}
  _Wow_${LabelPrefix}_EndIf0:
!macroend

!define RunningARM64 `"" RunningARM64 ""`
!macro _RunningARM64 _left _right _stayTarget _goTarget
  !insertmacro _Wow_GetWowArchitectures ${__COUNTER__}
  !insertmacro _= $_Wow_WowHost ${IMAGE_FILE_MACHINE_ARM64} `${_stayTarget}` `${_goTarget}`
!macroend

!define IsWow64 `"" IsWow64 ""`
!macro _IsWow64 _a _b _t _f
  !insertmacro _Wow_GetWowArchitectures ${__COUNTER__}
  !insertmacro _!= $_Wow_WowGuest ${IMAGE_FILE_MACHINE_UNKNOWN} `${_t}` `${_f}`
!macroend

!define RunningX86 `"" RunningX86 ""`
!macro _RunningX86 _a _b _t _f 
  !insertmacro _Wow_GetWowArchitectures ${__COUNTER__}
  !insertmacro _= $_Wow_WowHost ${IMAGE_FILE_MACHINE_I386} `${_t}` `${_f}`
!macroend

!define RunningX64 `"" RunningX64 ""`
!macro _RunningX64 _a _b _t _f 
  !insertmacro _Wow_GetWowArchitectures ${__COUNTER__}
  !insertmacro _= $_Wow_WowHost ${IMAGE_FILE_MACHINE_AMD64} `${_t}` `${_f}`
!macroend

; This currently does not support nested calls because the argument's value is not preservable.
!define DisableX64FSRedirection "!insertmacro DisableX64FSRedirection"
!macro DisableX64FSRedirection
  System::Call kernel32::Wow64EnableWow64FsRedirection(i0)
!macroend

!define EnableX64FSRedirection "!insertmacro EnableX64FSRedirection"
!macro EnableX64FSRedirection
  System::Call kernel32::Wow64EnableWow64FsRedirection(i1)
!macroend


!endif # !___WOW__NSH___
