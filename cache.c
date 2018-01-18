/*
    Waveform seekbar plugin for the DeaDBeeF audio player

    Copyright (C) 2014 Christian Boxdörfer <christian.boxdoerfer@posteo.de>

    Based on sndfile-tools waveform by Erik de Castro Lopo.
        waveform.c - v1.04
        Copyright (C) 2007-2012 Erik de Castro Lopo <erikd@mega-nerd.com>
        Copyright (C) 2012 Robin Gareus <robin@gareus.org>
        Copyright (C) 2013 driedfruit <driedfruit@mindloop.net>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "cache.h"

static sqlite3 *db;

void
waveform_db_open (const char* path)
{
    sqlite3_close(db);
    char db_path[1024] = "";
    snprintf (db_path, sizeof(db_path)/sizeof (char), "%s/%s", path, "wavecache.db");
    int rc = sqlite3_open(db_path, &db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }
}

void
waveform_db_close ()
{
    sqlite3_close(db);
}

void
waveform_db_init (char const *fname)
{
    char *zErrMsg = 0;
    int rc;

    char *query = "CREATE TABLE IF NOT EXISTS wave ( path TEXT PRIMARY KEY NOT NULL, channels INTEGER NOT NULL, compression INTEGER, data BLOB)";
    rc = sqlite3_exec(db, query, NULL, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
    }
    sqlite3_free(zErrMsg);
}

int
waveform_db_cached (char const *fname)
{
    int rc;
    sqlite3_stmt* p = 0;

    char* query = sqlite3_mprintf ("SELECT * FROM wave WHERE path = '%q'", fname);
    rc = sqlite3_prepare_v2 (db, query, strlen(query), &p, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "cached_perpare: SQL error: %d\n", rc);
    }
    rc = sqlite3_step (p);
    if (rc == SQLITE_ROW) {
        sqlite3_finalize (p);
        return 1;
    }
    sqlite3_finalize (p);
    return 0;
}

int
waveform_db_delete (char const *fname)
{
    int rc;
    sqlite3_stmt* p = 0;

    char* query = sqlite3_mprintf ("DELETE FROM wave WHERE path = '%q'", fname);
    rc = sqlite3_prepare_v2 (db, query, strlen(query), &p, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "delete_perpare: SQL error: %d\n", rc);
    }
    rc = sqlite3_step (p);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "delete_exec: SQL error: %d\n", rc);
    }
    sqlite3_finalize (p);
    return 1;
}

int
waveform_db_read (char const *fname, short *buffer, int buffer_len, int *channels)
{
    int rc;
    sqlite3_stmt* p = 0;

    char* query = sqlite3_mprintf("SELECT channels, data FROM wave WHERE path = '%q'", fname);
    rc = sqlite3_prepare_v2 (db, query, strlen(query), &p, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "read_perpare: SQL error: %d\n", rc);
    }
    rc = sqlite3_step (p);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize (p);
        return 0;
    }
    else if (rc != SQLITE_ROW) {
        fprintf(stderr, "read_exec: SQL error: %d\n", rc);
        sqlite3_finalize (p);
        return 0;
    }

    *channels = sqlite3_column_int (p,0);
    short *data = (short *)sqlite3_column_blob (p,1);

    int bytes = sqlite3_column_bytes (p,1);
    if (bytes > buffer_len * sizeof(short)) {
        bytes = buffer_len;
    }
    memcpy (buffer,data,bytes);

    sqlite3_finalize (p);
    return bytes / sizeof(short);
}

void
waveform_db_write (char const *fname, short *buffer, int buffer_len, int channels, int compression)
{
    int rc;
    sqlite3_stmt* p = 0;

    char* query = "INSERT INTO wave (path, channels, compression, data) VALUES (?, ?, ?, ?);";
    rc = sqlite3_prepare_v2 (db, query, strlen(query), &p, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "write_perpare: SQL error: %d\n", rc);
    }
    rc = sqlite3_bind_text (p, 1, fname, -1, SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "write_fname: SQL error: %d\n", rc);
    }
    rc = sqlite3_bind_int (p, 2, channels);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "write_channels: SQL error: %d\n", rc);
    }
    rc = sqlite3_bind_int (p, 3, compression);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "write_compression: SQL error: %d\n", rc);
    }
    rc = sqlite3_bind_blob (p, 4, buffer, buffer_len, SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "write_data: SQL error: %d\n", rc);
    }
    rc = sqlite3_step (p);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "write_exec: SQL error: %d\n", rc);
    }
    sqlite3_finalize (p);
}
