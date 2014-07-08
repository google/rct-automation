/*
 *   Copyright 2012-2014 Google, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include <sqlite3.h>
#include <glog/logging.h>
#include "playlist.pb.h"
#include "protostore.h"
#include <gflags/gflags.h>

DEFINE_string(dbname, "/var/automation/music.db", "Name of database to use");
DEFINE_bool(dbinit, false, "If true, start, create a database, and exit.");

void InitializeSchema(sqlite3 *db);

void TraceCallback( void* udp, const char* sql ) {
  VLOG(30) << "{SQL} " << sql;
}

sqlite3* DatabaseOpen() {
  CHECK(sqlite3_threadsafe()); 
  sqlite3 *db;
  sqlite3_open_v2(FLAGS_dbname.c_str(), &db, SQLITE_OPEN_READWRITE | (FLAGS_dbinit ? SQLITE_OPEN_CREATE : 0), NULL);
  sqlite3_trace(db, TraceCallback, NULL);
  CHECK(sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL) == SQLITE_OK) << sqlite3_errmsg(db);
  CHECK(sqlite3_exec(db, "PRAGMA read_uncommitted = ON;", NULL, NULL, NULL) == SQLITE_OK);
  if (FLAGS_dbinit) {
    InitializeSchema(db);
    LOG(INFO) << "DB created.";
    exit(0);
  }

  return db;
}

void InitializeSchema(sqlite3 *db) {
  std::string schema = 
"CREATE TABLE Playlist(PlaylistID INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,name STRING,weight INTEGER);"
"CREATE TABLE PlayableItem(PlayableItemID INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
"                          filename STRING,duration INTEGER,description STRING, playcount INTEGER);"
"CREATE TABLE Playlist_PlayableItemID (PlaylistID INTEGER NOT NULL REFERENCES Playlist(PlaylistID),"
"                                      PlayableItemID INTEGER NOT NULL REFERENCES PlayableItem(PlayableItemID));" 
"CREATE TABLE PlaylistLock(name STRING NOT NULL REFERENCES Playlist(name) DEFERRABLE INITIALLY DEFERRED);"
"CREATE TABLE ProtoTable(ProtoStoreID INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,label STRING,data BLOB);"

"CREATE UNIQUE INDEX findex ON PlayableItem(filename);"
"CREATE UNIQUE INDEX joindex ON Playlist_PlayableItemID (playlistID,playableItemID);"
"CREATE UNIQUE INDEX playlistname ON Playlist(name);"
"CREATE UNIQUE INDEX labeldex ON ProtoTable(label);"
"CREATE UNIQUE INDEX lockdex ON PlaylistLock(name);"

"CREATE VIEW Playlists_with_children AS "
"  SELECT Playlist.*,group_concat(Playlist_PlayableItemID.PlayableItemID) "
"                      AS PlayableItemID "
"  FROM Playlist JOIN Playlist_PlayableItemID USING(PlaylistID) "
"                JOIN (SELECT * From PlayableItem ORDER BY PlayableItem.duration DESC,RANDOM()) USING(PlayableItemID) "
"  GROUP BY PlaylistID;"

"CREATE VIEW Playlists_with_shuffled_children AS "
"  SELECT Playlist.*,group_concat(Playlist_PlayableItemID.PlayableItemID) "
"                      AS PlayableItemID "
"  FROM Playlist JOIN Playlist_PlayableItemID USING(PlaylistID) "
"                JOIN (SELECT * From PlayableItem ORDER BY PlayableItem.playcount ASC,RANDOM()) USING(PlayableItemID) "
"  GROUP BY PlaylistID;"

"CREATE VIEW Playlists_with_size AS "
"  SELECT Playlist.*,count(Playlist_PlayableItemID.PlayableItemID) "
"                      AS length "
"  FROM Playlist JOIN Playlist_PlayableItemID USING(PlaylistID) "
"GROUP BY PlaylistID;"
"CREATE VIEW Playlists_random_weight AS "
"  SELECT * FROM Playlists_with_shuffled_children WHERE (select 1+abs(random() % sum(weight)) From Playlist) "
"    <= (select sum(weight) FROM Playlist p2 WHERE p2.PlaylistID <= Playlists_with_shuffled_children.PlaylistID) "
"  ORDER BY weight DESC limit 1;";

  CHECK(sqlite3_exec(db, schema.c_str(), NULL, NULL, NULL) == SQLITE_OK) << sqlite3_errmsg(db);
}

