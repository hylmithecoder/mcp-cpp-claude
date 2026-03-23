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

int DataBase::insertData(FormatInput data, sqlite3* db) {
    sqlite3_stmt* stmt;
    const char* sql1 = "INSERT INTO history_title (mainContext, timestamp) VALUES (?, ?)";
    rc = sqlite3_prepare_v2(db, sql1, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, data.context.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, data.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return 0;

    long long last_id = sqlite3_last_insert_rowid(db);

    const char* sql2 = "INSERT INTO history_context (id_title, context, response, timestamp) VALUES (?, ?, ?, ?)";
    rc = sqlite3_prepare_v2(db, sql2, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_int64(stmt, 1, last_id);
    sqlite3_bind_text(stmt, 2, data.context.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, data.response.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, data.timestamp.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? SQLITE_OK : 0;
}

string DataBase::readTheContext(sqlite3* db, int idTitle) {
    sqlite3_stmt* stmt;
    json firstResponse, secondResponse = json::array();
    
    string sql = "SELECT mainContext, timestamp FROM history_title WHERE id = ?";
    rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, idTitle);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* ctx = (const char*)sqlite3_column_text(stmt, 0);
            const char* ts = (const char*)sqlite3_column_text(stmt, 1);
            firstResponse["mainContext"] = ctx ? ctx : "";
            firstResponse["timestamp"] = ts ? ts : "";
        }
        sqlite3_finalize(stmt);
    }

    sql = "SELECT context, response, timestamp FROM history_context WHERE id_title = ?";
    rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, idTitle);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            json item;
            const char* ctx = (const char*)sqlite3_column_text(stmt, 0);
            const char* res = (const char*)sqlite3_column_text(stmt, 1);
            const char* ts = (const char*)sqlite3_column_text(stmt, 2);
            item["context"] = ctx ? ctx : "";
            item["response"] = res ? res : "";
            item["timestamp"] = ts ? ts : "";
            secondResponse.push_back(item);
        }
        sqlite3_finalize(stmt);
    }

    json finalResult;
    finalResult["title"] = firstResponse;
    finalResult["history"] = secondResponse;
    return finalResult.dump();
}

DataBase::~DataBase(){
    if(db){
        sqlite3_close(db);
    }
}