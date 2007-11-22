#include <lightmediascanner_db.h>
#include "lightmediascanner_db_private.h"
#include <stdlib.h>
#include <stdio.h>

struct lms_db_audio {
    sqlite3 *db;
    sqlite3_stmt *insert_audio;
    sqlite3_stmt *insert_artist;
    sqlite3_stmt *insert_album;
    sqlite3_stmt *insert_genre;
    sqlite3_stmt *get_artist;
    sqlite3_stmt *get_album;
    sqlite3_stmt *get_genre;
    unsigned int _references;
    unsigned int _is_started:1;
};

static lms_db_audio_t *_singleton = NULL;

static int
_db_create(sqlite3 *db, const char *name, const char *sql)
{
    char *err;
    int r;

    r = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (r != SQLITE_OK) {
        fprintf(stderr, "ERROR: could not create \"%s\": %s\n", name, err);
        sqlite3_free(err);
        return -1;
    }

    return 0;
}

static int
_db_create_tables_if_required(sqlite3 *db)
{
    int ret;

    ret = _db_create(db, "audios",
        "CREATE TABLE IF NOT EXISTS audios ("
        "id INTEGER PRIMARY KEY, "
        "title TEXT, "
        "album_id INTEGER, "
        "genre_id INTEGER, "
        "length REAL NOT NULL, "
        "trackno INTEGER, "
        "rating INTEGER "
        ")");
    if (ret != 0)
        goto done;

    ret = _db_create(db, "audio_artists",
        "CREATE TABLE IF NOT EXISTS audio_artists ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT UNIQUE"
        ")");
    if (ret != 0)
        goto done;

    ret = _db_create(db, "audio_albums",
        "CREATE TABLE IF NOT EXISTS audio_albums ("
        "id INTEGER PRIMARY KEY, "
        "artist_id INTEGER, "
        "name TEXT"
        ")");
    if (ret != 0)
        goto done;

    ret = _db_create(db, "audio_genres",
        "CREATE TABLE IF NOT EXISTS audio_genres ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT UNIQUE"
        ")");
    if (ret != 0)
        goto done;

    ret = _db_create(db, "audios_title_idx",
        "CREATE INDEX IF NOT EXISTS "
        "audios_title_idx ON audios (title)");
    if (ret != 0)
        goto done;

    ret = _db_create(db, "audios_album_idx",
        "CREATE INDEX IF NOT EXISTS "
        "audios_album_idx ON audios (album_id)");
    if (ret != 0)
        goto done;

    ret = _db_create(db, "audios_genre_idx",
        "CREATE INDEX IF NOT EXISTS "
        "audios_genre_idx ON audios (genre_id)");
    if (ret != 0)
        goto done;

    ret = _db_create(db, "audio_artists_name_idx",
        "CREATE INDEX IF NOT EXISTS "
        "audio_artists_name_idx ON audio_artists (name)");
    if (ret != 0)
        goto done;

    ret = _db_create(db, "audio_albums_name_idx",
        "CREATE INDEX IF NOT EXISTS "
        "audio_albums_name_idx ON audio_albums (name)");
    if (ret != 0)
        goto done;

    ret = _db_create(db, "audio_albums_artist_idx",
        "CREATE INDEX IF NOT EXISTS "
        "audio_albums_artist_idx ON audio_albums (artist_id)");
    if (ret != 0)
        goto done;

    ret = _db_create(db, "audio_genres_name_idx",
        "CREATE INDEX IF NOT EXISTS "
        "audio_albums_name_idx ON audio_albums (name)");
    if (ret != 0)
        goto done;

    ret = lms_db_create_trigger_if_not_exists(db,
        "delete_audios_on_files_deleted "
        "DELETE ON files FOR EACH ROW BEGIN"
        "   DELETE FROM audios WHERE id = OLD.id; END;");
    if (ret != 0)
        goto done;

    ret = lms_db_create_trigger_if_not_exists(db,
        "delete_files_on_audios_deleted "
        "DELETE ON audios FOR EACH ROW BEGIN"
        " DELETE FROM files WHERE id = OLD.id; END;");
    if (ret != 0)
        goto done;

    ret = lms_db_create_trigger_if_not_exists(db,
        "delete_audios_on_albums_deleted "
        "DELETE ON audio_albums FOR EACH ROW BEGIN"
        " DELETE FROM audios WHERE album_id = OLD.id; END;");
    if (ret != 0)
        goto done;

    ret = lms_db_create_trigger_if_not_exists(db,
        "delete_audios_on_genres_deleted "
        "DELETE ON audio_genres FOR EACH ROW BEGIN"
        " DELETE FROM audios WHERE genre_id = OLD.id; END;");
    if (ret != 0)
        goto done;

    ret = lms_db_create_trigger_if_not_exists(db,
        "delete_audio_albums_on_artists_deleted "
        "DELETE ON audio_artists FOR EACH ROW BEGIN"
        " DELETE FROM audio_albums WHERE artist_id = OLD.id; END;");

  done:
    return ret;
}

