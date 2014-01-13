#include "cache.h"

void
waveform_db_open (char* path, int size)
{
    int rc;

    sqlite3_close(db);
    sprintf (path + size, "wavecache.db");
    rc = sqlite3_open(path, &db);
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
    if (rc != SQLITE_OK) fprintf(stderr, "SQL error: %s\n", zErrMsg);
}

int
waveform_db_read (char const *fname, float *buffer, int buffer_len, int *channels) {
    sqlite3_stmt* p = 0;
    char *zErrMsg = 0;
    int rc;

    char* query = sqlite3_mprintf("SELECT channels, data FROM wave WHERE path = '%q'", fname);
    rc = sqlite3_prepare_v2 (db, query, strlen(query), &p, NULL);
    rc = sqlite3_step (p);

    if(0){
         fprintf(stderr, "read: SQL error: %s\n", sqlite3_errstr(rc));
         sqlite3_free(zErrMsg);
         sqlite3_finalize (p);
         waveform_db_close ();
         return 0;
    }

    *channels = sqlite3_column_int (p,0);
    float *data = (float *)sqlite3_column_blob (p,1);
    int bytes = sqlite3_column_bytes (p,1);

    if (bytes > buffer_len * sizeof(float)) {
        bytes = buffer_len;
    }

    memcpy (buffer,data,bytes);

    sqlite3_free(zErrMsg);
    sqlite3_finalize (p);
    return bytes / sizeof(float);
}

inline void
waveform_db_write (char const *fname, float *buffer, int buffer_len, int channels, int compression)
{
    sqlite3_stmt* p = 0;
    char *zErrMsg = 0;
    int rc;

    char* query = "INSERT INTO wave (path, channels, compression, data) VALUES (?, ?, ?, ?);";
    rc = sqlite3_prepare_v2 (db, query, strlen(query), &p, NULL);
    if( rc!=SQLITE_OK ){
        fprintf(stderr, "write_perpare: SQL error: %s\n", sqlite3_errstr(rc));
        sqlite3_free(zErrMsg);
    }
    rc = sqlite3_bind_text (p, 1, fname, -1, SQLITE_STATIC);
    if( rc!=SQLITE_OK ){
        fprintf(stderr, "write_fname: SQL error: %s\n", sqlite3_errstr(rc));
        sqlite3_free(zErrMsg);
    }
    rc = sqlite3_bind_int (p, 2, channels);
    if( rc!=SQLITE_OK ){
        fprintf(stderr, "write_channels: SQL error: %s\n", sqlite3_errstr(rc));
        sqlite3_free(zErrMsg);
    }
    rc = sqlite3_bind_int (p, 3, compression);
    if( rc!=SQLITE_OK ){
        fprintf(stderr, "write_channels: SQL error: %s\n", sqlite3_errstr(rc));
        sqlite3_free(zErrMsg);
    }
    rc = sqlite3_bind_blob (p, 4, buffer, buffer_len, SQLITE_STATIC);
    if( rc!=SQLITE_OK ){
        fprintf(stderr, "write_data: SQL error: %s\n", sqlite3_errstr(rc));
        sqlite3_free(zErrMsg);
    }
    rc = sqlite3_step (p);
    if( rc!=SQLITE_DONE ){
        fprintf(stderr, "write_exec: SQL error: %s\n", sqlite3_errstr(rc));
        sqlite3_free(zErrMsg);
    }
    sqlite3_finalize (p);
}
