// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <furi.h>

/* Call on the loader thread. This owns the UI event loop until cancellation or selection. */
bool card_browser_select(FuriString *path);
