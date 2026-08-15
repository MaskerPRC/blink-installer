; Smoke test for blinkkit.dll.
;
; Not part of the shipped templates — this is the smallest script that proves
; the plugin loads, miniblink creates a transparent window, and the JS bridge
; round-trips. Compile with the staged makensis:
;
;   packages/runtime/nsis/makensis.exe native/test/smoke.nsi

Unicode true
RequestExecutionLevel user
Name "blinkkit smoke test"
OutFile "smoke.exe"
SilentInstall silent
ShowInstDetails nevershow

!addplugindir /x86-unicode "..\..\packages\runtime\bin"

Function .onInit
  InitPluginsDir

  ; The UI runtime: miniblink plus the single-file page.
  File `/oname=$PLUGINSDIR\node.dll` `..\..\packages\runtime\bin\node.dll`
  File `/oname=$PLUGINSDIR\index.min.html` `index.min.html`

  ; /NOUNLOAD on every call is mandatory, not stylistic: without it NSIS calls
  ; FreeLibrary as soon as each plugin call returns, which would discard the
  ; ability registry, the config store and the window between statements.
  ; Gives config.persist somewhere to write; without it that ability correctly
  ; refuses rather than silently doing nothing.
  blinkkit::SetRegistryKey /NOUNLOAD "HKCU" "Software\BlinkInstallerSmoke"

  blinkkit::SetConfig /NOUNLOAD "productName" "blinkkit smoke test"
  blinkkit::SetConfig /NOUNLOAD "version" "0.1.0"

  ; Prove the NSIS-side ability registry works end to end.
  GetFunctionAddress $0 OnPageAsksForNsis
  blinkkit::RegisterAbility /NOUNLOAD "sayHello" $0

  blinkkit::InitWindow /NOUNLOAD "$PLUGINSDIR" "blinkkit smoke test" "820" "880"
  Pop $1
  DetailPrint "InitWindow -> $1"

  blinkkit::ShowPage /NOUNLOAD
  Quit
FunctionEnd

Function OnPageAsksForNsis
  ; Called from the page via nsis.call({name:"sayHello"}).
  blinkkit::SetConfig /NOUNLOAD "nsisSaid" "hello from the NSIS script"
FunctionEnd

Section "Main"
SectionEnd
