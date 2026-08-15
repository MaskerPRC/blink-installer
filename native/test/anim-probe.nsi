; Hosts anim-probe.html so the harness can measure which animation techniques
; actually reach the screen on a layered (per-pixel alpha) window.
;
; Build two variants by defining FULLSCREEN:
;   makensis anim-probe.nsi                    -> 1100x620 transparent window
;   makensis /DFULLSCREEN anim-probe.nsi       -> work-area transparent window

Unicode true
ManifestDPIAware true
RequestExecutionLevel user
Name "anim probe"
SilentInstall silent
ShowInstDetails nevershow

!ifdef FULLSCREEN
  OutFile "anim-probe-full.exe"
  !define MODE "1"
!else
  OutFile "anim-probe-win.exe"
  !define MODE "0"
!endif

!addplugindir /x86-unicode "..\..\packages\runtime\bin"

Function .onInit
  InitPluginsDir
  File `/oname=$PLUGINSDIR\node.dll` `..\..\packages\runtime\bin\node.dll`
  File `/oname=$PLUGINSDIR\index.min.html` `anim-probe.html`

  blinkkit::InitWindow /NOUNLOAD "$PLUGINSDIR" "anim probe" "1100" "620" "${MODE}"
  Pop $0
  blinkkit::ShowPage /NOUNLOAD
  Quit
FunctionEnd

Section "Main"
SectionEnd
