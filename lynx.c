#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

typedef struct {
  char *alias;
  char *uri;;
} Bookmark;

typedef struct {
  size_t count;
  size_t capacity;
  Bookmark *items;
} Bookmarks;

Bookmarks bms;

void print_help() {
  printf("Usage: lynx <command> <arg>\n");
  printf("  ls - list all bookmarks\n");
  printf("  add <alias> <uri> - add a bookmark <alias>\n");
  printf("  open <alias> - launch <uri> of <alias> with default app\n");
  printf("  rm <alias> - remove an alias\n");
  printf("  update <alias> set <alias/uri> to <new_alias/new_uri> - update "
         "alias or uri to new_alias or new_uri\n");
}

void list_bookmarks() {
  if (bms.count == 0) {
    nob_log(INFO, " no bookmarks saved");
  }
  nob_da_foreach(Bookmark, bm, &bms) {
    nob_log(INFO, "bm.alias=%s - bm.uri=%s", bm->alias, bm->uri);
  }
}

bool add_bookmark(sqlite3 *db, char *alias, char *uri) {
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(
      db, "INSERT INTO bookmarks(alias,uri) VALUES(?,?);", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return false;
  }
  if(sqlite3_bind_text(stmt, 1, alias, -1, SQLITE_STATIC) != SQLITE_OK) {
    nob_log(ERROR, "could not bind %s because of %s", alias, sqlite3_errmsg(db));
  }
  if(sqlite3_bind_text(stmt, 2, uri, -1, SQLITE_STATIC) != SQLITE_OK) {
    nob_log(ERROR, "could not bind %s because of %s", uri, sqlite3_errmsg(db));
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    nob_log(ERROR, "%s", sqlite3_errmsg(db));
    return false;
  }

  Bookmark bm = {0};
  bm.alias = alias;
  bm.uri = uri;
  da_append(&bms, bm);
  nob_log(INFO, "added %s: %s, to bookmarks", bm.alias, bm.uri);
  return true;
}

int populate(void *data, int argc, char **argv, char **col_name) {
  UNUSED(data);
  UNUSED(argc);
  UNUSED(col_name);

  Bookmark bm = {0};
  bm.alias = nob_temp_strdup(*argv++);
  bm.uri = nob_temp_strdup(*argv);
  da_append(&bms, bm);
  return 0;
}

bool load_bookmarks(sqlite3 *db) {
  char *msg;
  int rc = sqlite3_exec(db, "SELECT * FROM bookmarks;", populate, 0, &msg);
  if (rc != SQLITE_OK) {
    nob_log(ERROR, "%s", msg);
    sqlite3_free(msg);
    return false;
  }
  sqlite3_free(msg);
  return true;
}

bool update_bookmark(sqlite3 *db, char *alias, char *col_name,
                     char *new_value) {
  sqlite3_stmt *stmt;
  int rc;
  if (strcmp("alias", col_name) == 0) {
    rc = sqlite3_prepare_v2(db, "UPDATE bookmarks SET alias=? WHERE alias=?;",
                            -1, &stmt, NULL);
  } else if (strcmp("uri", col_name) == 0){
    rc = sqlite3_prepare_v2(db, "UPDATE bookmarks SET uri=? WHERE alias=?;", -1,
                            &stmt, NULL);
  } else {
    nob_log(ERROR, "%s isn't a valid column name", col_name);
    return false;
  }

  if (rc != SQLITE_OK) {
    nob_log(ERROR, "%s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return false;
  }

  if (sqlite3_bind_text(stmt, 1, new_value, -1, SQLITE_STATIC) != SQLITE_OK) {
    nob_log(ERROR, "could not bind %s", new_value);
    return false;
  }
  if (sqlite3_bind_text(stmt, 2, alias, -1, SQLITE_STATIC) != SQLITE_OK) {
    nob_log(ERROR, "could not bind %s", alias);
    return false;
  }

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    nob_log(ERROR, "%s", sqlite3_errmsg(db));
    return false;
  }
  return true;
}

void remove_bookmark(sqlite3 *db, char *alias) {
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(db, "DELETE FROM bookmarks WHERE alias=?", -1, &stmt, NULL);
  if(rc != SQLITE_OK) {
    nob_log(ERROR, "%s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return;
  }
  if (sqlite3_bind_text(stmt, 1, alias, -1, SQLITE_STATIC) != SQLITE_OK) {
    nob_log(ERROR, "could not bind %s because of %s", alias, sqlite3_errmsg(db));
  }
  if (sqlite3_step(stmt) != SQLITE_DONE) {
    nob_log(ERROR, "%s", sqlite3_errmsg(db));
  }
  if (sqlite3_changes(db) == 0) {
    nob_log(ERROR, "no bookmark found with alias %s", alias);
  }
}

int main(int argc, char *argv[]) {
  if (argc == 1) {
    print_help();
    return 0;
  }

  sqlite3 *db;
  char *db_name = "lynx.db";

  int rc = sqlite3_open(db_name, &db);
  if (rc != SQLITE_OK) {
    nob_log(ERROR, "cannot open databse %s, because of %s", db_name,
            sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }

  char *table_name = "bookmarks";
  sqlite3_stmt *stmt;
  rc = sqlite3_prepare_v2(db,
                          "CREATE TABLE IF NOT EXISTS bookmarks(alias PRIMARY "
                          "KEY NOT NULL, uri NOT NULL);",
                          -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    nob_log(ERROR, "cannot create table %s, because of %s", table_name,
            sqlite3_errmsg(db));
    sqlite3_close(db);
    return 1;
  }
  sqlite3_step(stmt);

  if (!load_bookmarks(db)) {
    nob_log(ERROR, "cannot load bookmarks");
    return 1;
  }

  if (strcmp("ls", argv[1]) == 0) {
    printf("in ls\n");
    list_bookmarks();
  } else if (strcmp("add", argv[1]) == 0) {
    printf("in add\n");
    if (argc != 4) {
      print_help();
      return 1;
    }
    add_bookmark(db, argv[2], argv[3]);
  } else if (strcmp("open", argv[1]) == 0) {
    TODO("not implemented yet");
  } else if (strcmp("update", argv[1]) == 0) {
    printf("in update\n");
    if (argc != 7) {
      print_help();
      return 1;
    }
    // update alias set column_name to column_value
    update_bookmark(db, argv[2], argv[4], argv[6]);
  } else if(strcmp("rm", argv[1]) == 0) {
    printf("in rm\n");
    if(argc != 3) {
      print_help();
      return 1;
    }
    remove_bookmark(db,argv[2]);
  } else {
    nob_log(ERROR, "invalid command %s", argv[1]);
    return 1;
  }

  sqlite3_finalize(stmt);
  sqlite3_close(db);
  return 0;
}
