#pragma once

// Crash reporting for the bridge: ClaudeCrash.log in the game's Logs folder.
void ClaudeInstallCrashHandler();
// Must run before the DLL unloads: the handler is registered process-wide.
void ClaudeUninstallCrashHandler();
// Named in the crash log as what was running; "" clears it.
void ClaudeCrashSetContext(const char* text);
