// SPDX-License-Identifier: GPL-3.0-or-later
#include "card_browser.h"
#include <gui/gui.h>
#include <gui/icon_i.h>
#include <gui/modules/file_browser.h>
#include <gui/view_dispatcher.h>

/* Uncompressed 10 x 10 card outline, two bytes per row. A file-type icon only:
 * card compatibility is checked after selection by the NFC loader. */
static const uint8_t card_bits[] = {
    0x00, 0xfe, 0x01, 0x01, 0x02, 0x19, 0x02, 0x25, 0x02, 0x25, 0x02,
    0x19, 0x02, 0x01, 0x02, 0xf1, 0x02, 0x01, 0x02, 0xfe, 0x01,
};
static const uint8_t *const card_frames[] = {card_bits};
static const Icon card_icon = {
    .width = 10, .height = 10, .frame_count = 1, .frame_rate = 0, .frames = card_frames,
};
typedef struct {
    ViewDispatcher *dispatcher;
    bool selected;
} CardBrowser;

static void selected(void *context) {
    CardBrowser *browser = context;
    browser->selected = true;
    view_dispatcher_stop(browser->dispatcher);
}
static bool back(void *context) {
    UNUSED(context);
    return false;
}
bool card_browser_select(FuriString *path) {
    Gui *gui = furi_record_open(RECORD_GUI);
    CardBrowser browser = {.dispatcher = view_dispatcher_alloc(), .selected = false};
    FileBrowser *files = file_browser_alloc(path);
    file_browser_configure(files, ".nfc", "/ext/nfc", true, true, &card_icon, true);
    file_browser_set_callback(files, selected, &browser);
    view_dispatcher_set_event_callback_context(browser.dispatcher, &browser);
    view_dispatcher_set_navigation_event_callback(browser.dispatcher, back);
    view_dispatcher_add_view(browser.dispatcher, 0, file_browser_get_view(files));
    view_dispatcher_attach_to_gui(browser.dispatcher, gui, ViewDispatcherTypeFullscreen);
    file_browser_start(files, path);
    view_dispatcher_switch_to_view(browser.dispatcher, 0);
    /* The dispatcher queues GUI input and handles it on this loader thread.
     * The USB/NFC request loop stays on the main application thread. */
    view_dispatcher_run(browser.dispatcher);
    /* run() drains the release of the exit key before detaching the view. */
    view_dispatcher_remove_view(browser.dispatcher, 0);
    file_browser_stop(files);
    file_browser_free(files);
    view_dispatcher_free(browser.dispatcher);
    furi_record_close(RECORD_GUI);
    return browser.selected;
}
