#pragma once

// Reading the counter does not fetch clipboard contents. A named pasteboard is
// used by isolated native tests; production uses the general pasteboard.
long long deskPortClipboardChangeCount(const char* name = nullptr);
