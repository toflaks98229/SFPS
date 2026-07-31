@rem Double-click me while you are authoring.
@rem
@rem Builds both binaries and launches the dev one:
@rem
@rem   build\game.exe      shipped build. Self-contained: the assets are baked
@rem                       in at compile time, so it never reads assets\ and
@rem                       never reflects an edit until you rebuild. This is
@rem                       the one the 1.44MB budget measures.
@rem
@rem   build\game_dev.exe  authoring build. Reads assets\ live and reloads on
@rem                       save, so a change in modeledit shows up without a
@rem                       rebuild. Its title bar says "assets: LIVE".
@rem
@rem This wrapper exists because PowerShell's default execution policy on a
@rem fresh Windows install is Restricted, which blocks build.ps1 outright.
@rem -ExecutionPolicy Bypass applies to this one process only -- it does not
@rem change any machine or user setting.
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
if errorlevel 1 goto done
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Debug %*
if errorlevel 1 goto done
start "" "%~dp0build\game_dev.exe"
:done
echo.
pause
