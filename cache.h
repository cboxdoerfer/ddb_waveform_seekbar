#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <fcntl.h>
#include <sqlite3.h>
//#include "waveform.c"
void
waveform_db_open ();

void
waveform_db_close ();

void
waveform_db_init (char const *fname);

int
waveform_db_cached (char const *fname);

int
waveform_db_delete (char const *fname);

int
waveform_db_read (char const *fname, short *buffer, int buffer_len, int *channels);

void
waveform_db_write (char const *fname, short *buffer, int buffer_len, int channels, int compression);