lms_db_audio_t *
lms_db_audio_new(sqlite3 *db)
{
    lms_db_audio_t *lda;

    if (_singleton) {
        _singleton->_references++;
        return _singleton;
    }

    if (!db)
        return NULL;

    if (_db_create_tables_if_required(db) != 0) {
        fprintf(stderr, "ERROR: could not create tables.\n");
        return NULL;
    }

    lda = calloc(1, sizeof(lms_db_audio_t));
    lda->_references = 1;
    lda->db = db;

    return lda;
}

int
lms_db_audio_start(lms_db_audio_t *lda)
{
    if (!lda)
        return -1;
    if (lda->_is_started)
        return 0;

    lda->insert_audio = lms_db_compile_stmt(lda->db,
        "INSERT OR REPLACE INTO audios "
        "(id, title, album_id, genre_id, length, trackno, rating) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    if (!lda->insert_audio)
        return -2;

    lda->insert_artist = lms_db_compile_stmt(lda->db,
        "INSERT INTO audio_artists (name) VALUES (?)");
    if (!lda->insert_artist)
        return -3;

    lda->insert_album = lms_db_compile_stmt(lda->db,
        "INSERT INTO audio_albums (artist_id, name) VALUES (?, ?)");
    if (!lda->insert_album)
        return -4;

    lda->insert_genre = lms_db_compile_stmt(lda->db,
        "INSERT INTO audio_genres (name) VALUES (?)");
    if (!lda->insert_genre)
        return -5;

    lda->get_artist = lms_db_compile_stmt(lda->db,
        "SELECT id FROM audio_artists WHERE name = ? LIMIT 1");
    if (!lda->get_artist)
        return -6;

    lda->get_album = lms_db_compile_stmt(lda->db,
        "SELECT id FROM audio_albums WHERE name = ? AND artist_id = ? LIMIT 1");
    if (!lda->get_album)
        return -7;

    lda->get_genre = lms_db_compile_stmt(lda->db,
        "SELECT id FROM audio_genres WHERE name = ? LIMIT 1");
    if (!lda->get_genre)
        return -8;

    lda->_is_started = 1;
    return 0;
}

int
lms_db_audio_free(lms_db_audio_t *lda)
{
    if (!lda)
        return -1;
    if (lda->_references == 0) {
        fprintf(stderr, "ERROR: over-called lms_db_audio_free(%p)\n", lda);
        return -1;
    }

    lda->_references--;
    if (lda->_references > 0)
        return 0;

    if (lda->insert_audio)
        lms_db_finalize_stmt(lda->insert_audio, "insert_audio");

    if (lda->insert_artist)
        lms_db_finalize_stmt(lda->insert_artist, "insert_artist");

    if (lda->insert_album)
        lms_db_finalize_stmt(lda->insert_album, "insert_album");

    if (lda->insert_genre)
        lms_db_finalize_stmt(lda->insert_genre, "insert_genre");

    if (lda->get_artist)
        lms_db_finalize_stmt(lda->get_artist, "get_artist");

    if (lda->get_album)
        lms_db_finalize_stmt(lda->get_album, "get_album");

    if (lda->get_genre)
        lms_db_finalize_stmt(lda->get_genre, "get_genre");

    free(lda);
    _singleton = NULL;

    return 0;
}

static int
_db_get_id_by_name(sqlite3_stmt *stmt, const struct lms_string_size *name, int64_t *id)
{
    int r, ret;

    ret = lms_db_bind_text(stmt, 1, name->str, name->len);
    if (ret != 0)
        goto done;

    r = sqlite3_step(stmt);
    if (r == SQLITE_DONE) {
        ret = 1;
        goto done;
    }

    if (r != SQLITE_ROW) {
        fprintf(stderr, "ERROR: could not get id by name: %s\n",
                sqlite3_errmsg(sqlite3_db_handle(stmt)));
        ret = -2;
        goto done;
    }

    *id = sqlite3_column_int64(stmt, 0);
    ret = 0;

  done:
    lms_db_reset_stmt(stmt);

    return ret;

}
static int
_db_insert_name(sqlite3_stmt *stmt, const struct lms_string_size *name, int64_t *id)
{
    int r, ret;

    ret = lms_db_bind_text(stmt, 1, name->str, name->len);
    if (ret != 0)
        goto done;

    r = sqlite3_step(stmt);
    if (r != SQLITE_DONE) {
        fprintf(stderr, "ERROR: could not insert name: %s\n",
                sqlite3_errmsg(sqlite3_db_handle(stmt)));
        ret = -2;
        goto done;
    }

    *id = sqlite3_last_insert_rowid(sqlite3_db_handle(stmt));
    ret = 0;

  done:
    lms_db_reset_stmt(stmt);

    return ret;
}

static int
_db_get_artist(lms_db_audio_t *lda, const struct lms_audio_info *info, int64_t *artist_id)
{
    return _db_get_id_by_name(lda->get_artist, &info->artist, artist_id);
}

static int
_db_insert_artist(lms_db_audio_t *lda, const struct lms_audio_info *info, int64_t *artist_id)
{
    int r;

    if (!info->artist.str) /* fast path for unknown artist */
        return 1;

    r =_db_get_artist(lda, info, artist_id);
    if (r == 0)
        return 0;
    else if (r < 0)
        return -1;

    return _db_insert_name(lda->insert_artist, &info->artist, artist_id);
}

static int
_db_get_album(lms_db_audio_t *lda, const struct lms_audio_info *info, int64_t *artist_id, int64_t *album_id)
{
    sqlite3_stmt *stmt;
    int r, ret;

    stmt = lda->get_album;

    ret = lms_db_bind_text(stmt, 1, info->album.str, info->album.len);
    if (ret != 0)
        goto done;

    ret = lms_db_bind_int64_or_null(stmt, 2, album_id);
    if (ret != 0)
        goto done;

    r = sqlite3_step(stmt);
    if (r == SQLITE_DONE) {
        ret = 1;
        goto done;
    }

    if (r != SQLITE_ROW) {
        fprintf(stderr, "ERROR: could not get album from table: %s\n",
                sqlite3_errmsg(lda->db));
        ret = -2;
        goto done;
    }

    *album_id = sqlite3_column_int64(stmt, 0);
    ret = 0;

  done:
    lms_db_reset_stmt(stmt);

    return ret;

}

static int
_db_insert_album(lms_db_audio_t *lda, const struct lms_audio_info *info, int64_t *album_id)
{
    int r, ret, ret_artist;
    int64_t artist_id;
    sqlite3_stmt *stmt;

    if (!info->album.str) /* fast path for unknown album */
        return 1;

    ret_artist = _db_insert_artist(lda, info, &artist_id);
    if (ret_artist < 0)
        return -1;

    r =_db_get_album(lda, info,
                     (ret_artist == 0) ? &artist_id : NULL,
                     album_id);
    if (r == 0)
        return 0;
    else if (r < 0)
        return -1;

    stmt = lda->insert_album;
    ret = lms_db_bind_int64_or_null(stmt, 1,
                                    (ret_artist == 0) ? &artist_id : NULL);
    if (ret != 0)
        goto done;

    ret = lms_db_bind_text(stmt, 2, info->album.str, info->album.len);
    if (ret != 0)
        goto done;

    r = sqlite3_step(stmt);
    if (r != SQLITE_DONE) {
        fprintf(stderr, "ERROR: could not insert audio album: %s\n",
                sqlite3_errmsg(lda->db));
        ret = -3;
        goto done;
    }

    *album_id = sqlite3_last_insert_rowid(lda->db);
    ret = 0;

  done:
    lms_db_reset_stmt(stmt);

    return ret;
}

static int
_db_get_genre(lms_db_audio_t *lda, const struct lms_audio_info *info, int64_t *genre_id)
{
    return _db_get_id_by_name(lda->get_genre, &info->genre, genre_id);
}

static int
_db_insert_genre(lms_db_audio_t *lda, const struct lms_audio_info *info, int64_t *genre_id)
{
    int r;

    if (!info->genre.str) /* fast path for unknown genre */
        return 1;

    r =_db_get_genre(lda, info, genre_id);
    if (r == 0)
        return 0;
    else if (r < 0)
        return -1;

    return _db_insert_name(lda->insert_genre, &info->genre, genre_id);
}

static int
_db_insert_audio(lms_db_audio_t *lda, const struct lms_audio_info *info, int64_t *album_id, int64_t *genre_id)
{
    sqlite3_stmt *stmt;
    int r, ret;

    stmt = lda->insert_audio;
    ret = lms_db_bind_int64(stmt, 1, info->id);
    if (ret != 0)
        goto done;

    ret = lms_db_bind_text(stmt, 2, info->title.str, info->title.len);
    if (ret != 0)
        goto done;

    ret = lms_db_bind_int64_or_null(stmt, 3, album_id);
    if (ret != 0)
        goto done;

    ret = lms_db_bind_int64_or_null(stmt, 4, genre_id);
    if (ret != 0)
        goto done;

    ret = lms_db_bind_double(stmt, 5, info->length);
    if (ret != 0)
        goto done;

    ret = lms_db_bind_int(stmt, 6, info->trackno);
    if (ret != 0)
        goto done;

    ret = lms_db_bind_int(stmt, 7, info->rating);
    if (ret != 0)
        goto done;

    r = sqlite3_step(stmt);
    if (r != SQLITE_DONE) {
        fprintf(stderr, "ERROR: could not insert audio info: %s\n",
                sqlite3_errmsg(lda->db));
        ret = -8;
        goto done;
    }

    ret = 0;

  done:
    lms_db_reset_stmt(stmt);

    return ret;
}

int
lms_db_audio_add(lms_db_audio_t *lda, struct lms_audio_info *info)
{
    int64_t album_id, genre_id;
    int ret_album, ret_genre;

    if (!lda)
        return -1;
    if (!info)
        return -2;
    if (info->id < 1)
        return -3;

    ret_album = _db_insert_album(lda, info, &album_id);
    if (ret_album < 0)
        return -4;

    ret_genre = _db_insert_genre(lda, info, &genre_id);
    if (ret_genre < 0)
        return -5;

    return _db_insert_audio(lda, info,
                            (ret_album == 0) ? &album_id : NULL,
                            (ret_genre == 0) ? &genre_id : NULL);
}
