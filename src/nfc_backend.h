// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/sg_reader.h"

typedef struct ReaderNfc ReaderNfc;
ReaderNfc *reader_nfc_alloc(void);
void reader_nfc_free(ReaderNfc *instance);
SgBackend reader_nfc_backend(ReaderNfc *instance);
/* Call only after a synchronous backend operation has stopped its NFC worker. */
void reader_nfc_diagnostic(ReaderNfc *instance, char *out, size_t size);
