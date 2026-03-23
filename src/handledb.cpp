#include "../include/handledb.hpp"
#include <sqlite3.h>
#include <string>
using namespace Tools;

void DataBase::initDb(const char* dbPath){
    rc = sqlite3_open(dbPath, &db);
    if(rc != SQLITE_OK){
        cerr << "Error: " << sqlite3_errmsg(db) << endl;
        sqlite3_close(db);
        db = nullptr;
    }

    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS history_title (id INTEGER PRIMARY KEY AUTOINCREMENT, mainContext TEXT NOT NULL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)", nullptr, nullptr, nullptr);
    if(rc != SQLITE_OK){
        cerr << "Error: " << sqlite3_errmsg(db) << endl;
        sqlite3_close(db);
        db = nullptr;
    }

    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS history_context (id INTEGER PRIMARY KEY AUTOINCREMENT, id_title INTEGER ,context TEXT NOT NULL, response TEXT NOT NULL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)", nullptr, nullptr, nullptr);

    if(rc != SQLITE_OK){
        cerr << "Error: " << sqlite3_errmsg(db) << endl;
        sqlite3_close(db);
        db = nullptr;
    }
}

int DataBase::insertData(FormatInput data, sqlite3* db){
    string sql = "INSERT INTO history_title (mainContext, timestamp) VALUES ('" + data.context + "', '" + data.timestamp + "')";
    rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if(rc != SQLITE_OK){
        return 0;
    }

    sql = "INSERT INTO history_context (id_title, context, response, timestamp) VALUES ('" + to_string(sqlite3_last_insert_rowid(db)) + "', '" + data.context + "', '" + data.response + "', '" + data.timestamp + "')";
    rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    if(rc != SQLITE_OK){
        return 0;
    }
    return rc;
}

string DataBase::readTheContext(sqlite3* db, int idTitle){
    sqlite3_stmt* stmt;
    json firstResponse, secondResponse;
    string sql = "SELECT * FROM history_title WHERE aiu = " + to_string(idTitle);

    rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        return 0;
    }
    while(sqlite3_step(stmt) == SQLITE_ROW){
        firstResponse["mainContext"] = *sqlite3_column_text(stmt, 1);
        firstResponse["timestamp"] = *sqlite3_column_text(stmt, 2);
    }
    sqlite3_finalize(stmt);

    sql = "SELECT * FROM history_context WHERE id_title = " + to_string(idTitle);

    rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        return 0;
    }
    while(sqlite3_step(stmt) == SQLITE_ROW){
        secondResponse["context"] = *sqlite3_column_text(stmt, 1);
        secondResponse["response"] = *sqlite3_column_text(stmt, 2);
        secondResponse["timestamp"] = *sqlite3_column_text(stmt, 3);
    }
    sqlite3_finalize(stmt);

    return firstResponse.dump() + secondResponse.dump();
}

DataBase::~DataBase(){
    if(db){
        sqlite3_close(db);
    }
}